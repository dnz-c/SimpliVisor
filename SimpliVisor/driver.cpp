#include "driver.h"
#include "vmx.h"
#include "ept.h"
#include "vmexit_handlers.h"

void run_on_all_cores(function_t func)
{
	KAFFINITY AffinityMask;
	for (size_t i = 0; i < KeQueryActiveProcessorCount(NULL); i++)
	{
		AffinityMask = int_power(2, i);
		KeSetSystemAffinityThread(AffinityMask);

		DbgPrint("=====================================================\n");
		DbgPrint("Current thread is executing in %d th logical processor.\n", i);

		// run code here
		func(i);
	}
}

void run_on_single_core(function_t func, int core)
{
	KAFFINITY AffinityMask;

	AffinityMask = int_power(2, core);
	KeSetSystemAffinityThread(AffinityMask);

	DbgPrint("=====================================================\n");
	DbgPrint("Current thread is executing in %d th logical processor.\n", core);

	// run code here
	func(core);
}

EXTERN_C PCHAR PsGetProcessImageFileName(PEPROCESS Process);
// 1. Declare the signature
typedef NTSTATUS(*KeDelayExecutionThread_t)(
	KPROCESSOR_MODE WaitMode,
	BOOLEAN         Alertable,
	PLARGE_INTEGER  Interval
	);

KeDelayExecutionThread_t Original_KeDelayExecutionThread = NULL;

// 2. The Hook Payload
NTSTATUS My_KeDelayExecutionThread_Hook(KPROCESSOR_MODE WaitMode, BOOLEAN Alertable, PLARGE_INTEGER Interval)
{
	if (WaitMode == 1)
	{
		// 1. Get the current process object
		PEPROCESS current_process = PsGetCurrentProcess();

		// 2. Get the Process ID and Name
		HANDLE pid = PsGetProcessId(current_process);
		PCHAR process_name = PsGetProcessImageFileName(current_process);

		// 3. Prove exactly who went to sleep!
		DbgPrint("[Hypervisor] %s (PID: %d) called Sleep()!\n", process_name, (ULONG) (ULONG_PTR) pid);
	}

	return Original_KeDelayExecutionThread(WaitMode, Alertable, Interval);
}

// We need globals to pass the parameters to all cores
UINT64 g_virt_target = 0;
UINT64 g_phys_target = 0;
UINT64 g_destination = 0;
UINT64 g_tramp_buffer = 0;

bool broadcast_install_hook(int core)
{
	asm_vmcall(VMCALL_INSTALLHOOK, g_virt_target, g_destination, g_tramp_buffer, g_phys_target);
	return true;
}

void test_hook()
{
	DbgPrint("Allocating executable trampoline buffer...\n");
	PVOID trampoline_buffer = ExAllocatePool(NonPagedPoolExecute, 64);

	if (!trampoline_buffer) return;
	RtlSecureZeroMemory(trampoline_buffer, 64);

	// Resolve the undeniably exported KeDelayExecutionThread
	UNICODE_STRING routineName;
	RtlInitUnicodeString(&routineName, L"KeDelayExecutionThread");
	g_virt_target = (UINT64) MmGetSystemRoutineAddress(&routineName);

	if (!g_virt_target)
	{
		DbgPrint("Failed to resolve KeDelayExecutionThread!\n");
		ExFreePool(trampoline_buffer);
		return;
	}

	DbgPrint("Found KeDelayExecutionThread at: 0x%llX\n", g_virt_target);

	g_phys_target = MmGetPhysicalAddress((PVOID) g_virt_target).QuadPart;
	g_destination = (UINT64) &My_KeDelayExecutionThread_Hook;
	g_tramp_buffer = (UINT64) trampoline_buffer;

	DbgPrint("Broadcasting EPT Hook installation to all cores...\n");

	run_on_all_cores(broadcast_install_hook);

	Original_KeDelayExecutionThread = (KeDelayExecutionThread_t) trampoline_buffer;
	DbgPrint("Global Hook installed successfully! Trampoline at: %p\n", Original_KeDelayExecutionThread);
}

NTSTATUS mj_create(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	NTSTATUS status = STATUS_SUCCESS;
	PIO_STACK_LOCATION stackLocation = NULL;
	stackLocation = IoGetCurrentIrpStackLocation(Irp);

	ULONG processor_count = KeQueryActiveProcessorCount(NULL);

	if (!vmx_supported())
	{
		DbgPrint("VMX Operation is not supported on this CPU\n");
		goto Exit;
	}

	g_vcpus = (VCPU*) ExAllocatePool(NonPagedPool, processor_count * sizeof(VCPU));

	if (!mtrr_support())
	{
		DbgPrint("No MTRR support\n");
		goto Exit;
	}

	populate_mtrr_regions();
	init_all_core_eptp();
	allocate_vmx_regions();
	init_vmexit_dispatch_table();

	run_on_all_cores(asm_virtualize_core);

	test_hook();

	Exit:
	Irp->IoStatus.Information = 0;
	Irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return STATUS_SUCCESS;
}

NTSTATUS mj_close(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	NTSTATUS status = STATUS_SUCCESS;
	PIO_STACK_LOCATION stackLocation = NULL;
	stackLocation = IoGetCurrentIrpStackLocation(Irp);

	run_on_all_cores(exit_vmx_operation);
	run_on_all_cores(free_ept_pages);
	free_vmx_regions();

	Irp->IoStatus.Information = 0;
	Irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return STATUS_SUCCESS;
}

void drv_unload(PDRIVER_OBJECT dob)
{
	DbgPrint("Driver unloaded, deleting symbolic links and devices");
	IoDeleteDevice(dob->DeviceObject);
	IoDeleteSymbolicLink(&DEVICE_SYMBOLIC_NAME);
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(DriverObject);
	UNREFERENCED_PARAMETER(RegistryPath);

	NTSTATUS status = 0;

	DriverObject->DriverUnload = drv_unload;

	DriverObject->MajorFunction[IRP_MJ_CREATE] = mj_create;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = mj_close;

	IoCreateDevice(DriverObject, 0, &DEVICE_NAME, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &DriverObject->DeviceObject);
	if (!NT_SUCCESS(status))
	{
		DbgPrint("Could not create device %wZ", DEVICE_NAME);
	}
	else
	{
		DbgPrint("Device %wZ created", DEVICE_NAME);
	}

	status = IoCreateSymbolicLink(&DEVICE_SYMBOLIC_NAME, &DEVICE_NAME);
	if (NT_SUCCESS(status))
	{
		DbgPrint("Symbolic link %wZ created", DEVICE_SYMBOLIC_NAME);
	}
	else
	{
		DbgPrint("Error creating symbolic link %wZ", DEVICE_SYMBOLIC_NAME);
	}

	return STATUS_SUCCESS;
}
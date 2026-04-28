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

	if (mtrr_support())
	{
		populate_mtrr_regions();
		initialize_eptp();
		allocate_vmx_regions();
		init_vmexit_dispatch_table();
		
		run_on_all_cores(asm_virtualize_core);
	}

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
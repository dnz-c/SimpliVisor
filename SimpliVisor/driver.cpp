#include "driver.h"
#include "vmx.h"

void run_on_all_cores(function_t func)
{
	KAFFINITY AffinityMask;
	DbgPrint("%d\n", KeQueryActiveProcessors());
	for (size_t i = 0; i <= KeQueryActiveProcessors(); i++)
	{
		AffinityMask = int_power(2, i);
		KeSetSystemAffinityThread(AffinityMask);

		DbgPrint("=====================================================\n");
		DbgPrint("Current thread is executing in %ull th logical processor.\n", i);

		// run code here
		func();
	}
}

void run_on_single_core(function_t func, int core)
{
	KAFFINITY AffinityMask;

	AffinityMask = int_power(2, core);
	KeSetSystemAffinityThread(AffinityMask);

	DbgPrint("=====================================================\n");
	DbgPrint("Current thread is executing in %ull th logical processor.\n", core);

	// run code here
	func();
}

NTSTATUS mj_create(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	PIO_STACK_LOCATION stackLocation = NULL;
	stackLocation = IoGetCurrentIrpStackLocation(Irp);

	//RunAllCores(EnableVMXOperation);
	run_on_single_core(setup_vmxon_region, 0);

	Irp->IoStatus.Information = 0;
	Irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return STATUS_SUCCESS;
}

NTSTATUS mj_close(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	PIO_STACK_LOCATION stackLocation = NULL;
	stackLocation = IoGetCurrentIrpStackLocation(Irp);

	DbgPrint("Handle closed\n");
	run_on_single_core(exit_vmx_operation, 0);

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
#include "driver.h"
#include "vmx.h"
#include "ept.h"
#include "vmexit_handlers.h"

#include "memory.h"

#include "ioctl.h"

NTSTATUS mj_create(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	NTSTATUS status = STATUS_SUCCESS;
	PIO_STACK_LOCATION stackLocation = NULL;
	stackLocation = IoGetCurrentIrpStackLocation(Irp);

	DbgPrint("Hello from MJ_Create\n");

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

	Irp->IoStatus.Information = 0;
	Irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return STATUS_SUCCESS;
}

NTSTATUS mj_device_control(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
	ULONG control_code = stack->Parameters.DeviceIoControl.IoControlCode;

	switch (control_code)
	{
	case IOCTL_VIRTUALIZE:
	{
		ULONG processor_count = KeQueryActiveProcessorCount(NULL);

		if (!vmx_supported())
		{
			DbgPrint("VMX Operation is not supported on this CPU\n");
			status = STATUS_UNSUCCESSFUL;
			break;
		}

		if (!mtrr_support())
		{
			DbgPrint("No MTRR support\n");
			status = STATUS_UNSUCCESSFUL;
			break;
		}

		g_vcpus = (VCPU*) ExAllocatePool(NonPagedPool, processor_count * sizeof(VCPU));
		if (!g_vcpus)
		{
			DbgPrint("Failed to allocate VCPU struct\n");
			status = STATUS_UNSUCCESSFUL;
			break;
		}
		RtlSecureZeroMemory(g_vcpus, processor_count * sizeof(VCPU));

		run_on_all_cores(setup_hv_phys_window);
		test_hv_phys_window();

		populate_mtrr_regions();
		init_all_core_eptp();
		allocate_vmx_regions();
		init_vmexit_dispatch_table();

		run_on_all_cores(asm_virtualize_core);
	}
		break;
	case IOCTL_DEVIRTUALIZE:
	{
		run_on_all_cores(exit_vmx_operation);
		free_all_core_eptp();
		free_vmx_regions();
		run_on_all_cores(free_hv_phys_window);

		ExFreePool(g_vcpus);
	}
		break;
	}

	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
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
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = mj_device_control;

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
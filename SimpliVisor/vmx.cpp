#include "vmx.h"
#include "driver.h"

bool vmx_supported()
{
	int data[4];
	__cpuid(data, 1);

	if ((data[2] & (1 << 5)) == 0) // ECX
		return FALSE;

	unsigned long long feature_control = __readmsr(IA32_FEATURE_CONTROL);
	if ((feature_control & (1ULL << 0)) == 0)
	{
		DbgPrint("VMX is blocked from the BIOS\n");
		return FALSE;
	}
	if ((feature_control & (1ULL << 2)) == 0)
	{
		DbgPrint("VMX blocked outside SMX\n");
		return FALSE;
	}

	return TRUE;
}

void allocate_vmx_regions()
{
	run_on_all_cores(reserve_vmxon_region);
	run_on_all_cores(reserve_vmcs_region);
}

void free_vmx_regions()
{
	run_on_all_cores(free_vmxon_region);
	run_on_all_cores(free_vmcs_region);
}

bool reserve_vmxon_region(int core)
{
	KIRQL irql = KeGetCurrentIrql();
	if (irql > DISPATCH_LEVEL)
		KeRaiseIrql(DPC_NORMAL, &irql);

	PHYSICAL_ADDRESS max_phys = { 0 };
	max_phys.QuadPart = MAXULONG64;
	PVOID virtual_contig = MmAllocateContiguousMemory(PAGE_SIZE * 2, max_phys);

	RtlSecureZeroMemory(virtual_contig, PAGE_SIZE * 2);

	PVOID aligned_virtual = PAGE_ALIGN(virtual_contig);
	UINT64 aligned_phys = MmGetPhysicalAddress(aligned_virtual).QuadPart;

	DbgPrint("Aligned Physical: %#x\n", aligned_phys);

	IA32_VMX_BASIC_MSR vmx_basic = { 0 };
	vmx_basic.All = __readmsr(IA32_VMX_BASIC);

	DbgPrint("VMX Basic: %#x\n", vmx_basic.All);

	*(UINT64*) aligned_virtual = vmx_basic.Fields.RevisionIdentifier;

	KeLowerIrql(irql);

	g_vcpus[core].p_vmxon_region = aligned_phys;
	g_vcpus[core].v_vmxon_region = virtual_contig;

	return TRUE;
}

bool reserve_vmcs_region(int core)
{
	KIRQL irql = KeGetCurrentIrql();
	if (irql > DISPATCH_LEVEL)
		KeRaiseIrql(DPC_NORMAL, &irql);

	PHYSICAL_ADDRESS max_phys = { 0 };
	max_phys.QuadPart = MAXULONG64;
	PVOID virtual_contig = MmAllocateContiguousMemory(PAGE_SIZE * 2, max_phys);

	RtlSecureZeroMemory(virtual_contig, PAGE_SIZE * 2);

	PVOID aligned_virtual = PAGE_ALIGN(virtual_contig);
	UINT64 aligned_phys = MmGetPhysicalAddress(aligned_virtual).QuadPart;

	DbgPrint("Aligned Physical: %#x\n", aligned_phys);

	IA32_VMX_BASIC_MSR vmx_basic = { 0 };
	vmx_basic.All = __readmsr(IA32_VMX_BASIC);

	DbgPrint("VMX Basic: %#x\n", vmx_basic.All);

	*(UINT64*) aligned_virtual = vmx_basic.Fields.RevisionIdentifier;

	KeLowerIrql(irql);

	g_vcpus[core].p_vmcs_region = aligned_phys;
	g_vcpus[core].v_vmcs_region = virtual_contig;

	return TRUE;
}

bool free_vmxon_region(int core)
{
	DbgPrint("Freeing vmxon region...\n");
	MmFreeContiguousMemory(g_vcpus[core].v_vmxon_region);
	return TRUE;
}

bool free_vmcs_region(int core)
{
	DbgPrint("Freeing vmcs region...\n");
	MmFreeContiguousMemory(g_vcpus[core].v_vmcs_region);
	return TRUE;
}

bool enter_vmx_operation(int core)
{
	DbgPrint("Allocating 16KB host stack...\n");
	g_vcpus[core].host_stack = (UINT64)ExAllocatePool(NonPagedPool, 0x4000); // stack used on vmexit

	DbgPrint("Enabling VMX Operations...");
	UINT64 cr4 = __readcr4();
	DbgPrint("cr4: %llx", cr4);

	cr4 |= 0x2000;
	cr4 |= __readmsr(IA32_VMX_CR4_FIXED0);
	cr4 &= __readmsr(IA32_VMX_CR4_FIXED1);

	__writecr4(cr4);
	DbgPrint("cr4 (vmxon): %llx", cr4);

	int status = __vmx_on(&g_vcpus[core].p_vmxon_region);
	if (status)
	{
		DbgPrint("__vmx_on failed with code: %d\n", status);
		return FALSE;
	}

	DbgPrint("__vmx_on succeded on core: %ull\n", KeQueryActiveProcessors());

	return TRUE;
}

bool exit_vmx_operation(int core)
{
	__vmx_off();
	DbgPrint("__vmx_off succeded on core: %ull\n", core);
	return TRUE;
}

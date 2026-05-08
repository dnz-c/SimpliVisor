#include "vmx.h"
#include "driver.h"
#include "vmcs.h"
#include "ept.h"

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

	unsigned long long vmx_basic = __readmsr(IA32_VMX_BASIC);
	TRUE_MSR_SUPPORT = vmx_basic & (1ULL << 55);

	if (TRUE_MSR_SUPPORT) DbgPrint("True MSR support detected!\n");

	return TRUE;
}

void allocate_vmx_regions()
{
	run_on_all_cores(reserve_vmxon_region);
	run_on_all_cores(reserve_vmcs_region);
	run_on_all_cores(reserve_msr_bitmap_region);
}

void free_vmx_regions()
{
	run_on_all_cores(free_vmxon_region);
	run_on_all_cores(free_vmcs_region);
	run_on_all_cores(free_msr_bitmap_region);
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

bool reserve_msr_bitmap_region(int core)
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
	KeLowerIrql(irql);

	g_vcpus[core].p_msr_bitmap = aligned_phys;
	g_vcpus[core].v_msr_bitmap = virtual_contig;

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

bool free_msr_bitmap_region(int core)
{
	DbgPrint("Freeing msr_bitmap region...\n");
	MmFreeContiguousMemory(g_vcpus[core].v_msr_bitmap);
	return TRUE;
}

bool enter_vmx_operation(int core, ULONG64 rsp)
{
	DbgPrint("Guest RSP: %#x\n", rsp);

	DbgPrint("Allocating 16KB host stack...\n");
	g_vcpus[core].host_stack = (ULONG64) ExAllocatePool(NonPagedPool, 0x4000) + 0x4000; // stack used on vmexit

	DbgPrint("Enabling VMX Operations...\n");
	ULONG64 cr4 = __readcr4();
	DbgPrint("cr4: %llx\n", cr4);

	cr4 |= 0x2000;
	cr4 |= __readmsr(IA32_VMX_CR4_FIXED0);
	cr4 &= __readmsr(IA32_VMX_CR4_FIXED1);

	__writecr4(cr4);
	DbgPrint("cr4 (vmxon): %llx\n", cr4);

	int status = __vmx_on(&g_vcpus[core].p_vmxon_region);
	if (status)
	{
		DbgPrint("__vmx_on failed with code: %d\n", status);
		return FALSE;
	}

	DbgPrint("__vmx_on succeded on core: %ull\n", core);

	DbgPrint("Setting up vmcs structure...\n");
	__vmx_vmclear(&g_vcpus[core].p_vmcs_region);
	__vmx_vmptrld(&g_vcpus[core].p_vmcs_region);
	DbgPrint("VMCS loaded into core...\n");
	
	setup_vmcs(core, rsp);

	// resume the now virtualized kernel
	__vmx_vmlaunch();

	size_t error_code = 0;
	__vmx_vmread(VM_INSTRUCTION_ERROR, &error_code);
	DbgPrint("VMLAUNCH failed! Error code: %llx\n", error_code);

	return TRUE;
}

bool exit_vmx_operation(int core)
{
	asm_vmcall(VMCALL_REASON::EXIT_VM, 0, 0, 0, 0);
	DbgPrint("__vmx_off succeded on core: %ull\n", core);

	UINT64 cr4 = __readcr4();
	DbgPrint("cr4 (vmxon): %#x\n", cr4);

	cr4 &= ~0x2000;

	DbgPrint("cr4 (vmxoff): %#x\n", cr4);
	__writecr4(cr4);

	return TRUE;
}

bool get_segment_descriptor(IN PSEGMENT_SELECTOR segment_selector, IN USHORT selector, IN void* gdt_base)
{
	PSEGMENT_DESCRIPTOR SegDesc;

	if (!segment_selector)
		return FALSE;

	if (selector & 0x4)
	{
		return FALSE;
	}

	SegDesc = (PSEGMENT_DESCRIPTOR) ((PUCHAR) gdt_base + (selector & ~0x7));

	segment_selector->SEL = selector;
	segment_selector->BASE = SegDesc->BASE0 | SegDesc->BASE1 << 16 | SegDesc->BASE2 << 24;
	segment_selector->LIMIT = SegDesc->LIMIT0 | (SegDesc->LIMIT1ATTR1 & 0xf) << 16;
	segment_selector->ATTRIBUTES.UCHARs = SegDesc->ATTR0 | (SegDesc->LIMIT1ATTR1 & 0xf0) << 4;

	if (!(SegDesc->ATTR0 & 0x10))
	{ // LA_ACCESSED
		ULONG64 Tmp;

		//
		// this is a TSS or callgate etc, save the base high part
		//
		Tmp = (*(PULONG64) ((PUCHAR) SegDesc + 8));
		segment_selector->BASE = (segment_selector->BASE & 0xffffffff) | (Tmp << 32);
	}

	if (segment_selector->ATTRIBUTES.Fields.G)
	{
		//
		// 4096-bit granularity is enabled for this segment, scale the limit
		//
		segment_selector->LIMIT = (segment_selector->LIMIT << 12) + 0xfff;
	}

	return TRUE;
}

void fill_guest_selector_data(PVOID gdt_base, ULONG segreg, USHORT selector)
{
	SEGMENT_SELECTOR segment_selector = { 0 };
	ULONG            access_rights;

	get_segment_descriptor(&segment_selector, selector, gdt_base);
	access_rights = ((PUCHAR) &segment_selector.ATTRIBUTES)[0] + (((PUCHAR) &segment_selector.ATTRIBUTES)[1] << 12);

	if (!selector)
		access_rights |= 0x10000;

	__vmx_vmwrite(GUEST_ES_SELECTOR + segreg * 2, selector);
	__vmx_vmwrite(GUEST_ES_LIMIT + segreg * 2, segment_selector.LIMIT);
	__vmx_vmwrite(GUEST_ES_AR_BYTES + segreg * 2, access_rights);
	__vmx_vmwrite(GUEST_ES_BASE + segreg * 2, segment_selector.BASE);
}

ULONG adjust_controls(ULONG ctl, ULONG msr)
{
	MSR MsrValue = { 0 };

	MsrValue.Content = __readmsr(msr);
	ctl &= MsrValue.High; /* bit == 0 in high word ==> must be zero */
	ctl |= MsrValue.Low;  /* bit == 1 in low word  ==> must be one  */
	return ctl;
}

void setup_vmcs(int core, ULONG64 rsp)
{
	__vmx_vmwrite(EPT_POINTER, g_vcpus[core].eptp.all);

	__vmx_vmwrite(HOST_ES_SELECTOR, get_es() & 0xF8);
	__vmx_vmwrite(HOST_CS_SELECTOR, get_cs() & 0xF8);
	__vmx_vmwrite(HOST_SS_SELECTOR, get_ss() & 0xF8);
	__vmx_vmwrite(HOST_DS_SELECTOR, get_ds() & 0xF8);
	__vmx_vmwrite(HOST_FS_SELECTOR, get_fs() & 0xF8);
	__vmx_vmwrite(HOST_GS_SELECTOR, get_gs() & 0xF8);
	__vmx_vmwrite(HOST_TR_SELECTOR, get_tr() & 0xF8);

	__vmx_vmwrite(GUEST_IA32_DEBUGCTL, __readmsr(IA32_DEBUGCTL) & 0xFFFFFFFF);
	__vmx_vmwrite(GUEST_IA32_DEBUGCTL_HIGH, __readmsr(IA32_DEBUGCTL) >> 32);

	__vmx_vmwrite(VMCS_LINK_POINTER, ~0ULL);

	__vmx_vmwrite(TSC_OFFSET, 0);
	__vmx_vmwrite(TSC_OFFSET_HIGH, 0);

	__vmx_vmwrite(PAGE_FAULT_ERROR_CODE_MASK, 0);
	__vmx_vmwrite(PAGE_FAULT_ERROR_CODE_MATCH, 0);

	__vmx_vmwrite(VM_EXIT_MSR_STORE_COUNT, 0);
	__vmx_vmwrite(VM_EXIT_MSR_LOAD_COUNT, 0);

	__vmx_vmwrite(VM_ENTRY_MSR_LOAD_COUNT, 0);

	ULONG64 gdt_base = get_gdt_base();

	fill_guest_selector_data((PVOID) gdt_base, ES, get_es());
	fill_guest_selector_data((PVOID) gdt_base, CS, get_cs());
	fill_guest_selector_data((PVOID) gdt_base, SS, get_ss());
	fill_guest_selector_data((PVOID) gdt_base, DS, get_ds());
	fill_guest_selector_data((PVOID) gdt_base, FS, get_fs());
	fill_guest_selector_data((PVOID) gdt_base, GS, get_gs());
	fill_guest_selector_data((PVOID) gdt_base, LDTR, get_ldtr());
	fill_guest_selector_data((PVOID) gdt_base, TR, get_tr());

	__vmx_vmwrite(GUEST_FS_BASE, __readmsr(FS_BASE));
	__vmx_vmwrite(GUEST_GS_BASE, __readmsr(GS_BASE));

	__vmx_vmwrite(HOST_FS_BASE, __readmsr(FS_BASE));
	__vmx_vmwrite(HOST_GS_BASE, __readmsr(GS_BASE));

	// store host processor data in FS
	g_vcpus[core].processor_data.core_index = core;
	__vmx_vmwrite(HOST_FS_SELECTOR, 0);
	__vmx_vmwrite(HOST_FS_BASE, (size_t) & g_vcpus[core].processor_data);

	__vmx_vmwrite(VM_ENTRY_INTR_INFO, 0);

	__vmx_vmwrite(HOST_GDTR_BASE, get_gdt_base());
	__vmx_vmwrite(HOST_IDTR_BASE, get_idt_base());

	__vmx_vmwrite(CPU_BASED_VM_EXEC_CONTROL, adjust_controls(CPU_BASED_ACTIVATE_MSR_BITMAP | CPU_BASED_ACTIVATE_SECONDARY_CONTROLS, TRUE_MSR_SUPPORT ? IA32_VMX_TRUE_PROCBASED_CTLS : IA32_VMX_PROCBASED_CTLS));
	__vmx_vmwrite(SECONDARY_VM_EXEC_CONTROL, adjust_controls(CPU_BASED_CTL2_RDTSCP | CPU_BASED_CTL2_ENABLE_INVPCID | CPU_BASED_CTL2_ENABLE_XSAVE_XRSTORS | CPU_BASED_CTL2_ENABLE_USER_WAIT_PAUSE | CPU_BASED_CTL2_ENABLE_EPT, IA32_VMX_PROCBASED_CTLS2));
	__vmx_vmwrite(PIN_BASED_VM_EXEC_CONTROL, adjust_controls(0, TRUE_MSR_SUPPORT ? IA32_VMX_TRUE_PINBASED_CTLS : IA32_VMX_PINBASED_CTLS));

	__vmx_vmwrite(VM_EXIT_CONTROLS, adjust_controls(VM_EXIT_IA32E_MODE | VM_EXIT_SAVE_IA32_EFER, TRUE_MSR_SUPPORT ? IA32_VMX_TRUE_EXIT_CTLS : IA32_VMX_EXIT_CTLS));
	__vmx_vmwrite(VM_ENTRY_CONTROLS, adjust_controls(VM_ENTRY_IA32E_MODE | VM_ENTRY_LOAD_IA32_EFER, TRUE_MSR_SUPPORT ? IA32_VMX_TRUE_ENTRY_CTLS : IA32_VMX_ENTRY_CTLS));

	__vmx_vmwrite(CR3_TARGET_COUNT, 0);
	__vmx_vmwrite(CR3_TARGET_VALUE0, 0);
	__vmx_vmwrite(CR3_TARGET_VALUE1, 0);
	__vmx_vmwrite(CR3_TARGET_VALUE2, 0);
	__vmx_vmwrite(CR3_TARGET_VALUE3, 0);

	__vmx_vmwrite(CR0_GUEST_HOST_MASK, 0);
	__vmx_vmwrite(CR4_GUEST_HOST_MASK, 0);
	__vmx_vmwrite(CR0_READ_SHADOW, 0);
	__vmx_vmwrite(CR4_READ_SHADOW, 0);

	__vmx_vmwrite(GUEST_GDTR_BASE, get_gdt_base());
	__vmx_vmwrite(GUEST_IDTR_BASE, get_idt_base());
	__vmx_vmwrite(GUEST_GDTR_LIMIT, get_gdt_limit());
	__vmx_vmwrite(GUEST_IDTR_LIMIT, get_idt_limit());

	__vmx_vmwrite(GUEST_RFLAGS, __readeflags() | 0x200); // enable interrupts in the guest state

	__vmx_vmwrite(GUEST_SYSENTER_CS, __readmsr(IA32_SYSENTER_CS));
	__vmx_vmwrite(GUEST_SYSENTER_EIP, __readmsr(IA32_SYSENTER_EIP));
	__vmx_vmwrite(GUEST_SYSENTER_ESP, __readmsr(IA32_SYSENTER_ESP));

	__vmx_vmwrite(HOST_SYSENTER_CS, __readmsr(IA32_SYSENTER_CS));
	__vmx_vmwrite(HOST_SYSENTER_EIP, __readmsr(IA32_SYSENTER_EIP));
	__vmx_vmwrite(HOST_SYSENTER_ESP, __readmsr(IA32_SYSENTER_ESP));

	UINT64 efer = __readmsr(IA32_EFER); // IA32_EFER
	__vmx_vmwrite(GUEST_EFER, efer);
	__vmx_vmwrite(HOST_EFER, efer);

	SEGMENT_SELECTOR segment_selector;
	get_segment_descriptor(&segment_selector, get_tr(), (PUCHAR) gdt_base);
	__vmx_vmwrite(HOST_TR_BASE, segment_selector.BASE);

	__vmx_vmwrite(GUEST_RSP, rsp);
	__vmx_vmwrite(GUEST_RIP, (size_t)asm_vmx_restore_state);

	__vmx_vmwrite(HOST_RSP, g_vcpus[core].host_stack);
	__vmx_vmwrite(HOST_RIP, (size_t) asm_vmexit_handler);

	__vmx_vmwrite(GUEST_DR7, __readdr(7) | 0x400);

	__vmx_vmwrite(GUEST_CR3, __readcr3());
	__vmx_vmwrite(HOST_CR3, __readcr3());

	__vmx_vmwrite(MSR_BITMAP, g_vcpus[core].p_msr_bitmap);

	UINT64 cr0 = __readcr0();
	cr0 |= __readmsr(IA32_VMX_CR0_FIXED0);
	cr0 &= __readmsr(IA32_VMX_CR0_FIXED1);
	__vmx_vmwrite(GUEST_CR0, cr0);
	__vmx_vmwrite(HOST_CR0, cr0);

	UINT64 cr4 = __readcr4();
	cr4 |= __readmsr(IA32_VMX_CR4_FIXED0);
	cr4 &= __readmsr(IA32_VMX_CR4_FIXED1);
	__vmx_vmwrite(GUEST_CR4, cr4);
	__vmx_vmwrite(HOST_CR4, cr4);
}

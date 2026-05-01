#include "ept.h"
#include "driver.h"
#include "vmx.h"
#include "hde.h"

bool mtrr_support()
{
	int regs[4];
	__cpuid(regs, 0x1);
	int edx = regs[3];

	if ((edx & (1 << 12)) == 0)
		return false;

	IA32_MTRR_DEF_TYPE_MSR mtrr_def_type;
	mtrr_def_type.all = __readmsr(IA32_MTRR_DEF_TYPE);

	return mtrr_def_type.fields.enabled && mtrr_def_type.fields.fixed_enabled;
}

void populate_mtrr_regions()
{
	_IA32_MTRRCAP_MSR mtrrcap;
	mtrrcap.all = __readmsr(IA32_MTRRCAP);

	DbgPrint("Detected %d variable regions, Fixed regions are %s supported\n", mtrrcap.fields.vcnt, mtrrcap.fields.fix ? "" : "NOT");

	memory_regions = (PMEMORY_REGION) ExAllocatePool(NonPagedPool, sizeof(MEMORY_REGION) * mtrrcap.fields.vcnt);
	if (!memory_regions)
	{
		DbgPrint("Failed to allocate memory_regions buffer\n");
		return;
	}

	UINT64 max_phys_address;
	int regs[4];
	__cpuid(regs, 0x80000008);
	max_phys_address = regs[0] & 0xFF; // EAX
	
	DbgPrint("Max Phys address: %d\n", max_phys_address);

	ULONG64 max_phy_addr_mask = (1ULL << max_phys_address) - 1;

	memory_region_cnt = 0;
	for (size_t i = 0; i < mtrrcap.fields.vcnt; i++)
	{
		IA32_MTRRPHYSBASEN_MSR base_msr;
		IA32_MTRRPHYSMASKN_MSR mask_msr;

		base_msr.all = __readmsr(IA32_MTRRPHYSBASE(i));
		mask_msr.all = __readmsr(IA32_MTRRPHYSMASK(i));

		if (mask_msr.fields.valid)
		{
			ULONG64 base_address = base_msr.fields.physbase << 12;
			ULONG64 physical_mask = mask_msr.fields.mask << 12;

			ULONG64 region_size = (~physical_mask & max_phy_addr_mask) + 1;
			ULONG64 end_address = base_address + region_size - 1;
			UINT8   memory_type = (UINT8) base_msr.fields.memory_type;

			DbgPrint("Found valid mttr %d: Base=0x%llX, End=0x%llX, Size=0x%llX, Type=%d\n", i, base_address, end_address, region_size, memory_type);

			MEMORY_REGION region;
			region.end_address = end_address;
			region.memory_type = memory_type;
			region.start_address = base_address;
			region.size = region_size;

			memory_regions[memory_region_cnt++] = region;
		}
	}
}

UINT8 get_fixed_mtrr_type(UINT64 physical_address)
{
	ULONG64 msr_value;
	UINT8 type_index;

	// 64kb range
	if (physical_address < 0x80000)
	{
		msr_value = __readmsr(IA32_MTRR_FIX64K_00000);
		type_index = (physical_address / 0x10000) % 8;
	}
	// 16kb range
	else if (physical_address < 0xC0000)
	{
		if (physical_address < 0xA0000)
			msr_value = __readmsr(IA32_MTRR_FIX16K_80000);
		else
			msr_value = __readmsr(IA32_MTRR_FIX16K_A0000);
		type_index = ((physical_address - 0x80000) / 0x4000) % 8;
	}
	// 8kb range
	else if (physical_address < 0x100000)
	{
		UINT64 msr_offset = (physical_address - 0xC0000) / 0x8000;
		msr_value = __readmsr(IA32_MTRR_FIX4K_(msr_offset));
		type_index = ((physical_address - 0xC0000) / 0x1000) % 8;
	}
	else
	{
		return 0xff; // indicate to leave existing caching
	}

	return (UINT8) ((msr_value >> (type_index * 8)) & 0xFF);
}

void init_all_core_eptp()
{
	g_hook_map = init_hash_map(PTES_TO_ALLOCATE);
	run_on_all_cores(setup_core_eptp);
}

bool setup_core_eptp(int core)
{
	IA32_VMX_EPT_VPID_CAP_MSR vmx_ept_vpid_cap;
	vmx_ept_vpid_cap.all = __readmsr(IA32_VMX_EPT_VPID_CAP);

	PHYSICAL_ADDRESS max_phys = { 0 };
	max_phys.QuadPart = MAXULONG64;

	EPTP eptp = { 0 };
	eptp.fields.page_walk_length = 3; // 4 page table levels - 1
	eptp.fields.accessed_dirty_flags = vmx_ept_vpid_cap.fields.accessed_and_dirty_eptp;
	eptp.fields.memory_type = vmx_ept_vpid_cap.fields.eptp_wb ? MEMORY_TYPE::WB : MEMORY_TYPE::UC;

	// allocate one pml4e mapping 512gb of memory
	PEPT_PML4E pml4 = (PEPT_PML4E)MmAllocateContiguousMemory(PAGE_SIZE, max_phys);
	if (pml4 == NULL)
	{
		DbgPrint("Failed to allocate pml4 pool\n");
		return false;
	}
	RtlSecureZeroMemory(pml4, PAGE_SIZE);
	DbgPrint("EPT PML4 @ %p\n", pml4);
	g_vcpus[core].v_ept_pml4 = pml4;

	// link eptp -> pml4
	UINT64 pml4_phys = MmGetPhysicalAddress(pml4).QuadPart;
	eptp.fields.pml4_address = pml4_phys / PAGE_SIZE;

	// allocate 512 pdpte mapping 1gb each
	PEPT_PDPTE pdpt = (PEPT_PDPTE)MmAllocateContiguousMemory(PAGE_SIZE, max_phys);
	if (pdpt == NULL)
	{
		DbgPrint("Failed to allocate pdpt pool\n");
		return false;
	}
	RtlSecureZeroMemory(pdpt, PAGE_SIZE);
	DbgPrint("EPT PDPT @ %p\n", pml4);
	g_vcpus[core].v_ept_pdpt = pdpt;

	// link pml4 -> pdpt
	pml4[0].fields.read_access = 1;
	pml4[0].fields.write_access = 1;
	pml4[0].fields.execute_access = 1;

	UINT64 pdpt_phys = MmGetPhysicalAddress(pdpt).QuadPart;
	pml4[0].fields.pfn = pdpt_phys / PAGE_SIZE;

	// allocate 512 pages for pdes mapping 512 * 2mb each
	g_vcpus[core].pdes = (PEPT_PDE_2MB)MmAllocateContiguousMemory(PAGE_SIZE * 512, max_phys);
	if (g_vcpus[core].pdes == NULL)
	{
		DbgPrint("Failed to allocate pdpt pool\n");
		return false;
	}
	RtlSecureZeroMemory(g_vcpus[core].pdes, PAGE_SIZE * 512);
	DbgPrint("EPT 512x PDEs @ %p\n", g_vcpus[core].pdes);

	// link pdpt -> pd
	for (size_t i = 0; i < 512; i++)
	{
		EPT_PDPTE pdpte = { 0 };
		pdpte.fields.read_access = 1;
		pdpte.fields.write_access = 1;
		pdpte.fields.execute_access = 1;

		UINT64 pd_phys = MmGetPhysicalAddress(g_vcpus[core].pdes).QuadPart;
		pdpte.fields.pfn = (pd_phys + (i * PAGE_SIZE)) / PAGE_SIZE;

		pdpt[i].all = pdpte.all;
	}

	// init all 512 * 512 2mb pages, since the memory is contiguous we can just iterate over it like a flat array
	IA32_MTRR_DEF_TYPE_MSR mtrr_def_type;
	mtrr_def_type.all = __readmsr(IA32_MTRR_DEF_TYPE);
	UINT8 default_cache_type = (UINT8) mtrr_def_type.fields.memory_type;

	for (size_t idx_pd = 0; idx_pd < 512 * 512; idx_pd++)
	{
		EPT_PDE_2MB pde = { 0 };
		pde.fields.read_access = 1;
		pde.fields.write_access = 1;
		pde.fields.execute_access = 1;
		pde.fields.page_size = 1;
		pde.fields.pfn = idx_pd;

		UINT64 mapped_phys_start = idx_pd * PDE_PAGE_SIZE;
		UINT64 mapped_phys_end = mapped_phys_start + PDE_PAGE_SIZE - 1;

		UINT8 cache_candidate = default_cache_type;

		for (size_t idx_mtrr = 0; idx_mtrr < memory_region_cnt; idx_mtrr++)
		{
			MEMORY_REGION region = memory_regions[idx_mtrr];
			if (region.start_address <= mapped_phys_end && region.end_address >= mapped_phys_start)
			{
				if (region.memory_type == MEMORY_TYPE::UC)
				{
					cache_candidate = MEMORY_TYPE::UC;
					break;
				}
				else if (region.memory_type == MEMORY_TYPE::WT)
				{
					if (cache_candidate != MEMORY_TYPE::UC)
						cache_candidate = MEMORY_TYPE::WT;
				}
				else if (region.memory_type == MEMORY_TYPE::WB)
				{
					if (cache_candidate != MEMORY_TYPE::WT && cache_candidate != MEMORY_TYPE::UC)
						cache_candidate = MEMORY_TYPE::WB;
				}
			}
		}

		pde.fields.memory_type = cache_candidate;
		g_vcpus[core].pdes[idx_pd].all = pde.all;
	}

	PVOID pte_buffer = MmAllocateContiguousMemory(PAGE_SIZE * PTES_TO_ALLOCATE, max_phys);
	if (pte_buffer == NULL)
	{
		DbgPrint("Failed to allocate pte buffer\n");
		return false;
	}
	RtlSecureZeroMemory(pte_buffer, PAGE_SIZE * PTES_TO_ALLOCATE);

	DbgPrint("PTE Buffer @ %p\n", pte_buffer);

	g_vcpus[core].ept_pte_buffer.start_phys_address = MmGetPhysicalAddress(pte_buffer).QuadPart;
	g_vcpus[core].ept_pte_buffer.start_virt_address = (UINT64)pte_buffer;
	g_vcpus[core].ept_pte_buffer.curr_virt_address = (UINT64)pte_buffer;
	g_vcpus[core].ept_pte_buffer.size = PAGE_SIZE * PTES_TO_ALLOCATE;

	_IA32_MTRRCAP_MSR mtrrcap;
	mtrrcap.all = __readmsr(IA32_MTRRCAP);
	if (mtrrcap.fields.fix)
	{
		// handle fixed size MTRR regions across the first mb of ram
		PEPT_PTE split_pt = split_pde(core, 0);
		if (!split_pt)
		{
			DbgPrint("Failed to split first PDE!\n");
			return false;
		}

		for (size_t i = 0; i < 512; i++)
		{
			EPT_PTE pte = split_pt[i];
			UINT8 fixed_type = get_fixed_mtrr_type(pte.fields.pfn * PAGE_SIZE);
			if (fixed_type == 0xff) continue;

			pte.fields.memory_type = fixed_type;
			split_pt[i] = pte;

			//DbgPrint("applied fixed tpye: %d to page: %llx\n", fixed_type, pte.fields.pfn * PAGE_SIZE);
		}
	}

	DbgPrint("using eptp configuration %llx\n", eptp.all);
	g_vcpus[core].eptp.all = eptp.all;
	return true;
}

PEPT_PTE split_pde(UINT32 core, UINT64 pd_idx)
{
	if (pd_idx >= 512 * 512) return NULL;

	EPT_PDE_2MB ept_pde_2mb = g_vcpus[core].pdes[pd_idx];

	PHYSICAL_ADDRESS max_phys = { 0 };
	max_phys.QuadPart = MAXULONG64;
	
	// since we cant call any os mem alloc functions in vmx root mode we have to grab the memory from our pre allocated buffer
	UINT64 allocated_pt_addr = (UINT64) InterlockedExchangeAdd64((LONG64*) &g_vcpus[core].ept_pte_buffer.curr_virt_address, PAGE_SIZE);
	if (allocated_pt_addr >= g_vcpus[core].ept_pte_buffer.start_virt_address + g_vcpus[core].ept_pte_buffer.size)
	{
		DbgPrint("exhausted PT pool, cannot split page %d\n", pd_idx); // UNSAFE in VMX Root
		return NULL;
	}
	
	PEPT_PTE pt = (PEPT_PTE) allocated_pt_addr;
	RtlSecureZeroMemory(pt, PAGE_SIZE);

	for (size_t pt_idx = 0; pt_idx < 512; pt_idx++)
	{
		EPT_PTE pte = { 0 };
		pte.fields.read_access = ept_pde_2mb.fields.read_access;
		pte.fields.write_access = ept_pde_2mb.fields.write_access;
		pte.fields.execute_access = ept_pde_2mb.fields.execute_access;
		pte.fields.memory_type = ept_pde_2mb.fields.memory_type;
		pte.fields.pfn = ((pd_idx * PDE_PAGE_SIZE) + (pt_idx * PAGE_SIZE)) / PAGE_SIZE;

		pt[pt_idx] = pte;
	}

	EPT_PDE ept_pde_split = { 0 };
	ept_pde_split.fields.read_access = ept_pde_2mb.fields.read_access;
	ept_pde_split.fields.write_access = ept_pde_2mb.fields.write_access;
	ept_pde_split.fields.execute_access = ept_pde_2mb.fields.execute_access;
	ept_pde_split.fields.page_size = 0;

	UINT64 offset = allocated_pt_addr - g_vcpus[core].ept_pte_buffer.start_virt_address;
	UINT64 pt_phys = g_vcpus[core].ept_pte_buffer.start_phys_address + offset;
	ept_pde_split.fields.pfn = pt_phys / PAGE_SIZE;

	InterlockedExchange64((LONG64*) &g_vcpus[core].pdes[pd_idx], ept_pde_split.all);

	return pt;
}

PEPT_PTE get_ept_pte(UINT32 core, UINT64 guest_physical)
{
	int pd_idx = guest_physical / PDE_PAGE_SIZE;
	int pt_idx = (guest_physical % PDE_PAGE_SIZE) / PAGE_SIZE;

	if (pd_idx >= 512 * 512) return NULL;
	if (pt_idx >= 512) return NULL;

	EPT_PDE pde = { 0 };
	pde.all = g_vcpus[core].pdes[pd_idx].all;

	if (pde.fields.page_size == 1) return NULL; // the page has not been split yet

	UINT64 pt_phys = pde.fields.pfn * PAGE_SIZE;
	UINT64 offset = pt_phys - g_vcpus[core].ept_pte_buffer.start_phys_address;
	PEPT_PTE pt = (PEPT_PTE) (g_vcpus[core].ept_pte_buffer.start_virt_address + offset);
	
	return &pt[pt_idx];
}

PEPT_PDE_2MB get_ept_pde(UINT32 core, UINT64 guest_physical)
{
	int pd_idx = guest_physical / PDE_PAGE_SIZE;

	if (pd_idx >= 512 * 512) return NULL;

	EPT_PDE_2MB pde = { 0 };
	pde.all = g_vcpus[core].pdes[pd_idx].all;

	if (pde.fields.page_size == 0) return NULL; // the page has been split

	return &g_vcpus[core].pdes[pd_idx];
}

void write_absolute_jmp(UINT64 write_location, UINT64 destination_address)
{
	UINT8 jmp_shellcode[] = {
		0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};

	*(UINT64*) (&jmp_shellcode[6]) = destination_address;

	memcpy((PVOID) write_location, jmp_shellcode, sizeof(jmp_shellcode));
}

void install_ept_hook(UINT64 phys_target_address, UINT64 virt_target_address, UINT64 destination, UINT64 tramp_buffer, UINT32 core)
{
	int pd_idx = phys_target_address / PDE_PAGE_SIZE;
	int pt_idx = (phys_target_address % PDE_PAGE_SIZE) / PAGE_SIZE;

	PEPT_PTE pte = get_ept_pte(core, phys_target_address);
	if (!pte)
	{
		PEPT_PTE pt = split_pde(core, pd_idx);
		pte = &pt[pt_idx];
	}

	// build the "shadow" page
	UINT64 shadow_page = (UINT64) InterlockedExchangeAdd64((LONG64*) &g_vcpus[core].ept_pte_buffer.curr_virt_address, PAGE_SIZE);
	if (shadow_page >= g_vcpus[core].ept_pte_buffer.start_virt_address + g_vcpus[core].ept_pte_buffer.size)
	{
		DbgPrint("exhausted PT pool, cannot allocate shadow page %d\n", pt_idx); // UNSAFE in VMX Root
		return;
	}

	UINT64 page_aligned_target = virt_target_address & ~0xFFFull;
	memcpy((PVOID) shadow_page, (PVOID) page_aligned_target, PAGE_SIZE);

	UINT64 page_offset = virt_target_address & 0xFFF;
	UINT64 shadow_func_ptr = (UINT64) (shadow_page + page_offset);
	
	UINT32 stolen_length = 0;
	while (stolen_length < 14)
	{
		hde64s hs;
		hde64_disasm((PVOID)(shadow_func_ptr + stolen_length), &hs);
		stolen_length += hs.len;
	}

	memcpy((PVOID)tramp_buffer, (PVOID)shadow_func_ptr, stolen_length);

	UINT64 return_address = virt_target_address + stolen_length;
	write_absolute_jmp(tramp_buffer + stolen_length, return_address); // jump back to function

	write_absolute_jmp(shadow_func_ptr, destination);

	UINT64 offset = shadow_page - g_vcpus[core].ept_pte_buffer.start_virt_address;
	UINT64 shadow_page_phys = g_vcpus[core].ept_pte_buffer.start_phys_address + offset;

	insert_in_hashmap(g_hook_map, pte->fields.pfn, shadow_page_phys / PAGE_SIZE);

	pte->fields.execute_access = 0; // arm the hook
}

bool free_ept_pages(int core)
{
	DbgPrint("freing EPT page layers\n");
	if (g_vcpus[core].v_ept_pml4)
		MmFreeContiguousMemory(g_vcpus[core].v_ept_pml4);
	if (g_vcpus[core].v_ept_pdpt)
		MmFreeContiguousMemory(g_vcpus[core].v_ept_pdpt);
	if (g_vcpus[core].pdes)
		MmFreeContiguousMemory(g_vcpus[core].pdes);
	if (g_vcpus[core].ept_pte_buffer.start_virt_address)
		MmFreeContiguousMemory((PVOID)g_vcpus[core].ept_pte_buffer.start_virt_address);
	return true;
}
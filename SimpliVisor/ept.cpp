#include "ept.h"
#include "driver.h"
#include "vmx.h"

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

void initialize_eptp()
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
		return;
	}
	RtlSecureZeroMemory(pml4, PAGE_SIZE);
	DbgPrint("EPT PML4 @ %p\n", pml4);

	// link eptp -> pml4
	UINT64 pml4_phys = MmGetPhysicalAddress(pml4).QuadPart;
	eptp.fields.pml4_address = pml4_phys / PAGE_SIZE;

	// allocate 512 pdpte mapping 1gb each
	PEPT_PDPTE pdpt = (PEPT_PDPTE)MmAllocateContiguousMemory(PAGE_SIZE, max_phys);
	if (pdpt == NULL)
	{
		DbgPrint("Failed to allocate pdpt pool\n");
		return;
	}
	RtlSecureZeroMemory(pdpt, PAGE_SIZE);
	DbgPrint("EPT PDPT @ %p\n", pml4);

	// link pml4 -> pdpt
	pml4[0].fields.read_access = 1;
	pml4[0].fields.write_access = 1;
	pml4[0].fields.execute_access = 1;

	UINT64 pdpt_phys = MmGetPhysicalAddress(pdpt).QuadPart;
	pml4[0].fields.pfn = pdpt_phys / PAGE_SIZE;

	// allocate 512 pages for pdes mapping 512 * 2mb each
	g_pdes = (PEPT_PDE_2MB)MmAllocateContiguousMemory(PAGE_SIZE * 512, max_phys);
	if (g_pdes == NULL)
	{
		DbgPrint("Failed to allocate pdpt pool\n");
		return;
	}
	RtlSecureZeroMemory(g_pdes, PAGE_SIZE * 512);
	DbgPrint("EPT 512x PDEs @ %p\n", g_pdes);

	// link pdpt -> pd
	for (size_t i = 0; i < 512; i++)
	{
		EPT_PDPTE pdpte = { 0 };
		pdpte.fields.read_access = 1;
		pdpte.fields.write_access = 1;
		pdpte.fields.execute_access = 1;

		UINT64 pd_phys = MmGetPhysicalAddress(g_pdes).QuadPart;
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
		g_pdes[idx_pd].all = pde.all;
	}

	DbgPrint("using eptp configuration %llx\n", eptp.all);
	g_eptp.all = eptp.all;
}

/* 
TODO:	Add static range MTRR regions
		Add 2MB to 4KB splitting mechanism
		Add PTE pool with 10 * 512 pre allocated PTEs we can grab in vmx root mode without new allocations
		Add proper devirtualize routine to discard all allocated pages aswell as all split pages
*/
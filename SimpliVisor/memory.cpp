#include "memory.h"
#include "driver.h"

PVOID get_pte_for_va(UINT64 va)
{
	PHYSICAL_ADDRESS pml4_pa;
	pml4_pa.QuadPart = __readcr3() & ~0xFFF;

	UINT64* pml4 = (UINT64*) MmGetVirtualForPhysical(pml4_pa);
	if (!pml4) return NULL;
	int pml4_idx = (va >> 39) & 0x1FF;
	if ((pml4[pml4_idx] & 1) == 0) return NULL; // present

	PHYSICAL_ADDRESS pdpt_pa;
	pdpt_pa.QuadPart = pml4[pml4_idx] & 0xFFFFFFFFFF000;
	UINT64* pdpt = (UINT64*) MmGetVirtualForPhysical(pdpt_pa);
	if (!pdpt) return NULL;
	int pdpt_idx = (va >> 30) & 0x1FF;
	if ((pdpt[pdpt_idx] & 1) == 0) return NULL; // present
	if (pdpt[pdpt_idx] & (1 << 7)) return NULL; // large page

	PHYSICAL_ADDRESS pd_pa;
	pd_pa.QuadPart = pdpt[pdpt_idx] & 0xFFFFFFFFFF000;
	UINT64* pd = (UINT64*) MmGetVirtualForPhysical(pd_pa);
	if (!pd) return NULL;
	int pd_idx = (va >> 21) & 0x1FF;
	if ((pd[pd_idx] & 1) == 0) return NULL; // present
	if (pd[pd_idx] & (1 << 7)) return NULL; // large page

	PHYSICAL_ADDRESS pt_pa;
	pt_pa.QuadPart = pd[pd_idx] & 0xFFFFFFFFFF000;
	UINT64* pt = (UINT64*) MmGetVirtualForPhysical(pt_pa);
	if (!pt) return NULL;
	int pt_idx = (va >> 12) & 0x1FF;

	return (PPTE) &pt[pt_idx];
}

bool setup_hv_phys_window(int core)
{
	PHYSICAL_ADDRESS max_phys = { 0 };
	max_phys.QuadPart = MAXULONG64;

	g_vcpus[core].phys_window = (UINT64) MmAllocateContiguousMemory(PAGE_SIZE, max_phys);
	if (!g_vcpus[core].phys_window)
	{
		DbgPrint("failed to alloc pool\n");
		return false;
	}
	RtlSecureZeroMemory((PVOID) g_vcpus[core].phys_window, PAGE_SIZE);

	// get the pfn, multiply by 8 bytes and mask off the bottom bits
	UINT64 offset = ((g_vcpus[core].phys_window >> 12) << 3) & 0x7FFFFFFFF8ULL;

	g_vcpus[core].phys_pte = (PPTE)get_pte_for_va(g_vcpus[core].phys_window);

	if (!g_vcpus[core].phys_pte)
	{
		DbgPrint("failed to locate PTE for phys_window\n");
		MmFreeContiguousMemory((PVOID)g_vcpus[core].phys_window);
		return false;
	}
	
	g_vcpus[core].orig_window_pfn = g_vcpus[core].phys_pte->PageFrameNumber;
	DbgPrint("PFN: 0x%lx | MmGetPhysical: 0x%lx (these must match!!)\n", g_vcpus[core].phys_pte->PageFrameNumber * PAGE_SIZE, MmGetPhysicalAddress((PVOID) g_vcpus[core].phys_window).QuadPart);

	return true;
}

bool free_hv_phys_window(int core)
{
	g_vcpus[core].phys_pte->PageFrameNumber = g_vcpus[core].orig_window_pfn;
	__invlpg((PVOID) g_vcpus[core].phys_window);
	MmFreeContiguousMemory((PVOID)g_vcpus[core].phys_window);
	return true;
}

bool read_physical(UINT64 phys, UINT64 buf, size_t cnt, int core)
{
	UINT64 pfn = phys / PAGE_SIZE;
	g_vcpus[core].phys_pte->PageFrameNumber = pfn;
	__invlpg((PVOID) g_vcpus[core].phys_window);

	// TODO: if cnt + page_offset > PAGE_SIZE we will read into the next page and cause a bugcheck -> read in chunks in the future 
	UINT64 page_offset = phys & 0xFFF;
	memcpy((PVOID) buf, (PVOID) (g_vcpus[core].phys_window + page_offset), cnt);
	return true;
}

bool write_physical(UINT64 phys, UINT64 buf, size_t cnt, int core)
{
	UINT64 pfn = phys / PAGE_SIZE;
	g_vcpus[core].phys_pte->PageFrameNumber = pfn;
	__invlpg((PVOID) g_vcpus[core].phys_window);

	// TODO: if cnt + page_offset > PAGE_SIZE we will read into the next page and cause a bugcheck -> read in chunks in the future 
	UINT64 page_offset = phys & 0xFFF;
	memcpy((PVOID) (g_vcpus[core].phys_window + page_offset), (PVOID) buf, cnt);
	return true;
}

UINT64 virt_to_phys(UINT64 virt, UINT64 pml4, int core)
{
	unsigned short PML4 = (unsigned short) ((virt >> 39) & 0x1FF);
	UINT64 PML4E = 0;
	read_physical((pml4 + PML4 * sizeof(UINT64)), (UINT64) &PML4E, sizeof(PML4E), core);

	if ((PML4E & 1) == 0) return 0;

	unsigned short DirectoryPtr = (unsigned short) ((virt >> 30) & 0x1FF);
	UINT64 PDPTE = 0;
	read_physical(((PML4E & 0xFFFFFFFFFF000) + DirectoryPtr * sizeof(UINT64)), (UINT64) &PDPTE, sizeof(PDPTE), core);

	if ((PDPTE & 1) == 0) return 0;
	if ((PDPTE & (1 << 7)) != 0) return (PDPTE & 0x000FFFFFC0000000) + (virt & 0x3FFFFFFF);

	unsigned short Directory = (unsigned short) ((virt >> 21) & 0x1FF);
	UINT64 PDE = 0;
	read_physical(((PDPTE & 0xFFFFFFFFFF000) + Directory * sizeof(UINT64)), (UINT64) &PDE, sizeof(PDE), core);

	if ((PDE & 1) == 0) return 0;
	if ((PDE & (1 << 7)) != 0) return (PDE & 0x000FFFFFFFE00000) + (virt & 0x1FFFFF);

	unsigned short Table = (unsigned short) ((virt >> 12) & 0x1FF);
	UINT64 PTE = 0;
	read_physical(((PDE & 0xFFFFFFFFFF000) + Table * sizeof(UINT64)), (UINT64) &PTE, sizeof(PTE), core);

	if ((PTE & 1) == 0) return 0;

	return (PTE & 0xFFFFFFFFFF000) + (virt & 0xFFF);
}

bool read_virt(UINT64 virt, UINT64 pml4, UINT64 buf, size_t cnt, int core)
{
	UINT64 pa = virt_to_phys(virt, pml4, core);
	if (!pa) return false;

	return read_physical(pa, buf, cnt, core);
}

bool write_virt(UINT64 virt, UINT64 pml4, UINT64 buf, size_t cnt, int core)
{
	UINT64 pa = virt_to_phys(virt, pml4, core);
	if (!pa) return false;

	return write_physical(pa, buf, cnt, core);
}

bool test_hv_phys_window()
{
	UINT64 pool = (UINT64) ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, 'test');
	if (!pool)
	{
		DbgPrint("failed to alloc pool\n");
		return false;
	}
	RtlSecureZeroMemory((PVOID) pool, PAGE_SIZE);

	UINT64 test_val = 0x1337;
	memcpy((PVOID) pool, &test_val, sizeof(test_val));

	PHYSICAL_ADDRESS phys = MmGetPhysicalAddress((PVOID)pool);

	UINT64 read = 0;
	read_physical(phys.QuadPart, (UINT64) & read, sizeof(read), 0);

	DbgPrint("phys read test: %#lx (should be 0x1337)\n", read);

	read_virt(pool, __readcr3(), (UINT64) &read, sizeof(read), 0);
	DbgPrint("virt read test: %#lx (should be 0x1337)\n", read);

	read = 0x6767;
	write_virt(pool, __readcr3(), (UINT64) &read, sizeof(read), 0);
	read_virt(pool, __readcr3(), (UINT64) &read, sizeof(read), 0);
	DbgPrint("virt write test: %#lx (should be 0x6767)\n", read);
}

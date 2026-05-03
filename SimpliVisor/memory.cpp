#include "memory.h"
#include "driver.h"

UINT64 get_selfref_index()
{
	PHYSICAL_ADDRESS pml4_phys;
	pml4_phys.QuadPart = __readcr3() & ~0xFFF;

	UINT64 pml4_virt = (UINT64)MmGetVirtualForPhysical(pml4_phys);
	if (!pml4_virt) return 0;

	DbgPrint("kernel pml4 mapped to: %p\n", (PVOID)pml4_virt);

	for (size_t i = 0; i < 512; i++)
	{
		PML4E pml4e;
		memcpy(&pml4e, (PVOID) (pml4_virt + (i * sizeof(PML4E))), sizeof(PML4E));

		if (pml4e.PageFrameNumber == pml4_phys.QuadPart / PAGE_SIZE) return i;
	}

	return 0;
}

bool setup_hv_phys_window(int core)
{
	UINT64 selfref = get_selfref_index();
	if (selfref == 0)
	{
		DbgPrint("failed to get selfref idx\n");
		return false;
	}

	UINT64 mm_pte_base = 0xFFFF000000000000ULL | (selfref << 39);

	g_vcpus[core].phys_window = (UINT64)ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, 'pswd');
	if (!g_vcpus[core].phys_window)
	{
		DbgPrint("failed to alloc pool\n");
		return false;
	}
	RtlSecureZeroMemory((PVOID) g_vcpus[core].phys_window, PAGE_SIZE);

	// get the pfn, multiply by 8 bytes and mask off the bottom bits
	UINT64 offset = ((g_vcpus[core].phys_window >> 12) << 3) & 0x7FFFFFFFF8ULL;

	g_vcpus[core].phys_pte = (PPTE) (mm_pte_base + offset);
	if (!g_vcpus[core].phys_pte)
	{
		DbgPrint("failed to calc pte\n");
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
	ExFreePool((PVOID)g_vcpus[core].phys_window);
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
	read_physical((pml4 + PML4 * sizeof(UINT64)), (UINT64) & PML4E, sizeof(PML4E), core);

	if (PML4E == 0)
		return 0;

	unsigned short DirectoryPtr = (unsigned short) ((virt >> 30) & 0x1FF);
	UINT64 PDPTE = 0;
	read_physical(((PML4E & 0xFFFFFFFFFF000) + DirectoryPtr * sizeof(UINT64)), (UINT64) &PDPTE, sizeof(PDPTE), core);

	if (PDPTE == 0)
		return 0;

	if ((PDPTE & (1 << 7)) != 0)
		return (PDPTE & 0xFFFFFC0000000) + (virt & 0x3FFFFFFF);

	unsigned short Directory = (unsigned short) ((virt >> 21) & 0x1FF);

	UINT64 PDE = 0;
	read_physical(((PDPTE & 0xFFFFFFFFFF000) + Directory * sizeof(UINT64)), (UINT64) &PDE, sizeof(PDE), core);

	if (PDE == 0)
		return 0;

	if ((PDE & (1 << 7)) != 0)
	{
		return (PDE & 0xFFFFFFFE00000) + (virt & 0x1FFFFF);
	}

	unsigned short Table = (unsigned short) ((virt >> 12) & 0x1FF);
	UINT64 PTE = 0;

	read_physical(((PDE & 0xFFFFFFFFFF000) + Table * sizeof(UINT64)), (UINT64) &PTE, sizeof(PTE), core);

	if (PTE == 0)
		return 0;

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

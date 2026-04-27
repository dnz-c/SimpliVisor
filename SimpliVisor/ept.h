#pragma once

#include <wdm.h>
#include <ntddk.h>
#include <intrin.h>
#include <cmath>

// Table 25-9. Format of Extended-Page-Table Pointer
typedef union _EPTP 
{
    ULONG64 all;
    struct 
    {
        UINT64 memory_type : 3; // 2:0 PT paging-structure memory type (see Section 29.3.7): 0 = Uncacheable(UC) 6 = Write - back(WB)
        UINT64 page_walk_length : 3; // 5:3 This value is 1 less than the EPT page-walk length (see Section 29.3.2)
        UINT64 accessed_dirty_flags : 1; // 6  Setting this control to 1 enables accessed and dirty flags for EPT (see Section 29.3.5)
        UINT64 shadow_stack_pages : 1; // 7 Setting this control to 1 enables enforcement of access rights for supervisor shadow-stack pages (see Section 29.3.3.2)
        UINT64 reserved1 : 4; // 11:8 
        UINT64 PML4Address : 40; // n-1:12 Bits N–1:12 of the physical address of the 4-KByte aligned EPT paging-structure (an EPT PML4 table with 4-level EPT and an EPT PML5 table with 5 - level EPT)
        UINT64 reserved2 : 12; // 63:n
    } fields;
}EPTP, * PEPTP;

// Table 29-2. Format of an EPT PML4 Entry(PML4E) that References an EPT Page - Directory - Pointer Table
typedef union _EPT_PML4E
{
    ULONG64 all;
    struct
    {
        UINT64 read_access : 1; // 0 Read access; indicates whether reads are allowed from the 512-GByte region controlled by this entry.
        UINT64 write_access : 1; // 1 Write access; indicates whether writes are allowed to the 512-GByte region controlled by this entry.
        UINT64 execute_access : 1; // 2 If the “mode - based execute control for EPT” VM - execution control is 0, execute access; indicates whether instruction fetches are allowed from the 512 - GByte region controlled by this entry. If that control is 1, execute access for supervisor - mode linear addresses; indicates whether instruction fetches are allowed from supervisor - mode linear addresses in the 512 - GByte region controlled by this entry
        UINT64 reserved1 : 5; // 7:3
        UINT64 accessed : 1; // 8 If bit 6 of EPTP is 1, accessed flag for EPT; indicates whether software has accessed the 512-GByte region controlled by this entry(see Section 29.3.5).Ignored if bit 6 of EPTP is 0.
        UINT64 ignored1 : 1; // 9
        UINT64 user_execute : 1; // 10 Execute access for user-mode linear addresses. If the “mode-based execute control for EPT” VM-execution control is 1, indicates whether instruction fetches are allowed from user - mode linear addresses in the 512 - GByte region controlled by this entry.If that control is 0, this bit is ignored.
        UINT64 ignored2 : 1; // 11
        UINT64 pfn : 40; // (n-1):12 Physical address of 4-KByte aligned EPT page-directory-pointer table referenced by this entry.
        UINT64 ignored3 : 12; // 63:52
    } fields;
}EPT_PML4E, * PEPT_PML4E;

// Table 29-4. Format of an EPT Page-Directory-Pointer-Table Entry (PDPTE) that References an EPT Page Directory
typedef union _EPT_PDPTE
{
    ULONG64 all;
    struct
    {
        UINT64 read_access : 1; // 0 Read access; indicates whether reads are allowed from the 1-GByte region controlled by this entry.
        UINT64 write_access : 1; // 1 Write access; indicates whether writes are allowed to the 1-GByte region controlled by this entry.
        UINT64 execute_access : 1; // 2 If the “mode - based execute control for EPT” VM - execution control is 0, execute access; indicates whether instruction fetches are allowed from the 1 - GByte region controlled by this entry. If that control is 1, execute access for supervisor - mode linear addresses; indicates whether instruction fetches are allowed from supervisor - mode linear addresses in the 1 - GByte region controlled by this entry
        UINT64 reserved1 : 5; // 7:3
        UINT64 accessed : 1; // 8 If bit 6 of EPTP is 1, accessed flag for EPT; indicates whether software has accessed the 1-GByte region controlled by this entry(see Section 29.3.5).Ignored if bit 6 of EPTP is 0.
        UINT64 ignored1 : 1; // 9
        UINT64 user_execute : 1; // 10 Execute access for user-mode linear addresses. If the “mode-based execute control for EPT” VM-execution control is 1, indicates whether instruction fetches are allowed from user - mode linear addresses in the 1 - GByte region controlled by this entry.If that control is 0, this bit is ignored.
        UINT64 ignored2 : 1; // 11
        UINT64 pfn : 40; // (n-1):12 Physical address of 4-KByte aligned EPT page-directory referenced by this entry.
        UINT64 ignored3 : 12; // 63:52
    } fields;
}EPT_PDPTE, * PEPT_PDPTE;

// Table 29-5. Format of an EPT Page-Directory Entry (PDE) that Maps a 2-MByte Page
typedef union _EPT_PDE_2MB
{
    ULONG64 all;
    struct
    {
        UINT64 read_access : 1; // 0 Read access; indicates whether reads are allowed from the 2-MByte region controlled by this entry.
        UINT64 write_access : 1; // 1 Write access; indicates whether writes are allowed to the 2-MByte region controlled by this entry.
        UINT64 execute_access : 1; // 2 If the “mode - based execute control for EPT” VM - execution control is 0, execute access; indicates whether instruction fetches are allowed from the 2-MByte region controlled by this entry. If that control is 1, execute access for supervisor - mode linear addresses; indicates whether instruction fetches are allowed from supervisor - mode linear addresses in the 2-MByte region controlled by this entry
        UINT64 memory_type : 3; // 5:3 EPT memory type for this 2-MByte page (see Section 29.3.7).
        UINT64 ignore_pat : 1; // 6 Ignore PAT memory type for this 2-MByte page (see Section 29.3.7).
        UINT64 page_size : 1; // 7 Must be 1 (otherwise, this entry references an EPT page table).
        UINT64 accessed : 1; // 8 If bit 6 of EPTP is 1, accessed flag for EPT; indicates whether software has accessed the 2-MByte region controlled by this entry(see Section 29.3.5).Ignored if bit 6 of EPTP is 0.
        UINT64 dirty : 1; // 9  If bit 6 of EPTP is 1, dirty flag for EPT; indicates whether software has written to the 2-MByte page referenced by this entry(see Section 29.3.5).Ignored if bit 6 of EPTP is 0.
        UINT64 user_execute : 1; // 10 Execute access for user-mode linear addresses. If the “mode-based execute control for EPT” VM-execution control is 1, indicates whether instruction fetches are allowed from user - mode linear addresses in the 2-MByte region controlled by this entry.If that control is 0, this bit is ignored.
        UINT64 ignored1 : 1; // 11
        UINT64 reserved1 : 9; // 20:12 Reserved (must be 0).
        UINT64 pfn : 31; // (n-1):21 Physical address of 4-KByte aligned EPT page-directory referenced by this entry.
        UINT64 ignored2 : 5; // 56:52
        UINT64 verify_guest_paging : 1; // 57 Verify guest paging. If the “guest-paging verification” VM-execution control is 1, indicates limits on the guest paging structures used to access the 2 - MByte page controlled by this entry(see Section 29.3.3.2).If that control is 0, this bit is ignored.
        UINT64 paging_write_access : 1; // Paging-write access. If the “EPT paging-write control” VM-execution control is 1, indicates that guest paging may update the 2 - MByte page controlled by this entry(see Section 29.3.3.2).If that control is 0, this bit is ignored.
        UINT64 ignored3 : 1; // 59
        UINT64 supervisor_shadow_stack : 1; // 60  Supervisor shadow stack. If bit 7 of EPTP is 1, indicates whether supervisor shadow stack accesses are allowed to guest - physical addresses in the 2 - MByte page mapped by this entry(see Section 29.3.3.2). Ignored if bit 7 of EPTP is 0.
        UINT64 ignored4 : 2; // 62:61
        UINT64 suppress_ve : 1; // 63  Suppress #VE. If the “EPT-violation #VE” VM-execution control is 1, EPT violations caused by accesses to this page are convertible to virtualization exceptions only if this bit is 0 (see Section 26.5.7.1).If “EPT - violation #VE” VMexecution control is 0, this bit is ignored.
    } fields;
}EPT_PDE_2MB, * PEPT_PDE_2MB;

// Table 29-6. Format of an EPT Page-Directory Entry (PDE) that References an EPT Page Table
typedef union _EPT_PDE
{
    ULONG64 all;
    struct
    {
        UINT64 read_access : 1; // 0 Read access; indicates whether reads are allowed from the 2-MByte region controlled by this entry.
        UINT64 write_access : 1; // 1 Write access; indicates whether writes are allowed to the 2-MByte region controlled by this entry.
        UINT64 execute_access : 1; // 2 If the “mode - based execute control for EPT” VM - execution control is 0, execute access; indicates whether instruction fetches are allowed from the 2-MByte region controlled by this entry. If that control is 1, execute access for supervisor - mode linear addresses; indicates whether instruction fetches are allowed from supervisor - mode linear addresses in the 2-MByte region controlled by this entry
        UINT64 reserved1 : 4; // 6:3
        UINT64 page_size : 1; // 7 must be 0 (otherwise, this entry maps a 2-MByte page and should be of type PEPT_PDE_2MB)
        UINT64 accessed : 1; // 8 If bit 6 of EPTP is 1, accessed flag for EPT; indicates whether software has accessed the 2-MByte region controlled by this entry(see Section 29.3.5).Ignored if bit 6 of EPTP is 0.
        UINT64 ignored1 : 1; // 9
        UINT64 user_execute : 1; // 10 Execute access for user-mode linear addresses. If the “mode-based execute control for EPT” VM-execution control is 1, indicates whether instruction fetches are allowed from user - mode linear addresses in the 2-MByte region controlled by this entry.If that control is 0, this bit is ignored.
        UINT64 ignored2 : 1; // 11
        UINT64 pfn : 40; // (n-1):12 Physical address of 4-KByte aligned EPT page-table referenced by this entry.
        UINT64 ignored3 : 12; // 63:52
    } fields;
}EPT_PDE, * PEPT_PDE;

// Table 29-7. Format of an EPT Page-Table Entry that Maps a 4-KByte Page
typedef union _EPT_PTE
{
    ULONG64 all;
    struct
    {
        UINT64 read_access : 1; // 0 Read access; indicates whether reads are allowed from the 4-KByte region controlled by this entry.
        UINT64 write_access : 1; // 1 Write access; indicates whether writes are allowed to the 4-KByte region controlled by this entry.
        UINT64 execute_access : 1; // 2 If the “mode - based execute control for EPT” VM - execution control is 0, execute access; indicates whether instruction fetches are allowed from the 4-KByte region controlled by this entry. If that control is 1, execute access for supervisor - mode linear addresses; indicates whether instruction fetches are allowed from supervisor - mode linear addresses in the 4-KByte region controlled by this entry
        UINT64 memory_type : 3; // 5:3 EPT memory type for this 4-KByte page (see Section 29.3.7).
        UINT64 ignore_pat : 1; // 6 Ignore PAT memory type for this 4-KByte page (see Section 29.3.7).
        UINT64 ignored1 : 1; // 7
        UINT64 accessed : 1; // 8 If bit 6 of EPTP is 1, accessed flag for EPT; indicates whether software has accessed the 4-KByte region controlled by this entry(see Section 29.3.5).Ignored if bit 6 of EPTP is 0.
        UINT64 dirty : 1; // 9  If bit 6 of EPTP is 1, dirty flag for EPT; indicates whether software has written to the 4-KByte page referenced by this entry(see Section 29.3.5).Ignored if bit 6 of EPTP is 0.
        UINT64 user_execute : 1; // 10 Execute access for user-mode linear addresses. If the “mode-based execute control for EPT” VM-execution control is 1, indicates whether instruction fetches are allowed from user - mode linear addresses in the 4-KByte region controlled by this entry.If that control is 0, this bit is ignored.
        UINT64 ignored2 : 1; // 11
        UINT64 pfn : 40; // (n-1):12 Physical address of 4-KByte aligned EPT page-directory referenced by this entry.
        UINT64 ignored3 : 5; // 56:52
        UINT64 verify_guest_paging : 1; // 57 Verify guest paging. If the “guest-paging verification” VM-execution control is 1, indicates limits on the guest paging structures used to access the 4-KByte page controlled by this entry(see Section 29.3.3.2).If that control is 0, this bit is ignored.
        UINT64 paging_write_access : 1; // Paging-write access. If the “EPT paging-write control” VM-execution control is 1, indicates that guest paging may update the 4-KByte page controlled by this entry(see Section 29.3.3.2).If that control is 0, this bit is ignored.
        UINT64 ignored4 : 1; // 59
        UINT64 supervisor_shadow_stack : 1; // 60  Supervisor shadow stack. If bit 7 of EPTP is 1, indicates whether supervisor shadow stack accesses are allowed to guest - physical addresses in the 4-KByte page mapped by this entry(see Section 29.3.3.2). Ignored if bit 7 of EPTP is 0.
        UINT64 sub_page_write_permissions : 1; // 61 Sub-page write permissions. If the “sub-page write permissions for EPT” VM-execution control is 1, writes to individual 128 - byte regions of the 4 - KByte page referenced by this entry may be allowed even if the page would normally not be writable(see Section 29.3.4).If “sub - page write permissions for EPT” VM - execution control is 0, this bit is ignored.
        UINT64 ignored5 : 1; // 62
        UINT64 suppress_ve : 1; // 63  Suppress #VE. If the “EPT-violation #VE” VM-execution control is 1, EPT violations caused by accesses to this page are convertible to virtualization exceptions only if this bit is 0 (see Section 26.5.7.1).If “EPT - violation #VE” VMexecution control is 0, this bit is ignored.
    } fields;
}EPT_PTE, * PEPT_PTE;
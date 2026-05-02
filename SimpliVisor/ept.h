#pragma once

#include <wdm.h>
#include <ntddk.h>
#include <intrin.h>
#include <cmath>

#include "km_hashmap.hpp"

// number of PTs to allocate for later splitting
#define PTES_TO_ALLOCATE        0x10

#define PDE_PAGE_SIZE           0x200000

// MSRs
#define IA32_MTRRCAP            0xFE
#define IA32_MTRRPHYSBASE(N)    0x200 + (N * 2)
#define IA32_MTRRPHYSMASK(N)    0x201 + (N * 2)
#define IA32_MTRR_FIX64K_00000  0x250
#define IA32_MTRR_FIX16K_80000  0x258
#define IA32_MTRR_FIX16K_A0000  0x259
#define IA32_MTRR_FIX4K_(N)     0x268 + N
#define IA32_MTRR_DEF_TYPE      0x2FF
#define IA32_VMX_EPT_VPID_CAP   0x48C

enum MEMORY_TYPE
{
    UC = 0,
    WC = 1,
    WT = 4,
    WP = 5,
    WB = 6
};

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
        UINT64 pml4_address : 40; // n-1:12 Bits N–1:12 of the physical address of the 4-KByte aligned EPT paging-structure (an EPT PML4 table with 4-level EPT and an EPT PML5 table with 5 - level EPT)
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

typedef union _IA32_MTRR_DEF_TYPE_MSR
{
    ULONG64 all;
    struct
    {
        UINT64 memory_type : 8; // 7:0 default memory type for all regions not in MTRRs legal values: 0,1,4,5,6
        UINT64 reserved1 : 2; // 9:8
        UINT64 fixed_enabled : 1; // 10 1 if fixed MTRRs are enabled
        UINT64 enabled : 1; // 11 1 if MTRRs are enabled
        UINT64 reserved2 : 52; // 63:12 reserved
    } fields;
} IA32_MTRR_DEF_TYPE_MSR, * PIA32_MTRR_DEF_TYPE_MSR;

typedef union _IA32_MTRRCAP_MSR
{
    ULONG64 all;
    struct
    {
        UINT64 vcnt : 8; // 7:0 number of variable ranges implemented
        UINT64 fix : 1; // 8 if set fixed range MTRRs are supported
        UINT64 reserved1 : 1; // 9
        UINT64 wc : 1; // 10 if set write combining memory type is supported
        UINT64 smrr : 1; // 11 system management mode range register is supported if set
        UINT64 reserved2 : 52; // 63:12 reserved
    } fields;
} IA32_MTRRCAP_MSR, * PIA32_MTRRCAP_MSR;

typedef union _IA32_MTRRPHYSBASEN_MSR
{
    ULONG64 all;
    struct
    {
        UINT64 memory_type : 8; // 7:0 memory type for region legal values: 0,1,4,5,6
        UINT64 reserved1 : 4; // 11:8
        UINT64 physbase : 40; // range base address
        UINT64 reserved2 : 12;
    } fields;
} IA32_MTRRPHYSBASEN_MSR, * PIA32_MTRRPHYSBASEN_MSR;

typedef union _IA32_MTRRPHYSMASKN_MSR
{
    ULONG64 all;
    struct
    {
        UINT64 reserved1 : 11; // 10:0
        UINT64 valid : 1; // 11 if set the region is active
        UINT64 mask : 40; // range mask
        UINT64 reserved2 : 12;
    } fields;
} IA32_MTRRPHYSMASKN_MSR, * PIA32_MTRRPHYSMASKN_MSR;

typedef union _IA32_VMX_EPT_VPID_CAP_MSR
{
    ULONG64 all;
    struct
    {
        UINT64 execute_only : 1; // 0 If bit 0 is read as 1, the processor supports execute-only translations by EPT. This support allows software to configure EPT paging - structure entries in which bits 1:0 are clear(indicating that data accesses are not allowed) and bit 2 is set(indicating that instruction fetches are allowed)
        UINT64 reserved1 : 5;
        UINT64 page_walk_4 : 1; // 6 if 1 cpu supports 4 level page walk
        UINT64 page_walk_5 : 1; // 7 if 1 cpu supports 5 level page walk
        UINT64 eptp_uc : 1; // 8 if 1 eptp caching type can be UC
        UINT64 reserved2 : 5;
        UINT64 eptp_wb : 1; // 14 if 1 cpu allows caching type WB on eptp
        UINT64 reserved3 : 1;
        UINT64 pde_page_size : 1; // 16 if 1 cpu allows 2MB pages
        UINT64 pdpte_page_size : 1; // 17 if 1 cpu allows 1GB pages
        UINT64 reserved4 : 2;
        UINT64 invept : 1; // 20 if 1 cpu supports invept
        UINT64 accessed_and_dirty_eptp : 1; // 21 if 1 cpu supports eptp accessed and dirty page tracking
        UINT64 advanced_ept_violation_info : 1; // 22 if 1 cpu gives advanced ept violation information on vmexits
        UINT64 supervisor_shadow_stack : 1; // 23 if 1 cpu supports supervisor shadow stack
        UINT64 reserved5 : 1;
        UINT64 single_ctx_invept : 1; // 25 if 1 cpu supports the single context invept instruction
        UINT64 all_ctx_invept : 1; // 26 if 1 cpu supports the all context invept instruction
        UINT64 reserved6 : 5;
        UINT64 invvpid : 1; // 32
        UINT64 reserved7 : 7;
        UINT64 individual_address_invvpid : 1; // 40
        UINT64 single_ctx_invvpid : 1; // 41
        UINT64 all_ctx_invvpid : 1; // 42
        UINT64 single_ctx_retain_global_invvpid : 1; // 43
        UINT64 reserved8 : 4;
        UINT64 hlat_max_prefix_size : 6; // 53:48
        UINT64 reserved9 : 10;
    } fields;
} IA32_VMX_EPT_VPID_CAP_MSR, * PIA32_VMX_EPT_VPID_CAP_MSR;

typedef struct _MEMORY_REGION
{
    UINT64 start_address;
    UINT64 end_address;
    UINT64 memory_type;
    UINT64 size;
} MEMORY_REGION, * PMEMORY_REGION;

typedef struct _EPT_PTE_BUFFER
{
    UINT64 start_phys_address;
    UINT64 start_virt_address;
    UINT64 curr_virt_address;
    UINT64 size;
} EPT_PTE_BUFFER, * PEPT_PTE_BUFFER;

typedef struct _INVEPT_DESCRIPTOR
{
    UINT64 eptp;
    UINT64 reserved1;
} INVEPT_DESCRIPTOR, * PINVEPT_DESCRIPTOR;

extern "C"
{
    void asm_invept(UINT32 type, PINVEPT_DESCRIPTOR descriptor);
}

extern inline int memory_region_cnt = 0;
extern inline PMEMORY_REGION memory_regions = NULL;
extern inline PLINEAR_64b_HASH_MAP g_hook_map = NULL; // stores original pfn -> shadow pfn mappings
extern inline PLINEAR_64b_HASH_MAP g_hook_um_map = NULL; // stores original pfn -> cr3 allowed to "see" the hook

bool mtrr_support();
void populate_mtrr_regions();
UINT8 get_fixed_mtrr_type(UINT64 physical_address);

void init_all_core_eptp();
bool setup_core_eptp(int core);

// splits a 2mb large page into 512 4kb pages keeping the same access rights, returns the new PT
PEPT_PTE split_pde(UINT32 core, UINT64 pd_idx);
// will return NULL if the PDE has not been split
PEPT_PTE get_ept_pte(UINT32 core, UINT64 guest_physical);
// will return NULL if the PDE has been split
PEPT_PDE_2MB get_ept_pde(UINT32 core, UINT64 guest_physical);

void install_ept_hook(UINT64 virt_target_address, UINT64 destination, UINT64 tramp_buffer, UINT32 core, UINT64 cr3);

bool free_ept_pages(int core);
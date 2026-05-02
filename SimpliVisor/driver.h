#pragma once

#include <ntddk.h>
#include <wdm.h>
#include <intrin.h>
#include <cmath>
#include "ept.h"

__forceinline ULONG64 int_power(ULONG64 base, ULONG64 exp) {
    ULONG64 result = 1;
    while (exp > 0) {
        if (exp % 2 == 1) {
            result *= base;
        }
        exp /= 2;
        base *= base;
    }
    return result;
}

inline UNICODE_STRING DEVICE_NAME = RTL_CONSTANT_STRING(L"\\Device\\SimpliVisor");
inline UNICODE_STRING DEVICE_SYMBOLIC_NAME = RTL_CONSTANT_STRING(L"\\??\\SimpliVisorLink");

using function_t = bool(*)(int);
void run_on_all_cores(function_t func);
void run_on_single_core(function_t func, int core);

typedef struct _PML4E
{
	union
	{
		struct
		{
			ULONG64 Present : 1;              // Must be 1, region invalid if 0.
			ULONG64 ReadWrite : 1;            // If 0, writes not allowed.
			ULONG64 UserSupervisor : 1;       // If 0, user-mode accesses not allowed.
			ULONG64 PageWriteThrough : 1;     // Determines the memory type used to access PDPT.
			ULONG64 PageCacheDisable : 1;     // Determines the memory type used to access PDPT.
			ULONG64 Accessed : 1;             // If 0, this entry has not been used for translation.
			ULONG64 Ignored1 : 1;
			ULONG64 PageSize : 1;             // Must be 0 for PML4E.
			ULONG64 Ignored2 : 4;
			ULONG64 PageFrameNumber : 36;     // The page frame number of the PDPT of this PML4E.
			ULONG64 Reserved : 4;
			ULONG64 Ignored3 : 11;
			ULONG64 ExecuteDisable : 1;       // If 1, instruction fetches not allowed.
		};
		ULONG64 Value;
	};
} PML4E, * PPML4E;

typedef struct _PTE
{
	union
	{
		struct
		{
			ULONG64 Present : 1;              // Must be 1, region invalid if 0.
			ULONG64 ReadWrite : 1;            // If 0, writes not allowed.
			ULONG64 UserSupervisor : 1;       // If 0, user-mode accesses not allowed.
			ULONG64 PageWriteThrough : 1;     // Determines the memory type used to access the memory.
			ULONG64 PageCacheDisable : 1;     // Determines the memory type used to access the memory.
			ULONG64 Accessed : 1;             // If 0, this entry has not been used for translation.
			ULONG64 Dirty : 1;                // If 0, the memory backing this page has not been written to.
			ULONG64 PageAccessType : 1;       // Determines the memory type used to access the memory.
			ULONG64 Global : 1;                // If 1 and the PGE bit of CR4 is set, translations are global.
			ULONG64 Ignored2 : 3;
			ULONG64 PageFrameNumber : 36;     // The page frame number of the backing physical page.
			ULONG64 Reserved : 4;
			ULONG64 Ignored3 : 7;
			ULONG64 ProtectionKey : 4;         // If the PKE bit of CR4 is set, determines the protection key.
			ULONG64 ExecuteDisable : 1;       // If 1, instruction fetches not allowed.
		};
		ULONG64 Value;
	};
} PTE, * PPTE;

typedef struct _HOST_PROCESSOR_DATA
{
    UINT32 core_index;
} HOST_PROCESSOR_DATA, * PHOST_PROCESSOR_DATA;

struct VCPU
{
    PVOID v_vmxon_region;
    PVOID v_vmcs_region;
    PVOID v_msr_bitmap;
    UINT64 p_vmxon_region;
    UINT64 p_vmcs_region;
    UINT64 p_msr_bitmap;
    UINT64 host_stack;

    PVOID v_ept_pml4;
    PVOID v_ept_pdpt;
    PEPT_PDE_2MB pdes;
    EPTP eptp;
    EPT_PTE_BUFFER ept_pte_buffer;

    HOST_PROCESSOR_DATA processor_data;

    PEPT_PTE mtf_target_pte;
    UINT64 mtf_restore_pfn;

    PPTE phys_pte;
    UINT64 phys_window;
};

extern inline VCPU* g_vcpus = NULL;
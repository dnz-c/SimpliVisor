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
    PEPT_PDE_2MB pdes = NULL;
    EPTP eptp = { 0 };
    EPT_PTE_BUFFER ept_pte_buffer = { 0 };

    HOST_PROCESSOR_DATA processor_data;

    PEPT_PTE mtf_target_pte;
    UINT64 mtf_restore_pfn;
};

extern inline VCPU* g_vcpus = NULL;
#pragma once
#include <wdm.h>
#include <ntddk.h>
#include <intrin.h>
#include <cmath>

#define IA32_FEATURE_CONTROL 0x3A
#define IA32_VMX_BASIC 0x480
#define IA32_VMX_CR4_FIXED0 0x488
#define IA32_VMX_CR4_FIXED1 0x489

#define VMXON_TAG 'xmV '

extern "C" {
    PHYSICAL_ADDRESS MmGetPhysicalAddress(PVOID BaseAddress);

    bool asm_virtualize_core(int core);
    void asm_vmx_restore_state(void);
}

typedef union _IA32_VMX_BASIC_MSR
{
    ULONG64 All;
    struct
    {
        ULONG32 RevisionIdentifier : 31;   // [0-30]
        ULONG32 Reserved1 : 1;             // [31]
        ULONG32 RegionSize : 12;           // [32-43]
        ULONG32 RegionClear : 1;           // [44]
        ULONG32 Reserved2 : 3;             // [45-47]
        ULONG32 SupportedIA64 : 1;         // [48]
        ULONG32 SupportedDualMoniter : 1;  // [49]
        ULONG32 MemoryType : 4;            // [50-53]
        ULONG32 VmExitReport : 1;          // [54]
        ULONG32 VmxCapabilityHint : 1;     // [55]
        ULONG32 Reserved3 : 8;             // [56-63]
    } Fields;
} IA32_VMX_BASIC_MSR, * PIA32_VMX_BASIC_MSR;

bool vmx_supported();

// allocate VMX and VMCS regions across all VCPUs
void allocate_vmx_regions();
void free_vmx_regions();

bool reserve_vmxon_region(int core);
bool reserve_vmcs_region(int core);

bool free_vmxon_region(int core);
bool free_vmcs_region(int core);

extern "C" bool enter_vmx_operation(int core, UINT64 rsp);
bool exit_vmx_operation(int core);

void setup_vmcs(int core, int rsp);
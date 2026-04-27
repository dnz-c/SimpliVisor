#pragma once
#include <wdm.h>
#include <ntddk.h>
#include <intrin.h>
#include <cmath>

#define VMCALL_EXITVM 0x1337

// MSRs
#define IA32_FEATURE_CONTROL            0x3A
#define IA32_SYSENTER_CS                0x174
#define IA32_SYSENTER_ESP               0x175
#define IA32_SYSENTER_EIP               0x176
#define IA32_DEBUGCTL                   0x1D9
#define IA32_VMX_BASIC                  0x480
#define IA32_VMX_PINBASED_CTLS          0x481
#define IA32_VMX_PROCBASED_CTLS         0x482
#define IA32_VMX_EXIT_CTLS              0x483
#define IA32_VMX_ENTRY_CTLS             0x484
#define IA32_VMX_MISC                   0x485
#define IA32_VMX_CR0_FIXED0             0x486
#define IA32_VMX_CR0_FIXED1             0x487
#define IA32_VMX_CR4_FIXED0             0x488
#define IA32_VMX_CR4_FIXED1             0x489
#define IA32_VMX_VMCS_ENUM              0x48A
#define IA32_VMX_PROCBASED_CTLS2        0x48B
#define IA32_VMX_EPT_VPID_CAP           0x48C
#define IA32_VMX_TRUE_PINBASED_CTLS     0x48D
#define IA32_VMX_TRUE_PROCBASED_CTLS    0x48E
#define IA32_VMX_TRUE_EXIT_CTLS         0x48F
#define IA32_VMX_TRUE_ENTRY_CTLS        0x490
#define IA32_VMX_VMFUNC                 0x491
#define IA32_DEBUGCTL                   0x1D9
#define IA32_VMX_BASIC                  0x480
#define IA32_VMX_CR0_FIXED0             0x486
#define IA32_VMX_CR0_FIXED1             0x487
#define IA32_VMX_CR4_FIXED0             0x488
#define IA32_VMX_CR4_FIXED1             0x489
#define IA32_EFER                       0xC0000080
#define FS_BASE                         0xC0000100
#define GS_BASE                         0xC0000101

#define VMXON_TAG 'xmV '



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

typedef struct _GUEST_REGS
{
    ULONG64 r15;
    ULONG64 r14;
    ULONG64 r13;
    ULONG64 r12;
    ULONG64 r11;
    ULONG64 r10;
    ULONG64 r9;
    ULONG64 r8;
    ULONG64 rdi;
    ULONG64 rsi;
    ULONG64 rbp;
    ULONG64 rsp_placeholder;
    ULONG64 rbx;
    ULONG64 rdx;
    ULONG64 rcx;
    ULONG64 rax;
    ULONG64 rflags;
} GUEST_REGS, * PGUEST_REGS;

typedef struct _SEGMENT_DESCRIPTOR
{
    USHORT LIMIT0;
    USHORT BASE0;
    UCHAR  BASE1;
    UCHAR  ATTR0;
    UCHAR  LIMIT1ATTR1;
    UCHAR  BASE2;
} SEGMENT_DESCRIPTOR, * PSEGMENT_DESCRIPTOR;

typedef union SEGMENT_ATTRIBUTES
{
    USHORT UCHARs;
    struct
    {
        USHORT TYPE : 4; /* 0;  Bit 40-43 */
        USHORT S : 1;    /* 4;  Bit 44 */
        USHORT DPL : 2;  /* 5;  Bit 45-46 */
        USHORT P : 1;    /* 7;  Bit 47 */

        USHORT AVL : 1; /* 8;  Bit 52 */
        USHORT L : 1;   /* 9;  Bit 53 */
        USHORT DB : 1;  /* 10; Bit 54 */
        USHORT G : 1;   /* 11; Bit 55 */
        USHORT GAP : 4;

    } Fields;
} SEGMENT_ATTRIBUTES;

typedef struct SEGMENT_SELECTOR
{
    USHORT             SEL;
    SEGMENT_ATTRIBUTES ATTRIBUTES;
    ULONG32            LIMIT;
    ULONG64            BASE;
} SEGMENT_SELECTOR, * PSEGMENT_SELECTOR;

typedef union _MSR
{
    struct
    {
        ULONG Low;
        ULONG High;
    };

    ULONG64 Content;
} MSR, * PMSR;

enum SEGREGS
{
    ES = 0,
    CS,
    SS,
    DS,
    FS,
    GS,
    LDTR,
    TR
};

extern "C" {
    PHYSICAL_ADDRESS MmGetPhysicalAddress(PVOID BaseAddress);
    
    void asm_vmcall(UINT64 arg1);
    
    bool asm_virtualize_core(int core);
    void asm_vmx_restore_state(void);
    void asm_vmexit_handler(void);

    void asm_exit_vm(UINT64 rip, UINT64 rsp, UINT64 rflags, UINT64 cs, UINT64 ss, PGUEST_REGS regs);

    USHORT inline get_cs(void);
    USHORT inline get_ds(void);
    USHORT inline get_es(void);
    USHORT inline get_ss(void);
    USHORT inline get_fs(void);
    USHORT inline get_gs(void);
    USHORT inline get_ldtr(void);
    USHORT inline get_tr(void);
    USHORT inline get_idt_limit(void);
    USHORT inline get_gdt_limit(void);
    USHORT inline get_rflags(void);

    ULONG64 inline get_gdt_base(void);
    ULONG64 inline get_idt_base(void);
}

static bool TRUE_MSR_SUPPORT = false;

// determines vmx support and vm feature supports
bool vmx_supported();

// allocate VMX and VMCS regions across all VCPUs
void allocate_vmx_regions();
void free_vmx_regions();

bool reserve_vmxon_region(int core);
bool reserve_vmcs_region(int core);
bool reserve_msr_bitmap_region(int core);

bool free_vmxon_region(int core);
bool free_vmcs_region(int core);
bool free_msr_bitmap_region(int core);

extern "C" bool enter_vmx_operation(int core, ULONG64 rsp);
bool exit_vmx_operation(int core);

bool get_segment_descriptor(IN PSEGMENT_SELECTOR segment_selector, IN USHORT selector, IN void* gdt_base);
void fill_guest_selector_data(PVOID gdt_base, ULONG segreg, USHORT selector);

void setup_vmcs(int core, ULONG64 rsp);

extern "C" bool vmexit_handler(PGUEST_REGS regs);
#pragma once
#include <wdm.h>
#include <ntddk.h>
#include <intrin.h>
#include <cmath>
#include "driver.h"

#define MAX_VMEXIT_REASON 75

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

typedef struct _EXIT_CONTEXT
{
    PHOST_PROCESSOR_DATA host_data;
    PGUEST_REGS regs;
    BOOLEAN invalidate_tlb;
    BOOLEAN advance_rip;
} EXIT_CONTEXT, * PEXIT_CONTEXT;

#define TYPE_MOV_TO_CR 0
#define TYPE_MOV_FROM_CR 1
#define TYPE_CLTS 2
#define TYPE_LMSW 3

typedef void (*t_VMEXIT_HANDLER)(PEXIT_CONTEXT);
extern inline t_VMEXIT_HANDLER g_vmexit_handlers[MAX_VMEXIT_REASON] = { NULL };

void init_vmexit_dispatch_table();

void handle_cpuid(PEXIT_CONTEXT ctx);
void handle_vmcall(PEXIT_CONTEXT ctx);
void handle_cr_access(PEXIT_CONTEXT ctx);
void handle_rdmsr(PEXIT_CONTEXT ctx);
void handle_wrmsr(PEXIT_CONTEXT ctx);
void handle_mtf(PEXIT_CONTEXT ctx);
void handle_ept_violation(PEXIT_CONTEXT ctx);
void handle_unsupported(PEXIT_CONTEXT ctx);
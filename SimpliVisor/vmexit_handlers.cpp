#include "vmexit_handlers.h"
#include "vmcs.h"
#include "vmx.h"
#include "ept.h"

void init_vmexit_dispatch_table()
{
    for (size_t i = 0; i < MAX_VMEXIT_REASON; i++)
    {
        g_vmexit_handlers[i] = handle_unsupported;
    }

    g_vmexit_handlers[EXIT_REASON::CPUID] = handle_cpuid;
    g_vmexit_handlers[EXIT_REASON::VMCALL] = handle_vmcall;
    g_vmexit_handlers[EXIT_REASON::RDMSR] = handle_rdmsr;
    g_vmexit_handlers[EXIT_REASON::WRMSR] = handle_wrmsr;
    g_vmexit_handlers[EXIT_REASON::EPT_VIOLATION] = handle_ept_violation;
}

void handle_cpuid(PEXIT_CONTEXT ctx)
{
    int cpu_info[4];
    __cpuidex(cpu_info, (int) ctx->regs->rax, (int) ctx->regs->rcx);

    if (ctx->regs->rax == 1)
    {
        cpu_info[2] &= ~(1 << 5);
        cpu_info[2] &= ~(1 << 31);
    }

    else if (ctx->regs->rax == 0x40000000)
    {
        cpu_info[0] = 0;
        cpu_info[1] = 0;
        cpu_info[2] = 0;
        cpu_info[3] = 0;
    }

    else if (ctx->regs->rax == 0x40000001)
    {
        cpu_info[0] = 0x1337;
        cpu_info[1] = 0x1337;
        cpu_info[2] = 0x1337;
        cpu_info[3] = 0x1337;
    }

    ctx->regs->rax = (ULONG64) (unsigned int) cpu_info[0];
    ctx->regs->rbx = (ULONG64) (unsigned int) cpu_info[1];
    ctx->regs->rcx = (ULONG64) (unsigned int) cpu_info[2];
    ctx->regs->rdx = (ULONG64) (unsigned int) cpu_info[3];
}

void handle_vmcall(PEXIT_CONTEXT ctx)
{
    if (ctx->regs->rcx == VMCALL_EXITVM)
    {
        DbgPrint("VMEXIT on core: %d\n", ctx->host_data->core_index);

        UINT64 guest_rsp, guest_rip, guest_eflags, guest_cs, guest_ss;
        __vmx_vmread(GUEST_RSP, &guest_rsp);
        __vmx_vmread(GUEST_RIP, &guest_rip);
        __vmx_vmread(GUEST_RFLAGS, &guest_eflags);
        __vmx_vmread(GUEST_SS_SELECTOR, &guest_ss);
        __vmx_vmread(GUEST_CS_SELECTOR, &guest_cs);

        // advance rip so we don't instantly vmcall again when we return to the OS
        size_t inst_len;
        __vmx_vmread(VM_EXIT_INSTRUCTION_LEN, &inst_len);
        guest_rip += inst_len;

        size_t guest_fs_base, guest_fs_selector;
        __vmx_vmread(GUEST_FS_BASE, &guest_fs_base);
        __writemsr(FS_BASE, guest_fs_base);

        __vmx_off();

        asm_exit_vm(guest_rip, guest_rsp, guest_eflags, guest_cs, guest_ss, ctx->regs);

        // we will never reach this part of code, if we do our iretq stack frame is messed up and we can't do anything
        DbgBreakPoint();
    }
}

void handle_rdmsr(PEXIT_CONTEXT ctx)
{
    ctx->regs->rax = 0;
    ctx->regs->rdx = 0;
}

void handle_wrmsr(PEXIT_CONTEXT ctx)
{
    ULONG32 requested_msr = (ULONG32) ctx->regs->rcx;
    ULONG64 value = (ctx->regs->rax & 0xFFFFFFFF) | ((ULONG64) ctx->regs->rdx << 32);

    // forward hyperv enlightenment calls
    if (requested_msr >= 0x40000000 && requested_msr <= 0x400000FF)
    {
        __writemsr(requested_msr, value);
    }
}

void handle_ept_violation(PEXIT_CONTEXT ctx)
{
    size_t faulting_phys_addr = 0;
    __vmx_vmread(GUEST_PHYSICAL_ADDRESS, &faulting_phys_addr);

    PEPT_PTE pte = get_ept_pte(ctx->host_data->core_index, faulting_phys_addr);
    if (!pte)
        goto Exit;

    DbgPrint("CAUGHT EPT VIOLATION!\n");
    DbgPrint("Faulting Physical Addr: 0x%llX\n", faulting_phys_addr);

    pte->fields.read_access = 1;
    pte->fields.write_access = 1;

    DbgPrint("Restored permissions on PTE %p\n", pte);

    Exit:
    ctx->invalidate_tlb = true;
    ctx->advance_rip = false;
}

void handle_unsupported(PEXIT_CONTEXT ctx)
{
    size_t exit_reason;
    __vmx_vmread(VM_EXIT_REASON, &exit_reason);

    size_t _guest_rip = 0;
    size_t inst_len_debug = 0;
    __vmx_vmread(GUEST_RIP, &_guest_rip);
    __vmx_vmread(VM_EXIT_INSTRUCTION_LEN, &inst_len_debug);

    KeBugCheckEx(0xDEADDEAD, exit_reason, _guest_rip, inst_len_debug, 0);
}
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
    g_vmexit_handlers[EXIT_REASON::CR_ACCESS] = handle_cr_access;
    g_vmexit_handlers[EXIT_REASON::RDMSR] = handle_rdmsr;
    g_vmexit_handlers[EXIT_REASON::WRMSR] = handle_wrmsr;
    g_vmexit_handlers[EXIT_REASON::MTF] = handle_mtf;
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
    switch (ctx->regs->rcx)
    {
    case VMCALL_EXITVM:
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

        size_t guest_cr3;
        __vmx_vmread(GUEST_CR3, &guest_cr3);
        __writecr3(guest_cr3);

        __vmx_off();

        asm_exit_vm(guest_rip, guest_rsp, guest_eflags, guest_cs, guest_ss, ctx->regs);

        // we will never reach this part of code, if we do our iretq stack frame is messed up and we dont know where to go
        DbgBreakPoint();
    }
        break;
    case VMCALL_INSTALLHOOK:
    {
        UINT64 virt_target = ctx->regs->rdx;
        UINT64 destination = ctx->regs->r8;
        UINT64 tramp_buffer = ctx->regs->r9;

        UINT64 cr3;
        __vmx_vmread(GUEST_CR3, &cr3);

        install_ept_hook(virt_target, destination, tramp_buffer, ctx->host_data->core_index, cr3);

        ctx->invalidate_tlb = true;
    }
        break;
    }
}

void handle_cr_access(PEXIT_CONTEXT ctx)
{
    MOV_CR_QUALIFICATION qualification = { 0 };
    __vmx_vmread(EXIT_QUALIFICATION, &qualification.all);

    PUINT64 reg_ptr = (PUINT64) &ctx->regs->rax - qualification.fields.register_;

    UINT64 guest_rsp;
    if (qualification.fields.register_ == 4) // RSP
    {
        __vmx_vmread(GUEST_RSP, &guest_rsp);
        *reg_ptr = guest_rsp;
    }

    switch (qualification.fields.access_type)
    {
    case TYPE_MOV_TO_CR:
    {
        switch (qualification.fields.control_register)
        {
        case 0:
            __vmx_vmwrite(GUEST_CR0, *reg_ptr);
            __vmx_vmwrite(CR0_READ_SHADOW, *reg_ptr);
            break;
        case 3:
            __vmx_vmwrite(GUEST_CR3, *reg_ptr);
            break;
        case 4:
            __vmx_vmwrite(GUEST_CR4, *reg_ptr);
            __vmx_vmwrite(CR4_READ_SHADOW, *reg_ptr);
            break;
        }
    }
    break;
    case TYPE_MOV_FROM_CR:
    {
        switch (qualification.fields.control_register)
        {
        case 0:
            __vmx_vmread(GUEST_CR0, reg_ptr);
            break;
        case 3:
            __vmx_vmread(GUEST_CR3, reg_ptr);
            break;
        case 4:
            __vmx_vmread(GUEST_CR4, reg_ptr);
            break;
        }
    }
    break;
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

void handle_mtf(PEXIT_CONTEXT ctx)
{
    PEPT_PTE pte = g_vcpus[ctx->host_data->core_index].mtf_target_pte;

    if (pte)
    {
        pte->fields.pfn = g_vcpus[ctx->host_data->core_index].mtf_restore_pfn;

        pte->fields.read_access = 1;
        pte->fields.write_access = 1;
        pte->fields.execute_access = 0;

        g_vcpus[ctx->host_data->core_index].mtf_target_pte = NULL;
        g_vcpus[ctx->host_data->core_index].mtf_restore_pfn = 0;
    }

    // disable mtf
    size_t exec_ctrl = 0;
    __vmx_vmread(CPU_BASED_VM_EXEC_CONTROL, &exec_ctrl);
    exec_ctrl &= ~CPU_BASED_MONITOR_TRAP_FLAG;
    __vmx_vmwrite(CPU_BASED_VM_EXEC_CONTROL, exec_ctrl);

    ctx->invalidate_tlb = true;
    ctx->advance_rip = false;
}

void handle_ept_violation(PEXIT_CONTEXT ctx)
{
    size_t faulting_phys_addr = 0;
    __vmx_vmread(GUEST_PHYSICAL_ADDRESS, &faulting_phys_addr);

    EPT_VIOLATION_QUALIFICATION qualification = { 0 };
    __vmx_vmread(EXIT_QUALIFICATION, &qualification.all);

    PEPT_PTE pte = get_ept_pte(ctx->host_data->core_index, faulting_phys_addr);
    if (!pte)
        return;

    // check if this is a function we have a hook on / want to hide
    UINT64 shadow_pfn = 0;
    if (!get_in_hashmap(g_vcpus[ctx->host_data->core_index].hook_map, pte->fields.pfn, &shadow_pfn))
        return;

    // check if this process is allowed to see the hook
    UINT64 allowed_cr3 = 0;
    if (!get_in_hashmap(g_vcpus[ctx->host_data->core_index].hook_um_map, pte->fields.pfn, &allowed_cr3))
        return;

    if (qualification.fields.execute_access)
    {
        UINT64 original_pfn = pte->fields.pfn;

        UINT64 guest_cr3 = 0;
        __vmx_vmread(GUEST_CR3, &guest_cr3);

        // our process just executed the hooked function, inject the shadow page
        if ((allowed_cr3 & ~0xFFFull) == (guest_cr3 & ~0xFFFull))
        {
            pte->fields.pfn = shadow_pfn;
        }

        pte->fields.execute_access = 1;

        g_vcpus[ctx->host_data->core_index].mtf_target_pte = pte;
        g_vcpus[ctx->host_data->core_index].mtf_restore_pfn = original_pfn;

        // enable MTF so we instantly vmexit on the next instruction
        size_t exec_ctrl = 0;
        __vmx_vmread(CPU_BASED_VM_EXEC_CONTROL, &exec_ctrl);
        exec_ctrl |= CPU_BASED_MONITOR_TRAP_FLAG;
        __vmx_vmwrite(CPU_BASED_VM_EXEC_CONTROL, exec_ctrl);
        
        ctx->invalidate_tlb = true;
        ctx->advance_rip = false;
    }
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
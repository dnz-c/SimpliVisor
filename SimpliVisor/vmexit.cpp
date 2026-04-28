#include "vmx.h"
#include "vmcs.h"
#include "ept.h"

bool vmexit_handler(PGUEST_REGS regs)
{
    size_t exit_reason;
    __vmx_vmread(VM_EXIT_REASON, &exit_reason);

    switch (exit_reason)
    {
    case 0xA: // CPUID
        int cpu_info[4];
        __cpuidex(cpu_info, (int) regs->rax, (int) regs->rcx);

        if (regs->rax == 1)
        {
            cpu_info[2] &= ~(1 << 5);
            cpu_info[2] &= ~(1 << 31);
        }

        else if (regs->rax == 0x40000000)
        {
            cpu_info[0] = 0;
            cpu_info[1] = 0;
            cpu_info[2] = 0;
            cpu_info[3] = 0;
        }

        else if (regs->rax == 0x40000001)
        {
            cpu_info[0] = 0x1337;
            cpu_info[1] = 0x1337;
            cpu_info[2] = 0x1337;
            cpu_info[3] = 0x1337;
        }

        regs->rax = (ULONG64) (unsigned int) cpu_info[0];
        regs->rbx = (ULONG64) (unsigned int) cpu_info[1];
        regs->rcx = (ULONG64) (unsigned int) cpu_info[2];
        regs->rdx = (ULONG64) (unsigned int) cpu_info[3];
        break;
    case 0x12: // VMCALL
        if (regs->rcx == VMCALL_EXITVM)
        {
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

            __vmx_off();

            asm_exit_vm(guest_rip, guest_rsp, guest_eflags, guest_cs, guest_ss, regs);
        }
        break;
        // since the msr bitmap is zeroed we should only get weird out of bounds calls here so its best to ignore them / return 0
    case 0x1f: // RDMSR
    {
        regs->rax = 0;
        regs->rdx = 0;
    }
        break;
    case 0x20: // WRMSR
    {
        ULONG32 requested_msr = (ULONG32) regs->rcx;
        ULONG64 value = (regs->rax & 0xFFFFFFFF) | ((ULONG64) regs->rdx << 32);

        // forward hyperv enlightenment calls
        if (requested_msr >= 0x40000000 && requested_msr <= 0x400000FF)
        {
            __writemsr(requested_msr, value);
        }
    }
        break;
    case 0x30: // EPT Violation
    {
        size_t faulting_phys_addr = 0;
        __vmx_vmread(GUEST_PHYSICAL_ADDRESS, &faulting_phys_addr);
        DbgPrint("Caught EPT Violation, violating address %p\n", faulting_phys_addr);

        int pd_idx = faulting_phys_addr / PDE_PAGE_SIZE;
        int pt_idx = (faulting_phys_addr % PDE_PAGE_SIZE) / PAGE_SIZE;

        EPT_PDE pde = { 0 };
        pde.all = g_pdes[pd_idx].all;

        UINT64 pt_phys = pde.fields.pfn * PAGE_SIZE;

        UINT64 offset = pt_phys - ept_pte_buffer.start_phys_address;
        PEPT_PTE pt = (PEPT_PTE) (ept_pte_buffer.start_virt_address + offset);

        INVEPT_DESCRIPTOR desc = { 0 };
        desc.eptp = g_eptp.all;
        asm_invept(1, &desc);
    }
        return true; // dont advance rip on faults
    default:
        size_t _guest_rip = 0;
        size_t inst_len_debug = 0;
        __vmx_vmread(GUEST_RIP, &_guest_rip);
        __vmx_vmread(VM_EXIT_INSTRUCTION_LEN, &inst_len_debug);

        KeBugCheckEx(0xDEADDEAD, exit_reason, _guest_rip, inst_len_debug, 0);
        break;
    }

    size_t inst_len;
    __vmx_vmread(VM_EXIT_INSTRUCTION_LEN, &inst_len);

    size_t guest_rip;
    __vmx_vmread(GUEST_RIP, &guest_rip);
    __vmx_vmwrite(GUEST_RIP, guest_rip + inst_len);
    return true;
}
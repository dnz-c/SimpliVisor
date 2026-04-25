#include "vmx.h"
#include "vmcs.h"

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

        regs->rax = (ULONG64) (unsigned int) cpu_info[0];
        regs->rbx = (ULONG64) (unsigned int) cpu_info[1];
        regs->rcx = (ULONG64) (unsigned int) cpu_info[2];
        regs->rdx = (ULONG64) (unsigned int) cpu_info[3];
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

        if (requested_msr >= 0x40000000 && requested_msr <= 0x400000FF)
        {
            __writemsr(requested_msr, value);
        }
    }
        break;
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
#include "vmx.h"
#include "vmcs.h"

bool vmexit_handler(PGUEST_REGS regs)
{
    size_t exit_reason;
    __vmx_vmread(VM_EXIT_REASON, &exit_reason);

    switch (exit_reason)
    {
    case 0xA: // CPUID
        break;
    case 0x1f: // RDMSR
    {
        ULONG32 msr = (ULONG32) regs->rcx;
        ULONG64 value = __readmsr(msr);

        regs->rax = value & 0xFFFFFFFF;
        regs->rdx = value >> 32;
    }
        break;
    case 0x20: // WRMSR
    {
        ULONG32 msr = (ULONG32) regs->rcx;
        ULONG64 value = (regs->rax & 0xFFFFFFFF) | ((ULONG64) regs->rdx << 32);

        __writemsr(msr, value);
    }
        break;
    default:
        DbgPrint("Unknown exitcode %#llx\n", exit_reason);
        break;
    }

    size_t inst_len;
    __vmx_vmread(VM_EXIT_INSTRUCTION_LEN, &inst_len);

    size_t guest_rip;
    __vmx_vmread(GUEST_RIP, &guest_rip);
    __vmx_vmwrite(GUEST_RIP, guest_rip + inst_len);
    return true;
}
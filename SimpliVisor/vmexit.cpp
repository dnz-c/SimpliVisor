#include "vmexit_handlers.h"
#include "vmx.h"
#include "vmcs.h"
#include "ept.h"
#include "driver.h"

bool vmexit_handler(PGUEST_REGS regs)
{
    EXIT_CONTEXT ctx = { 0 };
    ctx.regs = regs;
    ctx.advance_rip = true;
    ctx.invalidate_tlb = false;

    PHOST_PROCESSOR_DATA host_data = (PHOST_PROCESSOR_DATA) __readmsr(FS_BASE);
    ctx.host_data = host_data;

    size_t exit_reason;
    __vmx_vmread(VM_EXIT_REASON, &exit_reason);

    if (exit_reason < MAX_VMEXIT_REASON)
    {
        g_vmexit_handlers[exit_reason](&ctx);
    }
    else
    {
        handle_unsupported(&ctx);
    }

    if (ctx.invalidate_tlb)
    {
        INVEPT_DESCRIPTOR desc = { 0 };
        desc.eptp = g_vcpus[ctx.host_data->core_index].eptp.all;
        asm_invept(1, &desc);
    }

    if (ctx.advance_rip)
    {
        size_t inst_len;
        __vmx_vmread(VM_EXIT_INSTRUCTION_LEN, &inst_len);

        size_t guest_rip;
        __vmx_vmread(GUEST_RIP, &guest_rip);
        __vmx_vmwrite(GUEST_RIP, guest_rip + inst_len);
    }
    return true;
}
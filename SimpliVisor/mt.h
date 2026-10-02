#pragma once
#pragma once
#include <ntddk.h>

extern "C" {
    VOID NTAPI KeSignalCallDpcDone(
        _In_ PVOID SystemArgument1
    );

    BOOLEAN NTAPI KeSignalCallDpcSynchronize(
        IN PVOID SystemArgument2
    );

    NTKERNELAPI
        VOID
        NTAPI
        KeGenericCallDpc(
            _In_ PKDEFERRED_ROUTINE Routine,
            _In_opt_ PVOID Context
        );

    void asm_vmcall(UINT64 reason, UINT64 arg1, UINT64 arg2, UINT64 arg3, UINT64 arg4);
};

using core_func_t = bool(*)(ULONG);
using vmcall_func_t = void(*)(UINT64, UINT64, UINT64, UINT64, UINT64);

template <typename T>
struct broadcast_ctx {
    T fn;
    volatile LONG failures; // example: aggregate result
};

inline VOID broadcast_core_dpc(PKDPC dpc, PVOID context, PVOID sys_arg1, PVOID sys_arg2)
{
    UNREFERENCED_PARAMETER(dpc);

    auto* ctx = static_cast<broadcast_ctx<core_func_t>*>(context);
    ULONG proc_idx = KeGetCurrentProcessorNumberEx(nullptr);

    DbgPrint("=====================================================\n");
    DbgPrint("Current thread is executing in %d th logical processor.\n", proc_idx);

    if (!ctx->fn(proc_idx))
        InterlockedIncrement(&ctx->failures);

    if (KeSignalCallDpcSynchronize(sys_arg2)) 
    {
        DbgPrint("=====================================================\n");
        DbgPrint("Procedure %p executed on all processors\n", ctx->fn);
    }

    KeSignalCallDpcDone(sys_arg1);
}

inline bool run_on_all_cores(core_func_t fn)
{
    NT_ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    broadcast_ctx<core_func_t> ctx { fn, 0 };
    KeGenericCallDpc(broadcast_core_dpc, &ctx);
    return ctx.failures == 0;
}

inline VOID broadcast_vmcall_dpc(PKDPC dpc, PVOID context, PVOID sys_arg1, PVOID sys_arg2)
{
    UNREFERENCED_PARAMETER(dpc);

    ULONG proc_idx = KeGetCurrentProcessorNumberEx(nullptr);

    DbgPrint("=====================================================\n");
    DbgPrint("Current thread is executing in %d th logical processor.\n", proc_idx);

    UINT64* args = (UINT64*) context;
    asm_vmcall(args[0], args[1], args[2], args[3], args[4]);

    if (KeSignalCallDpcSynchronize(sys_arg2))
    {
        DbgPrint("=====================================================\n");
        DbgPrint("vmcall broadcasted to all processors");
        ExFreePool(args);
    }

    KeSignalCallDpcDone(sys_arg1);
}

// args array can NOT be on stack, will be freed after all processors executed the vmcall
inline void broadcast_vmcall(UINT64 args[5])
{
    NT_ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    KeGenericCallDpc(broadcast_vmcall_dpc, (PVOID)args);
}
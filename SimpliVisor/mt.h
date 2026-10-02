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
};

using function_t = bool(*)(ULONG);

struct broadcast_ctx {
    function_t fn;
    volatile LONG failures; // example: aggregate result
};

inline VOID broadcast_dpc(PKDPC dpc, PVOID context, PVOID sys_arg1, PVOID sys_arg2)
{
    UNREFERENCED_PARAMETER(dpc);

    auto* ctx = static_cast<broadcast_ctx*>(context);

    if (!ctx->fn(KeGetCurrentProcessorNumberEx(nullptr)))
        InterlockedIncrement(&ctx->failures);

    if (KeSignalCallDpcSynchronize(sys_arg2)) {
    }

    KeSignalCallDpcDone(sys_arg1);
}

inline bool run_on_all_cores(function_t fn)
{
    NT_ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    broadcast_ctx ctx { fn, 0 };
    KeGenericCallDpc(broadcast_dpc, &ctx);
    return ctx.failures == 0;
}
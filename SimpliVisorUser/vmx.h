#pragma once
#include <Windows.h>

#define VMCALL_EXITVM 0x1337
#define VMCALL_INSTALLHOOK 0x1338

extern "C"
{
    void asm_vmcall(UINT64 reason, UINT64 arg1, UINT64 arg2, UINT64 arg3, UINT64 arg4);
}
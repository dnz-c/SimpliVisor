#pragma once

#include <wdm.h>
#include <intrin.h>
#include <cmath>

ULONG64 int_power(ULONG64 base, ULONG64 exp) {
    ULONG64 result = 1;
    while (exp > 0) {
        if (exp % 2 == 1) {
            result *= base;
        }
        exp /= 2;
        base *= base;
    }
    return result;
}

UNICODE_STRING DEVICE_NAME = RTL_CONSTANT_STRING(L"\\Device\\SimpliVisor");
UNICODE_STRING DEVICE_SYMBOLIC_NAME = RTL_CONSTANT_STRING(L"\\??\\SimpliVisorLink");

using function_t = bool(*)(void);
void run_on_all_cores(function_t func);
void run_on_single_core(function_t func, int core);
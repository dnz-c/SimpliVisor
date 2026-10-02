#include <iostream>
#include <Windows.h>
#include <intrin.h>

#include "../SimpliVisor/ioctl.h"

#define PAGE_SIZE 0x1000

#include "vmx.h"

PVOID g_trampoline = nullptr; 
PVOID g_target_func_memory = nullptr; // New isolated memory

typedef void(__stdcall* target_func_t)();

void target_func_hook()
{
    printf("\nHYPERVISOR CAUGHT EXECUTION\n");

    target_func_t o_target_func = (target_func_t) g_trampoline;
    return o_target_func();
}

int main()
{
    char vendor[13];
    int reg[4];
    __cpuid(reg, 0x0);

    ((unsigned int*) vendor)[0] = reg[1]; // EBX
    ((unsigned int*) vendor)[1] = reg[3]; // EDX
    ((unsigned int*) vendor)[2] = reg[2]; // ECX
    vendor[12] = '\0';

    printf("CPU Vendor: %s\n", vendor);

    if (strcmp(vendor, "GenuineIntel"))
    {
        printf("Only Intel CPUs supported!\n");
        return 1;
    }

    __cpuid(reg, 0x1);
    bool vmx_support = reg[2] & (1 << 5);
    printf("VMX is %s\n", vmx_support ? "*ON*" : "*OFF*");

    if (!vmx_support)
    {
        printf("Intel VMX operations not supported!\n");
        return 1;
    }

    HANDLE device = CreateFileW(L"\\\\.\\SimpliVisorLink", GENERIC_WRITE | GENERIC_READ | GENERIC_EXECUTE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_SYSTEM, 0);

    if (device == INVALID_HANDLE_VALUE)
    {
        printf_s("Could not open device: 0x%x\n", GetLastError());
        //return 1;
    }

    DeviceIoControl(device, IOCTL_VIRTUALIZE, NULL, NULL, NULL, NULL, NULL, NULL);

    system("pause");

    __cpuid(reg, 0x40000001);
    std::cout << std::hex << "reg[0]: " << reg[0] << "\nreg[1]: " << reg[1] << "\nreg[2]: " << reg[2] << "\nreg[3]: " << reg[3] << std::endl;

    g_target_func_memory = VirtualAlloc(NULL, PAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_target_func_memory) return 1;
    VirtualLock(g_target_func_memory, PAGE_SIZE);

    // mov eax, 1337h
    // ret
    BYTE dummy_function[] = { 0xB8, 0x37, 0x13, 0x00, 0x00, 0xC3 };
    memcpy(g_target_func_memory, dummy_function, sizeof(dummy_function));

    g_trampoline = VirtualAlloc(NULL, PAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_trampoline) return 1;
    VirtualLock(g_trampoline, PAGE_SIZE);
    RtlSecureZeroMemory(g_trampoline, PAGE_SIZE);

    std::cout << "Isolated Target  @ 0x" << std::hex << (UINT64) g_target_func_memory << std::endl;
    std::cout << "Trampoline       @ 0x" << std::hex << (UINT64) g_trampoline << std::endl;
    std::cout << "Hook Payload     @ 0x" << std::hex << (UINT64) target_func_hook << std::dec << std::endl;

    system("pause");
    INSTALL_EPT_HOOK_REQUEST* req = (INSTALL_EPT_HOOK_REQUEST*)malloc(sizeof(INSTALL_EPT_HOOK_REQUEST));
    if (req)
    {
        std::cout << "broadcasting hook to all cores\n";
        req->hook_func = (UINT64) target_func_hook;
        req->target_func = (UINT64) g_target_func_memory;
        req->trampoline = (UINT64) g_trampoline;
        DeviceIoControl(device, IOCTL_INSTALL_EPT_HOOK, (LPVOID) req, sizeof(INSTALL_EPT_HOOK_REQUEST), NULL, NULL, NULL, NULL);
    }
    system("pause");

    target_func_t isolated_func = (target_func_t) g_target_func_memory;
    for (int i = 1; i <= 3; i++)
    {
        std::cout << "calling isolated target_func... (" << i << "/3)" << std::endl;
        isolated_func();
    }

    system("pause");
    DeviceIoControl(device, IOCTL_DEVIRTUALIZE, NULL, NULL, NULL, NULL, NULL, NULL);
    CloseHandle(device);
    return 0;
}
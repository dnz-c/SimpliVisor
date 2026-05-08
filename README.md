# SimpliVisor (W.I.P)

![Logo](logo.png)

A simple implementation of a thin type 2 hypervisor running on intel-vtx


## Features

- Virtualize an already running operating system
- EPT Implementation that respects hardware caching types
- Stealth EPT Hooks using shadow pages and MTF stepping
- Hook functions from usermode


## Usage/Examples

Hook a function from usermode:
```c
#include <Windows.h>
#include <intrin.h>

#define PAGE_SIZE 0x1000

#include "vmx.h"

PVOID g_trampoline = nullptr; 
PVOID g_target_func_memory = nullptr;

typedef void(__stdcall* target_func_t)();

void target_func_hook()
{
    printf("\nHYPERVISOR CAUGHT EXECUTION\n");

    target_func_t o_target_func = (target_func_t) g_trampoline;
    return o_target_func();
}

void broadcast_hook_to_all_cores(UINT64 target_func, UINT64 payload_func, UINT64 tramp_buffer)
{
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    DWORD num_cores = sys_info.dwNumberOfProcessors;

    std::cout << "detected " << num_cores << " cores" << std::endl;

    HANDLE curr_thread = GetCurrentThread();
    DWORD_PTR affinity = 0;

    for (DWORD i = 0; i < num_cores; i++)
    {
        DWORD_PTR core_mask = (DWORD_PTR) 1 << i;

        DWORD_PTR prev_mask = SetThreadAffinityMask(curr_thread, core_mask);
        if (i == 0) affinity = prev_mask;

        SwitchToThread();

        asm_vmcall(VMCALL_INSTALLHOOK, target_func, payload_func, tramp_buffer, 0);

        std::cout << "hook installed on core: " << i << std::endl;
    }

    if (affinity)
    {
        SetThreadAffinityMask(curr_thread, affinity);
        SwitchToThread();
    }

    std::cout << "sent hook to all cores (hopefully)" << std::endl;
}

int main()
{
    // virtualize the system
    HANDLE device = CreateFileW(L"\\\\.\\SimpliVisorLink", GENERIC_WRITE | GENERIC_READ | GENERIC_EXECUTE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_SYSTEM, 0);

    if (device == INVALID_HANDLE_VALUE)
    {
        printf_s("Could not open device: 0x%x\n", GetLastError());
        //return 1;
    }

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

    broadcast_hook_to_all_cores((UINT64) g_target_func_memory, (UINT64) &target_func_hook, (UINT64) g_trampoline);

    target_func_t isolated_func = (target_func_t) g_target_func_memory;
    for (int i = 1; i <= 3; i++)
    {
        std::cout << "calling isolated target_func... (" << i << "/3)" << std::endl;
        isolated_func();
    }

    system("pause");
    CloseHandle(device);
    return 0;
}
```


## Roadmap

- Add protection of the hypervisors own pages using EPT

- Add a launch mechanism for use with kdmapper

- Add stealth options such as TSC spoofing or intercepting MSR reads

- Add interrupt handling

- Add better process filtering using the CR access vmexit and a EPTP copy containing all hooks



## Authors

- [@dnz-c](https://www.github.com/dnz-c)


## Credits

[Hypervisor from Scratch](https://github.com/SinaKarvandi/Hypervisor-From-Scratch) by Sina Karvandi for his great writeup on how Hypervisors work and his segment selector logic

[Gemini](https://gemini.google.com) for debugging support and IA32 architecture specifics

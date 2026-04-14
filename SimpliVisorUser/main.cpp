#include <iostream>
#include <Windows.h>
#include <intrin.h>

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
        printf_s("> Could not open device: 0x%x\n", GetLastError());
        return 1;
    }

    system("pause");

    CloseHandle(device);

    return 0;
}
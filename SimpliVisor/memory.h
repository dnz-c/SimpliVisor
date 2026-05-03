#pragma once
#include <ntddk.h>
#include <intrin.h>

UINT64 get_selfref_index();
bool setup_hv_phys_window(int core); // setup a pte we can use in vmx root mode to read physical memory
bool free_hv_phys_window(int core);

bool read_physical(UINT64 phys, UINT64 buf, size_t cnt, int core);
bool write_physical(UINT64 phys, UINT64 buf, size_t cnt, int core);
UINT64 virt_to_phys(UINT64 virt, UINT64 pml4, int core);
bool read_virt(UINT64 virt, UINT64 pml4, UINT64 buf, size_t cnt, int core);
bool write_virt(UINT64 virt, UINT64 pml4, UINT64 buf, size_t cnt, int core);

bool test_hv_phys_window();
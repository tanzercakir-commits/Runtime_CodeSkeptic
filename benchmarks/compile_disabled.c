// SPDX-License-Identifier: Apache-2.0
#define RS_MONITOR_COMPILE_DISABLED 1

#include <stdint.h>

#if defined(_WIN32)
#include "runtimeskeptic/runtime/runtime_windows.h"
#else
#include "runtimeskeptic/runtime/runtime_posix.h"
#endif

int rs_benchmark_compile_disabled_cycle(void) {
#if defined(_WIN32)
    void* memory = rs_virtual_alloc_v1(
        0, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (memory == 0) return 1;
    return rs_virtual_free_v1(memory, 0, MEM_RELEASE) ? 0 : 1;
#else
#if defined(MAP_ANONYMOUS)
    const int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#else
    const int flags = MAP_PRIVATE | MAP_ANON;
#endif
    void* memory = rs_mmap_v1(0, 4096, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (memory == MAP_FAILED) return 1;
    return rs_munmap_v1(memory, 4096) == 0 ? 0 : 1;
#endif
}

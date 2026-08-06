// SPDX-License-Identifier: Apache-2.0
#ifndef RUNTIMESKEPTIC_RUNTIME_RUNTIME_POSIX_H
#define RUNTIMESKEPTIC_RUNTIME_RUNTIME_POSIX_H

#if defined(_WIN32)
#error "runtime_posix.h is only available on POSIX platforms"
#endif

#include <sys/mman.h>
#include <sys/types.h>

#include "runtimeskeptic/runtime/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(RS_MONITOR_COMPILE_DISABLED)
#define rs_mmap_v1 mmap
#define rs_mprotect_v1 mprotect
#define rs_munmap_v1 munmap
static inline void* rs_mmap_checked_v1(
    void* address, size_t size, int protection, int flags, int descriptor,
    off_t offset, const rs_vm_expectation_v1* expectation) {
    (void)expectation;
    return mmap(address, size, protection, flags, descriptor, offset);
}
#else
RS_RUNTIME_API void* RS_RUNTIME_CALL rs_mmap_v1(
    void* address, size_t size, int protection, int flags, int descriptor,
    off_t offset);
RS_RUNTIME_API void* RS_RUNTIME_CALL rs_mmap_checked_v1(
    void* address, size_t size, int protection, int flags, int descriptor,
    off_t offset, const rs_vm_expectation_v1* expectation);
RS_RUNTIME_API int RS_RUNTIME_CALL rs_mprotect_v1(
    void* address, size_t size, int protection);
RS_RUNTIME_API int RS_RUNTIME_CALL rs_munmap_v1(void* address, size_t size);
#endif

#ifdef __cplusplus
}
#endif

#endif  // RUNTIMESKEPTIC_RUNTIME_RUNTIME_POSIX_H

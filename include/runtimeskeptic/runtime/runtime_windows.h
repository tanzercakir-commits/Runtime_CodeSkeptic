// SPDX-License-Identifier: Apache-2.0
#ifndef RUNTIMESKEPTIC_RUNTIME_RUNTIME_WINDOWS_H
#define RUNTIMESKEPTIC_RUNTIME_RUNTIME_WINDOWS_H

#if !defined(_WIN32)
#error "runtime_windows.h is only available on Windows"
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "runtimeskeptic/runtime/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(RS_MONITOR_COMPILE_DISABLED)
#define rs_virtual_alloc_v1 VirtualAlloc
#define rs_virtual_protect_v1 VirtualProtect
#define rs_virtual_free_v1 VirtualFree
static inline LPVOID WINAPI rs_virtual_alloc_checked_v1(
    LPVOID address, SIZE_T size, DWORD allocation_type, DWORD protection,
    const rs_vm_expectation_v1* expectation) {
    (void)expectation;
    return VirtualAlloc(address, size, allocation_type, protection);
}
#else
RS_RUNTIME_API LPVOID WINAPI rs_virtual_alloc_v1(
    LPVOID address, SIZE_T size, DWORD allocation_type, DWORD protection);
RS_RUNTIME_API LPVOID WINAPI rs_virtual_alloc_checked_v1(
    LPVOID address, SIZE_T size, DWORD allocation_type, DWORD protection,
    const rs_vm_expectation_v1* expectation);
RS_RUNTIME_API BOOL WINAPI rs_virtual_protect_v1(
    LPVOID address, SIZE_T size, DWORD new_protection,
    PDWORD old_protection);
RS_RUNTIME_API BOOL WINAPI rs_virtual_free_v1(
    LPVOID address, SIZE_T size, DWORD free_type);
#endif

#ifdef __cplusplus
}
#endif

#endif  // RUNTIMESKEPTIC_RUNTIME_RUNTIME_WINDOWS_H

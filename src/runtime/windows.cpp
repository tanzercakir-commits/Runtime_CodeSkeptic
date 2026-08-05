// SPDX-License-Identifier: Apache-2.0
#if defined(_WIN32)

#include "runtimeskeptic/runtime/runtime_windows.h"

#include <cstdint>
#include <limits>

#include "runtime_internal.hpp"

namespace {

uint64_t g_page_size = 0;

uint64_t address_of(const void* value) {
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(value));
}

uint64_t align_down(uint64_t value) noexcept {
    return g_page_size == 0 ? 0 : value - (value % g_page_size);
}

bool align_up(uint64_t value, uint64_t& aligned) noexcept {
    if (g_page_size == 0 ||
        value > std::numeric_limits<uint64_t>::max() - (g_page_size - 1)) {
        return false;
    }
    aligned = ((value + g_page_size - 1) / g_page_size) * g_page_size;
    return true;
}

void set_effective_range(rs_vm_event_v1& event, uint64_t range_start,
                         uint64_t effective_start) noexcept {
    uint64_t effective_end = 0;
    if (event.size == 0 ||
        range_start > std::numeric_limits<uint64_t>::max() - event.size ||
        !align_up(range_start + event.size, effective_end) ||
        effective_end < effective_start) {
        return;
    }
    event.effective_address = effective_start;
    event.effective_size = effective_end - effective_start;
}

uint32_t allocation_operation(DWORD allocation_type) noexcept {
    if ((allocation_type & MEM_RESERVE) != 0) {
        return RS_VM_OPERATION_WINDOWS_RESERVE_V1;
    }
    if ((allocation_type & MEM_COMMIT) != 0) {
        return RS_VM_OPERATION_WINDOWS_COMMIT_V1;
    }
#if defined(MEM_RESET_UNDO)
    if ((allocation_type & MEM_RESET_UNDO) != 0) {
        return RS_VM_OPERATION_WINDOWS_RESET_UNDO_V1;
    }
#endif
    if ((allocation_type & MEM_RESET) != 0) {
        return RS_VM_OPERATION_WINDOWS_RESET_V1;
    }
    return RS_VM_OPERATION_WINDOWS_ALLOCATE_OTHER_V1;
}

void apply_expectation(rs_vm_event_v1& event,
                       const rs_vm_expectation_v1* expectation) {
    if (expectation == nullptr) return;
    if (expectation->abi_version != RS_RUNTIME_ABI_VERSION_V1 ||
        expectation->struct_size != sizeof(rs_vm_expectation_v1) ||
        expectation->reserved_zero != 0 || expectation->reserved[0] != 0 ||
        expectation->reserved[1] != 0 || expectation->reserved[2] != 0) {
        event.violation = RS_VM_VIOLATION_INVALID_EXPECTATION_V1;
        return;
    }
    event.exact_address_required =
        expectation->exact_address_required != 0 ? 1u : 0u;
    event.expected_address = expectation->expected_address;
    if (event.success != 0 && event.exact_address_required != 0 &&
        event.returned_address != event.expected_address) {
        event.violation = RS_VM_VIOLATION_EXACT_ADDRESS_RELOCATED_V1;
    }
}

LPVOID virtual_alloc_once(LPVOID address, SIZE_T size, DWORD allocation_type,
                          DWORD protection,
                          const rs_vm_expectation_v1* expectation) {
    LPVOID const result = VirtualAlloc(address, size, allocation_type, protection);
    const DWORD native_error = GetLastError();
    rs_vm_event_v1 event{};
    event.platform = RS_RUNTIME_PLATFORM_WINDOWS_V1;
    event.operation = allocation_operation(allocation_type);
    event.success = result != nullptr ? 1u : 0u;
    event.native_error_domain = RS_NATIVE_ERROR_WIN32_V1;
    event.native_error_code = static_cast<int64_t>(native_error);
    event.requested_address = address_of(address);
    event.returned_address = result != nullptr ? address_of(result) : 0;
    event.size = static_cast<uint64_t>(size);
    if (event.success != 0) {
        const uint64_t requested_start =
            address != nullptr ? address_of(address) : event.returned_address;
        set_effective_range(event, requested_start, event.returned_address);
    }
    event.native_flags = static_cast<uint32_t>(allocation_type);
    if ((allocation_type & MEM_RESERVE) != 0) {
        event.semantic_flags |= RS_VM_SEMANTIC_RESERVE_V1;
    }
    if ((allocation_type & MEM_COMMIT) != 0) {
        event.semantic_flags |= RS_VM_SEMANTIC_COMMIT_V1;
    }
    event.requested_protection = static_cast<uint32_t>(protection);
    apply_expectation(event, expectation);
    rs::runtime::internal::record_event(event);
    SetLastError(native_error);
    return result;
}

}  // namespace

namespace rs::runtime::internal {

bool initialize_platform_metrics() noexcept {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    g_page_size = static_cast<uint64_t>(info.dwPageSize);
    return g_page_size != 0;
}

}  // namespace rs::runtime::internal

extern "C" {

LPVOID WINAPI rs_virtual_alloc_v1(LPVOID address, SIZE_T size,
                                  DWORD allocation_type, DWORD protection) {
    return virtual_alloc_once(address, size, allocation_type, protection,
                              nullptr);
}

LPVOID WINAPI rs_virtual_alloc_checked_v1(
    LPVOID address, SIZE_T size, DWORD allocation_type, DWORD protection,
    const rs_vm_expectation_v1* expectation) {
    return virtual_alloc_once(address, size, allocation_type, protection,
                              expectation);
}

BOOL WINAPI rs_virtual_protect_v1(LPVOID address, SIZE_T size,
                                  DWORD new_protection,
                                  PDWORD old_protection) {
    const BOOL result = VirtualProtect(address, size, new_protection,
                                       old_protection);
    const DWORD native_error = GetLastError();
    rs_vm_event_v1 event{};
    event.platform = RS_RUNTIME_PLATFORM_WINDOWS_V1;
    event.operation = RS_VM_OPERATION_WINDOWS_PROTECT_V1;
    event.success = result != FALSE ? 1u : 0u;
    event.native_error_domain = RS_NATIVE_ERROR_WIN32_V1;
    event.native_error_code = static_cast<int64_t>(native_error);
    event.requested_address = address_of(address);
    event.returned_address = event.success != 0 ? address_of(address) : 0;
    event.size = static_cast<uint64_t>(size);
    if (event.success != 0) {
        const uint64_t requested_start = address_of(address);
        set_effective_range(event, requested_start, align_down(requested_start));
    }
    event.requested_protection = static_cast<uint32_t>(new_protection);
    event.previous_protection =
        (event.success != 0 && old_protection != nullptr)
            ? static_cast<uint32_t>(*old_protection)
            : 0;
    rs::runtime::internal::record_event(event);
    SetLastError(native_error);
    return result;
}

BOOL WINAPI rs_virtual_free_v1(LPVOID address, SIZE_T size, DWORD free_type) {
    const BOOL result = VirtualFree(address, size, free_type);
    const DWORD native_error = GetLastError();
    rs_vm_event_v1 event{};
    event.platform = RS_RUNTIME_PLATFORM_WINDOWS_V1;
    event.operation = (free_type & MEM_DECOMMIT) != 0
                          ? RS_VM_OPERATION_WINDOWS_DECOMMIT_V1
                          : RS_VM_OPERATION_WINDOWS_RELEASE_V1;
    event.success = result != FALSE ? 1u : 0u;
    event.native_error_domain = RS_NATIVE_ERROR_WIN32_V1;
    event.native_error_code = static_cast<int64_t>(native_error);
    event.requested_address = address_of(address);
    event.returned_address = event.success != 0 ? address_of(address) : 0;
    event.size = static_cast<uint64_t>(size);
    if (event.success != 0) {
        const uint64_t requested_start = address_of(address);
        event.effective_address = requested_start;
        if ((free_type & MEM_DECOMMIT) != 0) {
            set_effective_range(event, requested_start,
                                align_down(requested_start));
        }
    }
    event.native_flags = static_cast<uint32_t>(free_type);
    event.semantic_flags = (free_type & MEM_DECOMMIT) != 0
                               ? RS_VM_SEMANTIC_DECOMMIT_V1
                               : RS_VM_SEMANTIC_RELEASE_V1;
    rs::runtime::internal::record_event(event);
    SetLastError(native_error);
    return result;
}

}  // extern "C"

#endif

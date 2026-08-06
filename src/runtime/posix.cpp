// SPDX-License-Identifier: Apache-2.0
#if !defined(_WIN32)

#include "runtimeskeptic/runtime/runtime_posix.h"

#include <cerrno>
#include <cstdint>
#include <limits>

#include <unistd.h>

#include "runtime_internal.hpp"

namespace {

uint64_t g_page_size = 0;

uint64_t address_of(const void* value) {
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(value));
}

uint64_t effective_size(uint64_t requested_size) noexcept {
    if (requested_size == 0 || g_page_size == 0 ||
        requested_size > std::numeric_limits<uint64_t>::max() -
                             (g_page_size - 1)) {
        return 0;
    }
    return ((requested_size + g_page_size - 1) / g_page_size) * g_page_size;
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

void* mmap_once(void* address, size_t size, int protection, int flags,
                int descriptor, off_t offset,
                const rs_vm_expectation_v1* expectation) {
    void* const result = mmap(address, size, protection, flags, descriptor, offset);
    const int native_error = errno;

    rs_vm_event_v1 event{};
    event.platform = RS_RUNTIME_PLATFORM_POSIX_V1;
    event.operation = RS_VM_OPERATION_POSIX_MMAP_V1;
    event.success = result != MAP_FAILED ? 1u : 0u;
    event.native_error_domain = RS_NATIVE_ERROR_ERRNO_V1;
    event.native_error_code = static_cast<int64_t>(native_error);
    event.requested_address = address_of(address);
    event.returned_address = event.success != 0 ? address_of(result) : 0;
    event.effective_address = event.returned_address;
    event.size = static_cast<uint64_t>(size);
    event.effective_size =
        event.success != 0 ? effective_size(event.size) : 0;
    event.offset = static_cast<int64_t>(offset);
    event.native_flags = static_cast<uint32_t>(flags);
#if defined(MAP_FIXED)
    if ((flags & MAP_FIXED) != 0) {
        event.semantic_flags |= RS_VM_SEMANTIC_REPLACE_V1;
    }
#endif
    event.requested_protection = static_cast<uint32_t>(protection);
    apply_expectation(event, expectation);
    rs::runtime::internal::record_event(event);
    errno = native_error;
    return result;
}

}  // namespace

namespace rs::runtime::internal {

bool initialize_platform_metrics() noexcept {
    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        g_page_size = 0;
        return false;
    }
    g_page_size = static_cast<uint64_t>(page_size);
    return true;
}

}  // namespace rs::runtime::internal

extern "C" {

void* RS_RUNTIME_CALL rs_mmap_v1(void* address, size_t size, int protection,
                                 int flags, int descriptor, off_t offset) {
    return mmap_once(address, size, protection, flags, descriptor, offset,
                     nullptr);
}

void* RS_RUNTIME_CALL rs_mmap_checked_v1(
    void* address, size_t size, int protection, int flags, int descriptor,
    off_t offset, const rs_vm_expectation_v1* expectation) {
    return mmap_once(address, size, protection, flags, descriptor, offset,
                     expectation);
}

int RS_RUNTIME_CALL rs_mprotect_v1(void* address, size_t size,
                                   int protection) {
    const int result = mprotect(address, size, protection);
    const int native_error = errno;
    rs_vm_event_v1 event{};
    event.platform = RS_RUNTIME_PLATFORM_POSIX_V1;
    event.operation = RS_VM_OPERATION_POSIX_MPROTECT_V1;
    event.success = result == 0 ? 1u : 0u;
    event.native_error_domain = RS_NATIVE_ERROR_ERRNO_V1;
    event.native_error_code = static_cast<int64_t>(native_error);
    event.requested_address = address_of(address);
    event.returned_address = event.success != 0 ? address_of(address) : 0;
    event.effective_address = event.returned_address;
    event.size = static_cast<uint64_t>(size);
    event.effective_size =
        event.success != 0 ? effective_size(event.size) : 0;
    event.requested_protection = static_cast<uint32_t>(protection);
    rs::runtime::internal::record_event(event);
    errno = native_error;
    return result;
}

int RS_RUNTIME_CALL rs_munmap_v1(void* address, size_t size) {
    const int result = munmap(address, size);
    const int native_error = errno;
    rs_vm_event_v1 event{};
    event.platform = RS_RUNTIME_PLATFORM_POSIX_V1;
    event.operation = RS_VM_OPERATION_POSIX_MUNMAP_V1;
    event.success = result == 0 ? 1u : 0u;
    event.native_error_domain = RS_NATIVE_ERROR_ERRNO_V1;
    event.native_error_code = static_cast<int64_t>(native_error);
    event.requested_address = address_of(address);
    event.returned_address = event.success != 0 ? address_of(address) : 0;
    event.effective_address = event.returned_address;
    event.size = static_cast<uint64_t>(size);
    event.effective_size =
        event.success != 0 ? effective_size(event.size) : 0;
    event.semantic_flags = RS_VM_SEMANTIC_RELEASE_V1;
    rs::runtime::internal::record_event(event);
    errno = native_error;
    return result;
}

}  // extern "C"

#endif

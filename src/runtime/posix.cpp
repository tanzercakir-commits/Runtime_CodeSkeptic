// SPDX-License-Identifier: Apache-2.0
#if !defined(_WIN32)

#include "runtimeskeptic/runtime/runtime_posix.h"

#include <cerrno>
#include <cstdint>

#include "runtime_internal.hpp"

namespace {

uint64_t address_of(const void* value) {
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(value));
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
    event.size = static_cast<uint64_t>(size);
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
    event.size = static_cast<uint64_t>(size);
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
    event.size = static_cast<uint64_t>(size);
    event.semantic_flags = RS_VM_SEMANTIC_RELEASE_V1;
    rs::runtime::internal::record_event(event);
    errno = native_error;
    return result;
}

}  // extern "C"

#endif

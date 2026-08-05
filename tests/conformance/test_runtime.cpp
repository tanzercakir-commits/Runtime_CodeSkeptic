// SPDX-License-Identifier: Apache-2.0
#include "test_support.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

#include "runtimeskeptic/runtime/runtime.h"
#if defined(_WIN32)
#include "runtimeskeptic/runtime/runtime_windows.h"
#else
#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>
#include "runtimeskeptic/runtime/runtime_posix.h"
#endif

namespace {

rs_runtime_config_v1 config(
    uint32_t capacity, rs_event_callback_v1 callback = nullptr,
    uint32_t mode = RS_MONITOR_MODE_RECORD_V1) {
    rs_runtime_config_v1 value{};
    value.abi_version = RS_RUNTIME_ABI_VERSION_V1;
    value.struct_size = static_cast<uint32_t>(sizeof(value));
    value.mode = mode;
    value.event_capacity = capacity;
    value.event_callback = callback;
    return value;
}

void initialize(uint32_t capacity,
                rs_event_callback_v1 callback = nullptr,
                uint32_t mode = RS_MONITOR_MODE_RECORD_V1) {
    const rs_runtime_config_v1 value = config(capacity, callback, mode);
    RS_CHECK_EQ(rs_runtime_initialize_v1(&value), RS_RUNTIME_OK_V1);
}

#if defined(_WIN32)
void* native_allocate() {
    return VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT,
                        PAGE_READWRITE);
}
void native_release(void* address) {
    if (address != nullptr) (void)VirtualFree(address, 0, MEM_RELEASE);
}
void* wrapped_allocate() {
    return rs_virtual_alloc_v1(nullptr, 4096, MEM_RESERVE | MEM_COMMIT,
                               PAGE_READWRITE);
}
void wrapped_release(void* address) {
    if (address != nullptr) (void)rs_virtual_free_v1(address, 0, MEM_RELEASE);
}
void RS_RUNTIME_CALL clobber_error_and_reenter(const rs_vm_event_v1*, void*) {
    void* nested = rs_virtual_alloc_v1(nullptr, 4096,
                                       MEM_RESERVE | MEM_COMMIT,
                                       PAGE_READWRITE);
    native_release(nested);
    SetLastError(ERROR_ACCESS_DENIED);
}
#else
int anonymous_flags() {
#if defined(MAP_ANONYMOUS)
    return MAP_PRIVATE | MAP_ANONYMOUS;
#else
    return MAP_PRIVATE | MAP_ANON;
#endif
}
void* native_allocate() {
    void* result = mmap(nullptr, 4096, PROT_READ | PROT_WRITE,
                        anonymous_flags(), -1, 0);
    return result == MAP_FAILED ? nullptr : result;
}
void native_release(void* address) {
    if (address != nullptr) (void)munmap(address, 4096);
}
void* wrapped_allocate() {
    void* result = rs_mmap_v1(nullptr, 4096, PROT_READ | PROT_WRITE,
                              anonymous_flags(), -1, 0);
    return result == MAP_FAILED ? nullptr : result;
}
void wrapped_release(void* address) {
    if (address != nullptr) (void)rs_munmap_v1(address, 4096);
}
void RS_RUNTIME_CALL clobber_error_and_reenter(const rs_vm_event_v1*, void*) {
    void* nested = rs_mmap_v1(nullptr, 4096, PROT_READ | PROT_WRITE,
                              anonymous_flags(), -1, 0);
    if (nested != MAP_FAILED) (void)munmap(nested, 4096);
    errno = EACCES;
}
#endif

}  // namespace

RS_TEST(c_abi_layout_is_fixed_for_v1) {
    RS_CHECK_EQ(sizeof(rs_vm_expectation_v1), static_cast<std::size_t>(48));
    RS_CHECK_EQ(sizeof(rs_vm_event_v1), static_cast<std::size_t>(152));
    RS_CHECK_EQ(sizeof(rs_runtime_stats_v1), static_cast<std::size_t>(80));
    const std::size_t expected_config = sizeof(void*) == 8 ? 64u : 56u;
    RS_CHECK_EQ(sizeof(rs_runtime_config_v1), expected_config);
    RS_CHECK_EQ(offsetof(rs_vm_event_v1, sequence), static_cast<std::size_t>(8));
    RS_CHECK_EQ(offsetof(rs_vm_event_v1, requested_address),
                static_cast<std::size_t>(40));
}

RS_TEST(configuration_rejects_unknown_abi_and_nonzero_reserved_fields) {
    rs_runtime_config_v1 value = config(8);
    value.abi_version = 99;
    RS_CHECK_EQ(rs_runtime_initialize_v1(&value),
                RS_RUNTIME_ERROR_UNSUPPORTED_ABI_V1);
    value.abi_version = RS_RUNTIME_ABI_VERSION_V1;
    value.reserved[2] = 1;
    RS_CHECK_EQ(rs_runtime_initialize_v1(&value),
                RS_RUNTIME_ERROR_INVALID_ARGUMENT_V1);
}

RS_TEST(runtime_disabled_is_a_transparent_passthrough) {
    initialize(8);
    rs_runtime_set_enabled_v1(0);
    void* memory = wrapped_allocate();
    RS_CHECK(memory != nullptr);
    wrapped_release(memory);
    rs_runtime_stats_v1 stats{};
    RS_CHECK_EQ(rs_runtime_get_stats_v1(&stats), RS_RUNTIME_OK_V1);
    RS_CHECK_EQ(stats.recorded_events, static_cast<uint64_t>(0));
    rs_runtime_shutdown_v1();
}

RS_TEST(wrapper_failure_matches_native_result_and_error) {
#if defined(_WIN32)
    SetLastError(ERROR_SUCCESS);
    void* native_result = VirtualAlloc(nullptr, 0, MEM_RESERVE, PAGE_NOACCESS);
    const DWORD native_error = GetLastError();
    initialize(8);
    SetLastError(ERROR_SUCCESS);
    void* wrapped_result = rs_virtual_alloc_v1(nullptr, 0, MEM_RESERVE,
                                                PAGE_NOACCESS);
    const DWORD wrapped_error = GetLastError();
    RS_CHECK_EQ(native_result, wrapped_result);
    RS_CHECK_EQ(native_error, wrapped_error);
#else
    errno = 0;
    void* native_result = mmap(nullptr, 0, PROT_NONE, anonymous_flags(), -1, 0);
    const int native_error = errno;
    initialize(8);
    errno = 0;
    void* wrapped_result = rs_mmap_v1(nullptr, 0, PROT_NONE,
                                      anonymous_flags(), -1, 0);
    const int wrapped_error = errno;
    RS_CHECK_EQ(native_result, wrapped_result);
    RS_CHECK_EQ(native_error, wrapped_error);
#endif
    rs_runtime_shutdown_v1();
}

RS_TEST(recorder_and_callback_cannot_change_native_error_state) {
#if defined(_WIN32)
    SetLastError(ERROR_ALREADY_EXISTS);
    void* native_memory = native_allocate();
    const DWORD native_post_error = GetLastError();
    native_release(native_memory);
    initialize(8, clobber_error_and_reenter, RS_MONITOR_MODE_REPORT_V1);
    SetLastError(ERROR_ALREADY_EXISTS);
    void* memory = wrapped_allocate();
    const DWORD observed = GetLastError();
    RS_CHECK(memory != nullptr);
    RS_CHECK_EQ(observed, native_post_error);
#else
    errno = E2BIG;
    void* native_memory = native_allocate();
    const int native_post_error = errno;
    native_release(native_memory);
    initialize(8, clobber_error_and_reenter);
    errno = E2BIG;
    void* memory = wrapped_allocate();
    const int observed = errno;
    RS_CHECK(memory != nullptr);
    RS_CHECK_EQ(observed, native_post_error);
#endif
    native_release(memory);
    rs_runtime_stats_v1 stats{};
    RS_CHECK_EQ(rs_runtime_get_stats_v1(&stats), RS_RUNTIME_OK_V1);
    RS_CHECK_EQ(stats.recorded_events, static_cast<uint64_t>(1));
    RS_CHECK_EQ(stats.reentrant_events, static_cast<uint64_t>(1));
    RS_CHECK_EQ(stats.complete, static_cast<uint32_t>(0));
    rs_runtime_shutdown_v1();
}

RS_TEST(checked_mapping_reports_relocation_without_changing_success) {
    initialize(8);
#if defined(_WIN32)
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const SIZE_T span = static_cast<SIZE_T>(info.dwAllocationGranularity) * 2u;
    void* candidate = VirtualAlloc(nullptr, span, MEM_RESERVE, PAGE_NOACCESS);
    RS_CHECK(candidate != nullptr);
    native_release(candidate);
    auto* unaligned = static_cast<unsigned char*>(candidate) + 1;
    rs_vm_expectation_v1 expectation{};
    expectation.abi_version = RS_RUNTIME_ABI_VERSION_V1;
    expectation.struct_size = static_cast<uint32_t>(sizeof(expectation));
    expectation.exact_address_required = 1;
    expectation.expected_address =
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(unaligned));
    void* mapped = rs_virtual_alloc_checked_v1(
        unaligned, 4096, MEM_RESERVE, PAGE_NOACCESS, &expectation);
    RS_CHECK(mapped != nullptr);
    RS_CHECK(mapped != unaligned);
    native_release(mapped);
#else
    void* occupied = native_allocate();
    RS_CHECK(occupied != nullptr);
    rs_vm_expectation_v1 expectation{};
    expectation.abi_version = RS_RUNTIME_ABI_VERSION_V1;
    expectation.struct_size = static_cast<uint32_t>(sizeof(expectation));
    expectation.exact_address_required = 1;
    expectation.expected_address =
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(occupied));
    void* mapped = rs_mmap_checked_v1(occupied, 4096, PROT_READ,
                                      anonymous_flags(), -1, 0,
                                      &expectation);
    RS_CHECK(mapped != MAP_FAILED);
    RS_CHECK(mapped != occupied);
    native_release(occupied);
    if (mapped != MAP_FAILED) native_release(mapped);
#endif
    std::array<rs_vm_event_v1, 8> events{};
    size_t written = 0;
    RS_CHECK_EQ(rs_runtime_snapshot_v1(events.data(), events.size(), &written),
                RS_RUNTIME_OK_V1);
    RS_CHECK_EQ(written, static_cast<std::size_t>(1));
    RS_CHECK_EQ(events[0].violation,
                static_cast<uint32_t>(
                    RS_VM_VIOLATION_EXACT_ADDRESS_RELOCATED_V1));
    rs_runtime_shutdown_v1();
}

RS_TEST(buffer_overflow_is_visible_and_marks_trace_incomplete) {
    initialize(1);
    void* first = wrapped_allocate();
    void* second = wrapped_allocate();
    native_release(first);
    native_release(second);
    rs_runtime_stats_v1 stats{};
    RS_CHECK_EQ(rs_runtime_get_stats_v1(&stats), RS_RUNTIME_OK_V1);
    RS_CHECK_EQ(stats.recorded_events, static_cast<uint64_t>(1));
    RS_CHECK_EQ(stats.dropped_events, static_cast<uint64_t>(1));
    RS_CHECK_EQ(stats.complete, static_cast<uint32_t>(0));
    rs_runtime_shutdown_v1();
}

RS_TEST(concurrent_calls_receive_one_contiguous_sequence) {
    constexpr std::size_t kThreads = 4;
    constexpr std::size_t kIterations = 24;
    constexpr std::size_t kEvents = kThreads * kIterations * 2;
    initialize(static_cast<uint32_t>(kEvents));
    std::vector<std::thread> threads;
    for (std::size_t thread = 0; thread < kThreads; ++thread) {
        threads.emplace_back([] {
            for (std::size_t i = 0; i < kIterations; ++i) {
                void* memory = wrapped_allocate();
                if (memory != nullptr) wrapped_release(memory);
            }
        });
    }
    for (std::thread& thread : threads) thread.join();
    std::vector<rs_vm_event_v1> events(kEvents);
    size_t written = 0;
    RS_CHECK_EQ(rs_runtime_snapshot_v1(events.data(), events.size(), &written),
                RS_RUNTIME_OK_V1);
    RS_CHECK_EQ(written, kEvents);
    for (std::size_t i = 0; i < written; ++i) {
        RS_CHECK_EQ(events[i].sequence, static_cast<uint64_t>(i + 1));
    }
    rs_runtime_shutdown_v1();
}

#if !defined(_WIN32)
RS_TEST(fork_child_does_not_inherit_an_active_recorder) {
    initialize(8);
    void* memory = wrapped_allocate();
    native_release(memory);
    const pid_t child = fork();
    RS_CHECK(child >= 0);
    if (child == 0) {
        _exit(rs_runtime_is_enabled_v1() == 0 ? 0 : 7);
    }
    int status = 0;
    RS_CHECK_EQ(waitpid(child, &status, 0), child);
    RS_CHECK(WIFEXITED(status));
    RS_CHECK_EQ(WEXITSTATUS(status), 0);
    rs_runtime_shutdown_v1();
}
#endif

RS_TEST_MAIN("runtime wrapper conformance")

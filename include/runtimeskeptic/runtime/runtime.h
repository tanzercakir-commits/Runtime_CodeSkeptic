// SPDX-License-Identifier: Apache-2.0
#ifndef RUNTIMESKEPTIC_RUNTIME_RUNTIME_H
#define RUNTIMESKEPTIC_RUNTIME_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  if defined(RS_RUNTIME_BUILDING_LIBRARY)
#    define RS_RUNTIME_API __declspec(dllexport)
#  else
#    define RS_RUNTIME_API __declspec(dllimport)
#  endif
#  define RS_RUNTIME_CALL __cdecl
#else
#  define RS_RUNTIME_API __attribute__((visibility("default")))
#  define RS_RUNTIME_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define RS_RUNTIME_ABI_VERSION_V1 1u
#define RS_RUNTIME_EVENT_CAPACITY_MAX_V1 4096u

typedef enum rs_runtime_result_v1 {
    RS_RUNTIME_OK_V1 = 0,
    RS_RUNTIME_ERROR_INVALID_ARGUMENT_V1 = 1,
    RS_RUNTIME_ERROR_UNSUPPORTED_ABI_V1 = 2,
    RS_RUNTIME_ERROR_BUSY_V1 = 3,
    RS_RUNTIME_ERROR_IO_V1 = 4,
    RS_RUNTIME_ERROR_INTERNAL_V1 = 5
} rs_runtime_result_v1;

typedef enum rs_monitor_mode_v1 {
    RS_MONITOR_MODE_RECORD_V1 = 1,
    RS_MONITOR_MODE_REPORT_V1 = 2,
    RS_MONITOR_MODE_ASSERT_V1 = 3
} rs_monitor_mode_v1;

typedef enum rs_runtime_platform_v1 {
    RS_RUNTIME_PLATFORM_POSIX_V1 = 1,
    RS_RUNTIME_PLATFORM_WINDOWS_V1 = 2
} rs_runtime_platform_v1;

typedef enum rs_vm_operation_v1 {
    RS_VM_OPERATION_POSIX_MMAP_V1 = 1,
    RS_VM_OPERATION_POSIX_MPROTECT_V1 = 2,
    RS_VM_OPERATION_POSIX_MUNMAP_V1 = 3,
    RS_VM_OPERATION_WINDOWS_RESERVE_V1 = 4,
    RS_VM_OPERATION_WINDOWS_COMMIT_V1 = 5,
    RS_VM_OPERATION_WINDOWS_PROTECT_V1 = 6,
    RS_VM_OPERATION_WINDOWS_DECOMMIT_V1 = 7,
    RS_VM_OPERATION_WINDOWS_RELEASE_V1 = 8
} rs_vm_operation_v1;

typedef enum rs_native_error_domain_v1 {
    RS_NATIVE_ERROR_NONE_V1 = 0,
    RS_NATIVE_ERROR_ERRNO_V1 = 1,
    RS_NATIVE_ERROR_WIN32_V1 = 2
} rs_native_error_domain_v1;

typedef enum rs_vm_violation_v1 {
    RS_VM_VIOLATION_NONE_V1 = 0,
    RS_VM_VIOLATION_EXACT_ADDRESS_RELOCATED_V1 = 1,
    RS_VM_VIOLATION_INVALID_EXPECTATION_V1 = 2
} rs_vm_violation_v1;

typedef enum rs_vm_semantic_flag_v1 {
    RS_VM_SEMANTIC_NONE_V1 = 0,
    RS_VM_SEMANTIC_REPLACE_V1 = 1u << 0,
    RS_VM_SEMANTIC_RESERVE_V1 = 1u << 1,
    RS_VM_SEMANTIC_COMMIT_V1 = 1u << 2,
    RS_VM_SEMANTIC_DECOMMIT_V1 = 1u << 3,
    RS_VM_SEMANTIC_RELEASE_V1 = 1u << 4
} rs_vm_semantic_flag_v1;

typedef struct rs_vm_expectation_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t exact_address_required;
    uint32_t reserved_zero;
    uint64_t expected_address;
    uint64_t reserved[3];
} rs_vm_expectation_v1;

typedef struct rs_vm_event_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    uint64_t sequence;
    uint32_t platform;
    uint32_t operation;
    uint32_t success;
    uint32_t native_error_domain;
    int64_t native_error_code;
    uint64_t requested_address;
    uint64_t returned_address;
    uint64_t expected_address;
    uint64_t size;
    int64_t offset;
    uint32_t native_flags;
    uint32_t semantic_flags;
    uint32_t requested_protection;
    uint32_t previous_protection;
    uint32_t exact_address_required;
    uint32_t violation;
    uint32_t reserved32[4];
    uint64_t reserved64[4];
} rs_vm_event_v1;

typedef void (RS_RUNTIME_CALL *rs_event_callback_v1)(
    const rs_vm_event_v1* event, void* user_data);

typedef struct rs_runtime_config_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t mode;
    uint32_t event_capacity;
    rs_event_callback_v1 event_callback;
    void* callback_user_data;
    uint64_t reserved[4];
} rs_runtime_config_v1;

typedef struct rs_runtime_stats_v1 {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t enabled;
    uint32_t complete;
    uint64_t recorded_events;
    uint64_t dropped_events;
    uint64_t reentrant_events;
    uint64_t next_sequence;
    uint64_t reserved[4];
} rs_runtime_stats_v1;

RS_RUNTIME_API int RS_RUNTIME_CALL rs_runtime_initialize_v1(
    const rs_runtime_config_v1* config);
RS_RUNTIME_API void RS_RUNTIME_CALL rs_runtime_set_enabled_v1(int enabled);
RS_RUNTIME_API int RS_RUNTIME_CALL rs_runtime_is_enabled_v1(void);
RS_RUNTIME_API int RS_RUNTIME_CALL rs_runtime_reset_v1(void);
RS_RUNTIME_API int RS_RUNTIME_CALL rs_runtime_get_stats_v1(
    rs_runtime_stats_v1* stats);
RS_RUNTIME_API int RS_RUNTIME_CALL rs_runtime_snapshot_v1(
    rs_vm_event_v1* events, size_t capacity, size_t* written);
RS_RUNTIME_API int RS_RUNTIME_CALL rs_runtime_flush_trace_v1(const char* path);
RS_RUNTIME_API void RS_RUNTIME_CALL rs_runtime_after_fork_child_v1(void);
RS_RUNTIME_API void RS_RUNTIME_CALL rs_runtime_shutdown_v1(void);

#ifdef __cplusplus
}
#endif

#endif  // RUNTIMESKEPTIC_RUNTIME_RUNTIME_H

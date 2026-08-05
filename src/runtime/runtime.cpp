// SPDX-License-Identifier: Apache-2.0
#include "runtimeskeptic/runtime/runtime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#if defined(RS_PLATFORM_LINUX) || defined(RS_PLATFORM_MACOS)
#include <pthread.h>
#endif

#include "runtimeskeptic/core/json.hpp"
#include "runtimeskeptic/core/sha256.hpp"
#include "runtimeskeptic/version.hpp"
#include "runtime_internal.hpp"

namespace {

struct Slot {
    std::atomic<uint32_t> published{0};
    rs_vm_event_v1 event{};
};

std::array<Slot, RS_RUNTIME_EVENT_CAPACITY_MAX_V1> g_slots{};
std::atomic<uint64_t> g_claimed{0};
std::atomic<uint64_t> g_dropped{0};
std::atomic<uint64_t> g_reentrant{0};
std::atomic<uint32_t> g_capacity{RS_RUNTIME_EVENT_CAPACITY_MAX_V1};
std::atomic<uint32_t> g_mode{RS_MONITOR_MODE_RECORD_V1};
std::atomic<bool> g_enabled{false};
std::atomic<rs_event_callback_v1> g_callback{nullptr};
std::atomic<void*> g_callback_data{nullptr};
thread_local bool g_inside_recorder = false;

bool reserved_zero(const uint64_t* fields, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        if (fields[i] != 0) return false;
    }
    return true;
}

void clear_slots() noexcept {
    g_enabled.store(false, std::memory_order_release);
    for (Slot& slot : g_slots) {
        slot.published.store(0, std::memory_order_relaxed);
        slot.event = rs_vm_event_v1{};
    }
    g_claimed.store(0, std::memory_order_relaxed);
    g_dropped.store(0, std::memory_order_relaxed);
    g_reentrant.store(0, std::memory_order_relaxed);
}

const char* platform_name(uint32_t platform) {
    return platform == RS_RUNTIME_PLATFORM_WINDOWS_V1 ? "windows" : "posix";
}

const char* mode_name(uint32_t mode) {
    switch (mode) {
        case RS_MONITOR_MODE_RECORD_V1: return "record";
        case RS_MONITOR_MODE_REPORT_V1: return "report";
        case RS_MONITOR_MODE_ASSERT_V1: return "assert";
        default: return "unknown";
    }
}

const char* operation_name(uint32_t operation) {
    switch (operation) {
        case RS_VM_OPERATION_POSIX_MMAP_V1: return "posix_mmap";
        case RS_VM_OPERATION_POSIX_MPROTECT_V1: return "posix_mprotect";
        case RS_VM_OPERATION_POSIX_MUNMAP_V1: return "posix_munmap";
        case RS_VM_OPERATION_WINDOWS_RESERVE_V1: return "windows_reserve";
        case RS_VM_OPERATION_WINDOWS_COMMIT_V1: return "windows_commit";
        case RS_VM_OPERATION_WINDOWS_PROTECT_V1: return "windows_protect";
        case RS_VM_OPERATION_WINDOWS_DECOMMIT_V1: return "windows_decommit";
        case RS_VM_OPERATION_WINDOWS_RELEASE_V1: return "windows_release";
        default: return "unknown";
    }
}

const char* error_domain_name(uint32_t domain) {
    switch (domain) {
        case RS_NATIVE_ERROR_ERRNO_V1: return "errno";
        case RS_NATIVE_ERROR_WIN32_V1: return "win32";
        default: return "none";
    }
}

const char* violation_name(uint32_t violation) {
    switch (violation) {
        case RS_VM_VIOLATION_EXACT_ADDRESS_RELOCATED_V1:
            return "exact_address_relocated";
        case RS_VM_VIOLATION_INVALID_EXPECTATION_V1:
            return "invalid_expectation";
        default: return "none";
    }
}

rs::json::Value header_json(uint32_t capacity) {
    rs::json::Value doc = rs::json::Value::object();
    doc["abi_version"] = static_cast<unsigned long long>(RS_RUNTIME_ABI_VERSION_V1);
    doc["buffer_capacity"] = static_cast<unsigned long long>(capacity);
    rs::json::Value apis = rs::json::Value::array();
#if defined(RS_PLATFORM_WINDOWS)
    apis.push_back("VirtualAlloc");
    apis.push_back("VirtualFree");
    apis.push_back("VirtualProtect");
#else
    apis.push_back("mmap");
    apis.push_back("mprotect");
    apis.push_back("munmap");
#endif
    doc["instrumented_apis"] = std::move(apis);
    doc["monitor_mode"] = mode_name(g_mode.load(std::memory_order_acquire));
    doc["platform"] = platform_name(rs::runtime::internal::platform_id());
    doc["record_type"] = "header";
    doc["schema"] = rs::kRuntimeTraceSchemaVersion;
    return doc;
}

rs::json::Value event_json(const rs_vm_event_v1& event) {
    rs::json::Value doc = rs::json::Value::object();
    doc["exact_address_required"] = event.exact_address_required != 0;
    doc["expected_address"] = rs::json::to_hex(event.expected_address);
    doc["native_error_code"] = static_cast<long long>(event.native_error_code);
    doc["native_error_domain"] = error_domain_name(event.native_error_domain);
    doc["native_flags"] = static_cast<unsigned long long>(event.native_flags);
    doc["offset"] = static_cast<long long>(event.offset);
    doc["operation"] = operation_name(event.operation);
    doc["platform"] = platform_name(event.platform);
    doc["previous_protection"] =
        static_cast<unsigned long long>(event.previous_protection);
    doc["record_type"] = "event";
    doc["requested_address"] = rs::json::to_hex(event.requested_address);
    doc["requested_protection"] =
        static_cast<unsigned long long>(event.requested_protection);
    doc["returned_address"] = rs::json::to_hex(event.returned_address);
    doc["schema"] = rs::kRuntimeTraceSchemaVersion;
    doc["semantic_flags"] = static_cast<unsigned long long>(event.semantic_flags);
    doc["sequence"] = static_cast<unsigned long long>(event.sequence);
    doc["size"] = static_cast<unsigned long long>(event.size);
    doc["status"] = event.success != 0 ? "success" : "failure";
    doc["violation"] = violation_name(event.violation);
    return doc;
}

rs::json::Value footer_json(uint64_t recorded, uint64_t dropped,
                            uint64_t reentrant, bool complete,
                            const std::string& digest) {
    rs::json::Value doc = rs::json::Value::object();
    doc["complete"] = complete;
    doc["digest"] = "sha256:" + digest;
    doc["dropped_events"] = static_cast<unsigned long long>(dropped);
    doc["event_count"] = static_cast<unsigned long long>(recorded);
    doc["record_type"] = "footer";
    doc["reentrant_events"] = static_cast<unsigned long long>(reentrant);
    doc["schema"] = rs::kRuntimeTraceSchemaVersion;
    return doc;
}

bool serialize_line(const rs::json::Value& doc, std::string& line) {
    auto encoded = rs::json::serialize_canonical(doc);
    if (!encoded) return false;
    line = std::move(*encoded);
    return true;
}

#if defined(RS_PLATFORM_LINUX) || defined(RS_PLATFORM_MACOS)
void child_after_fork() {
    clear_slots();
    g_callback.store(nullptr, std::memory_order_relaxed);
    g_callback_data.store(nullptr, std::memory_order_relaxed);
}

void register_atfork_once() {
    static const int registered = pthread_atfork(nullptr, nullptr, child_after_fork);
    (void)registered;
}
#else
void register_atfork_once() {}
#endif

}  // namespace

namespace rs::runtime::internal {

uint32_t platform_id() noexcept {
#if defined(RS_PLATFORM_WINDOWS)
    return RS_RUNTIME_PLATFORM_WINDOWS_V1;
#else
    return RS_RUNTIME_PLATFORM_POSIX_V1;
#endif
}

void record_event(rs_vm_event_v1 event) noexcept {
    if (!g_enabled.load(std::memory_order_acquire)) return;
    if (g_inside_recorder) {
        g_reentrant.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    g_inside_recorder = true;
    const uint64_t index = g_claimed.fetch_add(1, std::memory_order_acq_rel);
    const uint32_t capacity = g_capacity.load(std::memory_order_acquire);
    if (index >= capacity) {
        g_dropped.fetch_add(1, std::memory_order_relaxed);
        g_inside_recorder = false;
        return;
    }

    event.abi_version = RS_RUNTIME_ABI_VERSION_V1;
    event.struct_size = static_cast<uint32_t>(sizeof(rs_vm_event_v1));
    event.sequence = index + 1;
    Slot& slot = g_slots[static_cast<std::size_t>(index)];
    slot.event = event;
    slot.published.store(1, std::memory_order_release);

    const rs_event_callback_v1 callback =
        g_callback.load(std::memory_order_acquire);
    if (callback != nullptr) {
        callback(&slot.event, g_callback_data.load(std::memory_order_acquire));
    }

    const bool must_assert =
        g_mode.load(std::memory_order_acquire) == RS_MONITOR_MODE_ASSERT_V1 &&
        event.violation != RS_VM_VIOLATION_NONE_V1;
    g_inside_recorder = false;
    if (must_assert) std::abort();
}

}  // namespace rs::runtime::internal

extern "C" {

int RS_RUNTIME_CALL rs_runtime_initialize_v1(
    const rs_runtime_config_v1* config) {
    uint32_t mode = RS_MONITOR_MODE_RECORD_V1;
    uint32_t capacity = RS_RUNTIME_EVENT_CAPACITY_MAX_V1;
    rs_event_callback_v1 callback = nullptr;
    void* callback_data = nullptr;

    if (config != nullptr) {
        if (config->abi_version != RS_RUNTIME_ABI_VERSION_V1) {
            return RS_RUNTIME_ERROR_UNSUPPORTED_ABI_V1;
        }
        if (config->struct_size != sizeof(rs_runtime_config_v1)) {
            return RS_RUNTIME_ERROR_INVALID_ARGUMENT_V1;
        }
        if (config->mode < RS_MONITOR_MODE_RECORD_V1 ||
            config->mode > RS_MONITOR_MODE_ASSERT_V1 ||
            config->event_capacity == 0 ||
            config->event_capacity > RS_RUNTIME_EVENT_CAPACITY_MAX_V1 ||
            !reserved_zero(config->reserved, 4)) {
            return RS_RUNTIME_ERROR_INVALID_ARGUMENT_V1;
        }
        mode = config->mode;
        capacity = config->event_capacity;
        callback = config->event_callback;
        callback_data = config->callback_user_data;
    }

    clear_slots();
    g_capacity.store(capacity, std::memory_order_release);
    g_mode.store(mode, std::memory_order_release);
    g_callback.store(callback, std::memory_order_release);
    g_callback_data.store(callback_data, std::memory_order_release);
    register_atfork_once();
    g_enabled.store(true, std::memory_order_release);
    return RS_RUNTIME_OK_V1;
}

void RS_RUNTIME_CALL rs_runtime_set_enabled_v1(int enabled) {
    g_enabled.store(enabled != 0, std::memory_order_release);
}

int RS_RUNTIME_CALL rs_runtime_is_enabled_v1(void) {
    return g_enabled.load(std::memory_order_acquire) ? 1 : 0;
}

int RS_RUNTIME_CALL rs_runtime_reset_v1(void) {
    const bool was_enabled = g_enabled.load(std::memory_order_acquire);
    clear_slots();
    g_enabled.store(was_enabled, std::memory_order_release);
    return RS_RUNTIME_OK_V1;
}

int RS_RUNTIME_CALL rs_runtime_get_stats_v1(rs_runtime_stats_v1* stats) {
    if (stats == nullptr) return RS_RUNTIME_ERROR_INVALID_ARGUMENT_V1;
    if ((stats->abi_version != 0 &&
         stats->abi_version != RS_RUNTIME_ABI_VERSION_V1) ||
        (stats->struct_size != 0 &&
         stats->struct_size != sizeof(rs_runtime_stats_v1))) {
        return RS_RUNTIME_ERROR_UNSUPPORTED_ABI_V1;
    }
    const uint64_t claimed = g_claimed.load(std::memory_order_acquire);
    const uint64_t capacity = g_capacity.load(std::memory_order_acquire);
    const uint64_t recorded = std::min(claimed, capacity);
    const uint64_t dropped = g_dropped.load(std::memory_order_acquire);
    const uint64_t reentrant = g_reentrant.load(std::memory_order_acquire);
    *stats = rs_runtime_stats_v1{};
    stats->abi_version = RS_RUNTIME_ABI_VERSION_V1;
    stats->struct_size = static_cast<uint32_t>(sizeof(rs_runtime_stats_v1));
    stats->enabled = g_enabled.load(std::memory_order_acquire) ? 1u : 0u;
    stats->complete = (dropped == 0 && reentrant == 0) ? 1u : 0u;
    stats->recorded_events = recorded;
    stats->dropped_events = dropped;
    stats->reentrant_events = reentrant;
    stats->next_sequence = claimed + 1;
    return RS_RUNTIME_OK_V1;
}

int RS_RUNTIME_CALL rs_runtime_snapshot_v1(
    rs_vm_event_v1* events, size_t capacity, size_t* written) {
    if (written == nullptr) return RS_RUNTIME_ERROR_INVALID_ARGUMENT_V1;
    const uint64_t claimed = g_claimed.load(std::memory_order_acquire);
    const uint64_t configured = g_capacity.load(std::memory_order_acquire);
    const std::size_t count = static_cast<std::size_t>(
        std::min(claimed, configured));
    *written = count;
    if (events == nullptr) {
        return capacity == 0 ? RS_RUNTIME_OK_V1
                             : RS_RUNTIME_ERROR_INVALID_ARGUMENT_V1;
    }
    if (capacity < count) return RS_RUNTIME_ERROR_INVALID_ARGUMENT_V1;
    for (std::size_t i = 0; i < count; ++i) {
        if (g_slots[i].published.load(std::memory_order_acquire) == 0) {
            return RS_RUNTIME_ERROR_BUSY_V1;
        }
        events[i] = g_slots[i].event;
    }
    return RS_RUNTIME_OK_V1;
}

int RS_RUNTIME_CALL rs_runtime_flush_trace_v1(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return RS_RUNTIME_ERROR_INVALID_ARGUMENT_V1;
    }

    size_t count = 0;
    int result = rs_runtime_snapshot_v1(nullptr, 0, &count);
    if (result != RS_RUNTIME_OK_V1) return result;
    std::vector<rs_vm_event_v1> events(count);
    if (count != 0) {
        result = rs_runtime_snapshot_v1(events.data(), events.size(), &count);
        if (result != RS_RUNTIME_OK_V1) return result;
    }

    rs_runtime_stats_v1 stats{};
    result = rs_runtime_get_stats_v1(&stats);
    if (result != RS_RUNTIME_OK_V1) return result;

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return RS_RUNTIME_ERROR_IO_V1;

    rs::hash::Sha256 digest;
    std::string line;
    if (!serialize_line(header_json(g_capacity.load(std::memory_order_acquire)),
                        line)) {
        return RS_RUNTIME_ERROR_INTERNAL_V1;
    }
    output << line << '\n';
    digest.update(line);
    digest.update("\n");

    for (const rs_vm_event_v1& event : events) {
        if (!serialize_line(event_json(event), line)) {
            return RS_RUNTIME_ERROR_INTERNAL_V1;
        }
        output << line << '\n';
        digest.update(line);
        digest.update("\n");
    }

    if (!serialize_line(footer_json(
            static_cast<uint64_t>(count), stats.dropped_events,
            stats.reentrant_events, stats.complete != 0, digest.hex_digest()),
            line)) {
        return RS_RUNTIME_ERROR_INTERNAL_V1;
    }
    output << line << '\n';
    output.flush();
    return output ? RS_RUNTIME_OK_V1 : RS_RUNTIME_ERROR_IO_V1;
}

void RS_RUNTIME_CALL rs_runtime_after_fork_child_v1(void) {
    clear_slots();
    g_callback.store(nullptr, std::memory_order_release);
    g_callback_data.store(nullptr, std::memory_order_release);
}

void RS_RUNTIME_CALL rs_runtime_shutdown_v1(void) {
    g_enabled.store(false, std::memory_order_release);
    g_callback.store(nullptr, std::memory_order_release);
    g_callback_data.store(nullptr, std::memory_order_release);
}

}  // extern "C"

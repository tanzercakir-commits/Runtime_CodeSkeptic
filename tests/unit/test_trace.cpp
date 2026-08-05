// SPDX-License-Identifier: Apache-2.0
#include "test_support.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "runtimeskeptic/runtime/runtime.h"
#include "runtimeskeptic/runtime/trace.hpp"
#if defined(_WIN32)
#include "runtimeskeptic/runtime/runtime_windows.h"
#else
#include "runtimeskeptic/runtime/runtime_posix.h"
#endif

namespace {

std::filesystem::path temp_file(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

void write_bytes(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

rs_runtime_config_v1 trace_config() {
    rs_runtime_config_v1 value{};
    value.abi_version = RS_RUNTIME_ABI_VERSION_V1;
    value.struct_size = static_cast<uint32_t>(sizeof(value));
    value.mode = RS_MONITOR_MODE_RECORD_V1;
    value.event_capacity = 32;
    return value;
}

bool record_lifecycle() {
#if defined(_WIN32)
    void* memory = rs_virtual_alloc_v1(
        nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (memory == nullptr) return false;
    DWORD old_protection = 0;
    if (!rs_virtual_protect_v1(memory, 4096, PAGE_READONLY,
                               &old_protection)) return false;
    if (!rs_virtual_free_v1(memory, 4096, MEM_DECOMMIT)) return false;
    return rs_virtual_free_v1(memory, 0, MEM_RELEASE) != FALSE;
#else
#if defined(MAP_ANONYMOUS)
    const int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#else
    const int flags = MAP_PRIVATE | MAP_ANON;
#endif
    void* memory = rs_mmap_v1(nullptr, 4096, PROT_READ | PROT_WRITE,
                              flags, -1, 0);
    if (memory == MAP_FAILED) return false;
    if (rs_mprotect_v1(memory, 4096, PROT_READ) != 0) return false;
    return rs_munmap_v1(memory, 4096) == 0;
#endif
}

std::string valid_trace_bytes() {
    const auto first = temp_file("runtimeskeptic-trace-valid-a.jsonl");
    const auto second = temp_file("runtimeskeptic-trace-valid-b.jsonl");
    const rs_runtime_config_v1 value = trace_config();
    if (rs_runtime_initialize_v1(&value) != RS_RUNTIME_OK_V1 ||
        !record_lifecycle() ||
        rs_runtime_flush_trace_v1(first.string().c_str()) != RS_RUNTIME_OK_V1 ||
        rs_runtime_flush_trace_v1(second.string().c_str()) != RS_RUNTIME_OK_V1) {
        rs_runtime_shutdown_v1();
        return {};
    }
    rs_runtime_shutdown_v1();
    const std::string a = read_bytes(first);
    const std::string b = read_bytes(second);
    std::error_code ignored;
    std::filesystem::remove(first, ignored);
    std::filesystem::remove(second, ignored);
    return a == b ? a : std::string();
}

std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t offset = 0;
    while (offset < text.size()) {
        const std::size_t newline = text.find('\n', offset);
        if (newline == std::string::npos) break;
        lines.push_back(text.substr(offset, newline - offset));
        offset = newline + 1;
    }
    return lines;
}

std::string join_lines(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& line : lines) out += line + "\n";
    return out;
}

rs_vm_event_v1 event(uint64_t sequence, uint32_t platform,
                      uint32_t operation, uint64_t address, uint64_t size,
                      uint32_t semantic_flags = 0) {
    rs_vm_event_v1 value{};
    value.abi_version = RS_RUNTIME_ABI_VERSION_V1;
    value.struct_size = static_cast<uint32_t>(sizeof(value));
    value.sequence = sequence;
    value.platform = platform;
    value.operation = operation;
    value.success = 1;
    value.native_error_domain = platform == RS_RUNTIME_PLATFORM_WINDOWS_V1
                                    ? RS_NATIVE_ERROR_WIN32_V1
                                    : RS_NATIVE_ERROR_ERRNO_V1;
    value.requested_address = address;
    value.returned_address = address;
    value.size = size;
    value.semantic_flags = semantic_flags;
    value.requested_protection = 1;
    return value;
}

}  // namespace

RS_TEST(writer_is_byte_stable_and_reader_replay_accepts_its_trace) {
    const std::string bytes = valid_trace_bytes();
    RS_CHECK(!bytes.empty());
    const auto path = temp_file("runtimeskeptic-trace-roundtrip.jsonl");
    write_bytes(path, bytes);
    std::string error;
    auto trace = rs::runtime::trace::read_file(path.string(), error);
    RS_CHECK_MESSAGE(trace.has_value(), error);
    if (trace) {
        auto result = rs::runtime::trace::replay(*trace, error);
        RS_CHECK_MESSAGE(result.has_value(), error);
        if (result) {
            RS_CHECK(result->reproduced);
            RS_CHECK(result->event_count >= 3);
        }
    }
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

RS_TEST(reader_rejects_truncation_unknown_version_and_noncanonical_json) {
    const std::string valid = valid_trace_bytes();
    RS_CHECK(!valid.empty());
    auto lines = lines_of(valid);
    const auto path = temp_file("runtimeskeptic-trace-reject.jsonl");
    std::string error;

    auto truncated = lines;
    truncated.pop_back();
    write_bytes(path, join_lines(truncated));
    RS_CHECK(!rs::runtime::trace::read_file(path.string(), error));

    auto unknown = lines;
    const std::string needle = "\"abi_version\":1";
    const std::size_t at = unknown[0].find(needle);
    RS_CHECK(at != std::string::npos);
    if (at != std::string::npos) unknown[0].replace(at, needle.size(),
                                                    "\"abi_version\":2");
    write_bytes(path, join_lines(unknown));
    error.clear();
    RS_CHECK(!rs::runtime::trace::read_file(path.string(), error));

    auto noncanonical = lines;
    noncanonical[0] = " " + noncanonical[0];
    write_bytes(path, join_lines(noncanonical));
    error.clear();
    RS_CHECK(!rs::runtime::trace::read_file(path.string(), error));

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

RS_TEST(reader_rejects_sequence_reordering_digest_tamper_and_incomplete_footer) {
    const std::string valid = valid_trace_bytes();
    auto lines = lines_of(valid);
    RS_CHECK(lines.size() >= 5);
    const auto path = temp_file("runtimeskeptic-trace-adversarial.jsonl");
    std::string error;

    auto reordered = lines;
    std::swap(reordered[1], reordered[2]);
    write_bytes(path, join_lines(reordered));
    RS_CHECK(!rs::runtime::trace::read_file(path.string(), error));

    auto tampered = lines;
    const std::string size_needle = "\"size\":4096";
    const std::size_t size_at = tampered[1].find(size_needle);
    RS_CHECK(size_at != std::string::npos);
    if (size_at != std::string::npos) {
        tampered[1].replace(size_at, size_needle.size(), "\"size\":8192");
    }
    write_bytes(path, join_lines(tampered));
    error.clear();
    RS_CHECK(!rs::runtime::trace::read_file(path.string(), error));

    auto incomplete = lines;
    const std::string complete = "\"complete\":true";
    const std::size_t complete_at = incomplete.back().find(complete);
    RS_CHECK(complete_at != std::string::npos);
    if (complete_at != std::string::npos) {
        incomplete.back().replace(complete_at, complete.size(),
                                  "\"complete\":false");
    }
    write_bytes(path, join_lines(incomplete));
    error.clear();
    RS_CHECK(!rs::runtime::trace::read_file(path.string(), error));

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

RS_TEST(reader_enforces_file_line_and_event_limits) {
    const std::string valid = valid_trace_bytes();
    const auto path = temp_file("runtimeskeptic-trace-limits.jsonl");
    write_bytes(path, valid);
    std::string error;
    rs::runtime::trace::ReadLimits limits;
    limits.max_file_bytes = 8;
    RS_CHECK(!rs::runtime::trace::read_file(path.string(), error, limits));
    limits = {};
    limits.max_line_bytes = 8;
    error.clear();
    RS_CHECK(!rs::runtime::trace::read_file(path.string(), error, limits));
    limits = {};
    limits.max_events = 1;
    error.clear();
    RS_CHECK(!rs::runtime::trace::read_file(path.string(), error, limits));
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

RS_TEST(posix_lifecycle_replay_maps_protects_and_unmaps) {
    rs::runtime::trace::Trace trace;
    trace.header.platform = "posix";
    trace.events.push_back(event(1, RS_RUNTIME_PLATFORM_POSIX_V1,
                                 RS_VM_OPERATION_POSIX_MMAP_V1,
                                 0x10000, 0x3000));
    trace.events.push_back(event(2, RS_RUNTIME_PLATFORM_POSIX_V1,
                                 RS_VM_OPERATION_POSIX_MPROTECT_V1,
                                 0x11000, 0x1000));
    trace.events.push_back(event(3, RS_RUNTIME_PLATFORM_POSIX_V1,
                                 RS_VM_OPERATION_POSIX_MUNMAP_V1,
                                 0x10000, 0x3000,
                                 RS_VM_SEMANTIC_RELEASE_V1));
    std::string error;
    auto result = rs::runtime::trace::replay(trace, error);
    RS_CHECK_MESSAGE(result.has_value(), error);
    if (result) RS_CHECK(result->reproduced);
}

RS_TEST(windows_lifecycle_keeps_reserve_commit_decommit_and_release_distinct) {
    rs::runtime::trace::Trace trace;
    trace.header.platform = "windows";
    trace.events.push_back(event(1, RS_RUNTIME_PLATFORM_WINDOWS_V1,
                                 RS_VM_OPERATION_WINDOWS_RESERVE_V1,
                                 0x20000, 0x4000,
                                 RS_VM_SEMANTIC_RESERVE_V1));
    trace.events.push_back(event(2, RS_RUNTIME_PLATFORM_WINDOWS_V1,
                                 RS_VM_OPERATION_WINDOWS_COMMIT_V1,
                                 0x21000, 0x2000,
                                 RS_VM_SEMANTIC_COMMIT_V1));
    trace.events.push_back(event(3, RS_RUNTIME_PLATFORM_WINDOWS_V1,
                                 RS_VM_OPERATION_WINDOWS_PROTECT_V1,
                                 0x21000, 0x1000));
    trace.events.push_back(event(4, RS_RUNTIME_PLATFORM_WINDOWS_V1,
                                 RS_VM_OPERATION_WINDOWS_DECOMMIT_V1,
                                 0x21000, 0x2000,
                                 RS_VM_SEMANTIC_DECOMMIT_V1));
    trace.events.push_back(event(5, RS_RUNTIME_PLATFORM_WINDOWS_V1,
                                 RS_VM_OPERATION_WINDOWS_RELEASE_V1,
                                 0x20000, 0,
                                 RS_VM_SEMANTIC_RELEASE_V1));
    std::string error;
    auto result = rs::runtime::trace::replay(trace, error);
    RS_CHECK_MESSAGE(result.has_value(), error);
    if (result) RS_CHECK(result->reproduced);
}

RS_TEST(replay_recomputes_call_boundary_violations) {
    rs::runtime::trace::Trace trace;
    trace.header.platform = "posix";
    rs_vm_event_v1 relocated = event(
        1, RS_RUNTIME_PLATFORM_POSIX_V1, RS_VM_OPERATION_POSIX_MMAP_V1,
        0x30000, 0x1000);
    relocated.requested_address = 0x20000;
    relocated.expected_address = 0x20000;
    relocated.exact_address_required = 1;
    relocated.violation = RS_VM_VIOLATION_NONE_V1;
    trace.events.push_back(relocated);
    std::string error;
    RS_CHECK(!rs::runtime::trace::replay(trace, error));
    trace.events[0].violation = RS_VM_VIOLATION_EXACT_ADDRESS_RELOCATED_V1;
    error.clear();
    auto result = rs::runtime::trace::replay(trace, error);
    RS_CHECK_MESSAGE(result.has_value(), error);
    if (result) RS_CHECK_EQ(result->semantic_violations.size(),
                            static_cast<std::size_t>(1));
}

RS_TEST_MAIN("runtime trace")

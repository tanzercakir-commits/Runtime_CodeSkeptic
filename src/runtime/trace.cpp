// SPDX-License-Identifier: Apache-2.0
#include "runtimeskeptic/runtime/trace.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <string_view>

#include "runtimeskeptic/core/json.hpp"
#include "runtimeskeptic/core/schema_registry.hpp"
#include "runtimeskeptic/core/sha256.hpp"

namespace rs::runtime::trace {
namespace {

uint32_t operation_id(std::string_view name) {
    if (name == "posix_mmap") return RS_VM_OPERATION_POSIX_MMAP_V1;
    if (name == "posix_mprotect") return RS_VM_OPERATION_POSIX_MPROTECT_V1;
    if (name == "posix_munmap") return RS_VM_OPERATION_POSIX_MUNMAP_V1;
    if (name == "windows_reserve") return RS_VM_OPERATION_WINDOWS_RESERVE_V1;
    if (name == "windows_commit") return RS_VM_OPERATION_WINDOWS_COMMIT_V1;
    if (name == "windows_protect") return RS_VM_OPERATION_WINDOWS_PROTECT_V1;
    if (name == "windows_decommit") return RS_VM_OPERATION_WINDOWS_DECOMMIT_V1;
    if (name == "windows_release") return RS_VM_OPERATION_WINDOWS_RELEASE_V1;
    return 0;
}

uint32_t platform_id(std::string_view name) {
    return name == "windows" ? RS_RUNTIME_PLATFORM_WINDOWS_V1
                              : RS_RUNTIME_PLATFORM_POSIX_V1;
}

uint32_t error_domain_id(std::string_view name) {
    if (name == "errno") return RS_NATIVE_ERROR_ERRNO_V1;
    if (name == "win32") return RS_NATIVE_ERROR_WIN32_V1;
    return RS_NATIVE_ERROR_NONE_V1;
}

uint32_t violation_id(std::string_view name) {
    if (name == "exact_address_relocated") {
        return RS_VM_VIOLATION_EXACT_ADDRESS_RELOCATED_V1;
    }
    if (name == "invalid_expectation") {
        return RS_VM_VIOLATION_INVALID_EXPECTATION_V1;
    }
    return RS_VM_VIOLATION_NONE_V1;
}

bool address(const json::Value& doc, const char* key, uint64_t& value,
             std::string& error) {
    const json::Value* field = doc.find(key);
    if (field == nullptr || !field->is_string()) {
        error = std::string("trace record is missing address ") + key;
        return false;
    }
    auto parsed = json::from_hex(field->as_string());
    if (!parsed) {
        error = std::string("trace record has invalid address ") + key;
        return false;
    }
    value = *parsed;
    return true;
}

bool parse_event(const json::Value& doc, rs_vm_event_v1& event,
                 std::string& error) {
    event = rs_vm_event_v1{};
    event.abi_version = RS_RUNTIME_ABI_VERSION_V1;
    event.struct_size = static_cast<uint32_t>(sizeof(rs_vm_event_v1));
    event.sequence = doc.find("sequence")->as_uint();
    event.platform = platform_id(doc.find("platform")->as_string());
    event.operation = operation_id(doc.find("operation")->as_string());
    event.success = doc.find("status")->as_string() == "success" ? 1u : 0u;
    event.native_error_domain =
        error_domain_id(doc.find("native_error_domain")->as_string());
    event.native_error_code = doc.find("native_error_code")->as_int();
    if (!address(doc, "requested_address", event.requested_address, error) ||
        !address(doc, "returned_address", event.returned_address, error) ||
        !address(doc, "expected_address", event.expected_address, error)) {
        return false;
    }
    event.size = doc.find("size")->as_uint();
    event.offset = doc.find("offset")->as_int();
    event.native_flags = static_cast<uint32_t>(doc.find("native_flags")->as_uint());
    event.semantic_flags =
        static_cast<uint32_t>(doc.find("semantic_flags")->as_uint());
    event.requested_protection =
        static_cast<uint32_t>(doc.find("requested_protection")->as_uint());
    event.previous_protection =
        static_cast<uint32_t>(doc.find("previous_protection")->as_uint());
    event.exact_address_required =
        doc.find("exact_address_required")->as_bool() ? 1u : 0u;
    event.violation = violation_id(doc.find("violation")->as_string());
    return true;
}

bool is_mapping(uint32_t operation) {
    return operation == RS_VM_OPERATION_POSIX_MMAP_V1 ||
           operation == RS_VM_OPERATION_WINDOWS_RESERVE_V1 ||
           operation == RS_VM_OPERATION_WINDOWS_COMMIT_V1;
}

bool validate_event_contract(const rs_vm_event_v1& event,
                             const Header& header, std::string& error) {
    if (event.platform != platform_id(header.platform)) {
        error = "event platform differs from trace header";
        return false;
    }
    const bool posix_operation =
        event.operation >= RS_VM_OPERATION_POSIX_MMAP_V1 &&
        event.operation <= RS_VM_OPERATION_POSIX_MUNMAP_V1;
    if ((header.platform == "posix") != posix_operation) {
        error = "operation belongs to a different platform";
        return false;
    }
    if (event.platform == RS_RUNTIME_PLATFORM_POSIX_V1 &&
        event.native_error_domain != RS_NATIVE_ERROR_ERRNO_V1) {
        error = "POSIX event must carry errno state";
        return false;
    }
    if (event.platform == RS_RUNTIME_PLATFORM_WINDOWS_V1 &&
        event.native_error_domain != RS_NATIVE_ERROR_WIN32_V1) {
        error = "Windows event must carry Win32 error state";
        return false;
    }
    if (event.success == 0 && event.returned_address != 0) {
        error = "failed event cannot report a returned address";
        return false;
    }
    if (event.success != 0 && event.size == 0 &&
        event.operation != RS_VM_OPERATION_WINDOWS_RELEASE_V1) {
        error = "successful VM event has zero size";
        return false;
    }
    if (!is_mapping(event.operation) && event.exact_address_required != 0) {
        error = "only mapping operations may carry an exact-address expectation";
        return false;
    }
    return true;
}

struct Segment {
    uint64_t start = 0;
    uint64_t end = 0;
    uint32_t protection = 0;
    bool committed = false;
};

bool add_size(uint64_t start, uint64_t size, uint64_t& end) {
    if (size == 0 || start > std::numeric_limits<uint64_t>::max() - size) {
        return false;
    }
    end = start + size;
    return true;
}

void sort_segments(std::vector<Segment>& segments) {
    std::sort(segments.begin(), segments.end(),
              [](const Segment& a, const Segment& b) {
                  return a.start < b.start;
              });
}

bool overlaps(const std::vector<Segment>& segments, uint64_t start,
              uint64_t end) {
    for (const Segment& segment : segments) {
        if (segment.start < end && start < segment.end) return true;
    }
    return false;
}

void erase_range(std::vector<Segment>& segments, uint64_t start,
                 uint64_t end) {
    std::vector<Segment> next;
    next.reserve(segments.size() + 1);
    for (const Segment& segment : segments) {
        if (segment.end <= start || segment.start >= end) {
            next.push_back(segment);
            continue;
        }
        if (segment.start < start) {
            Segment left = segment;
            left.end = start;
            next.push_back(left);
        }
        if (segment.end > end) {
            Segment right = segment;
            right.start = end;
            next.push_back(right);
        }
    }
    segments = std::move(next);
    sort_segments(segments);
}

bool covered(const std::vector<Segment>& segments, uint64_t start,
             uint64_t end, bool require_committed) {
    uint64_t cursor = start;
    for (const Segment& segment : segments) {
        if (segment.end <= cursor) continue;
        if (segment.start > cursor) return false;
        if (require_committed && !segment.committed) return false;
        cursor = std::min(end, segment.end);
        if (cursor == end) return true;
    }
    return false;
}

void split_at(std::vector<Segment>& segments, uint64_t point) {
    for (std::size_t i = 0; i < segments.size(); ++i) {
        Segment& segment = segments[i];
        if (segment.start < point && point < segment.end) {
            Segment right = segment;
            right.start = point;
            segment.end = point;
            segments.insert(segments.begin() + static_cast<std::ptrdiff_t>(i + 1),
                            right);
            return;
        }
    }
}

void set_range(std::vector<Segment>& segments, uint64_t start, uint64_t end,
               bool committed, uint32_t protection) {
    split_at(segments, start);
    split_at(segments, end);
    for (Segment& segment : segments) {
        if (segment.start >= start && segment.end <= end) {
            segment.committed = committed;
            segment.protection = protection;
        }
    }
}

bool release_windows(std::vector<Segment>& segments, uint64_t address) {
    sort_segments(segments);
    std::size_t first = segments.size();
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (segments[i].start <= address && address < segments[i].end) {
            first = i;
            break;
        }
    }
    if (first == segments.size()) return false;
    uint64_t end = segments[first].end;
    std::size_t last = first + 1;
    while (last < segments.size() && segments[last].start == end) {
        end = segments[last].end;
        ++last;
    }
    segments.erase(segments.begin() + static_cast<std::ptrdiff_t>(first),
                   segments.begin() + static_cast<std::ptrdiff_t>(last));
    return true;
}

}  // namespace

std::optional<Trace> read_file(const std::string& path, std::string& error,
                               ReadLimits limits) {
    if (limits.max_file_bytes == 0 || limits.max_line_bytes == 0 ||
        limits.max_events == 0) {
        error = "trace read limits must be nonzero";
        return std::nullopt;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot open runtime trace";
        return std::nullopt;
    }
    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    if (length < 0 || static_cast<uint64_t>(length) > limits.max_file_bytes) {
        error = "runtime trace exceeds the file-size limit";
        return std::nullopt;
    }
    input.seekg(0, std::ios::beg);
    std::string bytes(static_cast<std::size_t>(length), '\0');
    if (!bytes.empty()) input.read(bytes.data(), length);
    if (!input && !bytes.empty()) {
        error = "cannot read runtime trace";
        return std::nullopt;
    }
    if (bytes.empty() || bytes.back() != '\n') {
        error = "runtime trace is empty or lacks its final newline";
        return std::nullopt;
    }

    Trace trace;
    hash::Sha256 digest;
    bool saw_header = false;
    bool saw_footer = false;
    uint64_t expected_sequence = 1;
    std::size_t offset = 0;
    std::size_t line_number = 0;
    while (offset < bytes.size()) {
        const std::size_t newline = bytes.find('\n', offset);
        const std::size_t length_line = newline - offset;
        ++line_number;
        if (length_line == 0 || length_line > limits.max_line_bytes) {
            error = "invalid or oversized trace line " + std::to_string(line_number);
            return std::nullopt;
        }
        const std::string_view line(bytes.data() + offset, length_line);
        auto parsed = json::parse(line);
        if (!parsed.ok() || !parsed.value->is_object()) {
            error = "invalid JSON on trace line " + std::to_string(line_number);
            return std::nullopt;
        }
        std::string schema_error;
        if (!schema::validate_runtime_trace_record(*parsed.value, schema_error)) {
            error = "trace schema violation on line " +
                    std::to_string(line_number) + ": " + schema_error;
            return std::nullopt;
        }
        auto canonical = json::serialize_canonical(*parsed.value);
        if (!canonical || *canonical != line) {
            error = "trace line " + std::to_string(line_number) +
                    " is not canonical JSON";
            return std::nullopt;
        }
        const std::string& type = parsed.value->find("record_type")->as_string();
        if (type == "header") {
            if (saw_header || line_number != 1) {
                error = "runtime trace must start with exactly one header";
                return std::nullopt;
            }
            saw_header = true;
            trace.header.abi_version = static_cast<uint32_t>(
                parsed.value->find("abi_version")->as_uint());
            trace.header.buffer_capacity = static_cast<uint32_t>(
                parsed.value->find("buffer_capacity")->as_uint());
            trace.header.platform = parsed.value->find("platform")->as_string();
            trace.header.monitor_mode =
                parsed.value->find("monitor_mode")->as_string();
            for (const json::Value& api :
                 parsed.value->find("instrumented_apis")->as_array()) {
                trace.header.instrumented_apis.push_back(api.as_string());
            }
            digest.update(line);
            digest.update("\n");
        } else if (type == "event") {
            if (!saw_header || saw_footer || trace.events.size() >= limits.max_events) {
                error = "event is outside the bounded header/footer sequence";
                return std::nullopt;
            }
            rs_vm_event_v1 event{};
            if (!parse_event(*parsed.value, event, error) ||
                !validate_event_contract(event, trace.header, error)) {
                return std::nullopt;
            }
            if (event.sequence != expected_sequence) {
                error = "runtime trace has a sequence gap or reordering";
                return std::nullopt;
            }
            ++expected_sequence;
            trace.events.push_back(event);
            digest.update(line);
            digest.update("\n");
        } else {
            if (!saw_header || saw_footer) {
                error = "runtime trace has a missing or duplicate footer";
                return std::nullopt;
            }
            saw_footer = true;
            trace.footer.event_count = parsed.value->find("event_count")->as_uint();
            trace.footer.dropped_events =
                parsed.value->find("dropped_events")->as_uint();
            trace.footer.reentrant_events =
                parsed.value->find("reentrant_events")->as_uint();
            trace.footer.complete = parsed.value->find("complete")->as_bool();
            trace.footer.digest = parsed.value->find("digest")->as_string();
            if (newline + 1 != bytes.size()) {
                error = "footer must be the final runtime-trace record";
                return std::nullopt;
            }
        }
        offset = newline + 1;
    }

    if (!saw_header || !saw_footer) {
        error = "runtime trace is missing its header or footer";
        return std::nullopt;
    }
    if (trace.footer.event_count != trace.events.size()) {
        error = "footer event count does not match the trace";
        return std::nullopt;
    }
    if (!trace.footer.complete || trace.footer.dropped_events != 0 ||
        trace.footer.reentrant_events != 0) {
        error = "incomplete runtime trace cannot be replayed as evidence";
        return std::nullopt;
    }
    if (trace.footer.digest != "sha256:" + digest.hex_digest()) {
        error = "runtime trace digest mismatch";
        return std::nullopt;
    }
    return trace;
}

std::optional<ReplayResult> replay(const Trace& trace, std::string& error) {
    std::vector<Segment> segments;
    ReplayResult result;
    result.event_count = trace.events.size();

    for (const rs_vm_event_v1& event : trace.events) {
        uint32_t expected_violation = RS_VM_VIOLATION_NONE_V1;
        if (event.exact_address_required != 0 && event.success != 0 &&
            event.returned_address != event.expected_address) {
            expected_violation = RS_VM_VIOLATION_EXACT_ADDRESS_RELOCATED_V1;
        }
        if (event.violation == RS_VM_VIOLATION_INVALID_EXPECTATION_V1) {
            expected_violation = RS_VM_VIOLATION_INVALID_EXPECTATION_V1;
        }
        if (event.violation != expected_violation) {
            error = "recorded call-boundary violation does not replay";
            return std::nullopt;
        }
        if (event.violation != RS_VM_VIOLATION_NONE_V1) {
            result.semantic_violations.push_back(
                event.violation == RS_VM_VIOLATION_EXACT_ADDRESS_RELOCATED_V1
                    ? "exact_address_relocated"
                    : "invalid_expectation");
        }
        if (event.success == 0) continue;

        uint64_t end = 0;
        switch (event.operation) {
            case RS_VM_OPERATION_POSIX_MMAP_V1: {
                if (!add_size(event.returned_address, event.size, end)) {
                    error = "successful mmap has an invalid range";
                    return std::nullopt;
                }
                if ((event.semantic_flags & RS_VM_SEMANTIC_REPLACE_V1) != 0) {
                    erase_range(segments, event.returned_address, end);
                } else if (overlaps(segments, event.returned_address, end)) {
                    error = "non-replacing mmap overlaps a live mapping";
                    return std::nullopt;
                }
                segments.push_back(Segment{event.returned_address, end,
                                           event.requested_protection, true});
                sort_segments(segments);
                break;
            }
            case RS_VM_OPERATION_POSIX_MPROTECT_V1:
            case RS_VM_OPERATION_WINDOWS_PROTECT_V1: {
                if (!add_size(event.requested_address, event.size, end) ||
                    !covered(segments, event.requested_address, end, true)) {
                    error = "successful protection change is outside live memory";
                    return std::nullopt;
                }
                set_range(segments, event.requested_address, end, true,
                          event.requested_protection);
                break;
            }
            case RS_VM_OPERATION_POSIX_MUNMAP_V1: {
                if (!add_size(event.requested_address, event.size, end)) {
                    error = "successful munmap has an invalid range";
                    return std::nullopt;
                }
                erase_range(segments, event.requested_address, end);
                break;
            }
            case RS_VM_OPERATION_WINDOWS_RESERVE_V1: {
                if (!add_size(event.returned_address, event.size, end) ||
                    overlaps(segments, event.returned_address, end)) {
                    error = "successful reserve overlaps or overflows";
                    return std::nullopt;
                }
                const bool committed =
                    (event.semantic_flags & RS_VM_SEMANTIC_COMMIT_V1) != 0;
                segments.push_back(Segment{event.returned_address, end,
                                           event.requested_protection, committed});
                sort_segments(segments);
                break;
            }
            case RS_VM_OPERATION_WINDOWS_COMMIT_V1: {
                if (!add_size(event.returned_address, event.size, end) ||
                    !covered(segments, event.returned_address, end, false)) {
                    error = "successful commit is outside a reservation";
                    return std::nullopt;
                }
                set_range(segments, event.returned_address, end, true,
                          event.requested_protection);
                break;
            }
            case RS_VM_OPERATION_WINDOWS_DECOMMIT_V1: {
                if (!add_size(event.requested_address, event.size, end) ||
                    !covered(segments, event.requested_address, end, true)) {
                    error = "successful decommit is outside committed memory";
                    return std::nullopt;
                }
                set_range(segments, event.requested_address, end, false, 0);
                break;
            }
            case RS_VM_OPERATION_WINDOWS_RELEASE_V1:
                if (!release_windows(segments, event.requested_address)) {
                    error = "successful release is outside a reservation";
                    return std::nullopt;
                }
                break;
            default:
                error = "runtime trace contains an unknown operation";
                return std::nullopt;
        }
    }

    result.reproduced = true;
    result.detail = std::to_string(result.event_count) +
                    " event(s) replayed without issuing OS calls";
    return result;
}

std::optional<ReplayResult> replay_file(const std::string& path,
                                        std::string& error,
                                        ReadLimits limits) {
    auto trace = read_file(path, error, limits);
    if (!trace) return std::nullopt;
    return replay(*trace, error);
}

}  // namespace rs::runtime::trace

// SPDX-License-Identifier: Apache-2.0
#ifndef RUNTIMESKEPTIC_RUNTIME_TRACE_HPP
#define RUNTIMESKEPTIC_RUNTIME_TRACE_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "runtimeskeptic/runtime/runtime.h"

namespace rs::runtime::trace {

struct Header {
    uint32_t abi_version = 0;
    uint32_t buffer_capacity = 0;
    std::string platform;
    std::string monitor_mode;
    std::vector<std::string> instrumented_apis;
};

struct Footer {
    uint64_t event_count = 0;
    uint64_t dropped_events = 0;
    uint64_t reentrant_events = 0;
    bool complete = false;
    std::string digest;
};

struct Trace {
    Header header;
    std::vector<rs_vm_event_v1> events;
    Footer footer;
};

struct ReadLimits {
    std::size_t max_file_bytes = 16u * 1024u * 1024u;
    std::size_t max_line_bytes = 64u * 1024u;
    std::size_t max_events = RS_RUNTIME_EVENT_CAPACITY_MAX_V1;
};

std::optional<Trace> read_file(const std::string& path, std::string& error,
                               ReadLimits limits = {});

struct ReplayResult {
    bool reproduced = false;
    std::size_t event_count = 0;
    std::vector<std::string> semantic_violations;
    std::string detail;
};

std::optional<ReplayResult> replay(const Trace& trace, std::string& error);
std::optional<ReplayResult> replay_file(const std::string& path,
                                        std::string& error,
                                        ReadLimits limits = {});

}  // namespace rs::runtime::trace

#endif  // RUNTIMESKEPTIC_RUNTIME_TRACE_HPP

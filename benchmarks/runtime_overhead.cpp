// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "runtimeskeptic/core/json.hpp"
#include "runtimeskeptic/core/schema_registry.hpp"
#include "runtimeskeptic/runtime/runtime.h"
#include "runtimeskeptic/version.hpp"
#if defined(_WIN32)
#include "runtimeskeptic/runtime/runtime_windows.h"
#else
#include "runtimeskeptic/runtime/runtime_posix.h"
#endif

extern "C" int rs_benchmark_compile_disabled_cycle(void);

using namespace rs;

namespace {

using Clock = std::chrono::steady_clock;
using Cycle = int (*)();

int native_cycle() {
#if defined(_WIN32)
    void* memory = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT,
                                PAGE_READWRITE);
    if (memory == nullptr) return 1;
    return VirtualFree(memory, 0, MEM_RELEASE) ? 0 : 1;
#else
#if defined(MAP_ANONYMOUS)
    const int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#else
    const int flags = MAP_PRIVATE | MAP_ANON;
#endif
    void* memory = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (memory == MAP_FAILED) return 1;
    return munmap(memory, 4096) == 0 ? 0 : 1;
#endif
}

int wrapped_cycle() {
#if defined(_WIN32)
    void* memory = rs_virtual_alloc_v1(
        nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (memory == nullptr) return 1;
    return rs_virtual_free_v1(memory, 0, MEM_RELEASE) ? 0 : 1;
#else
#if defined(MAP_ANONYMOUS)
    const int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#else
    const int flags = MAP_PRIVATE | MAP_ANON;
#endif
    void* memory = rs_mmap_v1(nullptr, 4096, PROT_READ | PROT_WRITE,
                              flags, -1, 0);
    if (memory == MAP_FAILED) return 1;
    return rs_munmap_v1(memory, 4096) == 0 ? 0 : 1;
#endif
}

struct Measurement {
    std::string name;
    uint64_t median_ns = 0;
    uint64_t p95_ns = 0;
};

bool measure(const std::string& name, Cycle cycle, std::size_t iterations,
             Measurement& out) {
    for (std::size_t i = 0; i < 8; ++i) {
        if (cycle() != 0) return false;
    }
    std::vector<uint64_t> samples;
    samples.reserve(iterations);
    for (std::size_t i = 0; i < iterations; ++i) {
        const auto start = Clock::now();
        const int status = cycle();
        const auto finish = Clock::now();
        if (status != 0) return false;
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            finish - start).count();
        samples.push_back(elapsed < 0 ? 0u : static_cast<uint64_t>(elapsed));
    }
    std::sort(samples.begin(), samples.end());
    out.name = name;
    out.median_ns = samples[samples.size() / 2];
    const std::size_t p95 = (samples.size() * 95u + 99u) / 100u - 1u;
    out.p95_ns = samples[p95];
    return true;
}

std::filesystem::path g_flush_path;

int wrapped_cycle_and_flush() {
    if (rs_runtime_reset_v1() != RS_RUNTIME_OK_V1 || wrapped_cycle() != 0) {
        return 1;
    }
    return rs_runtime_flush_trace_v1(g_flush_path.string().c_str()) ==
                   RS_RUNTIME_OK_V1
               ? 0
               : 1;
}

rs_runtime_config_v1 runtime_config(uint32_t capacity) {
    rs_runtime_config_v1 config{};
    config.abi_version = RS_RUNTIME_ABI_VERSION_V1;
    config.struct_size = static_cast<uint32_t>(sizeof(config));
    config.mode = RS_MONITOR_MODE_RECORD_V1;
    config.event_capacity = capacity;
    return config;
}

json::Value result_json(const std::vector<Measurement>& measurements,
                        std::size_t iterations) {
    const uint64_t baseline = std::max<uint64_t>(measurements[0].median_ns, 1);
    json::Value root = json::Value::object();
    root["iterations"] = static_cast<unsigned long long>(iterations);
    json::Value modes = json::Value::array();
    for (const Measurement& measurement : measurements) {
        json::Value mode = json::Value::object();
        const int64_t delta =
            static_cast<int64_t>(measurement.median_ns) -
            static_cast<int64_t>(measurements[0].median_ns);
        mode["delta_ns"] = static_cast<long long>(delta);
        mode["median_ns"] =
            static_cast<unsigned long long>(measurement.median_ns);
        mode["name"] = measurement.name;
        mode["p95_ns"] = static_cast<unsigned long long>(measurement.p95_ns);
        mode["ratio_milli"] = static_cast<unsigned long long>(
            measurement.median_ns * 1000u / baseline);
        modes.push_back(std::move(mode));
    }
    root["modes"] = std::move(modes);
    root["schema"] = kRuntimeOverheadSchemaVersion;
    root["unit"] = "nanoseconds";
    return root;
}

bool parse_count(const std::string& text, std::size_t& value) {
    try {
        const unsigned long parsed = std::stoul(text);
        if (parsed == 0 || parsed > 2048) return false;
        value = static_cast<std::size_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::size_t iterations = 128;
    std::string output_path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--iterations" && i + 1 < argc) {
            if (!parse_count(argv[++i], iterations)) {
                std::cerr << "invalid --iterations value\n";
                return 64;
            }
        } else if (arg == "--output" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "rs-runtime-benchmark [--iterations N] [--output FILE]\n";
            return 0;
        } else {
            std::cerr << "unrecognized benchmark argument: " << arg << "\n";
            return 64;
        }
    }

    std::vector<Measurement> measurements(5);
    if (!measure("native", native_cycle, iterations, measurements[0]) ||
        !measure("compile_disabled", rs_benchmark_compile_disabled_cycle,
                 iterations, measurements[1])) {
        std::cerr << "native benchmark cycle failed\n";
        return 70;
    }

    rs_runtime_config_v1 config = runtime_config(
        static_cast<uint32_t>(iterations * 2u));
    if (rs_runtime_initialize_v1(&config) != RS_RUNTIME_OK_V1) return 70;
    rs_runtime_set_enabled_v1(0);
    if (!measure("runtime_disabled", wrapped_cycle, iterations,
                 measurements[2])) return 70;
    rs_runtime_set_enabled_v1(1);
    if (rs_runtime_reset_v1() != RS_RUNTIME_OK_V1 ||
        !measure("enabled_buffered", wrapped_cycle, iterations,
                 measurements[3])) return 70;
    rs_runtime_shutdown_v1();

    g_flush_path = std::filesystem::temp_directory_path() /
                   "runtimeskeptic-overhead-trace.jsonl";
    config = runtime_config(2);
    if (rs_runtime_initialize_v1(&config) != RS_RUNTIME_OK_V1 ||
        !measure("enabled_plus_flush", wrapped_cycle_and_flush,
                 iterations, measurements[4])) return 70;
    rs_runtime_shutdown_v1();
    std::error_code ignored;
    std::filesystem::remove(g_flush_path, ignored);

    json::Value result = result_json(measurements, iterations);
    std::string schema_error;
    if (!schema::validate_runtime_overhead(result, schema_error)) {
        std::cerr << "benchmark emitted invalid JSON: " << schema_error << "\n";
        return 70;
    }
    auto encoded = json::serialize_pretty(result);
    if (output_path.empty()) {
        std::cout << encoded << "\n";
    } else {
        std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
        output << encoded << "\n";
        if (!output) return 70;
    }
    return 0;
}

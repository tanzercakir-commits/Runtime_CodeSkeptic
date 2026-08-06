// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>

#include "runtimeskeptic/runtime/runtime_windows.h"

int main(int argc, char** argv) {
    const char* trace_path = argc > 1 ? argv[1] : "runtime_trace.jsonl";
    rs_runtime_config_v1 config = {0};
    config.abi_version = RS_RUNTIME_ABI_VERSION_V1;
    config.struct_size = (uint32_t)sizeof(config);
    config.mode = RS_MONITOR_MODE_REPORT_V1;
    config.event_capacity = 16;
    if (rs_runtime_initialize_v1(&config) != RS_RUNTIME_OK_V1) return 1;

    void* page = rs_virtual_alloc_v1(
        0, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (page == 0) return 2;
    DWORD previous = 0;
    if (!rs_virtual_protect_v1(page, 4096, PAGE_READONLY, &previous)) return 3;
    if (!rs_virtual_free_v1(page, 4096, MEM_DECOMMIT)) return 4;
    if (!rs_virtual_free_v1(page, 0, MEM_RELEASE)) return 5;
    if (rs_runtime_flush_trace_v1(trace_path) != RS_RUNTIME_OK_V1) return 6;

    rs_runtime_stats_v1 stats = {0};
    if (rs_runtime_get_stats_v1(&stats) != RS_RUNTIME_OK_V1) return 7;
    printf("wrote %llu event(s) to %s\n",
           (unsigned long long)stats.recorded_events, trace_path);
    rs_runtime_shutdown_v1();
    return stats.complete ? 0 : 8;
}

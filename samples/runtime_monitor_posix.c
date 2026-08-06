// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>

#include "runtimeskeptic/runtime/runtime_posix.h"

static int anonymous_flags(void) {
#if defined(MAP_ANONYMOUS)
    return MAP_PRIVATE | MAP_ANONYMOUS;
#else
    return MAP_PRIVATE | MAP_ANON;
#endif
}

int main(int argc, char** argv) {
    const char* trace_path = argc > 1 ? argv[1] : "runtime_trace.jsonl";
    rs_runtime_config_v1 config = {0};
    config.abi_version = RS_RUNTIME_ABI_VERSION_V1;
    config.struct_size = (uint32_t)sizeof(config);
    config.mode = RS_MONITOR_MODE_REPORT_V1;
    config.event_capacity = 16;
    if (rs_runtime_initialize_v1(&config) != RS_RUNTIME_OK_V1) return 1;

    void* page = rs_mmap_v1(0, 4096, PROT_READ | PROT_WRITE,
                            anonymous_flags(), -1, 0);
    if (page == MAP_FAILED) return 2;
    if (rs_mprotect_v1(page, 4096, PROT_READ) != 0) return 3;
    if (rs_munmap_v1(page, 4096) != 0) return 4;
    if (rs_runtime_flush_trace_v1(trace_path) != RS_RUNTIME_OK_V1) return 5;

    rs_runtime_stats_v1 stats = {0};
    if (rs_runtime_get_stats_v1(&stats) != RS_RUNTIME_OK_V1) return 6;
    printf("wrote %llu event(s) to %s\n",
           (unsigned long long)stats.recorded_events, trace_path);
    rs_runtime_shutdown_v1();
    return stats.complete ? 0 : 7;
}

// SPDX-License-Identifier: Apache-2.0
#include <stddef.h>
#include <stdint.h>

#include "runtimeskeptic/runtime/runtime.h"
#if defined(_WIN32)
#include "runtimeskeptic/runtime/runtime_windows.h"
#else
#include "runtimeskeptic/runtime/runtime_posix.h"
#endif

_Static_assert(sizeof(rs_vm_expectation_v1) == 48, "expectation ABI drift");
_Static_assert(sizeof(rs_vm_event_v1) == 152, "event ABI drift");
_Static_assert(sizeof(rs_runtime_stats_v1) == 80, "stats ABI drift");
_Static_assert(offsetof(rs_vm_event_v1, sequence) == 8, "sequence offset drift");
_Static_assert(offsetof(rs_vm_event_v1, requested_address) == 40,
               "address offset drift");

int main(void) {
    rs_runtime_config_v1 config = {0};
    config.abi_version = RS_RUNTIME_ABI_VERSION_V1;
    config.struct_size = (uint32_t)sizeof(config);
    config.mode = RS_MONITOR_MODE_RECORD_V1;
    config.event_capacity = 1;
    if (config.struct_size != sizeof(config)) return 1;
    if (rs_runtime_initialize_v1(&config) != RS_RUNTIME_OK_V1) return 2;
    rs_runtime_shutdown_v1();
    return 0;
}

// SPDX-License-Identifier: Apache-2.0
#ifndef RUNTIMESKEPTIC_RUNTIME_INTERNAL_HPP
#define RUNTIMESKEPTIC_RUNTIME_INTERNAL_HPP

#include "runtimeskeptic/runtime/runtime.h"

namespace rs::runtime::internal {

void record_event(rs_vm_event_v1 event) noexcept;
uint32_t platform_id() noexcept;

}  // namespace rs::runtime::internal

#endif  // RUNTIMESKEPTIC_RUNTIME_INTERNAL_HPP

// SPDX-License-Identifier: Apache-2.0
//
// Environment probe for the virtual-memory domain.
//
// Safety rules, from ROADMAP section 18, are hard constraints here:
//
//   - no destructive memory operation. MAP_FIXED (which unmaps whatever is
//     already at the address) is NEVER used. Exact placement is only probed
//     through MAP_FIXED_NOREPLACE, and the probe first verifies that the
//     kernel really honours it rather than silently falling back.
//   - anything that can fault runs in a forked child; the parent records the
//     signal as the observation.
//   - every mapping the probe creates is unmapped again.
//   - strict size caps, and no elevated privilege.
//
// The probe's other job is epistemic: it must never write a fact it did not
// establish. An operation that was not attempted, or whose outcome was
// ambiguous, leaves the corresponding Fact<T> unknown.
#ifndef RUNTIMESKEPTIC_PROBE_VM_PROBE_HPP
#define RUNTIMESKEPTIC_PROBE_VM_PROBE_HPP

#include <string>
#include <vector>

#include "runtimeskeptic/vm/profile.hpp"

namespace rs::probe {

struct Options {
    // Sweep candidate addresses across the address space to build
    // available/unavailable range facts.
    //
    // Turning this off does NOT suppress every range fact: establishing
    // max_user_address requires surveying the space, and on a platform with
    // holes that survey finds them. Those are measurements and are kept.
    bool scan_address_space = true;
    // Run the tests that can fault (currently: file mapping past end of file).
    // These fork a child; the parent is never at risk.
    bool run_faulting_tests = true;
    // Upper bound on any single test mapping.
    std::uint64_t max_test_mapping_bytes = 4ull * 1024 * 1024;
    // Wall-clock cap for a forked child test.
    unsigned child_timeout_seconds = 5;
};

struct Result {
    vm::EnvironmentProfile profile;
    // True when this build actually implements probing for the host platform.
    // A false value still yields a well-formed profile - one in which every
    // fact is unknown.
    bool implemented = false;
};

// Probe version, bumped whenever the set of experiments changes, because two
// profiles are only comparable when they were produced by the same probe.
inline constexpr const char* kProbeVersion = "rs-env-probe/vm/0.1.0";

Result probe_virtual_memory(const Options& options = {});

// Platform name this build can probe, for diagnostics.
std::string probe_platform_name();

}  // namespace rs::probe

#endif  // RUNTIMESKEPTIC_PROBE_VM_PROBE_HPP

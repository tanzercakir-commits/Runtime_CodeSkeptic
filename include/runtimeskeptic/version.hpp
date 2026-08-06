// SPDX-License-Identifier: Apache-2.0
#ifndef RUNTIMESKEPTIC_VERSION_HPP
#define RUNTIMESKEPTIC_VERSION_HPP

namespace rs {

inline constexpr const char* kToolVersion = "runtimeskeptic/0.2.0";

// Schema versions are independent of the tool version on purpose: a tool
// upgrade must not invalidate stored profiles, and a schema change must be
// visible even in a patch release.
inline constexpr const char* kProfileSchemaVersion =
    "runtime-skeptic.environment-profile.v1";
inline constexpr const char* kRequirementSchemaVersion =
    "runtime-skeptic.application-requirements.v1";
inline constexpr const char* kResultSchemaVersion =
    "runtime-skeptic.compatibility-result.v1";
inline constexpr const char* kRuntimeTraceSchemaVersion =
    "runtime-skeptic.runtime-trace-record.v1";
inline constexpr const char* kRuntimeOverheadSchemaVersion =
    "runtime-skeptic.runtime-overhead.v1";

}  // namespace rs

#endif  // RUNTIMESKEPTIC_VERSION_HPP

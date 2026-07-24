// SPDX-License-Identifier: Apache-2.0
//
// Fallback probe for platforms this build does not measure yet.
//
// It deliberately produces a complete, schema-valid profile in which every
// fact is unknown, rather than refusing to run or - far worse - filling in
// plausible defaults. A profile full of `unknown` is useful: rs-check will
// answer UNKNOWN, which is the correct answer, and the analyzer will say
// which facts it needed.
#include "runtimeskeptic/probe/vm_probe.hpp"

#if !defined(RS_PLATFORM_LINUX)

#include <string>

namespace rs::probe {

std::string probe_platform_name() {
#if defined(RS_PLATFORM_MACOS)
    return "macos";
#elif defined(RS_PLATFORM_WINDOWS)
    return "windows";
#else
    return "unknown";
#endif
}

Result probe_virtual_memory(const Options&) {
    Result result;
    result.implemented = false;

    vm::EnvironmentProfile& profile = result.profile;
    // Not `measured`: nothing here was measured. Calling it measured would be
    // the exact failure mode this project exists to catch.
    profile.origin = vm::ProfileOrigin::Synthetic;
    profile.run.probe_version = kProbeVersion;

#if defined(RS_PLATFORM_MACOS)
    profile.platform.os = vm::OperatingSystem::MacOS;
#elif defined(RS_PLATFORM_WINDOWS)
    profile.platform.os = vm::OperatingSystem::Windows;
#else
    profile.platform.os = vm::OperatingSystem::Unknown;
#endif

    // Process architecture is knowable at compile time without measuring
    // anything, so it is the one fact this stub may legitimately fill in.
#if defined(__aarch64__) || defined(_M_ARM64)
    profile.platform.process_arch = vm::Architecture::Aarch64;
#elif defined(__x86_64__) || defined(_M_X64)
    profile.platform.process_arch = vm::Architecture::X86_64;
#elif defined(__i386__) || defined(_M_IX86)
    profile.platform.process_arch = vm::Architecture::X86;
#else
    profile.platform.process_arch = vm::Architecture::Unknown;
#endif
    profile.platform.host_arch = vm::Architecture::Unknown;
    profile.platform.translation_mode = vm::TranslationMode::Unknown;

    profile.run.warnings.emplace_back(
        "no virtual-memory probe is implemented for this platform in v0.1; "
        "every capability is reported as unknown. Nothing here was measured.");
    profile.notes.emplace_back(
        "Do not use this profile as evidence. It exists so that tooling has a "
        "schema-valid document to work with on unsupported platforms.");

    return result;
}

}  // namespace rs::probe

#endif  // !RS_PLATFORM_LINUX

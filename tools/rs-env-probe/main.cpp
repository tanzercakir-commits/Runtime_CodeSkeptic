// SPDX-License-Identifier: Apache-2.0
//
//   rs-env-probe vm [--output FILE] [--no-scan] [--no-faulting-tests]
//                   [--pretty|--canonical] [--name NAME]
//
// Measures this host's virtual-memory behavior and writes an environment
// profile. Exit code 0 means a profile was produced, whether or not this
// platform is implemented; use `rs-profile verify` to judge its usefulness.
#include <cstring>
#include <iostream>
#include <string>

#include "runtimeskeptic/core/io.hpp"
#include "runtimeskeptic/core/json.hpp"
#include "runtimeskeptic/probe/vm_probe.hpp"
#include "runtimeskeptic/reports/report.hpp"
#include "runtimeskeptic/version.hpp"

namespace {

void print_usage() {
    std::cout <<
        R"(rs-env-probe - measure host runtime capabilities

USAGE
  rs-env-probe vm [OPTIONS]

OPTIONS
  --output FILE          write the profile here (default: stdout)
  --name NAME            human label stored in the profile; it is NOT part of
                         the profile_id, so renaming never changes identity
  --no-scan              skip the address-space scan
  --no-faulting-tests    skip tests that deliberately fault in an isolated child
  --pretty               indented JSON (default)
  --canonical            byte-exact canonical JSON, the form that is hashed
  -h, --help             show this help

PLATFORMS
  Linux, macOS and Windows are measured directly. On a platform with no probe,
  the profile is schema-valid with every fact unknown, and exit is still 0; use
  `rs-profile verify` to see how much it actually established.

SAFETY
  The probe never destroys an existing mapping on any platform. Where it tests
  exact placement it uses only non-destructive primitives - on Linux
  MAP_FIXED_NOREPLACE, never MAP_FIXED, and only after verifying the kernel
  honours the flag; macOS and Windows use their platforms' non-clobbering
  equivalents. Tests that can fault run in an isolated child process with a
  timeout, so a fault cannot bring down the probe.
)";
}

}  // namespace

int main(int argc, char** argv) {
    using namespace rs;

    if (argc < 2) {
        print_usage();
        return reports::exit_code::kUsage;
    }

    const std::string command = argv[1];
    if (command == "-h" || command == "--help" || command == "help") {
        print_usage();
        return reports::exit_code::kSupported;
    }
    if (command != "vm") {
        std::cerr << "rs-env-probe: unknown domain '" << command
                  << "'. Only 'vm' exists in v0.1.\n";
        return reports::exit_code::kUsage;
    }

    probe::Options options;
    std::string output_path = "-";
    std::string profile_name;
    bool canonical = false;

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--output" || arg == "-o") {
            if (i + 1 >= argc) {
                std::cerr << "rs-env-probe: --output requires a path\n";
                return reports::exit_code::kUsage;
            }
            output_path = argv[++i];
        } else if (arg == "--name") {
            if (i + 1 >= argc) {
                std::cerr << "rs-env-probe: --name requires a value\n";
                return reports::exit_code::kUsage;
            }
            profile_name = argv[++i];
        } else if (arg == "--no-scan") {
            options.scan_address_space = false;
        } else if (arg == "--no-faulting-tests") {
            options.run_faulting_tests = false;
        } else if (arg == "--canonical") {
            canonical = true;
        } else if (arg == "--pretty") {
            canonical = false;
        } else if (arg == "-h" || arg == "--help") {
            print_usage();
            return reports::exit_code::kSupported;
        } else {
            std::cerr << "rs-env-probe: unrecognized option '" << arg << "'\n";
            return reports::exit_code::kUsage;
        }
    }

    probe::Result result = probe::probe_virtual_memory(options);
    result.profile.run.tool_version = kToolVersion;
    // `--name` wins, then whatever the probe chose for itself, then a default
    // built from the platform name.
    //
    // The middle case was missing and the override was unconditional, which
    // discarded the one thing the probe knows and this tool cannot: the
    // Windows probe detects Wine through `wine_get_version` and names itself
    // `wine-on-posix-x86_64` so the profile cannot be read as a Windows
    // measurement - and this line renamed it `windows-x86_64` regardless.
    if (!profile_name.empty()) {
        result.profile.profile_name = profile_name;
    } else if (result.profile.profile_name.empty()) {
        result.profile.profile_name =
            probe::probe_platform_name() + "-" +
            std::string(vm::to_string(result.profile.platform.process_arch));
    }

    if (!result.implemented) {
        std::cerr << "rs-env-probe: no probe is implemented for "
                  << probe::probe_platform_name()
                  << " in v0.1. A profile with every fact unknown was written; "
                     "it must not be used as evidence.\n";
    }

    const json::Value document = result.profile.to_json();
    std::string serialized;
    if (canonical) {
        auto canonical_text = json::serialize_canonical(document);
        if (!canonical_text) {
            std::cerr << "rs-env-probe: internal error, the profile has no "
                         "canonical form\n";
            return reports::exit_code::kInternal;
        }
        serialized = std::move(*canonical_text);
        serialized.push_back('\n');
    } else {
        serialized = json::serialize_pretty(document);
    }

    std::string error;
    if (!io::write_file(output_path, serialized, error)) {
        std::cerr << "rs-env-probe: " << error << "\n";
        return reports::exit_code::kInput;
    }

    if (output_path != "-") {
        std::cerr << "profile written to " << output_path << "\n";
        std::cerr << "profile_id: " << result.profile.profile_id() << "\n";
        for (const auto& warning : result.profile.run.warnings) {
            std::cerr << "warning: " << warning << "\n";
        }
    }
    return reports::exit_code::kSupported;
}

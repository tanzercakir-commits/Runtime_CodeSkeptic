// SPDX-License-Identifier: Apache-2.0
//
//   rs-replay BUNDLE_DIR
//   rs-replay trace TRACE.jsonl
//
// Re-runs an analysis bundle or a sealed runtime trace without issuing the
// observed operating-system calls again.
#include <iostream>
#include <string>

#include "runtimeskeptic/reports/bundle.hpp"
#include "runtimeskeptic/runtime/trace.hpp"

namespace {

void print_usage() {
    std::cout <<
        R"(rs-replay - re-derive a verdict or replay a runtime trace

USAGE
  rs-replay BUNDLE_DIR
  rs-replay trace TRACE.jsonl

BUNDLE REPLAY
  Checks stored hashes, re-runs analysis, and compares verdict and finding IDs.

TRACE REPLAY
  Validates canonical JSONL, sequence, completeness and SHA-256, then applies a
  pure VM lifecycle reducer. Replay never reissues mmap, VirtualAlloc, or any
  other operating-system call.

EXIT CODES
  0   reproduced
  1   diverged
  64  usage error
  65  input could not be read or did not satisfy its sealed contract
)";
}

int replay_trace(const std::string& path) {
    std::string error;
    auto outcome = rs::runtime::trace::replay_file(path, error);
    if (!outcome) {
        std::cerr << "rs-replay: " << error << "\n";
        return 65;
    }
    std::cout << (outcome->reproduced ? "reproduced" : "diverged") << ": "
              << outcome->detail << "\n";
    std::cout << "  semantic violations: "
              << outcome->semantic_violations.size() << "\n";
    for (const std::string& violation : outcome->semantic_violations) {
        std::cout << "    " << violation << "\n";
    }
    return outcome->reproduced ? 0 : 1;
}

int replay_bundle(const std::string& dir) {
    std::string error;
    auto outcome = rs::bundle::replay_bundle(dir, error);
    if (!outcome) {
        std::cerr << "rs-replay: " << error << "\n";
        return 65;
    }

    for (const auto& file : outcome->tampered_files) {
        std::cerr << "rs-replay: TAMPERED - '" << file
                  << "' no longer matches its recorded hash\n";
    }

    std::cout << (outcome->reproduced ? "reproduced" : "diverged") << ": "
              << outcome->detail << "\n";
    std::cout << "  recorded verdict : " << outcome->recorded_overall << "\n";
    std::cout << "  replayed verdict : " << outcome->replayed_overall << "\n";
    if (outcome->recorded_finding_ids != outcome->replayed_finding_ids) {
        std::cout << "  finding IDs differ\n";
        std::cout << "    recorded:";
        for (const auto& id : outcome->recorded_finding_ids) {
            std::cout << " " << id;
        }
        std::cout << "\n    replayed:";
        for (const auto& id : outcome->replayed_finding_ids) {
            std::cout << " " << id;
        }
        std::cout << "\n";
    }
    return outcome->reproduced ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2) {
        const std::string arg = argv[1];
        if (arg == "-h" || arg == "--help") {
            print_usage();
            return 0;
        }
        if (!arg.empty() && arg[0] == '-') {
            std::cerr << "rs-replay: unrecognized option '" << arg << "'\n";
            return 64;
        }
        return replay_bundle(arg);
    }
    if (argc == 3 && std::string(argv[1]) == "trace") {
        return replay_trace(argv[2]);
    }
    print_usage();
    return 64;
}

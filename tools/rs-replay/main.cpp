// SPDX-License-Identifier: Apache-2.0
//
//   rs-replay BUNDLE_DIR
//
// Re-runs an analysis from an evidence bundle ALONE and reports whether it
// reproduces the verdict the bundle was sealed with. A verdict that cannot be
// replayed by someone else is an opinion with a machine behind it; this is the
// someone else.
#include <iostream>
#include <string>

#include "runtimeskeptic/reports/bundle.hpp"

namespace {

void print_usage() {
    std::cout <<
        R"(rs-replay - re-derive a verdict from an evidence bundle, and check it

USAGE
  rs-replay BUNDLE_DIR

Reads only the bundle: its recorded requirement and profile, and its manifest.
Re-runs the analysis, then checks three things -

  * every stored file still hashes to what the manifest recorded (not edited)
  * the re-derived verdict equals the recorded one
  * the re-derived finding IDs equal the recorded ones

and prints the result. The bundle is produced by `rs-check --bundle DIR`.

EXIT CODES
  0   reproduced: the bundle re-derives its own verdict and findings
  1   diverged: it does not, or a stored file was edited after sealing
  64  usage error
  65  the bundle could not be read or is not an analysis bundle
)";
}

}  // namespace

int main(int argc, char** argv) {
    std::string dir;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage();
            return 0;
        }
        if (!arg.empty() && arg[0] == '-') {
            std::cerr << "rs-replay: unrecognized option '" << arg << "'\n";
            return 64;
        }
        if (!dir.empty()) {
            std::cerr << "rs-replay: unexpected extra argument '" << arg << "'\n";
            return 64;
        }
        dir = arg;
    }
    if (dir.empty()) {
        print_usage();
        return 64;
    }

    std::string error;
    auto outcome = rs::bundle::replay_bundle(dir, error);
    if (!outcome) {
        std::cerr << "rs-replay: " << error << "\n";
        return 65;
    }

    for (const auto& f : outcome->tampered_files) {
        std::cerr << "rs-replay: TAMPERED - '" << f
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

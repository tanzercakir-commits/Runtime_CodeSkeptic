// SPDX-License-Identifier: Apache-2.0
//
//   rs-extract SOURCE... [--out BUNDLE.json] [--component NAME] [--explain]
//
// Reads C/C++ source and writes the requirement bundle rs-check consumes.
//
// This is a REFERENCE extractor, and the distinction matters. The roadmap has
// CodeSkeptic producing these documents from real static analysis; this tool
// exists so that the path from source to verdict is not purely hypothetical
// while that work sits elsewhere. It recognises a handful of named patterns and
// is loud about the rest.
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "runtimeskeptic/core/io.hpp"
#include "runtimeskeptic/core/json.hpp"
#include "runtimeskeptic/extract/scanner.hpp"
#include "runtimeskeptic/version.hpp"

namespace {

void print_usage() {
    std::cout <<
        R"(rs-extract - read C/C++ source, write candidate requirements

USAGE
  rs-extract SOURCE... [OPTIONS]

Emits a runtime-skeptic.application-requirements-bundle.v1 document on stdout,
or to --out. Feed it to rs-check against a measured profile.

WHAT IT IS
  A bounded text scanner. It does NOT resolve macros, follow dataflow,
  evaluate constant expressions, or know which branch runs. Every requirement
  it produces carries `statically_inferred` evidence - which caps any finding
  derived from it at COUNTEREXAMPLE, never PROVEN - and an
  `extraction_limitations` list naming what it could not determine at that
  specific site.

  A pattern it does not recognise produces nothing. Nothing is not evidence
  that the program has no such requirement.

OPTIONS
  --out FILE           write the bundle here (default: stdout)
  --component NAME     component label for every requirement
  --explain            also print, to stderr, every mapping call that matched
                       no recognizer - the near misses are the honest measure
                       of what this tool is not seeing
  --list-recognizers   print the named patterns and what each cannot determine
  -h, --help           show this help

EXIT CODES
  0   scan completed (even when it found nothing)
  64  usage error
  65  a source file could not be read
)";
}

void list_recognizers() {
    for (const auto& r : rs::extract::recognizers()) {
        std::cout << r.name << "\n";
        std::cout << "  matches            : " << r.matches << "\n";
        std::cout << "  cannot determine   : " << r.cannot_determine << "\n\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> sources;
    std::string out_path;
    rs::extract::ScanOptions options;
    bool explain = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage();
            return 0;
        }
        if (arg == "--list-recognizers") {
            list_recognizers();
            return 0;
        }
        if (arg == "--explain") {
            explain = true;
        } else if (arg == "--out" && i + 1 < argc) {
            out_path = argv[++i];
        } else if (arg == "--component" && i + 1 < argc) {
            options.component = argv[++i];
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "rs-extract: unrecognized option " << arg << "\n";
            return 64;
        } else {
            sources.push_back(arg);
        }
    }

    if (sources.empty()) {
        print_usage();
        return 64;
    }

    std::vector<rs::extract::ScanReport> reports;
    for (const auto& path : sources) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            std::cerr << "rs-extract: cannot read " << path << "\n";
            return 65;
        }
        std::stringstream buffer;
        buffer << in.rdbuf();
        reports.push_back(
            rs::extract::scan_source(path, buffer.str(), options));
    }

    std::size_t candidates = 0;
    std::size_t rejected = 0;
    for (const auto& r : reports) {
        candidates += r.candidates.size();
        rejected += r.rejected_sites.size();
        if (explain) {
            for (const auto& site : r.rejected_sites) {
                std::cerr << "not recognised: " << site << "\n";
            }
        }
    }

    const rs::json::Value bundle =
        rs::extract::to_bundle(reports, rs::kToolVersion);
    auto text = rs::json::serialize_canonical(bundle);
    if (!text) {
        std::cerr << "rs-extract: could not serialize the bundle\n";
        return 70;
    }

    if (out_path.empty()) {
        std::cout << *text << "\n";
    } else {
        std::ofstream out(out_path, std::ios::binary);
        if (!out) {
            std::cerr << "rs-extract: cannot write " << out_path << "\n";
            return 65;
        }
        out << *text << "\n";
    }

    std::cerr << "rs-extract: " << candidates << " candidate requirement(s) from "
              << sources.size() << " file(s); " << rejected
              << " mapping call(s) matched no recognizer";
    if (rejected > 0 && !explain) std::cerr << " (--explain to list them)";
    std::cerr << "\n";
    return 0;
}

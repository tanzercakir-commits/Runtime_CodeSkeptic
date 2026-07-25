// SPDX-License-Identifier: Apache-2.0
//
// A BOUNDED source scanner: C/C++ text in, candidate requirements out.
//
// WHAT THIS IS NOT. It is not a compiler, a parser, or a static analyzer. It
// does not resolve macros, follow dataflow, evaluate constant expressions, or
// know which branch runs. It reads text and recognises a small, NAMED set of
// virtual-memory patterns.
//
// It exists because until now there was no path at all from source to a
// requirement document - every contract in this repository was written by hand,
// which the external evaluation correctly named as the project's largest gap.
// A bounded scanner that says what it saw and what it could not see closes more
// of that gap than nothing, and less of it than a real extractor would.
//
// THE HONESTY CONTRACT. Every requirement this produces:
//
//   - carries `statically_inferred` evidence, which ceilings any finding
//     derived from it at COUNTEREXAMPLE, never PROVEN;
//   - carries `extraction_limitations` naming what the scanner could not
//     determine for THAT site, not a generic disclaimer;
//   - is a CANDIDATE for a human to check, never an assertion about the
//     program.
//
// A pattern this scanner does not recognise produces nothing, and producing
// nothing is not evidence that the program has no such requirement. That
// asymmetry is the whole reason `Fact` and the evidence ladder exist, and it
// applies to this tool with more force than to any other part of the project.
#ifndef RUNTIMESKEPTIC_EXTRACT_SCANNER_HPP
#define RUNTIMESKEPTIC_EXTRACT_SCANNER_HPP

#include <string>
#include <vector>

#include "runtimeskeptic/vm/requirement.hpp"

namespace rs::extract {

// One recognised site, before it becomes a Requirement.
struct Candidate {
    std::string recognizer;   // which named pattern matched
    std::string file;
    std::size_t line = 0;
    std::string symbol;       // enclosing function, if the scanner could tell
    std::string quote;        // the source text that matched
    vm::Requirement requirement;
};

struct ScanOptions {
    // Emitted into every requirement's component field when the caller knows
    // what it is scanning.
    std::string component;
    // Lines of context searched after a mapping call for its failure sink.
    std::size_t sink_search_lines = 12;
};

struct ScanReport {
    std::vector<Candidate> candidates;

    // Files read, and files skipped with the reason. A scanner that silently
    // skips input is a scanner whose empty result means nothing.
    std::vector<std::string> files_scanned;
    std::vector<std::string> files_skipped;

    // Sites that matched a recognizer's opening pattern but were dropped,
    // with why. These are the near-misses, and they are the honest measure of
    // how much the scanner is not seeing.
    std::vector<std::string> rejected_sites;
};

// Scans one translation unit's text. `path` is used for source locations only;
// nothing is read from disk here, so callers control IO and encoding.
ScanReport scan_source(const std::string& path, const std::string& text,
                       const ScanOptions& options = {});

// Merges scan reports into the bundle document rs-check consumes.
json::Value to_bundle(const std::vector<ScanReport>& reports,
                      const std::string& tool_version);

// The recognizers this build knows, for `rs-extract --list-recognizers` and for
// the tests that assert the list has not silently shrunk.
struct RecognizerInfo {
    const char* name;
    const char* matches;
    const char* cannot_determine;
};
std::vector<RecognizerInfo> recognizers();

}  // namespace rs::extract

#endif  // RUNTIMESKEPTIC_EXTRACT_SCANNER_HPP

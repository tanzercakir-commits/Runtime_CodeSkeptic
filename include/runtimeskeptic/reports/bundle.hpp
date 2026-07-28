// SPDX-License-Identifier: Apache-2.0
//
// The evidence bundle (ROADMAP §17). A verdict that cannot be replayed by
// someone else is an opinion with a machine behind it.
//
// A bundle is a directory that carries everything an analysis consumed and
// produced - the requirement, the profile, the findings, a human report, and a
// manifest of hashes - together with a REPLAY STATUS that certifies the bundle
// reproduces its own verdict. The certification is not a promise; it is done by
// re-running the analysis from the just-written files at write time and
// recording what happened, so a bundle that says `reproduced` was demonstrated
// to, and one that says `diverged` names the fact that moved.
//
// Two operations, and the second reads only the bundle:
//   write_bundle   emit the directory from the two input documents, and
//                  self-certify by replaying from what was written
//   replay_bundle  re-run from the bundle ALONE and compare to what it recorded
//
// The suggested §17 layout also lists `static_assumptions.json`,
// `runtime_trace.jsonl` and a `contracts/` directory. Those come from the
// monitor and the static extractor, which are Phase 4 and Phase 5 and do not
// exist. The manifest records their absence with the reason rather than writing
// empty files that would read as "there was no trace" instead of "this tool does
// not produce one yet".
#ifndef RUNTIMESKEPTIC_REPORTS_BUNDLE_HPP
#define RUNTIMESKEPTIC_REPORTS_BUNDLE_HPP

#include <optional>
#include <string>
#include <vector>

#include "runtimeskeptic/vm/analyzer.hpp"

namespace rs::bundle {

// The two documents an analysis consumes, as their EXACT input bytes. The bundle
// stores them verbatim rather than re-serialising a parsed copy: a re-serialised
// profile could differ from the file the host actually produced, and then the
// bundle would not be a record of what was run.
struct Inputs {
    std::string requirement_text;
    std::string profile_text;
    // Provenance, for the manifest only. Where these documents came from does not
    // affect the verdict and is not hashed into anything.
    std::string requirement_source = "";
    std::string profile_source = "";
};

// What a replay found. `reproduced` is the whole question; the rest is why.
struct ReplayOutcome {
    bool reproduced = false;
    std::string recorded_overall;    // the verdict the bundle was written with
    std::string replayed_overall;    // the verdict re-derived from the bundle
    std::vector<std::string> recorded_finding_ids;
    std::vector<std::string> replayed_finding_ids;
    // Present when a stored file's bytes no longer match the manifest's hash of
    // them - a bundle that was edited after it was sealed.
    std::vector<std::string> tampered_files;
    std::string detail;
};

// Writes `dir/` (creating it and parents) with the full bundle, and self-
// certifies by replaying from the written files. Returns false and sets `error`
// on an IO or parse failure; a bundle that writes but fails to reproduce is NOT
// an error here - it is a valid bundle whose manifest says `diverged`, and the
// caller decides what that means. `replay_out`, when non-null, receives the
// self-certification result.
bool write_bundle(const std::string& dir, const Inputs& inputs,
                  const vm::AnalysisOptions& options, std::string& error,
                  ReplayOutcome* replay_out = nullptr);

// Re-runs the analysis from the bundle at `dir` alone - reading only its files -
// and compares the verdict and finding IDs against what the manifest recorded.
// Also checks every stored file against its hash in the manifest. Returns
// nullopt only when the bundle cannot be read or is not a bundle; a bundle that
// reads but does not reproduce returns an outcome with `reproduced == false`.
std::optional<ReplayOutcome> replay_bundle(const std::string& dir,
                                           std::string& error);

}  // namespace rs::bundle

#endif  // RUNTIMESKEPTIC_REPORTS_BUNDLE_HPP

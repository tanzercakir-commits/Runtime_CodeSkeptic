// SPDX-License-Identifier: Apache-2.0
//
// The compatibility analyzer: does this host satisfy this requirement?
//
// Design constraints taken from the ROADMAP:
//
//   - "The first implementation should use bounded, explainable reasoning.
//      It should avoid immediately building a general symbolic executor."
//     (10.6) Every rule here is a named, total function over two documents.
//     There is no search and no solver.
//
//   - "Every non-supported result must include a machine-readable reason."
//     (Phase 2 exit criteria) A rule may not lower the verdict without
//     emitting a Finding that carries an evidence chain.
//
//   - Confidence is never asserted by a rule directly. Each rule states the
//     confidence it would like, and the engine clamps it to the ceiling
//     implied by the weakest fact in that finding's own evidence chain.
#ifndef RUNTIMESKEPTIC_VM_ANALYZER_HPP
#define RUNTIMESKEPTIC_VM_ANALYZER_HPP

#include <string>
#include <vector>

#include "runtimeskeptic/core/json.hpp"
#include "runtimeskeptic/vm/finding.hpp"
#include "runtimeskeptic/vm/profile.hpp"
#include "runtimeskeptic/vm/requirement.hpp"

namespace rs::vm {

inline constexpr const char* kResultSchema =
    "runtime-skeptic.compatibility-result.v1";

struct AnalysisOptions {
    // Emit an informational finding for every requested capability whose
    // profile fact is unknown. Off by default in CI reports because it is
    // noisy; on by default in the terminal report because it tells the user
    // which probe to run next.
    bool report_unknowns = true;
};

struct AnalysisResult {
    std::string schema = kResultSchema;
    SupportLevel overall = SupportLevel::Unknown;
    std::vector<Finding> findings;

    std::string profile_id;
    std::string requirement_id;
    std::string profile_name;
    std::string requirement_name;
    ProfileOrigin profile_origin = ProfileOrigin::Unknown;

    // Set when the analyzer refused to reason further, e.g. an operation kind
    // the VM domain does not model.
    std::vector<std::string> analyzer_limitations;

    json::Value to_json() const;

    std::size_t count_at_least(Severity threshold) const;
    bool has_confidence(Confidence c) const;
};

AnalysisResult analyze(const Requirement& requirement,
                       const EnvironmentProfile& profile,
                       const AnalysisOptions& options = {});

}  // namespace rs::vm

#endif  // RUNTIMESKEPTIC_VM_ANALYZER_HPP

// SPDX-License-Identifier: Apache-2.0
//
// Which contracts change their verdict when the environment changes.
//
// `rs-profile diff` answers "did the platform change?" at the level of facts:
// 138 differences between two measured macOS lanes, one line per JSON pointer.
// That is precise and almost unreadable, and it does not answer the question a
// team actually asks when a kernel ships:
//
//     "does any of this matter to US?"
//
// This does. Given a set of contracts and two profiles, it re-evaluates every
// contract against both and reports only the ones whose verdict MOVED. A
// profile difference that no contract depends on is noise; a profile
// difference that flips one contract from SUPPORTED to UNSUPPORTED is the
// whole story.
//
// THREE DESIGN DECISIONS, each of which could have gone the easy way.
//
// 1. UNKNOWN ON BOTH SIDES IS NOT "UNCHANGED". A contract nobody could answer
//    before and nobody can answer now has not stayed the same - it has never
//    been answered at all. Counting it as unchanged is how a report that says
//    "3 of 40 contracts affected" gets read as "37 are fine". It is reported
//    as its own class, `NeverAnswered`, and printed before the good news.
//
// 2. NO FINDING IS NAMED "RESPONSIBLE". Naming one id as the cause of a
//    verdict change is a causal claim, and with several rules firing at once
//    it is often the wrong one. What can be said mechanically is which ids
//    APPEARED and which DISAPPEARED. When exactly one appeared alongside a
//    regression the reader will draw the obvious conclusion, and it will be
//    theirs rather than the tool's.
//
// 3. A BUNDLE IS COMPARED REQUIREMENT BY REQUIREMENT, NOT BY ITS WORST. A
//    contract file may hold many requirements, and `rs-check` reports the
//    worst of them. If one requirement improves from UNSUPPORTED to SUPPORTED
//    while another regresses the other way, the worst-of is identical on both
//    sides and a whole-file comparison would print nothing at all. That is the
//    silence-reads-as-success failure this project has already made twice, so
//    the comparison is per requirement and the file is marked changed if ANY
//    of them moved.
#ifndef RUNTIMESKEPTIC_VM_IMPACT_HPP
#define RUNTIMESKEPTIC_VM_IMPACT_HPP

#include <string>
#include <vector>

#include "runtimeskeptic/core/evidence.hpp"
#include "runtimeskeptic/vm/analyzer.hpp"
#include "runtimeskeptic/vm/profile.hpp"
#include "runtimeskeptic/vm/requirement.hpp"

namespace rs::vm {

enum class VerdictChange {
    // The verdict moved to a worse level, in the aggregation order
    // Supported < ConditionallySupported < Unknown < Unsupported. This is the
    // class that fails the run.
    Regressed = 0,
    // Moved to a better level. Reported, never celebrated: an improvement can
    // also mean a fact stopped being measured.
    Improved = 1,
    // Same verdict, and it was an answer.
    Unchanged = 2,
    // UNKNOWN on both sides. Deliberately not `Unchanged` - see decision 1.
    NeverAnswered = 3,
};

std::string_view to_string(VerdictChange c);

// One requirement, evaluated against both profiles.
struct RequirementImpact {
    std::string requirement_name;
    std::size_t index = 0;  // position within the contract file
    SupportLevel before = SupportLevel::Unknown;
    SupportLevel after = SupportLevel::Unknown;
    VerdictChange change = VerdictChange::Unchanged;

    // Finding ids present after but not before, and before but not after.
    // Sorted, deduplicated. NOT a claim about causation.
    std::vector<std::string> ids_appeared;
    std::vector<std::string> ids_disappeared;
};

// One contract file.
struct ContractImpact {
    std::string path;
    std::vector<RequirementImpact> requirements;

    // The worst change across the file's requirements, which is what a caller
    // gating a pipeline wants. Regressed beats Improved beats NeverAnswered
    // beats Unchanged - ordered by "how much does this need a human".
    VerdictChange change = VerdictChange::Unchanged;

    bool moved() const {
        return change == VerdictChange::Regressed ||
               change == VerdictChange::Improved;
    }
};

struct ImpactReport {
    std::string before_profile_id;
    std::string after_profile_id;
    std::string before_profile_name;
    std::string after_profile_name;

    std::vector<ContractImpact> contracts;   // in the order supplied

    // Files that could not be read, parsed, or that held no usable
    // requirement. Reported, never skipped in silence: a contract that fails
    // to load is not a contract that passed.
    std::vector<std::string> unreadable;

    std::size_t count(VerdictChange c) const;
    bool any_regression() const { return count(VerdictChange::Regressed) > 0; }

    json::Value to_json() const;
};

// Compares one already-loaded contract across two profiles.
ContractImpact compare_contract(const std::string& path,
                                const std::vector<Requirement>& requirements,
                                const EnvironmentProfile& before,
                                const EnvironmentProfile& after,
                                const AnalysisOptions& options);

// Loads each path, compares it, and collects the failures rather than
// aborting: one unreadable file in forty should not cost the other
// thirty-nine.
ImpactReport compare_contracts(const std::vector<std::string>& paths,
                               const EnvironmentProfile& before,
                               const EnvironmentProfile& after,
                               const AnalysisOptions& options);

}  // namespace rs::vm

#endif  // RUNTIMESKEPTIC_VM_IMPACT_HPP

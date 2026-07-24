// SPDX-License-Identifier: Apache-2.0
//
// The evidence model is the load-bearing idea of RuntimeSkeptic.
//
// ROADMAP section 8.2: "Every fact in the system must include an evidence
// class."  ROADMAP section 8.3: "Proof levels must never be mixed."
//
// Two separate ladders exist and they must not be conflated:
//
//   EvidenceClass  - how we know a single FACT
//   Confidence     - how strong a FINDING derived from those facts is
//
// The mapping from facts to a finding's confidence is deliberately
// conservative: a finding can never be stronger than its weakest supporting
// fact. That rule lives in `weakest()` / `confidence_ceiling()` below and is
// enforced by unit tests, not by convention.
#ifndef RUNTIMESKEPTIC_CORE_EVIDENCE_HPP
#define RUNTIMESKEPTIC_CORE_EVIDENCE_HPP

#include <string>
#include <string_view>
#include <vector>

#include "runtimeskeptic/core/json.hpp"

namespace rs {

// ---------------------------------------------------------------------------
// How do we know a fact?
// ---------------------------------------------------------------------------
// Ordered from strongest to weakest. The numeric order is part of the
// semantics: `weakest()` relies on it.
//
// THE ORDER IS DEFINED BY `confidence_ceiling`, not by intuition about which
// kind of knowing feels more respectable. That constraint is load-bearing and
// was learned the hard way: an earlier version ranked ObservedInvariant above
// StaticallyInferred (as the ROADMAP's prose does), which made the mapping
// non-monotonic - a chain whose weakest link was `statically_inferred` could
// claim COUNTEREXAMPLE while a chain whose weakest link was the *stronger*
// `observed_invariant` was capped at OBSERVED_INVARIANT. Adding better
// evidence to a chain could therefore weaken its conclusion.
//
// The ladder's only job is to decide the ceiling, so it is ordered by the
// ceiling. `ceiling_is_monotonic_in_the_evidence_order` in the unit tests
// enforces this and will fail if a future class is inserted in the wrong
// place.
enum class EvidenceClass {
    SpecifiedGuarantee = 0,    // documented, binding platform contract
    MeasuredCapability = 1,    // rs-env-probe observed it on this host
    StaticallyInferred = 2,    // derived from source analysis
    BoundedCounterexample = 3, // found within an explicit exploration bound
    ObservedInvariant = 4,     // held across a recorded trace set, or a
                               // credible third-party observation we cannot
                               // reproduce
    HeuristicRisk = 5,         // pattern match / trend, no proof
    Unknown = 6,               // NOT probed, NOT specified. Never "supported".
};

std::string_view to_string(EvidenceClass c);
bool evidence_class_from_string(std::string_view s, EvidenceClass& out);

// Returns the weaker (numerically larger) of the two.
EvidenceClass weakest(EvidenceClass a, EvidenceClass b);

// ---------------------------------------------------------------------------
// How strong is a finding? (ROADMAP section 11)
// ---------------------------------------------------------------------------
enum class Confidence {
    Proven = 0,             // constraints are unsatisfiable
    Counterexample = 1,     // a platform-legal outcome reaches a failure sink
    ObservedInvariant = 2,  // historical invariant changed; evidence, not proof
    Predictive = 3,         // trend suggests future failure
    Hypothesis = 4,         // plausible, incomplete evidence
};

std::string_view to_string(Confidence c);
bool confidence_from_string(std::string_view s, Confidence& out);

// The strongest confidence a finding may claim, given the weakest fact it
// depends on. This is the single place where "do not overclaim" is encoded.
//
//   SpecifiedGuarantee / MeasuredCapability -> Proven is allowed
//   ObservedInvariant                       -> at most ObservedInvariant
//   StaticallyInferred                      -> at most Counterexample
//   BoundedCounterexample                   -> at most Counterexample
//   HeuristicRisk                           -> at most Predictive
//   Unknown                                 -> at most Hypothesis
Confidence confidence_ceiling(EvidenceClass weakest_fact);

// Clamps `desired` so that it never exceeds the ceiling implied by `weakest_fact`.
Confidence clamp_confidence(Confidence desired, EvidenceClass weakest_fact);

// ---------------------------------------------------------------------------
// Compatibility verdict (ROADMAP Phase 2 exit criteria)
// ---------------------------------------------------------------------------
enum class SupportLevel {
    Supported = 0,
    ConditionallySupported = 1,
    Unknown = 2,
    Unsupported = 3,
};

std::string_view to_string(SupportLevel s);
bool support_level_from_string(std::string_view s, SupportLevel& out);

// Aggregation order for an overall verdict:
//
//   Unsupported > Unknown > ConditionallySupported > Supported
//
// Unknown deliberately outranks ConditionallySupported. If any relevant
// platform fact was never established, the tool must not present the request
// as merely "conditional" - that would imply the conditions are known.
SupportLevel combine(SupportLevel a, SupportLevel b);

// ---------------------------------------------------------------------------
// Severity
// ---------------------------------------------------------------------------
enum class Severity {
    Critical = 0,  // process cannot start / will abort
    High = 1,      // semantic guarantee violated, corruption or fatal sink
    Medium = 2,    // degraded semantics, may be tolerable
    Low = 3,       // portability smell
    Info = 4,
};

std::string_view to_string(Severity s);
bool severity_from_string(std::string_view s, Severity& out);

// ---------------------------------------------------------------------------
// A single step in a finding's evidence chain (ROADMAP section 8.6)
// ---------------------------------------------------------------------------
//
// A useful report must answer:
//   What was required? Which layer required it? What did the environment
//   provide? Which layer weakened the behavior? Why is the outcome
//   impossible, possible or risky?
//
// `layer` names the architectural layer the fact belongs to, which is what
// turns a list of facts into a cross-layer narrative.
enum class Layer {
    Application = 0,
    Library = 1,
    LanguageRuntime = 2,
    CompatibilityLayer = 3,  // emulator / translator / adapter
    SystemCallInterface = 4,
    OperatingSystem = 5,
    Kernel = 6,
    Hardware = 7,
    Analyzer = 8,  // a fact contributed by RuntimeSkeptic itself
};

std::string_view to_string(Layer l);
bool layer_from_string(std::string_view s, Layer& out);

struct EvidenceStep {
    Layer layer = Layer::Analyzer;
    EvidenceClass evidence = EvidenceClass::Unknown;
    std::string claim;    // the fact, stated plainly
    std::string source;   // where the fact came from: probe id, doc, source loc
    std::string detail;   // optional supporting numbers

    json::Value to_json() const;
};

class EvidenceChain {
public:
    void add(Layer layer, EvidenceClass evidence, std::string claim,
             std::string source, std::string detail = {});

    void add(EvidenceStep step);

    bool empty() const { return steps_.empty(); }
    std::size_t size() const { return steps_.size(); }
    const std::vector<EvidenceStep>& steps() const { return steps_; }

    // The weakest evidence class in the chain. Drives confidence clamping.
    EvidenceClass weakest_class() const;

    json::Value to_json() const;

private:
    std::vector<EvidenceStep> steps_;
};

}  // namespace rs

#endif  // RUNTIMESKEPTIC_CORE_EVIDENCE_HPP

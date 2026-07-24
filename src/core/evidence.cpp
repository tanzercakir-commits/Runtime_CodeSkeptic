// SPDX-License-Identifier: Apache-2.0
#include "runtimeskeptic/core/evidence.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace rs {
namespace {

template <typename Enum, std::size_t N>
bool lookup(const std::array<std::pair<std::string_view, Enum>, N>& table,
            std::string_view s, Enum& out) {
    for (const auto& [name, value] : table) {
        if (name == s) {
            out = value;
            return true;
        }
    }
    return false;
}

constexpr std::array<std::pair<std::string_view, EvidenceClass>, 7> kEvidence{{
    {"specified_guarantee", EvidenceClass::SpecifiedGuarantee},
    {"measured_capability", EvidenceClass::MeasuredCapability},
    {"observed_invariant", EvidenceClass::ObservedInvariant},
    {"statically_inferred", EvidenceClass::StaticallyInferred},
    {"bounded_counterexample", EvidenceClass::BoundedCounterexample},
    {"heuristic_risk", EvidenceClass::HeuristicRisk},
    {"unknown", EvidenceClass::Unknown},
}};

constexpr std::array<std::pair<std::string_view, Confidence>, 5> kConfidence{{
    {"PROVEN", Confidence::Proven},
    {"COUNTEREXAMPLE", Confidence::Counterexample},
    {"OBSERVED_INVARIANT", Confidence::ObservedInvariant},
    {"PREDICTIVE", Confidence::Predictive},
    {"HYPOTHESIS", Confidence::Hypothesis},
}};

constexpr std::array<std::pair<std::string_view, SupportLevel>, 4> kSupport{{
    {"SUPPORTED", SupportLevel::Supported},
    {"CONDITIONALLY_SUPPORTED", SupportLevel::ConditionallySupported},
    {"UNKNOWN", SupportLevel::Unknown},
    {"UNSUPPORTED", SupportLevel::Unsupported},
}};

constexpr std::array<std::pair<std::string_view, Severity>, 5> kSeverity{{
    {"critical", Severity::Critical},
    {"high", Severity::High},
    {"medium", Severity::Medium},
    {"low", Severity::Low},
    {"info", Severity::Info},
}};

constexpr std::array<std::pair<std::string_view, Layer>, 9> kLayer{{
    {"application", Layer::Application},
    {"library", Layer::Library},
    {"language_runtime", Layer::LanguageRuntime},
    {"compatibility_layer", Layer::CompatibilityLayer},
    {"syscall_interface", Layer::SystemCallInterface},
    {"operating_system", Layer::OperatingSystem},
    {"kernel", Layer::Kernel},
    {"hardware", Layer::Hardware},
    {"analyzer", Layer::Analyzer},
}};

template <typename Enum, std::size_t N>
std::string_view name_of(
    const std::array<std::pair<std::string_view, Enum>, N>& table, Enum value) {
    for (const auto& [name, v] : table) {
        if (v == value) return name;
    }
    return "unknown";
}

}  // namespace

std::string_view to_string(EvidenceClass c) { return name_of(kEvidence, c); }
std::string_view to_string(Confidence c) { return name_of(kConfidence, c); }
std::string_view to_string(SupportLevel s) { return name_of(kSupport, s); }
std::string_view to_string(Severity s) { return name_of(kSeverity, s); }
std::string_view to_string(Layer l) { return name_of(kLayer, l); }

bool evidence_class_from_string(std::string_view s, EvidenceClass& out) {
    return lookup(kEvidence, s, out);
}
bool confidence_from_string(std::string_view s, Confidence& out) {
    return lookup(kConfidence, s, out);
}
bool support_level_from_string(std::string_view s, SupportLevel& out) {
    return lookup(kSupport, s, out);
}
bool severity_from_string(std::string_view s, Severity& out) {
    return lookup(kSeverity, s, out);
}
bool layer_from_string(std::string_view s, Layer& out) {
    return lookup(kLayer, s, out);
}

EvidenceClass weakest(EvidenceClass a, EvidenceClass b) {
    return static_cast<int>(a) >= static_cast<int>(b) ? a : b;
}

Confidence confidence_ceiling(EvidenceClass weakest_fact) {
    switch (weakest_fact) {
        case EvidenceClass::SpecifiedGuarantee:
        case EvidenceClass::MeasuredCapability:
            return Confidence::Proven;
        case EvidenceClass::ObservedInvariant:
            return Confidence::ObservedInvariant;
        case EvidenceClass::StaticallyInferred:
        case EvidenceClass::BoundedCounterexample:
            return Confidence::Counterexample;
        case EvidenceClass::HeuristicRisk:
            return Confidence::Predictive;
        case EvidenceClass::Unknown:
            return Confidence::Hypothesis;
    }
    return Confidence::Hypothesis;
}

Confidence clamp_confidence(Confidence desired, EvidenceClass weakest_fact) {
    const Confidence ceiling = confidence_ceiling(weakest_fact);
    return static_cast<int>(desired) > static_cast<int>(ceiling) ? desired : ceiling;
}

SupportLevel combine(SupportLevel a, SupportLevel b) {
    return static_cast<int>(a) >= static_cast<int>(b) ? a : b;
}

// ---------------------------------------------------------------------------

json::Value EvidenceStep::to_json() const {
    json::Value v = json::Value::object();
    v["layer"] = std::string(rs::to_string(layer));
    v["evidence"] = std::string(rs::to_string(evidence));
    v["claim"] = claim;
    v["source"] = source;
    if (!detail.empty()) v["detail"] = detail;
    return v;
}

void EvidenceChain::add(Layer layer, EvidenceClass evidence, std::string claim,
                        std::string source, std::string detail) {
    steps_.push_back(EvidenceStep{layer, evidence, std::move(claim),
                                  std::move(source), std::move(detail)});
}

void EvidenceChain::add(EvidenceStep step) { steps_.push_back(std::move(step)); }

EvidenceClass EvidenceChain::weakest_class() const {
    if (steps_.empty()) return EvidenceClass::Unknown;
    EvidenceClass w = EvidenceClass::SpecifiedGuarantee;
    for (const auto& s : steps_) w = weakest(w, s.evidence);
    return w;
}

json::Value EvidenceChain::to_json() const {
    json::Value arr = json::Value::array();
    for (const auto& s : steps_) arr.push_back(s.to_json());
    return arr;
}

}  // namespace rs

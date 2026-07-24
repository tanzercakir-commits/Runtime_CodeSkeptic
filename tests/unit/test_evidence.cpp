// SPDX-License-Identifier: Apache-2.0
//
// These tests guard the project's central promise: RuntimeSkeptic does not
// overclaim. If `confidence_ceiling` or `combine` ever changes so that a
// weakly-supported finding can be labelled PROVEN, or so that an unknown fact
// can be reported as merely conditional, these fail.
#include "runtimeskeptic/core/evidence.hpp"

#include <iterator>
#include <string>

#include "runtimeskeptic/core/fact.hpp"
#include "test_support.hpp"

using namespace rs;

RS_TEST(evidence_class_names_round_trip) {
    const EvidenceClass all[] = {
        EvidenceClass::SpecifiedGuarantee, EvidenceClass::MeasuredCapability,
        EvidenceClass::ObservedInvariant,  EvidenceClass::StaticallyInferred,
        EvidenceClass::BoundedCounterexample, EvidenceClass::HeuristicRisk,
        EvidenceClass::Unknown};
    for (EvidenceClass c : all) {
        EvidenceClass parsed = EvidenceClass::Unknown;
        RS_CHECK(evidence_class_from_string(to_string(c), parsed));
        RS_CHECK(parsed == c);
    }
}

RS_TEST(weakest_picks_the_weaker_class) {
    RS_CHECK(weakest(EvidenceClass::SpecifiedGuarantee,
                     EvidenceClass::HeuristicRisk) == EvidenceClass::HeuristicRisk);
    RS_CHECK(weakest(EvidenceClass::Unknown,
                     EvidenceClass::MeasuredCapability) == EvidenceClass::Unknown);
    RS_CHECK(weakest(EvidenceClass::MeasuredCapability,
                     EvidenceClass::MeasuredCapability) ==
             EvidenceClass::MeasuredCapability);
}

RS_TEST(confidence_ceiling_matches_the_documented_table) {
    RS_CHECK(confidence_ceiling(EvidenceClass::SpecifiedGuarantee) ==
             Confidence::Proven);
    RS_CHECK(confidence_ceiling(EvidenceClass::MeasuredCapability) ==
             Confidence::Proven);
    RS_CHECK(confidence_ceiling(EvidenceClass::ObservedInvariant) ==
             Confidence::ObservedInvariant);
    RS_CHECK(confidence_ceiling(EvidenceClass::StaticallyInferred) ==
             Confidence::Counterexample);
    RS_CHECK(confidence_ceiling(EvidenceClass::BoundedCounterexample) ==
             Confidence::Counterexample);
    RS_CHECK(confidence_ceiling(EvidenceClass::HeuristicRisk) ==
             Confidence::Predictive);
    RS_CHECK(confidence_ceiling(EvidenceClass::Unknown) == Confidence::Hypothesis);
}

RS_TEST(ceiling_is_monotonic_in_the_evidence_order) {
    // The whole point of ordering evidence classes is to decide the ceiling,
    // so weaker evidence must never permit a stronger claim. An earlier
    // version ranked ObservedInvariant above StaticallyInferred and broke
    // this: adding better evidence to a chain could weaken its conclusion,
    // because weakest() would select a class whose ceiling was higher.
    const EvidenceClass ladder[] = {
        EvidenceClass::SpecifiedGuarantee, EvidenceClass::MeasuredCapability,
        EvidenceClass::StaticallyInferred, EvidenceClass::BoundedCounterexample,
        EvidenceClass::ObservedInvariant,  EvidenceClass::HeuristicRisk,
        EvidenceClass::Unknown};

    for (std::size_t i = 0; i + 1 < std::size(ladder); ++i) {
        // The enum values must be in the order this test lists them.
        RS_CHECK_MESSAGE(static_cast<int>(ladder[i]) <
                             static_cast<int>(ladder[i + 1]),
                         std::string("evidence ladder is out of order at ") +
                             std::string(to_string(ladder[i])));
        // And the ceiling must never get stronger as evidence gets weaker.
        RS_CHECK_MESSAGE(
            static_cast<int>(confidence_ceiling(ladder[i])) <=
                static_cast<int>(confidence_ceiling(ladder[i + 1])),
            std::string("weaker evidence permits a stronger claim: ") +
                std::string(to_string(ladder[i + 1])) + " allows " +
                std::string(to_string(confidence_ceiling(ladder[i + 1]))) +
                " while the stronger " + std::string(to_string(ladder[i])) +
                " allows only " +
                std::string(to_string(confidence_ceiling(ladder[i]))));
    }
}

RS_TEST(adding_better_evidence_never_weakens_a_conclusion) {
    // The practical consequence of monotonicity, stated as the property a
    // user would notice.
    EvidenceChain chain;
    chain.add(Layer::Application, EvidenceClass::StaticallyInferred, "a", "s");
    const Confidence before =
        clamp_confidence(Confidence::Proven, chain.weakest_class());

    chain.add(Layer::OperatingSystem, EvidenceClass::MeasuredCapability, "b", "s");
    const Confidence after =
        clamp_confidence(Confidence::Proven, chain.weakest_class());

    RS_CHECK_MESSAGE(static_cast<int>(after) <= static_cast<int>(before),
                     "adding a measured fact weakened the conclusion");
}

RS_TEST(clamping_never_strengthens_a_claim) {
    // Wanting PROVEN does not make it so.
    RS_CHECK(clamp_confidence(Confidence::Proven, EvidenceClass::Unknown) ==
             Confidence::Hypothesis);
    RS_CHECK(clamp_confidence(Confidence::Proven, EvidenceClass::HeuristicRisk) ==
             Confidence::Predictive);
    // A modest claim on strong evidence stays modest; clamping is one-way.
    RS_CHECK(clamp_confidence(Confidence::Hypothesis,
                              EvidenceClass::MeasuredCapability) ==
             Confidence::Hypothesis);
}

RS_TEST(unknown_outranks_conditional_in_the_overall_verdict) {
    // The ordering that matters most: never present an unestablished fact as
    // a known condition.
    RS_CHECK(combine(SupportLevel::ConditionallySupported,
                     SupportLevel::Unknown) == SupportLevel::Unknown);
    RS_CHECK(combine(SupportLevel::Unknown, SupportLevel::Unsupported) ==
             SupportLevel::Unsupported);
    RS_CHECK(combine(SupportLevel::Supported,
                     SupportLevel::ConditionallySupported) ==
             SupportLevel::ConditionallySupported);
    RS_CHECK(combine(SupportLevel::Supported, SupportLevel::Supported) ==
             SupportLevel::Supported);
}

RS_TEST(combine_is_commutative_and_idempotent) {
    const SupportLevel all[] = {
        SupportLevel::Supported, SupportLevel::ConditionallySupported,
        SupportLevel::Unknown, SupportLevel::Unsupported};
    for (SupportLevel a : all) {
        RS_CHECK(combine(a, a) == a);
        for (SupportLevel b : all) {
            RS_CHECK(combine(a, b) == combine(b, a));
        }
    }
}

RS_TEST(evidence_chain_reports_its_weakest_link) {
    EvidenceChain chain;
    RS_CHECK(chain.weakest_class() == EvidenceClass::Unknown);  // empty is unknown

    chain.add(Layer::OperatingSystem, EvidenceClass::MeasuredCapability, "a", "p");
    RS_CHECK(chain.weakest_class() == EvidenceClass::MeasuredCapability);

    chain.add(Layer::Application, EvidenceClass::StaticallyInferred, "b", "p");
    RS_CHECK(chain.weakest_class() == EvidenceClass::StaticallyInferred);

    chain.add(Layer::Kernel, EvidenceClass::SpecifiedGuarantee, "c", "p");
    RS_CHECK(chain.weakest_class() == EvidenceClass::StaticallyInferred);
}

RS_TEST(fact_rejects_a_value_carrying_unknown_evidence) {
    // The combination is meaningless and would create a fact that reports
    // itself unknown while holding data.
    const auto bad = Fact<std::uint64_t>::known(4096, EvidenceClass::Unknown, "x");
    RS_CHECK(!bad.is_known());
    RS_CHECK(bad.evidence() == EvidenceClass::Unknown);
}

RS_TEST(fact_defaults_to_unknown) {
    const Fact<std::uint64_t> f;
    RS_CHECK(!f.is_known());
    RS_CHECK(f.evidence() == EvidenceClass::Unknown);
    RS_CHECK_EQ(f.value_or(99), std::uint64_t{99});
}

RS_TEST(fact_json_round_trips) {
    const auto original = Fact<std::uint64_t>::known(
        16384, EvidenceClass::MeasuredCapability, "probe");
    const json::Value node = original.to_json();
    std::string error;
    const auto restored = fact_from_json<std::uint64_t>(&node, read_uint, error);
    RS_CHECK(error.empty());
    RS_CHECK(restored.is_known());
    RS_CHECK_EQ(restored.value(), std::uint64_t{16384});
    RS_CHECK(restored.evidence() == EvidenceClass::MeasuredCapability);
}

RS_TEST(fact_parsing_requires_an_explicit_evidence_class) {
    auto parsed = json::parse("{\"value\": 4096}");
    RS_CHECK(parsed.ok());
    std::string error;
    const auto f = fact_from_json<std::uint64_t>(&*parsed.value, read_uint, error);
    RS_CHECK(!error.empty());
    RS_CHECK(!f.is_known());
}

RS_TEST(fact_address_serializes_as_hex) {
    const auto f = Fact<Address>::known(Address(0x1000000000ull),
                                        EvidenceClass::MeasuredCapability, "p");
    const json::Value node = f.to_json();
    RS_CHECK_EQ(node.find("value")->as_string(), std::string("0x1000000000"));
}

RS_TEST_MAIN("evidence")

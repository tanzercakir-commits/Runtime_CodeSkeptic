// SPDX-License-Identifier: Apache-2.0
//
// Mutation tests (ROADMAP section 16).
//
// "Tests must prove that meaningful semantic mutations are detected."
//
// Each case takes a baseline that produces a specific verdict, applies one
// semantically meaningful mutation to either the contract or the profile, and
// asserts that the verdict moves. A rule engine that silently ignores a field
// passes ordinary tests and fails these.
#include "runtimeskeptic/vm/analyzer.hpp"

#include <string>

#include "fixtures.hpp"
#include "test_support.hpp"

using namespace rs;
using namespace rs::vm;
using namespace rs::test;

namespace {

// Asserts that a mutation changes the outcome in some observable way.
void expect_mutation_detected(const char* description,
                              const AnalysisResult& before,
                              const AnalysisResult& after) {
    const bool verdict_moved = before.overall != after.overall;
    const bool findings_moved =
        before.findings.size() != after.findings.size() ||
        !(before.to_json() == after.to_json());
    RS_CHECK_MESSAGE(verdict_moved || findings_moved,
                     std::string("mutation not detected: ") + description);
}

}  // namespace

RS_TEST(mutation_remove_exact_address_requirement) {
    const Requirement baseline = exact_mapping_requirement();
    const EnvironmentProfile host = host_with_reserved_band();
    const auto before = analyze(baseline, host);

    Requirement mutated = baseline;
    mutated.request.exact_address_required = false;
    const auto after = analyze(mutated, host);

    expect_mutation_detected("exact_address_required true -> false", before,
                             after);
    RS_CHECK(before.overall == SupportLevel::Unsupported);
}

RS_TEST(mutation_change_page_size) {
    Requirement r = plain_anonymous_mapping();
    r.request.required_page_size = 4096;
    const auto before = analyze(r, permissive_host());

    EnvironmentProfile mutated = permissive_host();
    mutated.vm.page_size = Fact<std::uint64_t>::known(
        16384, EvidenceClass::MeasuredCapability, "mutated");
    const auto after = analyze(r, mutated);

    expect_mutation_detected("host page size 4096 -> 16384", before, after);
    RS_CHECK(before.overall == SupportLevel::Supported);
    RS_CHECK(after.overall == SupportLevel::Unsupported);
}

RS_TEST(mutation_weaken_success_postcondition) {
    // Dropping the identity assumption must change the analysis; if it does
    // not, the assumption is being ignored.
    const Requirement baseline = exact_mapping_requirement();
    const auto before = analyze(baseline, permissive_host());

    Requirement mutated = baseline;
    mutated.request.exact_address_required = false;
    mutated.assumptions.guest_host_identity_required = false;
    const auto after = analyze(mutated, permissive_host());

    expect_mutation_detected("identity postcondition removed", before, after);
}

RS_TEST(mutation_permit_invalid_fallback) {
    const Requirement baseline = exact_mapping_requirement();
    const auto before = analyze(baseline, permissive_host());

    Requirement mutated = baseline;
    mutated.permitted_fallbacks.push_back(FallbackKind::Relocate);
    const auto after = analyze(mutated, permissive_host());

    expect_mutation_detected("relocation added to permitted_fallbacks", before,
                             after);
    RS_CHECK(before.overall == SupportLevel::Supported);
    RS_CHECK(after.overall == SupportLevel::Unsupported);
}

RS_TEST(mutation_invert_protection_transition) {
    Requirement r = plain_anonymous_mapping();
    r.request.write_then_execute = true;
    const auto before = analyze(r, permissive_host());

    EnvironmentProfile mutated = permissive_host();
    mutated.vm.protection.write_then_execute_transition = Fact<bool>::known(
        false, EvidenceClass::MeasuredCapability, "mutated");
    const auto after = analyze(r, mutated);

    expect_mutation_detected("RW->RX transition allowed -> forbidden", before,
                             after);
}

RS_TEST(mutation_remove_translation_capability) {
    Requirement baseline = exact_mapping_requirement();
    baseline.assumptions.translation_layer_available = true;
    const auto before = analyze(baseline, host_with_reserved_band());

    Requirement mutated = baseline;
    mutated.assumptions.translation_layer_available = false;
    const auto after = analyze(mutated, host_with_reserved_band());

    expect_mutation_detected("translation layer removed", before, after);
}

RS_TEST(mutation_downgrade_evidence_downgrades_confidence) {
    // The most important mutation of all: weakening the evidence behind a
    // fact must weaken every finding that rests on it.
    const Requirement r = exact_mapping_requirement();

    EnvironmentProfile strong = host_with_reserved_band();
    const auto before = analyze(r, strong);

    EnvironmentProfile weak = strong;
    for (auto& range : weak.vm.unavailable_ranges) {
        range.evidence = EvidenceClass::HeuristicRisk;
    }
    const auto after = analyze(r, weak);

    const Finding* strong_finding =
        get_finding(before, ids::kExactAddressUnavailable);
    const Finding* weak_finding = get_finding(after, ids::kExactAddressUnavailable);

    RS_CHECK(strong_finding != nullptr);
    RS_CHECK(weak_finding != nullptr);
    if (strong_finding == nullptr || weak_finding == nullptr) return;

    RS_CHECK(strong_finding->confidence == Confidence::Proven);
    RS_CHECK_MESSAGE(weak_finding->confidence != Confidence::Proven,
                     "a heuristic fact produced a PROVEN finding");
    RS_CHECK(weak_finding->confidence == Confidence::Predictive);
}

RS_TEST(mutation_erase_a_fact_moves_verdict_to_unknown_not_supported) {
    const Requirement r = exact_mapping_requirement();
    const auto before = analyze(r, permissive_host());
    RS_CHECK(before.overall == SupportLevel::Supported);

    EnvironmentProfile mutated = permissive_host();
    mutated.vm.available_ranges.clear();  // we no longer know anything here
    const auto after = analyze(r, mutated);

    RS_CHECK_MESSAGE(after.overall == SupportLevel::Unknown,
                     "erasing the supporting observation must not leave the "
                     "verdict at SUPPORTED");
}

RS_TEST(mutation_change_failure_sink_changes_severity) {
    Requirement baseline = exact_mapping_requirement();
    baseline.failure_sink.kind = FailureSinkKind::ErrorReturn;
    const auto before = analyze(baseline, host_with_reserved_band());

    Requirement mutated = baseline;
    mutated.failure_sink.kind = FailureSinkKind::FatalAssert;
    const auto after = analyze(mutated, host_with_reserved_band());

    const Finding* a = get_finding(before, ids::kExactAddressUnavailable);
    const Finding* b = get_finding(after, ids::kExactAddressUnavailable);
    RS_CHECK(a != nullptr && b != nullptr);
    if (a == nullptr || b == nullptr) return;
    RS_CHECK_MESSAGE(a->severity != b->severity,
                     "the failure sink must influence severity");
    RS_CHECK(b->severity == Severity::Critical);
}

RS_TEST(mutation_shift_address_out_of_the_reserved_band) {
    const EnvironmentProfile host = host_with_reserved_band();
    const auto before = analyze(exact_mapping_requirement(0x1000000000ull), host);
    // 0x8000000000 sits above the reserved band but inside user space.
    const auto after = analyze(exact_mapping_requirement(0x8000000000ull), host);

    expect_mutation_detected("base address moved out of the reserved band",
                             before, after);
    RS_CHECK(before.overall == SupportLevel::Unsupported);
    // Nothing was probed at the new address, so the honest answer is UNKNOWN.
    RS_CHECK(after.overall == SupportLevel::Unknown);
}

RS_TEST_MAIN("mutation")

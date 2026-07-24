// SPDX-License-Identifier: Apache-2.0
//
// Rule tests, organized around the seven MVP demonstrations in ROADMAP
// section 14 plus the internal-consistency rules.
#include "runtimeskeptic/vm/analyzer.hpp"

#include "fixtures.hpp"
#include "test_support.hpp"

using namespace rs;
using namespace rs::vm;
using namespace rs::test;

// ---------------------------------------------------------------------------
// Demonstration 1: exact mapping impossible on the selected host.
// This is the ROADMAP's motivating incident, end to end.
// ---------------------------------------------------------------------------
RS_TEST(motivating_incident_is_proven_unsupported) {
    const auto result =
        analyze(exact_mapping_requirement(), host_with_reserved_band());

    RS_CHECK(result.overall == SupportLevel::Unsupported);
    RS_CHECK(has_finding(result, ids::kExactAddressUnavailable));

    const Finding* f = get_finding(result, ids::kExactAddressUnavailable);
    RS_CHECK(f != nullptr);
    if (f == nullptr) return;

    RS_CHECK(f->confidence == Confidence::Proven);
    RS_CHECK(f->severity == Severity::Critical);  // the sink is a fatal assert

    // The evidence chain must cross layers, not just restate the platform.
    bool has_application = false;
    bool has_os = false;
    bool has_compatibility = false;
    for (const auto& step : f->evidence.steps()) {
        if (step.layer == Layer::Application) has_application = true;
        if (step.layer == Layer::OperatingSystem) has_os = true;
        if (step.layer == Layer::CompatibilityLayer) has_compatibility = true;
    }
    RS_CHECK_MESSAGE(has_application, "evidence chain lacks an application claim");
    RS_CHECK_MESSAGE(has_os, "evidence chain lacks a platform fact");
    RS_CHECK_MESSAGE(has_compatibility,
                     "evidence chain lacks the missing-translation-layer step");

    // A proven contradiction must say which superficial fixes are useless.
    RS_CHECK(!f->rejected_fixes.empty());
    RS_CHECK(!f->remediations.empty());
}

RS_TEST(same_request_on_a_permissive_host_is_supported) {
    // The environment is an input (ROADMAP 8.4): the identical requirement
    // must produce a different verdict on a host that can grant the range.
    const auto result = analyze(exact_mapping_requirement(), permissive_host());
    RS_CHECK(result.overall == SupportLevel::Supported);
    RS_CHECK(!has_finding(result, ids::kExactAddressUnavailable));
}

RS_TEST(unknown_host_yields_unknown_not_supported) {
    const auto result = analyze(exact_mapping_requirement(), unknown_host());
    RS_CHECK(result.overall == SupportLevel::Unknown);
    RS_CHECK(has_finding(result, ids::kRequiredFactUnknown));

    // And it must not be dressed up as proof.
    const Finding* f = get_finding(result, ids::kRequiredFactUnknown);
    RS_CHECK(f != nullptr);
    if (f != nullptr) RS_CHECK(f->confidence == Confidence::Hypothesis);
}

// ---------------------------------------------------------------------------
// Demonstration 2: hinted mapping may relocate.
// ---------------------------------------------------------------------------
RS_TEST(hinted_mapping_with_identity_assumption_is_a_counterexample) {
    Requirement r = exact_mapping_requirement();
    r.request.exact_address_required = false;  // a hint, not a demand
    const auto result = analyze(r, permissive_host());

    RS_CHECK(has_finding(result, ids::kHintedMappingMayRelocate));
    const Finding* f = get_finding(result, ids::kHintedMappingMayRelocate);
    RS_CHECK(f != nullptr);
    if (f == nullptr) return;
    // Not PROVEN: relocation is permitted, not guaranteed.
    RS_CHECK(f->confidence == Confidence::Counterexample);
    RS_CHECK(result.overall == SupportLevel::Unsupported);
}

RS_TEST(hinted_mapping_without_identity_assumption_is_silent) {
    Requirement r = plain_anonymous_mapping();
    r.request.address = 0x1000000000ull;
    const auto result = analyze(r, permissive_host());
    RS_CHECK(!has_finding(result, ids::kHintedMappingMayRelocate));
}

// ---------------------------------------------------------------------------
// Demonstration 3: page-size mismatch.
// ---------------------------------------------------------------------------
RS_TEST(page_size_mismatch_is_proven) {
    Requirement r = plain_anonymous_mapping();
    r.request.required_page_size = 16384;  // host fixture uses 4096
    const auto result = analyze(r, permissive_host());

    RS_CHECK(has_finding(result, ids::kPageSizeMismatch));
    RS_CHECK(result.overall == SupportLevel::Unsupported);
    const Finding* f = get_finding(result, ids::kPageSizeMismatch);
    if (f != nullptr) RS_CHECK(f->confidence == Confidence::Proven);
}

RS_TEST(matching_page_size_is_silent) {
    Requirement r = plain_anonymous_mapping();
    r.request.required_page_size = 4096;
    const auto result = analyze(r, permissive_host());
    RS_CHECK(!has_finding(result, ids::kPageSizeMismatch));
}

// ---------------------------------------------------------------------------
// Demonstration 4: W^X and executable memory.
// ---------------------------------------------------------------------------
RS_TEST(simultaneous_write_execute_on_a_wx_host_is_unsupported) {
    EnvironmentProfile p = permissive_host();
    p.vm.protection.write_execute_simultaneous = Fact<bool>::known(
        false, EvidenceClass::MeasuredCapability, "fixture: W^X enforced");

    Requirement r = plain_anonymous_mapping();
    r.request.protection.execute = true;  // rwx
    r.request.simultaneous_write_execute = true;

    const auto result = analyze(r, p);
    RS_CHECK(has_finding(result, ids::kWriteExecuteRestricted));
    RS_CHECK(result.overall == SupportLevel::Unsupported);

    const Finding* f = get_finding(result, ids::kWriteExecuteRestricted);
    RS_CHECK(f != nullptr);
    if (f == nullptr) return;
    // The report must not recommend disabling a security control.
    bool rejects_disabling = false;
    for (const auto& rejected : f->rejected_fixes) {
        if (rejected.proposal.find("Disable") != std::string::npos) {
            rejects_disabling = true;
        }
    }
    RS_CHECK(rejects_disabling);
}

RS_TEST(write_then_execute_is_accepted_when_the_flip_is_allowed) {
    Requirement r = plain_anonymous_mapping();
    r.request.write_then_execute = true;
    const auto result = analyze(r, permissive_host());
    RS_CHECK(!has_finding(result, ids::kWriteExecuteRestricted));
}

RS_TEST(jit_entitlement_is_reported_as_conditional) {
    EnvironmentProfile p = permissive_host();
    p.vm.protection.jit_entitlement_required = Fact<bool>::known(
        true, EvidenceClass::SpecifiedGuarantee, "fixture");

    Requirement r = plain_anonymous_mapping();
    r.request.write_then_execute = true;
    const auto result = analyze(r, p);

    RS_CHECK(has_finding(result, ids::kJitEntitlementRequired));
    RS_CHECK(result.overall == SupportLevel::ConditionallySupported);
}

RS_TEST(executable_mapping_unsupported_is_proven) {
    EnvironmentProfile p = permissive_host();
    p.vm.protection.anonymous_executable_mapping = Fact<bool>::known(
        false, EvidenceClass::MeasuredCapability, "fixture");

    Requirement r = plain_anonymous_mapping();
    r.request.protection.execute = true;
    r.request.protection.write = false;

    const auto result = analyze(r, p);
    RS_CHECK(has_finding(result, ids::kExecutableMappingUnsupported));
}

// ---------------------------------------------------------------------------
// Demonstration 5: reserve/commit mismatch.
// ---------------------------------------------------------------------------
RS_TEST(reserve_commit_on_a_lazy_host_is_conditional) {
    Requirement r = plain_anonymous_mapping();
    r.request.reserve_then_commit = true;
    const auto result = analyze(r, permissive_host());

    RS_CHECK(has_finding(result, ids::kReserveCommitSemanticMismatch));
    RS_CHECK(result.overall == SupportLevel::ConditionallySupported);
}

// ---------------------------------------------------------------------------
// Demonstration 6: a valid host operation rejected by the caller assumption.
// ---------------------------------------------------------------------------
RS_TEST(alignment_mismatch_is_proven_from_host_granularity) {
    EnvironmentProfile p = permissive_host();
    p.vm.allocation_granularity = Fact<std::uint64_t>::known(
        65536, EvidenceClass::MeasuredCapability, "fixture: 64 KiB granularity");

    // 0x1000000000 is 64 KiB aligned; add one page to break it.
    Requirement r = exact_mapping_requirement(0x1000001000ull);
    const auto result = analyze(r, p);

    RS_CHECK(has_finding(result, ids::kAddressAlignmentMismatch));
    RS_CHECK(result.overall == SupportLevel::Unsupported);
}

RS_TEST(size_granularity_mismatch_is_conditional_not_fatal) {
    EnvironmentProfile p = permissive_host();
    p.vm.allocation_granularity = Fact<std::uint64_t>::known(
        65536, EvidenceClass::MeasuredCapability, "fixture");

    Requirement r = plain_anonymous_mapping();
    r.request.size = 4096;  // not a multiple of 64 KiB
    const auto result = analyze(r, p);

    RS_CHECK(has_finding(result, ids::kSizeGranularityMismatch));
    const Finding* f = get_finding(result, ids::kSizeGranularityMismatch);
    if (f != nullptr) {
        RS_CHECK(f->support_impact == SupportLevel::ConditionallySupported);
    }
}

// ---------------------------------------------------------------------------
// Demonstration 7: an invalid fallback reported as success.
// ---------------------------------------------------------------------------
RS_TEST(relocation_fallback_contradicts_identity_requirement) {
    Requirement r = exact_mapping_requirement();
    r.permitted_fallbacks.push_back(FallbackKind::Relocate);

    // Deliberately analyzed against a host that CAN satisfy the address: the
    // contradiction is internal to the requirement and must be found anyway.
    const auto result = analyze(r, permissive_host());

    RS_CHECK(has_finding(result, ids::kInvalidFallbackContract));
    RS_CHECK(result.overall == SupportLevel::Unsupported);

    const Finding* f = get_finding(result, ids::kInvalidFallbackContract);
    RS_CHECK(f != nullptr);
    if (f != nullptr) RS_CHECK(f->confidence == Confidence::Proven);
}

// ---------------------------------------------------------------------------
// Bounds and widths.
// ---------------------------------------------------------------------------
RS_TEST(address_below_minimum_is_named_precisely) {
    const auto result =
        analyze(exact_mapping_requirement(0x1000ull), permissive_host());
    RS_CHECK(has_finding(result, ids::kAddressBelowMinimum));
    RS_CHECK(!has_finding(result, ids::kExactAddressUnavailable));
}

RS_TEST(address_above_user_space_is_named_precisely) {
    const auto result = analyze(exact_mapping_requirement(0x800000000000ull),
                                permissive_host());
    RS_CHECK(has_finding(result, ids::kAddressAboveUserSpace));
}

RS_TEST(pointer_truncation_is_detected) {
    Requirement r = exact_mapping_requirement(0x1000000000ull);
    r.assumptions.pointer_storage_width_bits = 32;
    const auto result = analyze(r, permissive_host());
    RS_CHECK(has_finding(result, ids::kPointerTruncation));
}

RS_TEST(pointer_truncation_is_silent_when_the_address_fits) {
    Requirement r = exact_mapping_requirement(0x10000ull);
    r.assumptions.pointer_storage_width_bits = 32;
    const auto result = analyze(r, permissive_host());
    RS_CHECK(!has_finding(result, ids::kPointerTruncation));
}

// ---------------------------------------------------------------------------
// Error-code laundering.
// ---------------------------------------------------------------------------
RS_TEST(retrying_a_proven_impossibility_is_reported) {
    Requirement r = exact_mapping_requirement();
    r.assumptions.retries_on_failure = true;
    const auto result = analyze(r, host_with_reserved_band());

    RS_CHECK(has_finding(result, ids::kPermanentErrorRetried));
}

RS_TEST(retry_rule_stays_silent_when_nothing_was_proven_impossible) {
    Requirement r = plain_anonymous_mapping();
    r.assumptions.retries_on_failure = true;
    const auto result = analyze(r, permissive_host());
    RS_CHECK(!has_finding(result, ids::kPermanentErrorRetried));
}

RS_TEST(retry_finding_inherits_the_evidence_of_what_it_reasons_about) {
    // The retry rule knows nothing first-hand. If the impossibility was only
    // statically inferred, the retry finding must not claim PROVEN either.
    Requirement r = exact_mapping_requirement();
    r.assumptions.retries_on_failure = true;
    r.assumption_evidence = EvidenceClass::StaticallyInferred;

    EnvironmentProfile p = host_with_reserved_band();
    for (auto& range : p.vm.unavailable_ranges) {
        range.evidence = EvidenceClass::StaticallyInferred;
    }

    const auto result = analyze(r, p);
    const Finding* f = get_finding(result, ids::kPermanentErrorRetried);
    if (f != nullptr) {
        RS_CHECK(f->confidence != Confidence::Proven);
    }
}

// ---------------------------------------------------------------------------
// Beyond end of file.
// ---------------------------------------------------------------------------
RS_TEST(mapping_past_eof_without_access_is_silent) {
    Requirement r = plain_anonymous_mapping();
    r.request.file_backed = true;
    r.request.file_length = 4096;
    r.request.size = 8192;
    r.request.accesses_beyond_eof = false;
    const auto result = analyze(r, permissive_host());
    RS_CHECK(!has_finding(result, ids::kFileMappingBeyondEof));
}

RS_TEST(accessing_past_eof_on_a_sigbus_host_is_unsupported) {
    Requirement r = plain_anonymous_mapping();
    r.request.file_backed = true;
    r.request.file_length = 4096;
    r.request.size = 8192;
    r.request.accesses_beyond_eof = true;
    const auto result = analyze(r, permissive_host());
    RS_CHECK(has_finding(result, ids::kFileMappingBeyondEof));
    RS_CHECK(result.overall == SupportLevel::Unsupported);
}

// ---------------------------------------------------------------------------
// Determinism and structure of the result.
// ---------------------------------------------------------------------------
RS_TEST(analysis_is_deterministic) {
    const Requirement r = exact_mapping_requirement();
    const EnvironmentProfile p = host_with_reserved_band();
    const auto a = analyze(r, p);
    const auto b = analyze(r, p);
    RS_CHECK(a.to_json() == b.to_json());
}

RS_TEST(every_non_supported_result_carries_a_reason) {
    // Phase 2 exit criterion.
    const auto result =
        analyze(exact_mapping_requirement(), host_with_reserved_band());
    RS_CHECK(result.overall != SupportLevel::Supported);
    RS_CHECK(!result.findings.empty());
    for (const auto& f : result.findings) {
        RS_CHECK_MESSAGE(!f.conclusion.empty(), f.id + " has no conclusion");
        RS_CHECK_MESSAGE(!f.evidence.empty(), f.id + " has no evidence chain");
        RS_CHECK_MESSAGE(!f.required.empty(), f.id + " does not state the "
                                                     "requirement");
    }
}

RS_TEST(every_emitted_finding_id_is_in_the_registry) {
    const EnvironmentProfile hosts[] = {permissive_host(),
                                        host_with_reserved_band(), unknown_host()};
    Requirement wild = exact_mapping_requirement();
    wild.request.write_then_execute = true;
    wild.request.reserve_then_commit = true;
    wild.request.required_page_size = 16384;
    wild.assumptions.retries_on_failure = true;
    wild.assumptions.pointer_storage_width_bits = 32;

    for (const auto& host : hosts) {
        const auto result = analyze(wild, host);
        for (const auto& f : result.findings) {
            RS_CHECK_MESSAGE(find_definition(f.id) != nullptr,
                             "unregistered finding id: " + f.id);
        }
    }
}

RS_TEST(registry_titles_are_stable_per_id) {
    // A finding id must always render the same title, or CI dashboards and
    // suppression lists drift apart from the tool.
    const EnvironmentProfile hosts[] = {permissive_host(),
                                        host_with_reserved_band(), unknown_host()};
    Requirement wild = exact_mapping_requirement();
    wild.request.required_page_size = 16384;

    for (const auto& host : hosts) {
        const auto result = analyze(wild, host);
        for (const auto& f : result.findings) {
            const FindingDefinition* def = find_definition(f.id);
            if (def == nullptr) continue;
            RS_CHECK_MESSAGE(f.title == def->title,
                             f.id + " rendered a title that differs from the "
                                    "registry: '" + f.title + "'");
        }
    }
}

RS_TEST(fixture_origin_is_flagged_as_a_limitation) {
    EnvironmentProfile p = host_with_reserved_band();
    p.origin = ProfileOrigin::HandAuthoredFixture;
    const auto result = analyze(exact_mapping_requirement(), p);
    RS_CHECK(!result.analyzer_limitations.empty());
}

RS_TEST(unknown_reporting_can_be_suppressed_without_changing_the_verdict) {
    AnalysisOptions quiet;
    quiet.report_unknowns = false;
    const auto loud = analyze(exact_mapping_requirement(), unknown_host());
    const auto silent = analyze(exact_mapping_requirement(), unknown_host(), quiet);

    RS_CHECK(loud.overall == SupportLevel::Unknown);
    RS_CHECK_MESSAGE(silent.overall == SupportLevel::Unknown,
                     "suppressing the explanation must not change the verdict");
    RS_CHECK(silent.findings.size() < loud.findings.size());
}

RS_TEST_MAIN("analyzer")

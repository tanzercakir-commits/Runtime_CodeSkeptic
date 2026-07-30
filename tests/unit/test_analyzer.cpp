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

// T-019. This test used to expect CONDITIONALLY_SUPPORTED for every caller,
// and the false-positive campaign measured what that cost: 42% of all real
// mappings (544 of 1292) pass unrounded sizes, because that is how mmap is
// called, and every one came back conditional. The rule's precondition -
// "relies on the bytes past its requested size being unmapped" - is now a
// declarable fact, on the precedent of `accesses_beyond_eof`.
RS_TEST(size_granularity_rounding_without_declared_reliance_is_information) {
    EnvironmentProfile p = permissive_host();
    p.vm.allocation_granularity = Fact<std::uint64_t>::known(
        65536, EvidenceClass::MeasuredCapability, "fixture");

    Requirement r = plain_anonymous_mapping();
    r.request.size = 4096;  // not a multiple of 64 KiB
    const auto result = analyze(r, p);

    // Still emitted - the campaign's own analysis rejected deleting the fact -
    // but as information on a SUPPORTED verdict, not as a condition.
    RS_CHECK(has_finding(result, ids::kSizeGranularityMismatch));
    const Finding* f = get_finding(result, ids::kSizeGranularityMismatch);
    if (f != nullptr) {
        RS_CHECK(f->support_impact == SupportLevel::Supported);
        RS_CHECK(f->severity == Severity::Info);
    }
    RS_CHECK_MESSAGE(result.overall == SupportLevel::Supported,
                     "an undeclared reliance must not gate the verdict - that "
                     "is the 42% the campaign measured");
}

RS_TEST(size_granularity_rounding_with_declared_reliance_is_unsupported) {
    EnvironmentProfile p = permissive_host();
    p.vm.allocation_granularity = Fact<std::uint64_t>::known(
        65536, EvidenceClass::MeasuredCapability, "fixture");

    Requirement r = plain_anonymous_mapping();
    r.request.size = 4096;
    r.request.relies_on_unmapped_beyond_size = true;  // guard-page scheme

    const auto result = analyze(r, p);

    // The caller stated the guarantee this host cannot give: bytes
    // [4096, 65536) are addressable in EVERY execution. Not conditional -
    // there is no execution in which the reliance holds.
    const Finding* f = get_finding(result, ids::kSizeGranularityMismatch);
    RS_CHECK(f != nullptr);
    if (f != nullptr) {
        RS_CHECK(f->support_impact == SupportLevel::Unsupported);
        RS_CHECK(f->confidence == Confidence::Proven);
        RS_CHECK_MESSAGE(f->severity != Severity::Info,
                         "a declared reliance that cannot hold is a silent "
                         "guarantee violation, not a note");
    }
    RS_CHECK(result.overall == SupportLevel::Unsupported);
}

RS_TEST(an_aligned_size_says_nothing_either_way) {
    EnvironmentProfile p = permissive_host();
    p.vm.allocation_granularity = Fact<std::uint64_t>::known(
        65536, EvidenceClass::MeasuredCapability, "fixture");

    Requirement r = plain_anonymous_mapping();
    r.request.size = 131072;  // 2 x 64 KiB
    r.request.relies_on_unmapped_beyond_size = true;

    // The reliance is declared AND satisfiable: the reservation ends exactly
    // where the program thinks it does, so the rule must stay silent rather
    // than reward the declaration with a finding.
    const auto result = analyze(r, p);
    RS_CHECK(!has_finding(result, ids::kSizeGranularityMismatch));
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
// Baseline capability. Regression tests for a real defect: a request that did
// not care where its mapping landed came out SUPPORTED, exit code 0, against
// a profile that had established nothing at all. No rule objected, and the
// absence of objections was being reported as support.
// ---------------------------------------------------------------------------
RS_TEST(a_profile_that_knows_nothing_can_never_yield_supported) {
    Requirement r = plain_anonymous_mapping();  // no address, no exotic needs
    const auto result = analyze(r, unknown_host());

    RS_CHECK_MESSAGE(result.overall != SupportLevel::Supported,
                     "an empty profile produced a SUPPORTED verdict");
    RS_CHECK(result.overall == SupportLevel::Unknown);
    RS_CHECK(has_finding(result, ids::kRequiredFactUnknown));
}

RS_TEST(suppressing_unknown_findings_still_cannot_yield_supported) {
    // The quiet path had its own copy of the verdict logic; make sure it did
    // not keep the old behaviour.
    AnalysisOptions quiet;
    quiet.report_unknowns = false;
    const auto result = analyze(plain_anonymous_mapping(), unknown_host(), quiet);
    RS_CHECK(result.overall == SupportLevel::Unknown);
}

RS_TEST(a_host_that_cannot_map_at_all_is_proven_unsupported) {
    EnvironmentProfile p = permissive_host();
    p.vm.anonymous_mapping_supported = Fact<bool>::known(
        false, EvidenceClass::MeasuredCapability, "fixture: mmap refused");

    const auto result = analyze(plain_anonymous_mapping(), p);
    RS_CHECK(has_finding(result, ids::kAnonymousMappingUnavailable));
    RS_CHECK(result.overall == SupportLevel::Unsupported);
}

RS_TEST(a_wasted_hint_is_reported_without_failing_the_request) {
    // The address is a hint, the caller tolerates relocation, and the hint
    // points into an unavailable range. The mapping still succeeds - but the
    // hint is silently useless, which is worth one quiet line.
    Requirement r = plain_anonymous_mapping();
    r.request.address = 0x2000000000ull;  // inside the reserved band
    const auto result = analyze(r, host_with_reserved_band());

    RS_CHECK(has_finding(result, ids::kAddressHintNotHonourable));
    const Finding* f = get_finding(result, ids::kAddressHintNotHonourable);
    if (f != nullptr) {
        RS_CHECK(f->severity == Severity::Low);
        RS_CHECK(f->support_impact == SupportLevel::Supported);
    }
    RS_CHECK_MESSAGE(result.overall != SupportLevel::Unsupported,
                     "a tolerable hint miss must not fail the request");
}

RS_TEST(a_hint_that_the_host_can_honour_is_silent) {
    Requirement r = plain_anonymous_mapping();
    r.request.address = 0x1000000000ull;  // inside the available range
    const auto result = analyze(r, permissive_host());
    RS_CHECK(!has_finding(result, ids::kAddressHintNotHonourable));
}

// ---------------------------------------------------------------------------
// Defects found by running real projects through the analyzer.
//
// Each of these is a bug that shipped and was caught by a campaign over QEMU,
// Box64, LuaJIT, mimalloc, jemalloc and V8 - not by an author imagining what
// might go wrong. The test names say what was wrong.
// ---------------------------------------------------------------------------

RS_TEST(a_reservation_larger_than_the_address_space_is_refused) {
    // QEMU's aarch64 user mode reserves MAX_RESERVED_VA = (1<<52)-1. Every
    // placement rule began with "if no address, return", and nothing compared
    // the SIZE against anything, so four petabytes came back SUPPORTED with
    // no findings at all.
    Requirement r = plain_anonymous_mapping();
    r.operation = OperationKind::VirtualMemoryReserve;
    r.request.size = std::uint64_t{1} << 52;

    const auto result = analyze(r, permissive_host());
    RS_CHECK_MESSAGE(result.overall == SupportLevel::Unsupported,
                     "a 4 PiB reservation was not refused");
    RS_CHECK(has_finding(result, ids::kSizeExceedsAddressSpace));

    const Finding* f = get_finding(result, ids::kSizeExceedsAddressSpace);
    if (f != nullptr) {
        RS_CHECK(f->confidence == Confidence::Proven);
        RS_CHECK(f->structural_impossibility);
    }
}

RS_TEST(a_reservation_that_fits_is_not_refused) {
    Requirement r = plain_anonymous_mapping();
    r.request.size = std::uint64_t{1} << 30;  // 1 GiB, fits comfortably
    const auto result = analyze(r, permissive_host());
    RS_CHECK(!has_finding(result, ids::kSizeExceedsAddressSpace));
}

// ---------------------------------------------------------------------------
// Fitting the address space is NECESSARY, NOT SUFFICIENT.
//
// A 5-level-paging CI runner proved it in one line. On a 4-level host QEMU's
// 4 PiB request does not fit, the verdict is UNSUPPORTED, the kernel refuses, and
// the prediction holds FOR THE WRONG REASON. On a 56-bit host it fits, the verdict
// became SUPPORTED, and the kernel refused anyway:
//
//   oversized-reservation-4pib   SUPPORTED   refused   CONTRADICTED
//       mmap of 4503599627370496 bytes (4096.0 TiB) refused: ENOMEM
// ---------------------------------------------------------------------------
namespace {

// A host with 56-bit user space - so 4 PiB FITS - that was measured to grant no
// more than 64 TiB in one reservation.
vm::EnvironmentProfile five_level_paging_host() {
    vm::EnvironmentProfile p = permissive_host();
    p.profile_name = "synthetic-la57";
    p.vm.max_user_address = Fact<Address>::known(
        Address(0xfffffffffff000ull), EvidenceClass::MeasuredCapability,
        "synthetic: a host with 5-level paging");
    p.vm.max_single_reservation = Fact<std::uint64_t>::known(
        std::uint64_t{1} << 46, EvidenceClass::MeasuredCapability,
        "synthetic: largest power-of-two reservation granted");
    return p;
}

}  // namespace

RS_TEST(a_4pib_reservation_that_fits_is_still_refused_when_the_host_will_not_grant_it) {
    Requirement r = plain_anonymous_mapping();
    r.operation = OperationKind::VirtualMemoryReserve;
    r.request.size = std::uint64_t{1} << 52;

    const auto result = analyze(r, five_level_paging_host());
    RS_CHECK_MESSAGE(result.overall == SupportLevel::Unsupported,
                     "4 PiB fits below a 56-bit max_user_address, and this host "
                     "was measured to grant 64 TiB. Answering SUPPORTED is the "
                     "false positive an LA57 runner caught");
    // RS-VM-0021 must NOT fire: it does fit.
    RS_CHECK(!has_finding(result, ids::kSizeExceedsAddressSpace));
    RS_CHECK(has_finding(result, ids::kSizeExceedsGrantedReservation));

    const Finding* f = get_finding(result, ids::kSizeExceedsGrantedReservation);
    if (f != nullptr) {
        // 4 PiB is above 2 x 64 TiB, so a reservation of 128 TiB was measured to
        // FAIL on this host and this is larger still. That is proof, not a guess.
        RS_CHECK(f->confidence == Confidence::Proven);
    }
}

RS_TEST(a_size_between_the_largest_success_and_the_smallest_failure_is_conditional) {
    // 96 TiB: above the 64 TiB that succeeded, below the 128 TiB that failed.
    // Nothing measured this size, and saying either UNSUPPORTED or SUPPORTED
    // would be a guess dressed as an answer.
    Requirement r = plain_anonymous_mapping();
    r.operation = OperationKind::VirtualMemoryReserve;
    r.request.size = (std::uint64_t{1} << 46) + (std::uint64_t{1} << 45);

    const auto result = analyze(r, five_level_paging_host());
    RS_CHECK(has_finding(result, ids::kSizeExceedsGrantedReservation));
    const Finding* f = get_finding(result, ids::kSizeExceedsGrantedReservation);
    if (f != nullptr) {
        RS_CHECK_MESSAGE(f->support_impact == SupportLevel::ConditionallySupported,
                         "an unmeasured size was reported as if it had been "
                         "measured");
        RS_CHECK(f->confidence == Confidence::Hypothesis);
    }
}

RS_TEST(a_reservation_within_what_the_host_granted_is_not_flagged) {
    Requirement r = plain_anonymous_mapping();
    r.operation = OperationKind::VirtualMemoryReserve;
    r.request.size = std::uint64_t{1} << 40;   // 1 TiB, well under 64
    const auto result = analyze(r, five_level_paging_host());
    RS_CHECK(!has_finding(result, ids::kSizeExceedsGrantedReservation));
    RS_CHECK(!has_finding(result, ids::kReservationGrantabilityUnknown));
}

RS_TEST(without_the_measurement_a_huge_reservation_is_unknown_not_supported) {
    // The other half of the defect: silence used to read as yes. A profile that
    // never measured the largest reservation must not license a positive answer
    // for a size no real program in this project's corpus has ever requested
    // (the largest observed was 1.96 GiB across 1292 observations).
    vm::EnvironmentProfile p = five_level_paging_host();
    p.vm.max_single_reservation = Fact<std::uint64_t>{};   // unknown

    Requirement r = plain_anonymous_mapping();
    r.operation = OperationKind::VirtualMemoryReserve;
    r.request.size = std::uint64_t{1} << 52;

    const auto result = analyze(r, p);
    RS_CHECK_MESSAGE(result.overall == SupportLevel::Unknown,
                     "a 4 PiB reservation against a profile that never measured "
                     "reservation grantability came back as something other than "
                     "UNKNOWN");
    RS_CHECK(has_finding(result, ids::kReservationGrantabilityUnknown));

    // And a size within what real programs actually request stays quiet, because
    // otherwise every ordinary mapping on every hand-authored fixture would carry
    // this finding.
    Requirement small = plain_anonymous_mapping();
    small.request.size = std::uint64_t{1} << 20;
    const auto quiet = analyze(small, p);
    RS_CHECK(!has_finding(quiet, ids::kReservationGrantabilityUnknown));
}

RS_TEST(alignment_of_the_reservation_is_checked_without_an_address) {
    // V8's pointer-compression cage is 4 GiB aligned to 4 GiB, placed
    // anywhere. mimalloc needs 32 MiB segment alignment. required_alignment
    // was only consulted when an address was ALSO given, so deleting the
    // field from those contracts changed nothing - the definition of dead
    // code.
    Requirement r = plain_anonymous_mapping();
    r.operation = OperationKind::VirtualMemoryReserve;
    r.request.size = std::uint64_t{4} << 30;
    r.request.required_alignment = std::uint64_t{4} << 30;
    RS_CHECK(!r.request.address.has_value());

    const auto result = analyze(r, permissive_host());
    RS_CHECK(has_finding(result, ids::kReservationAlignmentUnguaranteed));
    RS_CHECK(result.overall == SupportLevel::ConditionallySupported);
}

RS_TEST(alignment_within_the_api_guarantee_is_silent) {
    Requirement r = plain_anonymous_mapping();
    r.request.required_alignment = 4096;  // the host already guarantees this
    const auto result = analyze(r, permissive_host());
    RS_CHECK(!has_finding(result, ids::kReservationAlignmentUnguaranteed));
}

RS_TEST(a_page_size_upper_bound_is_satisfied_by_a_smaller_page) {
    // jemalloc built with --with-lg-page=16 checks `if (os_page > PAGE)`.
    // It demands AT MOST 64 KiB and runs correctly on a 4 KiB kernel. The
    // equality-only rule called that a critical impossibility and advised
    // finding "a host whose page size is 65536".
    Requirement r = plain_anonymous_mapping();
    r.request.required_page_size = 65536;
    r.request.required_page_size_relation = SizeRelation::AtMost;

    const auto result = analyze(r, permissive_host());  // host is 4096
    RS_CHECK_MESSAGE(!has_finding(result, ids::kPageSizeMismatch),
                     "an at-most page-size bound was reported as a mismatch");
    RS_CHECK(result.overall == SupportLevel::Supported);
}

RS_TEST(a_page_size_lower_bound_that_the_host_misses_is_reported) {
    Requirement r = plain_anonymous_mapping();
    r.request.required_page_size = 16384;
    r.request.required_page_size_relation = SizeRelation::AtLeast;
    const auto result = analyze(r, permissive_host());
    RS_CHECK(has_finding(result, ids::kPageSizeMismatch));
}

RS_TEST(the_retry_rule_is_reachable_for_statically_inferred_requirements) {
    // proven_unsupported() tested `confidence == Proven`. Confidence is
    // clamped by evidence, so a statically inferred requirement can never
    // reach Proven - which made this rule dead on exactly the input a static
    // extractor produces. Every document CodeSkeptic emits is in that class.
    Requirement r = exact_mapping_requirement();
    r.assumption_evidence = EvidenceClass::StaticallyInferred;
    r.assumptions.retries_on_failure = true;
    r.assumptions.max_retries = 30;

    const auto result = analyze(r, host_with_reserved_band());
    RS_CHECK_MESSAGE(has_finding(result, ids::kPermanentErrorRetried),
                     "the retry rule did not fire for a statically inferred "
                     "requirement");

    const Finding* f = get_finding(result, ids::kPermanentErrorRetried);
    if (f != nullptr) {
        // Reachable, but still honest about its evidence.
        RS_CHECK(f->confidence != Confidence::Proven);
    }
}

RS_TEST(a_program_that_validates_its_address_is_not_accused_of_truncation) {
    // LuaJIT tests (addr >> 31) == 0 and munmaps anything that fails, thirty
    // times. It never stores an un-vetted address, so "the high bits are
    // discarded and the pointer refers to unrelated memory" describes a
    // failure that cannot occur in that program.
    Requirement r = exact_mapping_requirement(0x1000000000ull);
    r.assumptions.pointer_storage_width_bits = 31;
    r.request.validates_returned_address = true;
    r.assumptions.retries_on_failure = true;
    r.assumptions.max_retries = 30;

    const auto result = analyze(r, permissive_host());
    const Finding* f = get_finding(result, ids::kPointerTruncation);
    RS_CHECK(f != nullptr);
    if (f == nullptr) return;

    RS_CHECK_MESSAGE(f->support_impact == SupportLevel::ConditionallySupported,
                     "a test-and-reject program was reported as impossible");
    RS_CHECK(!f->structural_impossibility);
    RS_CHECK_MESSAGE(
        f->modeled_fallback.find("exhaustion") != std::string::npos,
        "the finding still describes truncation rather than retry exhaustion");
}

RS_TEST(a_program_that_does_not_validate_is_still_accused_of_truncation) {
    Requirement r = exact_mapping_requirement(0x1000000000ull);
    r.assumptions.pointer_storage_width_bits = 31;
    const auto result = analyze(r, permissive_host());
    const Finding* f = get_finding(result, ids::kPointerTruncation);
    RS_CHECK(f != nullptr);
    if (f != nullptr) {
        RS_CHECK(f->support_impact == SupportLevel::Unsupported);
        RS_CHECK(f->structural_impossibility);
    }
}

RS_TEST(a_declared_no_op_failure_sink_caps_severity) {
    // mimalloc's huge-page loop keeps whatever contiguous prefix it got and
    // warns. Reporting that at Critical made a graceful fallback look like an
    // abort.
    Requirement r = plain_anonymous_mapping();
    r.request.required_page_size = 16384;
    r.failure_sink.kind = FailureSinkKind::None;

    const auto result = analyze(r, permissive_host());
    const Finding* f = get_finding(result, ids::kPageSizeMismatch);
    RS_CHECK(f != nullptr);
    if (f != nullptr) {
        RS_CHECK_MESSAGE(f->severity != Severity::Critical,
                         "a declared no-op failure sink still produced a "
                         "critical finding");
    }
}

RS_TEST(an_address_bound_the_host_cannot_reach_is_refused) {
    // LuaJIT below 2^31, Box64's box32 below 2^32, Box64's dynarec above 2^32.
    // Before the field existed, authors reached for identity requirements and
    // the analyzer reported a contradiction it had manufactured itself.
    EnvironmentProfile p = permissive_host();
    p.vm.min_map_address = Fact<Address>::known(
        Address(0x100000000ull), EvidenceClass::MeasuredCapability, "fixture");

    Requirement r = plain_anonymous_mapping();
    r.request.address_max = 0x80000000ull;  // must be below 2 GiB

    const auto result = analyze(r, p);
    RS_CHECK(has_finding(result, ids::kAddressBoundUnsatisfiable));
    RS_CHECK(result.overall == SupportLevel::Unsupported);
}

RS_TEST(an_address_bound_the_host_can_reach_is_not_refused) {
    Requirement r = plain_anonymous_mapping();
    r.request.address_min = 0x1000000000ull;
    r.request.address_max = 0x1010000000ull;  // the fixture's available range
    const auto result = analyze(r, permissive_host());
    RS_CHECK(!has_finding(result, ids::kAddressBoundUnsatisfiable));
}

RS_TEST(an_address_outside_its_own_bound_is_an_internal_contradiction) {
    Requirement r = exact_mapping_requirement(0x1000000000ull);
    r.request.address_max = 0x80000000ull;
    const auto result = analyze(r, permissive_host());
    const Finding* f = get_finding(result, ids::kAddressBoundUnsatisfiable);
    RS_CHECK(f != nullptr);
    if (f != nullptr) RS_CHECK(f->structural_impossibility);
}

RS_TEST(a_displacement_constraint_is_acknowledged_not_ignored) {
    // The campaign's verdict on LuaJIT's machine-code window was not wrong, it
    // was EMPTY: stripped of the +/-2 GiB requirement the request is trivially
    // supportable, and nothing said the hard part had been dropped.
    Requirement r = plain_anonymous_mapping();
    r.request.max_displacement_bytes = std::uint64_t{2} << 30;
    r.request.displacement_reference = "the VM exit handler";

    const auto result = analyze(r, permissive_host());
    RS_CHECK(has_finding(result, ids::kDisplacementConstraintNotEvaluable));
    RS_CHECK_MESSAGE(result.overall == SupportLevel::Unknown,
                     "an unevaluated constraint left the verdict at SUPPORTED");
}

RS_TEST(a_posix_two_step_reservation_is_not_a_windows_commit_model) {
    // QEMU reserves with PROT_NONE and maps over it, checking THAT call. The
    // rule downgraded four campaign contracts on a distinction it could not
    // see.
    Requirement r = plain_anonymous_mapping();
    r.request.reserve_then_commit = true;
    r.request.commit_is_checked_call = true;

    const auto result = analyze(r, permissive_host());
    RS_CHECK(!has_finding(result, ids::kReserveCommitSemanticMismatch));

    Requirement windows_style = r;
    windows_style.request.commit_is_checked_call = false;
    RS_CHECK(has_finding(analyze(windows_style, permissive_host()),
                         ids::kReserveCommitSemanticMismatch));
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

// ---------------------------------------------------------------------------
// T-020: the two rules that had coverage of NO kind.
//
// Splitting `groundtruth_coverage.py` into four buckets on 2026-07-30 made
// these visible for the first time - they had been hiding inside "13 of the 20
// reachable" while the tool itself said 9. Neither had ever been executed
// against a kernel OR named by a test. A rule nothing has ever run is a claim,
// and this repository's whole method is that claims get graded.
//
// Both are graded synthetically, and that is the honest instrument here, for
// opposite reasons:
//
//   RS-VM-0016 cannot be reached by measurement on any host this project has.
//              Linux >= 4.17 has MAP_FIXED_NOREPLACE, macOS has a
//              non-destructive form of VM_FLAGS_FIXED, Windows has
//              VirtualAlloc2 - every runner reports the primitive present. A
//              synthetic profile is not a weaker test here; it is the only one.
//
//   RS-VM-0025 is PREDICTIVE per ROADMAP section 11: a statement about how a
//              host behaves as it fragments, not about what was measured. A
//              measurement cannot confirm it by construction.
// ---------------------------------------------------------------------------

RS_TEST(exact_placement_that_exists_only_destructively_is_reported) {
    // The host CAN place at an exact address; it has no way to do so without
    // overwriting whatever is already there. Success then carries no
    // information, and the caller reads it as "the range was free".
    EnvironmentProfile p = permissive_host();
    p.vm.fixed_noreplace_available = Fact<bool>::known(
        false, EvidenceClass::MeasuredCapability, "fixture: pre-4.17 kernel");

    const auto result = analyze(exact_mapping_requirement(), p);

    const Finding* f =
        get_finding(result, ids::kExactMappingNonDestructiveUnavailable);
    RS_CHECK(f != nullptr);
    if (f != nullptr) {
        RS_CHECK(f->confidence == Confidence::Proven);
        RS_CHECK_MESSAGE(f->support_impact == SupportLevel::ConditionallySupported,
                         "the operation is available, so this is not "
                         "UNSUPPORTED - it is supported at a cost the caller "
                         "is never told about");
        RS_CHECK_MESSAGE(!f->structural_impossibility,
                         "nothing here is impossible; that is the point");
    }
}

RS_TEST(a_host_that_has_the_primitive_says_nothing) {
    // The negative half. permissive_host() reports the primitive present, as
    // does every runner this project measures - which is precisely why the
    // positive case above can only ever be synthetic.
    const auto result = analyze(exact_mapping_requirement(), permissive_host());
    RS_CHECK(!has_finding(result,
                          ids::kExactMappingNonDestructiveUnavailable));
}

RS_TEST(a_program_confined_to_a_corner_of_the_space_is_warned) {
    // Satisfiable today, and dependent on the allocator continuing to hand out
    // low addresses. The fixture's usable space ends at 0x7ffffffff000 (~128
    // TiB) and its one free range is [0x1000000000, 0x1010000000) - so a
    // ceiling at the top of that range is ~64.25 GiB of a ~128 TiB space.
    Requirement r = plain_anonymous_mapping();
    r.request.address_min = 0x1000000000ull;
    r.request.address_max = 0x1010000000ull;

    const auto result = analyze(r, permissive_host());

    const Finding* f = get_finding(result, ids::kAddressBoundIsTight);
    RS_CHECK(f != nullptr);
    if (f != nullptr) {
        RS_CHECK_MESSAGE(f->confidence == Confidence::Predictive,
                         "a forecast about fragmentation, not a measurement - "
                         "ROADMAP section 11");
        RS_CHECK(f->support_impact == SupportLevel::ConditionallySupported);
    }
    RS_CHECK_MESSAGE(!has_finding(result, ids::kAddressBoundUnsatisfiable),
                     "the bound IS reachable; calling it unsatisfiable would "
                     "be the manufactured contradiction this rule replaced");
}

RS_TEST(a_program_using_a_comfortable_share_is_not_warned) {
    // The threshold is top/4. Warning above it would make the rule fire on
    // nearly every bounded request - the RS-VM-0005 shape (42% of all real
    // mappings) that T-019 exists to decide.
    Requirement r = plain_anonymous_mapping();
    r.request.address_min = 0x1000000000ull;
    r.request.address_max = 0x4000000000000ull;  // ~1 PiB, well above top/4
    const auto result = analyze(r, permissive_host());
    RS_CHECK(!has_finding(result, ids::kAddressBoundIsTight));
}

RS_TEST_MAIN("analyzer")

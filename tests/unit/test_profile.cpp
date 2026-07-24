// SPDX-License-Identifier: Apache-2.0
#include "runtimeskeptic/vm/profile.hpp"

#include <string>

#include "fixtures.hpp"
#include "runtimeskeptic/vm/requirement.hpp"
#include "test_support.hpp"

using namespace rs;
using namespace rs::vm;
using namespace rs::test;

namespace {

std::string canonical_of(const json::Value& v) {
    auto s = json::serialize_canonical(v);
    return s.value_or("<none>");
}

}  // namespace

RS_TEST(profile_round_trips_through_json) {
    const EnvironmentProfile original = permissive_host();
    const json::Value document = original.to_json();

    std::string error;
    auto restored = EnvironmentProfile::from_json(document, error);
    RS_CHECK_MESSAGE(restored.has_value(), error);
    if (!restored) return;

    RS_CHECK_EQ(canonical_of(restored->facts_json()),
                canonical_of(original.facts_json()));
    RS_CHECK_EQ(restored->profile_id(), original.profile_id());
}

RS_TEST(profile_id_ignores_run_metadata) {
    EnvironmentProfile a = permissive_host();
    EnvironmentProfile b = permissive_host();
    b.run.timestamp_utc = "2030-01-01T00:00:00Z";
    b.run.run_id = "deadbeef";
    b.run.duration_ms = 999;
    b.run.warnings.emplace_back("noise");
    RS_CHECK_EQ(a.profile_id(), b.profile_id());
}

RS_TEST(profile_id_ignores_the_human_label) {
    EnvironmentProfile a = permissive_host();
    EnvironmentProfile b = permissive_host();
    b.profile_name = "renamed-for-the-report";
    RS_CHECK_MESSAGE(a.profile_id() == b.profile_id(),
                     "renaming a profile must not change its identity");
}

RS_TEST(profile_id_changes_when_a_fact_changes) {
    EnvironmentProfile a = permissive_host();
    EnvironmentProfile b = permissive_host();
    b.vm.page_size = Fact<std::uint64_t>::known(
        16384, EvidenceClass::MeasuredCapability, "fixture");
    RS_CHECK(a.profile_id() != b.profile_id());
}

RS_TEST(profile_id_changes_when_evidence_changes) {
    // Same value, weaker evidence: a different profile, because it supports
    // different conclusions.
    EnvironmentProfile a = permissive_host();
    EnvironmentProfile b = permissive_host();
    b.vm.page_size =
        Fact<std::uint64_t>::known(4096, EvidenceClass::HeuristicRisk, "fixture");
    RS_CHECK(a.profile_id() != b.profile_id());
}

RS_TEST(profile_requires_an_explicit_origin) {
    json::Value document = permissive_host().to_json();
    document.object_ref().erase("origin");
    std::string error;
    RS_CHECK(!EnvironmentProfile::from_json(document, error).has_value());
    RS_CHECK(error.find("origin") != std::string::npos);
}

RS_TEST(profile_rejects_an_unknown_schema) {
    json::Value document = permissive_host().to_json();
    document["schema"] = std::string("runtime-skeptic.environment-profile.v99");
    std::string error;
    RS_CHECK(!EnvironmentProfile::from_json(document, error).has_value());
}

RS_TEST(profile_rejects_a_range_with_unknown_evidence) {
    // Absence of observation must not be smuggled in as a limitation.
    auto parsed = json::parse(R"({
        "schema": "runtime-skeptic.environment-profile.v1",
        "origin": "measured",
        "platform": {"os": "linux", "process_arch": "x86_64"},
        "virtual_memory": {
            "unavailable_ranges": [
                {"start": "0x1000", "end": "0x2000", "evidence": "unknown"}
            ]
        }
    })");
    RS_CHECK(parsed.ok());
    std::string error;
    RS_CHECK(!EnvironmentProfile::from_json(*parsed.value, error).has_value());
}

RS_TEST(query_range_reports_unknown_for_unprobed_space) {
    const EnvironmentProfile p = permissive_host();
    const RangeVerdict verdict =
        p.query_range(AddressRange{0x500000000000ull, 0x500000001000ull});
    RS_CHECK(verdict.level == SupportLevel::Unknown);
    RS_CHECK(verdict.evidence == EvidenceClass::Unknown);
}

RS_TEST(query_range_requires_full_containment_to_report_support) {
    const EnvironmentProfile p = permissive_host();
    // available_ranges holds [0x1000000000, 0x1010000000).
    RS_CHECK(p.query_range(AddressRange{0x1000000000ull, 0x1000004000ull}).level ==
             SupportLevel::Supported);
    // Straddling the end: partial containment proves nothing about the rest.
    RS_CHECK(p.query_range(AddressRange{0x100ffff000ull, 0x1010001000ull}).level ==
             SupportLevel::Unknown);
}

RS_TEST(query_range_checks_bounds_before_ranges) {
    const EnvironmentProfile p = permissive_host();
    const RangeVerdict low = p.query_range(AddressRange{0x1000, 0x2000});
    RS_CHECK(low.level == SupportLevel::Unsupported);
    RS_CHECK(low.reason.find("below") != std::string::npos);

    const RangeVerdict high =
        p.query_range(AddressRange{0x7ffffffff000ull, 0x800000000000ull});
    RS_CHECK(high.level == SupportLevel::Unsupported);
}

RS_TEST(requirement_round_trips_through_json) {
    const Requirement original = exact_mapping_requirement();
    const json::Value document = original.to_json();
    std::string error;
    auto restored = Requirement::from_json(document, error);
    RS_CHECK_MESSAGE(restored.has_value(), error);
    if (!restored) return;
    RS_CHECK_EQ(canonical_of(restored->to_json()), canonical_of(document));
    RS_CHECK_EQ(restored->requirement_id(), original.requirement_id());
}

RS_TEST(requirement_requires_an_assumption_evidence_class) {
    json::Value document = exact_mapping_requirement().to_json();
    document.object_ref().erase("assumption_evidence");
    std::string error;
    RS_CHECK(!Requirement::from_json(document, error).has_value());
}

RS_TEST(requirement_rejects_exact_address_without_an_address) {
    auto parsed = json::parse(R"({
        "schema": "runtime-skeptic.application-requirements.v1",
        "operation": "virtual_memory_map",
        "assumption_evidence": "specified_guarantee",
        "request": {"size": 4096, "exact_address_required": true}
    })");
    RS_CHECK(parsed.ok());
    std::string error;
    RS_CHECK(!Requirement::from_json(*parsed.value, error).has_value());
}

RS_TEST(requirement_rejects_zero_size) {
    auto parsed = json::parse(R"({
        "schema": "runtime-skeptic.application-requirements.v1",
        "operation": "virtual_memory_map",
        "assumption_evidence": "specified_guarantee",
        "request": {"size": 0}
    })");
    RS_CHECK(parsed.ok());
    std::string error;
    RS_CHECK(!Requirement::from_json(*parsed.value, error).has_value());
}

RS_TEST(requirement_accepts_decimal_or_hex_addresses) {
    auto parsed = json::parse(R"({
        "schema": "runtime-skeptic.application-requirements.v1",
        "operation": "virtual_memory_map",
        "assumption_evidence": "specified_guarantee",
        "request": {"address": 4096, "size": 4096}
    })");
    RS_CHECK(parsed.ok());
    std::string error;
    auto r = Requirement::from_json(*parsed.value, error);
    RS_CHECK_MESSAGE(r.has_value(), error);
    if (r) RS_CHECK_EQ(*r->request.address, std::uint64_t{4096});
}

RS_TEST(unavailable_ranges_are_sorted_for_canonical_output) {
    auto parsed = json::parse(R"({
        "schema": "runtime-skeptic.environment-profile.v1",
        "origin": "measured",
        "platform": {"os": "linux", "process_arch": "x86_64"},
        "virtual_memory": {
            "unavailable_ranges": [
                {"start": "0x9000", "end": "0xa000", "evidence": "measured_capability"},
                {"start": "0x1000", "end": "0x2000", "evidence": "measured_capability"}
            ]
        }
    })");
    RS_CHECK(parsed.ok());
    std::string error;
    auto p = EnvironmentProfile::from_json(*parsed.value, error);
    RS_CHECK_MESSAGE(p.has_value(), error);
    if (!p) return;
    RS_CHECK_EQ(p->vm.unavailable_ranges.front().range.start, std::uint64_t{0x1000});

    // Reordering the input must not change the identity.
    auto reordered = json::parse(R"({
        "schema": "runtime-skeptic.environment-profile.v1",
        "origin": "measured",
        "platform": {"os": "linux", "process_arch": "x86_64"},
        "virtual_memory": {
            "unavailable_ranges": [
                {"start": "0x1000", "end": "0x2000", "evidence": "measured_capability"},
                {"start": "0x9000", "end": "0xa000", "evidence": "measured_capability"}
            ]
        }
    })");
    auto q = EnvironmentProfile::from_json(*reordered.value, error);
    RS_CHECK(q.has_value());
    if (q) RS_CHECK_EQ(p->profile_id(), q->profile_id());
}

RS_TEST_MAIN("profile")

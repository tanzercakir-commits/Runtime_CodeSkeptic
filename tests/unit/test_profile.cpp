// SPDX-License-Identifier: Apache-2.0
#include "runtimeskeptic/vm/profile.hpp"

#include <string>

#include "fixtures.hpp"
#include "runtimeskeptic/vm/analyzer.hpp"
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

// Regression: reading a profile used to substitute the note "declared unknown"
// for an unknown fact that had no note, and profile_id hashes the note. Writing
// a profile and reading it back therefore changed its identity - the same
// document naming two different hosts depending on whether it had made a round
// trip.
//
// It hid for the project's whole life because every fact in every profile
// produced so far was known. The first profile with a genuine hole in it was
// the macOS one, after min_map_address stopped being recorded.
RS_TEST(an_unknown_fact_survives_a_write_read_round_trip) {
    EnvironmentProfile p = rs::test::permissive_host();
    p.vm.min_map_address = Fact<Address>{};  // unknown, and says nothing about why
    const std::string original = p.profile_id();

    auto text = json::serialize_canonical(p.to_json());
    RS_CHECK(text.has_value());
    if (!text) return;
    auto reparsed = json::parse(*text);
    RS_CHECK(reparsed.ok());
    if (!reparsed.ok()) return;

    std::string error;
    auto restored = EnvironmentProfile::from_json(*reparsed.value, error);
    RS_CHECK_MESSAGE(restored.has_value(), error);
    if (!restored) return;
    RS_CHECK_EQ(restored->profile_id(), original);
    // And the reader must not have invented a reason the document never gave.
    RS_CHECK(restored->vm.min_map_address.note().empty());
}

// The other half: a note the document DOES carry has to survive intact.
RS_TEST(a_stated_reason_for_unknown_is_preserved) {
    EnvironmentProfile p = rs::test::permissive_host();
    p.vm.min_map_address = Fact<Address>::unknown("probe declined to guess");

    auto text = json::serialize_canonical(p.to_json());
    RS_CHECK(text.has_value());
    if (!text) return;
    auto reparsed = json::parse(*text);
    std::string error;
    auto restored = EnvironmentProfile::from_json(*reparsed.value, error);
    RS_CHECK(restored.has_value());
    if (!restored) return;
    RS_CHECK_EQ(restored->vm.min_map_address.note(),
                std::string("probe declined to guess"));
    RS_CHECK_EQ(restored->profile_id(), p.profile_id());
}

// Two documents that state the same facts must name the same host, whether an
// unknown fact is written out as null or simply left out.
RS_TEST(an_omitted_fact_and_an_explicit_unknown_are_the_same_profile) {
    const char* omitted = R"({
        "schema": "runtime-skeptic.environment-profile.v1",
        "origin": "measured",
        "platform": {"os": "linux", "process_arch": "x86_64"},
        "virtual_memory": {
            "page_size": {"value": 4096, "evidence": "measured_capability"}
        }
    })";
    const char* explicit_unknown = R"({
        "schema": "runtime-skeptic.environment-profile.v1",
        "origin": "measured",
        "platform": {"os": "linux", "process_arch": "x86_64"},
        "virtual_memory": {
            "page_size": {"value": 4096, "evidence": "measured_capability"},
            "min_map_address": {"value": null, "evidence": "unknown"}
        }
    })";
    auto a = json::parse(omitted);
    auto b = json::parse(explicit_unknown);
    RS_CHECK(a.ok());
    RS_CHECK(b.ok());
    if (!a.ok() || !b.ok()) return;

    std::string error;
    auto pa = EnvironmentProfile::from_json(*a.value, error);
    auto pb = EnvironmentProfile::from_json(*b.value, error);
    RS_CHECK(pa.has_value());
    RS_CHECK(pb.has_value());
    if (!pa || !pb) return;
    RS_CHECK_EQ(pa->profile_id(), pb->profile_id());
}

// A misspelled field used to change a contract's meaning in silence. Write
// `acceses_beyond_eof` and the requirement quietly reads false, the rule that
// would have caught it never runs, and the report is a clean bill of health for
// a claim nobody evaluated - the exact shape of thing this project exists to
// object to.
RS_TEST(unrecognized_requirement_fields_are_reported_not_dropped) {
    auto parsed = json::parse(R"({
        "schema": "runtime-skeptic.application-requirements.v1",
        "operation": "virtual_memory_map",
        "assumption_evidence": "specified_guarantee",
        "request": {
            "size": 4096,
            "acceses_beyond_eof": true,
            "x_provenance": "extensions are exempt by convention"
        },
        "assumptions": { "retries_on_faliure": true }
    })");
    RS_CHECK(parsed.ok());
    std::string error;
    auto r = Requirement::from_json(*parsed.value, error);
    RS_CHECK_MESSAGE(r.has_value(), error);
    if (!r) return;

    bool saw_request_typo = false;
    bool saw_assumption_typo = false;
    bool saw_extension = false;
    for (const auto& f : r->unrecognized_fields) {
        if (f == "request.acceses_beyond_eof") saw_request_typo = true;
        if (f == "assumptions.retries_on_faliure") saw_assumption_typo = true;
        if (f.find("x_provenance") != std::string::npos) saw_extension = true;
    }
    RS_CHECK(saw_request_typo);
    RS_CHECK(saw_assumption_typo);
    // x_ is the extension namespace; this project's own x_campaign and
    // x_groundtruth blocks live there and must not be reported as mistakes.
    RS_CHECK(!saw_extension);
}

RS_TEST(a_correct_requirement_reports_no_unrecognized_fields) {
    const Requirement original = exact_mapping_requirement();
    std::string error;
    auto restored = Requirement::from_json(original.to_json(), error);
    RS_CHECK_MESSAGE(restored.has_value(), error);
    if (!restored) return;
    // A round trip of the tool's own output must be clean, or the known-key
    // list has drifted away from the writer.
    RS_CHECK_MESSAGE(restored->unrecognized_fields.empty(),
                     restored->unrecognized_fields.empty()
                         ? ""
                         : restored->unrecognized_fields.front());
}

// ---------------------------------------------------------------------------
// Independent review 2026-08-02, A2: the profile parser did not match the
// published environment-profile.v1 schema in either direction. It REJECTED
// documents the schema allows (a profile with no virtual_memory; an os or arch
// string the tool does not model) and ACCEPTED documents the schema forbids (a
// non-string profile_name; a fact carrying an extra field or a non-string
// source). The two directions are the real false-green: a profile CI believes
// it verified can be one the tool silently mangled, or one it needlessly threw
// away. These regressions pin both halves shut.
// ---------------------------------------------------------------------------

// Over-strict half. virtual_memory is not a required field in the schema, and
// "absent = unknown" makes a platform-only profile valid: it simply answers
// UNKNOWN to every memory question rather than being refused outright.
RS_TEST(a_profile_without_virtual_memory_is_valid_and_all_unknown) {
    auto parsed = json::parse(R"({
        "schema": "runtime-skeptic.environment-profile.v1",
        "origin": "measured",
        "platform": {"os": "linux", "process_arch": "x86_64"}
    })");
    RS_CHECK(parsed.ok());
    std::string error;
    auto p = EnvironmentProfile::from_json(*parsed.value, error);
    RS_CHECK_MESSAGE(p.has_value(), error);
    if (!p) return;
    RS_CHECK(!p->vm.page_size.is_known());
    RS_CHECK(!p->vm.max_user_address.is_known());
    RS_CHECK(p->vm.available_ranges.empty());
    // And it names the same host as an explicit empty virtual_memory object,
    // because omitting the block says exactly what an empty block says.
    auto with_empty = json::parse(R"({
        "schema": "runtime-skeptic.environment-profile.v1",
        "origin": "measured",
        "platform": {"os": "linux", "process_arch": "x86_64"},
        "virtual_memory": {}
    })");
    auto q = EnvironmentProfile::from_json(*with_empty.value, error);
    RS_CHECK(q.has_value());
    if (q) RS_CHECK_EQ(p->profile_id(), q->profile_id());
}

// Over-strict half. os / host_arch / process_arch are typed `string` in the
// schema, not `enum`: a host the tool does not model specially is still a real
// measurement, so an unrecognized name maps to `other`, not to a rejection.
RS_TEST(an_unmodeled_os_or_arch_maps_to_other_not_rejected) {
    auto parsed = json::parse(R"({
        "schema": "runtime-skeptic.environment-profile.v1",
        "origin": "measured",
        "platform": {"os": "freebsd", "process_arch": "loongarch64"}
    })");
    RS_CHECK(parsed.ok());
    std::string error;
    auto p = EnvironmentProfile::from_json(*parsed.value, error);
    RS_CHECK_MESSAGE(p.has_value(), error);
    if (!p) return;
    RS_CHECK(p->platform.os == OperatingSystem::Other);
    RS_CHECK(p->platform.process_arch == Architecture::Other);
    // `other` is not `unknown`: the field was present, just unmodeled. But an
    // unmodeled arch carries no pointer width, so nothing is fabricated from it.
    RS_CHECK_EQ(p->process_pointer_width(), 0u);
}

RS_TEST(riscv64_is_first_class_and_has_a_64_bit_pointer_width) {
    Architecture arch = Architecture::Unknown;
    RS_CHECK(architecture_from_string("riscv64", arch));
    RS_CHECK(arch == Architecture::Riscv64);
    RS_CHECK_EQ(to_string(arch), std::string_view("riscv64"));
    RS_CHECK_EQ(pointer_width_bits(arch), 64u);

    auto parsed = json::parse(R"({
        "schema": "runtime-skeptic.environment-profile.v1",
        "origin": "measured",
        "platform": {"os": "linux", "host_arch": "riscv64",
                     "process_arch": "riscv64"}
    })");
    RS_CHECK(parsed.ok());
    std::string error;
    auto profile = EnvironmentProfile::from_json(*parsed.value, error);
    RS_CHECK_MESSAGE(profile.has_value(), error);
    if (profile) {
        RS_CHECK(profile->platform.host_arch == Architecture::Riscv64);
        RS_CHECK(profile->platform.process_arch == Architecture::Riscv64);
        RS_CHECK_EQ(profile->process_pointer_width(), 64u);
    }
}

// Under-strict half. The schema types os as a string; a number is not a
// string, and coercing it to "" silently (the old behavior) is a schema
// violation the reader must refuse, not paper over.
RS_TEST(a_non_string_os_is_rejected) {
    auto parsed = json::parse(R"({
        "schema": "runtime-skeptic.environment-profile.v1",
        "origin": "measured",
        "platform": {"os": 123, "process_arch": "x86_64"}
    })");
    RS_CHECK(parsed.ok());
    std::string error;
    RS_CHECK(!EnvironmentProfile::from_json(*parsed.value, error).has_value());
}

// Under-strict half. profile_name is typed `string`; a non-string used to be
// coerced to "(unnamed)" and the document accepted.
RS_TEST(a_non_string_profile_name_is_rejected) {
    auto parsed = json::parse(R"({
        "schema": "runtime-skeptic.environment-profile.v1",
        "origin": "measured",
        "profile_name": 123,
        "platform": {"os": "linux", "process_arch": "x86_64"}
    })");
    RS_CHECK(parsed.ok());
    std::string error;
    RS_CHECK(!EnvironmentProfile::from_json(*parsed.value, error).has_value());
    RS_CHECK(error.find("profile_name") != std::string::npos);
}

// Under-strict half. A fact is additionalProperties:false in the schema. An
// extra key is a misspelling whose data is then dropped in silence - write
// `valeu` and the real value goes unread - so it is a violation, not a fact.
RS_TEST(an_extra_field_in_a_fact_is_rejected) {
    auto parsed = json::parse(R"({
        "schema": "runtime-skeptic.environment-profile.v1",
        "origin": "measured",
        "platform": {"os": "linux", "process_arch": "x86_64"},
        "virtual_memory": {
            "page_size": {"value": 4096, "evidence": "measured_capability",
                          "valeu": 8192}
        }
    })");
    RS_CHECK(parsed.ok());
    std::string error;
    RS_CHECK(!EnvironmentProfile::from_json(*parsed.value, error).has_value());
    RS_CHECK(error.find("valeu") != std::string::npos);
}

// Under-strict half. A fact's source/note are typed `string`; a number was
// coerced silently.
RS_TEST(a_non_string_fact_source_is_rejected) {
    auto parsed = json::parse(R"({
        "schema": "runtime-skeptic.environment-profile.v1",
        "origin": "measured",
        "platform": {"os": "linux", "process_arch": "x86_64"},
        "virtual_memory": {
            "page_size": {"value": 4096, "evidence": "measured_capability",
                          "source": 5}
        }
    })");
    RS_CHECK(parsed.ok());
    std::string error;
    RS_CHECK(!EnvironmentProfile::from_json(*parsed.value, error).has_value());
}

// Under-strict half, ranges. A range is additionalProperties:false too, and a
// misspelled `evidance` slips the real evidence past the reader.
RS_TEST(an_extra_field_in_a_range_is_rejected) {
    auto parsed = json::parse(R"({
        "schema": "runtime-skeptic.environment-profile.v1",
        "origin": "measured",
        "platform": {"os": "linux", "process_arch": "x86_64"},
        "virtual_memory": {
            "unavailable_ranges": [
                {"start": "0x1000", "end": "0x2000",
                 "evidence": "measured_capability", "reigon": "typo"}
            ]
        }
    })");
    RS_CHECK(parsed.ok());
    std::string error;
    RS_CHECK(!EnvironmentProfile::from_json(*parsed.value, error).has_value());
    RS_CHECK(error.find("reigon") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Independent RE-TEST, 2026-08-02: the first round of A1/A2/A5 fixes patched the
// named examples but not the class of bug - the readers still treated a wrong
// TYPE as an absent field. A 300-case boundary matrix (tools/audit/
// boundary_matrix.py) drove these out; the cases below pin the ones a unit test
// can hold, in both parsers. The matrix is the exhaustive check; this is the
// fast one.
// ---------------------------------------------------------------------------

RS_TEST(a_string_typed_numeric_requirement_field_is_rejected_not_dropped) {
    // required_page_size:"16384" (a string) was read as "no page size required",
    // so a 16 KiB-page program passed on a 4 KiB host. A wrong type is a schema
    // violation, not an absent field.
    auto parsed = json::parse(R"({
        "schema": "runtime-skeptic.application-requirements.v1",
        "operation": "virtual_memory_map",
        "assumption_evidence": "specified_guarantee",
        "request": {"size": 65536, "required_page_size": "16384"}
    })");
    RS_CHECK(parsed.ok());
    std::string error;
    RS_CHECK(!Requirement::from_json(*parsed.value, error).has_value());
}

RS_TEST(a_null_boolean_requirement_flag_is_rejected) {
    // The schema types these flags boolean, not nullable; null coerced to false.
    auto parsed = json::parse(R"({
        "schema": "runtime-skeptic.application-requirements.v1",
        "operation": "virtual_memory_map",
        "assumption_evidence": "specified_guarantee",
        "request": {"size": 65536, "simultaneous_write_execute": null}
    })");
    RS_CHECK(parsed.ok());
    std::string error;
    RS_CHECK(!Requirement::from_json(*parsed.value, error).has_value());
}

RS_TEST(a_negative_optional_uint_is_rejected_on_every_field) {
    for (const char* field :
         {"required_alignment", "required_page_size", "file_length",
          "max_displacement_bytes"}) {
        const std::string text = std::string(R"({
            "schema": "runtime-skeptic.application-requirements.v1",
            "operation": "virtual_memory_map",
            "assumption_evidence": "specified_guarantee",
            "request": {"size": 65536, ")") + field + R"(": -1}})";
        auto parsed = json::parse(text);
        RS_CHECK(parsed.ok());
        std::string error;
        RS_CHECK_MESSAGE(!Requirement::from_json(*parsed.value, error).has_value(),
                         std::string("a negative ") + field + " was accepted");
    }
}

RS_TEST(a_mapping_that_wraps_the_address_space_is_unsupported_not_supported) {
    // address + size overflows uint64: the region wraps past the end of the
    // 64-bit space and cannot exist. It was recorded only as an analyzer
    // limitation, leaving the verdict SUPPORTED; it must be a proven UNSUPPORTED.
    auto parsed = json::parse(R"({
        "schema": "runtime-skeptic.application-requirements.v1",
        "operation": "virtual_memory_map",
        "assumption_evidence": "specified_guarantee",
        "request": {"address": "0xffffffffffff0000", "size": 131072,
                    "exact_address_required": true}
    })");
    RS_CHECK(parsed.ok());
    std::string error;
    auto req = Requirement::from_json(*parsed.value, error);
    RS_CHECK_MESSAGE(req.has_value(), error);
    if (!req) return;
    const AnalysisResult result = analyze(*req, permissive_host(), {});
    RS_CHECK(result.overall == SupportLevel::Unsupported);
}

RS_TEST(a_non_string_os_version_is_rejected) {
    auto parsed = json::parse(R"({
        "schema": "runtime-skeptic.environment-profile.v1",
        "origin": "measured",
        "platform": {"os": "linux", "process_arch": "x86_64", "os_version": 12345}
    })");
    RS_CHECK(parsed.ok());
    std::string error;
    RS_CHECK(!EnvironmentProfile::from_json(*parsed.value, error).has_value());
}

RS_TEST(a_fact_without_a_value_key_is_rejected) {
    // The schema requires 'value' on every fact; a value-less object used to
    // read as an honest "unknown" fact, so a truncated profile passed.
    auto parsed = json::parse(R"({
        "schema": "runtime-skeptic.environment-profile.v1",
        "origin": "measured",
        "platform": {"os": "linux", "process_arch": "x86_64"},
        "virtual_memory": {"page_size": {"evidence": "measured_capability"}}
    })");
    RS_CHECK(parsed.ok());
    std::string error;
    RS_CHECK(!EnvironmentProfile::from_json(*parsed.value, error).has_value());
}

RS_TEST(a_range_start_must_be_a_hex_string_not_a_number) {
    auto parsed = json::parse(R"({
        "schema": "runtime-skeptic.environment-profile.v1",
        "origin": "measured",
        "platform": {"os": "linux", "process_arch": "x86_64"},
        "virtual_memory": {"unavailable_ranges": [
            {"start": 4096, "end": "0x2000", "evidence": "measured_capability"}
        ]}
    })");
    RS_CHECK(parsed.ok());
    std::string error;
    RS_CHECK(!EnvironmentProfile::from_json(*parsed.value, error).has_value());
}

// ---------------------------------------------------------------------------
// Independent RE-TEST round 3, 2026-08-02: the accept/reject matrix was clean
// and CI green, but it never checked verdict correctness, the nested/container
// fields, or the bundle's file integrity. These pin the parser half of what the
// third round found.
// ---------------------------------------------------------------------------

RS_TEST(a_zero_page_size_is_rejected_as_impossible) {
    auto parsed = json::parse(R"({
        "schema": "runtime-skeptic.environment-profile.v1",
        "origin": "measured",
        "platform": {"os": "linux", "process_arch": "x86_64"},
        "virtual_memory": {"page_size":
            {"value": 0, "evidence": "measured_capability"}}
    })");
    RS_CHECK(parsed.ok());
    std::string error;
    RS_CHECK(!EnvironmentProfile::from_json(*parsed.value, error).has_value());
}

RS_TEST(a_non_object_protection_is_rejected) {
    // "rwx" made every prot->find(key) return null, so all protection facts read
    // unknown and a JIT's W^X need was silently lost.
    auto parsed = json::parse(R"({
        "schema": "runtime-skeptic.environment-profile.v1",
        "origin": "measured",
        "platform": {"os": "linux", "process_arch": "x86_64"},
        "virtual_memory": {"protection": "rwx"}
    })");
    RS_CHECK(parsed.ok());
    std::string error;
    RS_CHECK(!EnvironmentProfile::from_json(*parsed.value, error).has_value());
}

RS_TEST(a_non_object_source_location_entry_is_rejected) {
    auto parsed = json::parse(R"({
        "schema": "runtime-skeptic.application-requirements.v1",
        "operation": "virtual_memory_map",
        "assumption_evidence": "specified_guarantee",
        "request": {"size": 4096},
        "source_locations": ["notanobject"]
    })");
    RS_CHECK(parsed.ok());
    std::string error;
    RS_CHECK(!Requirement::from_json(*parsed.value, error).has_value());
}

RS_TEST(a_non_string_failure_sink_description_is_rejected) {
    auto parsed = json::parse(R"({
        "schema": "runtime-skeptic.application-requirements.v1",
        "operation": "virtual_memory_map",
        "assumption_evidence": "specified_guarantee",
        "request": {"size": 4096},
        "failure_sink": {"kind": "fatal_assert", "description": 12345}
    })");
    RS_CHECK(parsed.ok());
    std::string error;
    RS_CHECK(!Requirement::from_json(*parsed.value, error).has_value());
}

RS_TEST(a_file_offset_that_overflows_is_unsupported_not_supported) {
    // file_offset+size overflows uint64: the mapping is entirely past a
    // one-byte file. On a host that faults past EOF this is UNSUPPORTED, exactly
    // as file_offset=0 is - it was coming out SUPPORTED via a skipped rule.
    auto parsed = json::parse(R"({
        "schema": "runtime-skeptic.application-requirements.v1",
        "operation": "virtual_memory_map",
        "assumption_evidence": "specified_guarantee",
        "request": {"size": 4096, "file_backed": true, "file_length": 1,
                    "accesses_beyond_eof": true,
                    "file_offset": 18446744073709551615},
        "failure_sink": {"kind": "fatal_assert"}
    })");
    RS_CHECK(parsed.ok());
    std::string error;
    auto req = Requirement::from_json(*parsed.value, error);
    RS_CHECK_MESSAGE(req.has_value(), error);
    if (!req) return;
    auto pp = json::parse(R"({"schema":"runtime-skeptic.environment-profile.v1",
        "origin":"measured","platform":{"os":"linux","process_arch":"x86_64"},
        "virtual_memory":{"file_map_beyond_eof":
            {"value":"sigbus","evidence":"measured_capability"}}})");
    auto prof = EnvironmentProfile::from_json(*pp.value, error);
    RS_CHECK(prof.has_value());
    if (!prof) return;
    RS_CHECK(analyze(*req, *prof, {}).overall == SupportLevel::Unsupported);
}

RS_TEST(a_profile_with_min_map_at_or_above_max_user_is_refused) {
    // 2026-08-02 re-test, verdict group: a profile whose lowest mappable address
    // sits at or above the exclusive upper bound of user space describes an
    // impossible address space, yet it loaded and verified with exit 0. from_json
    // must refuse it, so no verdict is ever derived from contradictory bounds.
    std::string error;
    auto bad = json::parse(R"({"schema":"runtime-skeptic.environment-profile.v1",
        "origin":"measured","platform":{"os":"linux","process_arch":"x86_64"},
        "virtual_memory":{
            "min_map_address":{"value":"0xffff000000000000","evidence":"measured_capability"},
            "max_user_address":{"value":"0x1000","evidence":"measured_capability"}}})");
    RS_CHECK(bad.ok());
    auto prof = EnvironmentProfile::from_json(*bad.value, error);
    RS_CHECK_MESSAGE(!prof.has_value(),
                     "a contradictory address space was accepted");
    RS_CHECK(error.find("max_user_address") != std::string::npos);

    // The boundary case min == max is still empty space, so it is refused too.
    auto edge = json::parse(R"({"schema":"runtime-skeptic.environment-profile.v1",
        "origin":"measured","platform":{"os":"linux","process_arch":"x86_64"},
        "virtual_memory":{
            "min_map_address":{"value":"0x1000","evidence":"measured_capability"},
            "max_user_address":{"value":"0x1000","evidence":"measured_capability"}}})");
    RS_CHECK(!EnvironmentProfile::from_json(*edge.value, error).has_value());

    // A normal ordering (min below max) still loads.
    auto good = json::parse(R"({"schema":"runtime-skeptic.environment-profile.v1",
        "origin":"measured","platform":{"os":"linux","process_arch":"x86_64"},
        "virtual_memory":{
            "min_map_address":{"value":"0x1000","evidence":"measured_capability"},
            "max_user_address":{"value":"0x7ffffffff000","evidence":"measured_capability"}}})");
    RS_CHECK_MESSAGE(EnvironmentProfile::from_json(*good.value, error).has_value(),
                     error);
}

RS_TEST_MAIN("profile")

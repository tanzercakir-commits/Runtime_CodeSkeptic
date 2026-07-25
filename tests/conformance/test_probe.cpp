// SPDX-License-Identifier: Apache-2.0
//
// Probe conformance (ROADMAP Phase 1 exit criteria).
//
// These tests run against the real host. They assert properties the probe must
// hold no matter what the host turns out to be, so they are meaningful on a
// developer laptop and in CI alike.
#include "runtimeskeptic/probe/vm_probe.hpp"

#include <algorithm>
#include <memory>
#include <string>

#include "runtimeskeptic/core/json.hpp"
#include "test_support.hpp"

using namespace rs;
using namespace rs::vm;

namespace {

// Walks the document and returns true if any Fact node carries a non-null
// value while declaring evidence "unknown". That combination is the exact
// dishonesty the type system is meant to prevent.
bool has_value_with_unknown_evidence(const json::Value& v) {
    if (v.is_object()) {
        const json::Value* evidence = v.find("evidence");
        const json::Value* value = v.find("value");
        if (evidence != nullptr && evidence->is_string() && value != nullptr) {
            if (evidence->as_string() == "unknown" && !value->is_null()) return true;
        }
        for (const auto& [key, child] : v.as_object()) {
            (void)key;
            if (has_value_with_unknown_evidence(child)) return true;
        }
        return false;
    }
    if (v.is_array()) {
        for (const auto& child : v.as_array()) {
            if (has_value_with_unknown_evidence(child)) return true;
        }
    }
    return false;
}

}  // namespace

RS_TEST(probe_produces_a_schema_valid_profile_on_every_platform) {
    const probe::Result result = probe::probe_virtual_memory();
    const json::Value document = result.profile.to_json();

    std::string error;
    auto restored = EnvironmentProfile::from_json(document, error);
    RS_CHECK_MESSAGE(restored.has_value(),
                     "the probe emitted a profile it cannot read back: " + error);
}

RS_TEST(probe_never_attaches_a_value_to_unknown_evidence) {
    const probe::Result result = probe::probe_virtual_memory();
    RS_CHECK(!has_value_with_unknown_evidence(result.profile.facts_json()));
}

RS_TEST(unimplemented_platforms_report_synthetic_origin) {
    const probe::Result result = probe::probe_virtual_memory();
    if (result.implemented) return;
    // A stub must never claim to have measured anything.
    RS_CHECK(result.profile.origin != ProfileOrigin::Measured);
    RS_CHECK(!result.profile.run.warnings.empty());
}

RS_TEST(repeated_calls_in_one_process_produce_the_same_profile_id) {
    // Phase 1 exit criterion: "repeated runs on the same stable host produce
    // equivalent canonical profiles". Run metadata differs between calls by
    // construction, so this also proves that profile_id excludes it.
    //
    // WHAT THIS DOES NOT COVER, and once did not admit. Both calls happen
    // inside ONE process, so they share an image base, a __PAGEZERO, and every
    // other thing ASLR fixes at exec time. Cross-process variance is invisible
    // here - and on macOS under Rosetta 2 that was the only kind there was:
    // min_map_address moved ~48 MiB between two runs of the same CI job while
    // this test stayed green, because "repeated runs" had quietly come to mean
    // "repeated calls".
    //
    // tools/campaign/check_reproducible.sh runs the probe as two separate
    // processes and is the test that actually covers the criterion. This one
    // covers determinism within a process, which is a different and weaker
    // claim; it is kept because it is fast and catches ordering bugs.
    const probe::Result a = probe::probe_virtual_memory();
    const probe::Result b = probe::probe_virtual_memory();
    RS_CHECK_EQ(a.profile.profile_id(), b.profile.profile_id());
}

RS_TEST(profile_id_survives_a_write_read_round_trip) {
    // Regression: the probe discovers ranges in experiment order, which is not
    // sorted. Before the serializer sorted them, writing a profile and reading
    // it back produced a DIFFERENT profile_id, which silently defeated the
    // whole point of having one.
    const probe::Result result = probe::probe_virtual_memory();
    const std::string original_id = result.profile.profile_id();

    const json::Value document = result.profile.to_json();
    auto text = json::serialize_canonical(document);
    RS_CHECK(text.has_value());
    if (!text) return;
    auto reparsed = json::parse(*text);
    RS_CHECK(reparsed.ok());
    if (!reparsed.ok()) return;

    std::string error;
    auto restored = EnvironmentProfile::from_json(*reparsed.value, error);
    RS_CHECK_MESSAGE(restored.has_value(), error);
    if (!restored) return;
    RS_CHECK_EQ(restored->profile_id(), original_id);
}

RS_TEST(range_order_does_not_affect_identity) {
    probe::Result result = probe::probe_virtual_memory();
    if (result.profile.vm.available_ranges.size() < 2) return;
    const std::string before = result.profile.profile_id();

    std::reverse(result.profile.vm.available_ranges.begin(),
                 result.profile.vm.available_ranges.end());
    RS_CHECK_MESSAGE(result.profile.profile_id() == before,
                     "the order in which the probe happened to discover ranges "
                     "leaked into the profile identity");
}

RS_TEST(canonical_serialization_is_byte_stable) {
    const probe::Result result = probe::probe_virtual_memory();
    auto first = json::serialize_canonical(result.profile.facts_json());
    RS_CHECK(first.has_value());
    if (!first) return;

    auto reparsed = json::parse(*first);
    RS_CHECK(reparsed.ok());
    if (!reparsed.ok()) return;
    auto second = json::serialize_canonical(*reparsed.value);
    RS_CHECK(second.has_value());
    if (second) RS_CHECK_EQ(*first, *second);
}

RS_TEST(measured_page_size_is_a_power_of_two) {
    const probe::Result result = probe::probe_virtual_memory();
    if (!result.profile.vm.page_size.is_known()) return;
    const std::uint64_t page_size = result.profile.vm.page_size.value();
    RS_CHECK_MESSAGE(is_power_of_two(page_size),
                     "page size is not a power of two: " +
                         std::to_string(page_size));
    RS_CHECK(page_size >= 4096);
}

RS_TEST(measured_bounds_are_ordered) {
    const probe::Result result = probe::probe_virtual_memory();
    const auto& vm_model = result.profile.vm;
    if (vm_model.min_map_address.is_known() &&
        vm_model.max_user_address.is_known()) {
        RS_CHECK(vm_model.min_map_address.value().value <
                 vm_model.max_user_address.value().value);
    }
}

RS_TEST(no_recorded_range_carries_unknown_evidence) {
    // Absence of observation must never be recorded as a limitation.
    const probe::Result result = probe::probe_virtual_memory();
    for (const auto& r : result.profile.vm.unavailable_ranges) {
        RS_CHECK(r.evidence != EvidenceClass::Unknown);
        RS_CHECK(!r.range.empty());
    }
    for (const auto& r : result.profile.vm.available_ranges) {
        RS_CHECK(r.evidence != EvidenceClass::Unknown);
        RS_CHECK(!r.range.empty());
    }
}

RS_TEST(available_and_unavailable_ranges_do_not_overlap) {
    const probe::Result result = probe::probe_virtual_memory();
    for (const auto& a : result.profile.vm.available_ranges) {
        for (const auto& u : result.profile.vm.unavailable_ranges) {
            RS_CHECK_MESSAGE(
                !a.range.intersects(u.range),
                "the probe reported " + a.range.to_string() +
                    " as both available and unavailable");
        }
    }
}

RS_TEST(disabling_the_scan_drops_the_sweep_but_keeps_measured_bounds) {
    // Caught by the first macOS CI run of the corrected probe, and the test
    // was the thing that was wrong.
    //
    // --no-scan skips the broad candidate sweep. It does NOT discard facts the
    // BOUNDS search establishes on its way to max_user_address. On a platform
    // whose address space has holes - macOS does, Linux does not - those holes
    // are found there, and they are real measurements. Throwing them away so
    // that an option name reads tidily would be discarding evidence, which is
    // the one thing this project must never do.
    probe::Options options;
    options.scan_address_space = false;
    options.run_faulting_tests = false;
    const probe::Result result = probe::probe_virtual_memory(options);

    // available_ranges comes only from the sweep, so it must be empty.
    RS_CHECK_MESSAGE(result.profile.vm.available_ranges.empty(),
                     "available ranges survived --no-scan; they can only come "
                     "from the sweep");

    // unavailable_ranges may still carry bounds-derived holes. Whatever is
    // there must still be properly classified.
    for (const auto& r : result.profile.vm.unavailable_ranges) {
        RS_CHECK_MESSAGE(r.evidence != EvidenceClass::Unknown,
                         "a bounds-derived hole carries unknown evidence");
        RS_CHECK(!r.range.empty());
    }

    if (result.implemented) {
        RS_CHECK_MESSAGE(result.profile.vm.page_size.is_known(),
                         "page size should not depend on the address sweep");
    }
    RS_CHECK(!result.profile.run.warnings.empty());
}

// ---------------------------------------------------------------------------
// The probe must cover the region where programs actually map.
//
// It did not, for the project's whole life, and nothing said so: the ladder
// sampled powers of two plus four emulator landmarks - 224 MiB of a 128 TiB
// space, nowhere near `mmap_base`. Thirteen real programs made 639 MAP_FIXED
// requests on a Linux host and 637 fell outside every established range, so
// the address rules answered UNKNOWN 99.7% of the time. Correct, and useless.
// See docs/campaigns/2026-07-false-positive-rate.md.
//
// This asserts the COVERAGE, not the implementation, so a future rewrite of
// the sweep is free as long as the answer survives.
// ---------------------------------------------------------------------------
RS_TEST(the_scan_covers_where_this_process_is_actually_mapped) {
    const probe::Result result = probe::probe_virtual_memory();
    if (!result.implemented) return;
    if (result.profile.vm.available_ranges.empty() &&
        result.profile.vm.unavailable_ranges.empty()) {
        return;  // the sweep was disabled; a different test covers that
    }

    // Two addresses this process is using right now. Nothing about either is
    // special except that it is real, which is the entire point.
    const auto code_addr = reinterpret_cast<std::uint64_t>(
        &has_value_with_unknown_evidence);
    const auto heap = std::make_unique<char[]>(1 << 20);
    const auto heap_addr = reinterpret_cast<std::uint64_t>(heap.get());

    for (std::uint64_t addr : {code_addr, heap_addr}) {
        const std::uint64_t page = addr & ~std::uint64_t{0xfff};
        const RangeVerdict verdict =
            result.profile.query_range(AddressRange{page, page + 4096});
        // Deliberately NOT asserting Supported: an address may legitimately
        // sit in a structurally refused band on some host. Asserting the
        // profile has SOMETHING to say, because saying nothing is the failure
        // this test exists for.
        RS_CHECK_MESSAGE(verdict.level != SupportLevel::Unknown,
                         "the profile establishes nothing about an address "
                         "this process is executing from or allocating in; "
                         "the scan is looking in the wrong part of the "
                         "address space");
    }
}

RS_TEST(probe_leaves_the_process_address_space_usable) {
    // A crude but real check that the probe cleaned up: after probing, we can
    // still allocate and touch memory. If the probe had unmapped something it
    // did not own, this test process would already have crashed.
    const probe::Result result = probe::probe_virtual_memory();
    (void)result;
    volatile char* buffer = new char[1 << 20];
    for (int i = 0; i < (1 << 20); i += 4096) buffer[i] = 1;
    RS_CHECK_EQ(buffer[0], char{1});
    delete[] buffer;
}

RS_TEST_MAIN("probe conformance")

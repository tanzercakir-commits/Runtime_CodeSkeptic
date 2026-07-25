// SPDX-License-Identifier: Apache-2.0
//
// Verdict diff across two profiles.
//
// The interesting tests here are not "does it spot a change" - that is one
// comparison. They are the three ways this could be quietly, flatteringly
// wrong: an unanswerable contract counted as fine, a bundle whose worst-of
// hides two opposite moves, and an improvement that is really a fact that
// stopped being measured.
#include "runtimeskeptic/vm/impact.hpp"

#include "runtimeskeptic/core/io.hpp"

#include "fixtures.hpp"
#include "test_support.hpp"

using namespace rs;
using namespace rs::vm;
using namespace rs::test;

namespace {

ContractImpact compare_one(const Requirement& r,
                           const EnvironmentProfile& before,
                           const EnvironmentProfile& after) {
    return compare_contract("test.json", {r}, before, after, AnalysisOptions{});
}

}  // namespace

// ---------------------------------------------------------------------------
// The classification itself.
// ---------------------------------------------------------------------------
RS_TEST(a_verdict_getting_worse_is_a_regression) {
    // Same contract, permissive host -> host that reserves the band it wants.
    const auto impact = compare_one(exact_mapping_requirement(),
                                    permissive_host(), host_with_reserved_band());

    RS_CHECK(impact.change == VerdictChange::Regressed);
    RS_CHECK(impact.moved());
    RS_CHECK(impact.requirements.size() == 1);
    RS_CHECK(impact.requirements[0].after == SupportLevel::Unsupported);
}

RS_TEST(a_verdict_getting_better_is_an_improvement_and_is_reported) {
    const auto impact = compare_one(exact_mapping_requirement(),
                                    host_with_reserved_band(), permissive_host());

    RS_CHECK(impact.change == VerdictChange::Improved);
    RS_CHECK(impact.moved());
}

RS_TEST(an_unchanged_answered_verdict_is_unchanged) {
    const auto impact = compare_one(plain_anonymous_mapping(),
                                    permissive_host(), permissive_host());

    RS_CHECK(impact.change == VerdictChange::Unchanged);
    RS_CHECK(!impact.moved());
}

// ---------------------------------------------------------------------------
// THE TRAP. A contract nobody could answer before and nobody can answer now
// has not "stayed the same". Counting it as unchanged is how "3 of 40
// affected" gets read as "37 are fine".
// ---------------------------------------------------------------------------
RS_TEST(unknown_on_both_sides_is_never_answered_not_unchanged) {
    EnvironmentProfile a = unknown_host();
    a.profile_name = "unknown-a";
    EnvironmentProfile b = unknown_host();
    b.profile_name = "unknown-b";

    const auto impact = compare_one(exact_mapping_requirement(), a, b);

    RS_CHECK(impact.requirements.size() == 1);
    RS_CHECK(impact.requirements[0].before == SupportLevel::Unknown);
    RS_CHECK(impact.requirements[0].after == SupportLevel::Unknown);
    RS_CHECK(impact.change == VerdictChange::NeverAnswered);
    RS_CHECK(impact.change != VerdictChange::Unchanged);
    // And it is not a regression either: nothing got worse, nothing is known.
    RS_CHECK(!impact.moved());
}

RS_TEST(an_empty_contract_is_never_answered_not_unchanged) {
    const ContractImpact impact = compare_contract(
        "empty.json", {}, permissive_host(), permissive_host(),
        AnalysisOptions{});

    RS_CHECK(impact.requirements.empty());
    RS_CHECK(impact.change == VerdictChange::NeverAnswered);
}

// ---------------------------------------------------------------------------
// Unknown outranks conditional in the aggregation order, so losing the ability
// to answer IS a regression even though nothing was refused.
// ---------------------------------------------------------------------------
RS_TEST(losing_the_ability_to_answer_is_a_regression) {
    const auto impact = compare_one(exact_mapping_requirement(),
                                    permissive_host(), unknown_host());

    RS_CHECK(impact.requirements[0].before != SupportLevel::Unknown);
    RS_CHECK(impact.requirements[0].after == SupportLevel::Unknown);
    RS_CHECK(impact.change == VerdictChange::Regressed);
}

// ---------------------------------------------------------------------------
// THE SILENCE CASE. A contract file holding several requirements is compared
// requirement by requirement, NOT by its worst-of. rs-check reports the worst;
// if one requirement improves while another regresses, the worst-of is
// identical on both sides and a whole-file comparison prints nothing at all.
//
// This project has shipped that failure twice already - a crashing ground-truth
// case counted as a confirmed refusal, and a comparison table running green
// while discarding compiler warnings. Not a third time.
// ---------------------------------------------------------------------------
RS_TEST(opposite_moves_inside_one_bundle_are_not_cancelled_out) {
    // Two hosts that differ in exactly two facts, in opposite directions.
    EnvironmentProfile small_pages_no_rwx = permissive_host();
    small_pages_no_rwx.profile_name = "4k-pages-wx-enforced";
    small_pages_no_rwx.vm.protection.write_execute_simultaneous =
        Fact<bool>::known(false, EvidenceClass::MeasuredCapability, "fixture");

    EnvironmentProfile big_pages_rwx = permissive_host();
    big_pages_rwx.profile_name = "16k-pages-rwx-allowed";
    big_pages_rwx.vm.page_size =
        Fact<std::uint64_t>::known(16384, EvidenceClass::MeasuredCapability,
                                   "fixture");

    // Wants 4 KiB pages: fine on the first host, refused on the second.
    Requirement needs_4k = plain_anonymous_mapping();
    needs_4k.name = "assumes 4 KiB pages";
    needs_4k.request.required_page_size = 4096;

    // Wants RWX: refused on the first host, fine on the second.
    Requirement needs_rwx = plain_anonymous_mapping();
    needs_rwx.name = "writes and executes the same mapping";
    needs_rwx.request.protection.execute = true;
    needs_rwx.request.simultaneous_write_execute = true;

    const ContractImpact impact = compare_contract(
        "bundle.json", {needs_4k, needs_rwx}, small_pages_no_rwx,
        big_pages_rwx, AnalysisOptions{});

    RS_CHECK(impact.requirements.size() == 2);

    // The two requirements moved in opposite directions...
    const bool one_regressed =
        impact.requirements[0].change == VerdictChange::Regressed ||
        impact.requirements[1].change == VerdictChange::Regressed;
    const bool one_improved =
        impact.requirements[0].change == VerdictChange::Improved ||
        impact.requirements[1].change == VerdictChange::Improved;
    RS_CHECK(one_regressed);
    RS_CHECK(one_improved);

    // ...and the file is NOT reported as unchanged.
    RS_CHECK(impact.change != VerdictChange::Unchanged);
    RS_CHECK(impact.change == VerdictChange::Regressed);  // attention order
    RS_CHECK(impact.moved());
}

// ---------------------------------------------------------------------------
// Findings that appeared and disappeared. Deliberately NOT "the finding
// responsible" - that is a causal claim, and several rules fire at once.
// ---------------------------------------------------------------------------
RS_TEST(finding_ids_that_appeared_are_listed_without_claiming_causation) {
    const auto impact = compare_one(exact_mapping_requirement(),
                                    permissive_host(), host_with_reserved_band());

    const RequirementImpact& r = impact.requirements[0];
    RS_CHECK(!r.ids_appeared.empty());

    bool saw_exact_address = false;
    for (const auto& id : r.ids_appeared) {
        if (id == ids::kExactAddressUnavailable) saw_exact_address = true;
    }
    RS_CHECK(saw_exact_address);

    // Sorted and unique, so two runs produce byte-identical output.
    for (std::size_t i = 1; i < r.ids_appeared.size(); ++i) {
        RS_CHECK(r.ids_appeared[i - 1] < r.ids_appeared[i]);
    }
}

RS_TEST(comparing_a_profile_with_itself_finds_no_regression) {
    const ContractImpact impact = compare_contract(
        "self.json", {exact_mapping_requirement(), plain_anonymous_mapping()},
        permissive_host(), permissive_host(), AnalysisOptions{});

    for (const auto& r : impact.requirements) {
        RS_CHECK(r.before == r.after);
        RS_CHECK(r.ids_appeared.empty());
        RS_CHECK(r.ids_disappeared.empty());
    }
    RS_CHECK(impact.change != VerdictChange::Regressed);
    RS_CHECK(impact.change != VerdictChange::Improved);
}

// ---------------------------------------------------------------------------
// The report's own accounting has to add up, or the summary line lies.
// ---------------------------------------------------------------------------
RS_TEST(report_counts_partition_the_contracts) {
    ImpactReport report;
    for (auto c : {VerdictChange::Regressed, VerdictChange::Improved,
                   VerdictChange::NeverAnswered, VerdictChange::Unchanged,
                   VerdictChange::Unchanged}) {
        ContractImpact e;
        e.change = c;
        report.contracts.push_back(e);
    }

    const std::size_t sum = report.count(VerdictChange::Regressed) +
                            report.count(VerdictChange::Improved) +
                            report.count(VerdictChange::NeverAnswered) +
                            report.count(VerdictChange::Unchanged);
    RS_CHECK(sum == report.contracts.size());
    RS_CHECK(report.count(VerdictChange::Unchanged) == 2);
    RS_CHECK(report.any_regression());
}

RS_TEST(an_unreadable_contract_is_reported_not_skipped) {
    const ImpactReport report = compare_contracts(
        {"/nonexistent/contract.json"}, permissive_host(),
        host_with_reserved_band(), AnalysisOptions{});

    RS_CHECK(report.contracts.empty());
    RS_CHECK(report.unreadable.size() == 1);
    // A file that failed to load is not a file that passed.
    RS_CHECK(!report.any_regression());
}

// ---------------------------------------------------------------------------
// End to end, against the two MEASURED Apple Silicon profiles. Everything
// above is synthetic and proves the logic; this proves the thing works on real
// measurements, which is the only kind of proof this project accepts.
//
// The two profiles were taken on the SAME physical machine minutes apart - one
// native arm64, one x86-64 under Rosetta - and `rs-profile diff` reports 138
// differences between them. The question this answers is which of those 138
// actually move a verdict.
// ---------------------------------------------------------------------------
namespace {

std::optional<EnvironmentProfile> load_profile_file(const std::string& rel) {
    std::string error;
    auto text = rs::io::read_file(std::string(RS_REPO_ROOT) + "/" + rel, error);
    if (!text) return std::nullopt;
    auto parsed = rs::json::parse(*text);
    if (!parsed.ok()) return std::nullopt;
    return EnvironmentProfile::from_json(*parsed.value, error);
}

const char* kNative = "profiles/measured/macos-14-arm64-native.measured.json";
const char* kRosetta =
    "profiles/measured/macos-14-arm64-rosetta-x86_64.measured.json";
const char* kPageSize4k =
    "tests/groundtruth/contracts/page-size-at-most-4kib.json";

}  // namespace

RS_TEST(measured_apple_silicon_lanes_move_a_real_verdict) {
    auto native = load_profile_file(kNative);
    auto rosetta = load_profile_file(kRosetta);
    RS_CHECK(native.has_value());
    RS_CHECK(rosetta.has_value());
    if (!native || !rosetta) return;

    // The profiles are genuinely different documents.
    RS_CHECK(native->profile_id() != rosetta->profile_id());

    const std::string contract = std::string(RS_REPO_ROOT) + "/" + kPageSize4k;

    // Rosetta has 4 KiB pages, native arm64 has 16 KiB. A program compiled for
    // 4 KiB is fine on one lane and broken on the other, on ONE machine.
    const ImpactReport worse = compare_contracts({contract}, *rosetta, *native,
                                                 AnalysisOptions{});
    RS_CHECK(worse.contracts.size() == 1);
    RS_CHECK(worse.any_regression());
    RS_CHECK(worse.contracts[0].change == VerdictChange::Regressed);
    RS_CHECK(worse.unreadable.empty());

    bool blamed_page_size = false;
    for (const auto& r : worse.contracts[0].requirements) {
        for (const auto& id : r.ids_appeared) {
            if (id == ids::kPageSizeMismatch) blamed_page_size = true;
        }
    }
    RS_CHECK(blamed_page_size);

    // ...and the comparison is symmetric.
    const ImpactReport better = compare_contracts({contract}, *native, *rosetta,
                                                  AnalysisOptions{});
    RS_CHECK(!better.any_regression());
    RS_CHECK(better.contracts[0].change == VerdictChange::Improved);
}

// The other direction of the same measurement, and the more surprising one:
// on one Apple Silicon machine a TRANSLATED x86-64 process is granted
// simultaneous write+execute that the NATIVE arm64 process is refused. Both
// facts are `measured_capability`, not inferred.
RS_TEST(rosetta_grants_rwx_that_native_arm64_refuses) {
    auto native = load_profile_file(kNative);
    auto rosetta = load_profile_file(kRosetta);
    RS_CHECK(native.has_value() && rosetta.has_value());
    if (!native || !rosetta) return;

    RS_CHECK(native->vm.protection.write_execute_simultaneous.is_known());
    RS_CHECK(rosetta->vm.protection.write_execute_simultaneous.is_known());
    RS_CHECK(native->vm.protection.write_execute_simultaneous.value() == false);
    RS_CHECK(rosetta->vm.protection.write_execute_simultaneous.value() == true);

    const std::string contract = std::string(RS_REPO_ROOT) +
                                 "/tests/groundtruth/contracts/"
                                 "rwx-anonymous-2mib.json";
    const ImpactReport report = compare_contracts({contract}, *rosetta,
                                                  *native, AnalysisOptions{});
    RS_CHECK(report.unreadable.empty());
    RS_CHECK(report.any_regression());
}

RS_TEST_MAIN("impact")

// SPDX-License-Identifier: Apache-2.0
//
// Incident regression tests: on-disk requirement documents evaluated against
// on-disk profiles, asserting the verdict AND the confidence.
//
// The confidence assertions matter more than the verdicts. Getting
// "UNSUPPORTED" right while labelling it PROVEN on the strength of a
// hand-authored fixture would be exactly the failure this project exists to
// prevent.
#include <string>

#include "runtimeskeptic/core/io.hpp"
#include "runtimeskeptic/core/json.hpp"
#include "runtimeskeptic/reports/report.hpp"
#include "runtimeskeptic/vm/analyzer.hpp"
#include "test_support.hpp"

using namespace rs;
using namespace rs::vm;

namespace {

std::string repo_path(const char* relative) {
    return std::string(RS_REPO_ROOT) + "/" + relative;
}

std::optional<Requirement> load_requirement(const char* relative) {
    std::string error;
    auto text = io::read_file(repo_path(relative), error);
    if (!text) {
        RS_CHECK_MESSAGE(false, "cannot read " + std::string(relative) + ": " + error);
        return std::nullopt;
    }
    auto parsed = json::parse(*text);
    if (!parsed.ok()) {
        RS_CHECK_MESSAGE(false, std::string(relative) + " is not valid JSON: " +
                                    parsed.error->to_string());
        return std::nullopt;
    }
    auto requirement = Requirement::from_json(*parsed.value, error);
    RS_CHECK_MESSAGE(requirement.has_value(),
                     std::string(relative) + ": " + error);
    return requirement;
}

std::optional<EnvironmentProfile> load_profile(const char* relative) {
    std::string error;
    auto text = io::read_file(repo_path(relative), error);
    if (!text) {
        RS_CHECK_MESSAGE(false, "cannot read " + std::string(relative) + ": " + error);
        return std::nullopt;
    }
    auto parsed = json::parse(*text);
    if (!parsed.ok()) {
        RS_CHECK_MESSAGE(false, std::string(relative) + " is not valid JSON: " +
                                    parsed.error->to_string());
        return std::nullopt;
    }
    auto profile = EnvironmentProfile::from_json(*parsed.value, error);
    RS_CHECK_MESSAGE(profile.has_value(), std::string(relative) + ": " + error);
    return profile;
}

const Finding* find(const AnalysisResult& result, const char* id) {
    for (const auto& f : result.findings) {
        if (f.id == id) return &f;
    }
    return nullptr;
}

constexpr const char* kMacosFixture =
    "profiles/fixtures/macos-arm64-rosetta-x86_64.fixture.json";

}  // namespace

RS_TEST(all_shipped_contracts_parse) {
    for (const char* path : {"contracts/emulator-fixed-guest-mapping.json",
                             "contracts/emulator-hinted-guest-mapping.json",
                             "contracts/jit-code-buffer.json",
                             "contracts/database-reserve-commit.json"}) {
        load_requirement(path);
    }
}

RS_TEST(all_shipped_profiles_parse) {
    load_profile(kMacosFixture);
}

// ---------------------------------------------------------------------------
// RSC-0001, the ROADMAP's motivating incident, against the macOS fixture.
// ---------------------------------------------------------------------------
RS_TEST(motivating_incident_against_the_fixture) {
    auto requirement = load_requirement("contracts/emulator-fixed-guest-mapping.json");
    auto profile = load_profile(kMacosFixture);
    if (!requirement || !profile) return;

    const auto result = analyze(*requirement, *profile);

    RS_CHECK(result.overall == SupportLevel::Unsupported);
    const Finding* f = find(result, ids::kExactAddressUnavailable);
    RS_CHECK_MESSAGE(f != nullptr, "the exact-address contradiction was not found");
    if (f == nullptr) return;

    // The decisive fact in this fixture is heuristic_risk, so the finding must
    // NOT claim PROVEN however obvious the contradiction looks.
    RS_CHECK_MESSAGE(f->confidence != Confidence::Proven,
                     "a hand-authored fixture produced a PROVEN finding");
    RS_CHECK(f->confidence == Confidence::Predictive);

    // And the report must say the profile did not come from a real host.
    RS_CHECK(!result.analyzer_limitations.empty());
}

RS_TEST(page_size_mismatch_also_fires_on_the_fixture) {
    // The fixture has 16 KiB pages; an x86-64 emulator built for 4 KiB pages
    // is a second, independent contradiction on the same host.
    auto requirement = load_requirement("contracts/emulator-fixed-guest-mapping.json");
    auto profile = load_profile(kMacosFixture);
    if (!requirement || !profile) return;

    requirement->request.required_page_size = 4096;
    const auto result = analyze(*requirement, *profile);
    RS_CHECK(find(result, ids::kPageSizeMismatch) != nullptr);
}

RS_TEST(hinted_variant_is_a_counterexample_not_a_proof) {
    auto requirement = load_requirement("contracts/emulator-hinted-guest-mapping.json");
    auto profile = load_profile(kMacosFixture);
    if (!requirement || !profile) return;

    const auto result = analyze(*requirement, *profile);
    const Finding* f = find(result, ids::kHintedMappingMayRelocate);
    RS_CHECK_MESSAGE(f != nullptr, "hinted relocation was not diagnosed");
    if (f == nullptr) return;
    RS_CHECK(f->confidence == Confidence::Counterexample);
}

RS_TEST(jit_buffer_hits_write_xor_execute_on_the_fixture) {
    auto requirement = load_requirement("contracts/jit-code-buffer.json");
    auto profile = load_profile(kMacosFixture);
    if (!requirement || !profile) return;

    const auto result = analyze(*requirement, *profile);
    RS_CHECK(find(result, ids::kWriteExecuteRestricted) != nullptr);
    RS_CHECK(find(result, ids::kJitEntitlementRequired) != nullptr);
    RS_CHECK(result.overall == SupportLevel::Unsupported);
}

RS_TEST(database_reservation_is_conditional_on_a_lazy_host) {
    auto requirement = load_requirement("contracts/database-reserve-commit.json");
    auto profile = load_profile(kMacosFixture);
    if (!requirement || !profile) return;

    const auto result = analyze(*requirement, *profile);
    RS_CHECK(find(result, ids::kReserveCommitSemanticMismatch) != nullptr);
    // Nothing here is impossible; it is a semantic mismatch.
    RS_CHECK(result.overall != SupportLevel::Unsupported);
}

// ---------------------------------------------------------------------------
// Public retrospective: PCSX2 #11728 / PR #11734.
//
// A reusable host profile cannot know where a future PCSX2 process image will
// land. The regression is therefore not a guessed compatibility verdict: all
// checked-in measured profiles must preserve the executable-relative
// requirement and stop at UNKNOWN through RS-VM-0024.
// ---------------------------------------------------------------------------
RS_TEST(pcsx2_relative_window_retrospective_stays_evidence_bounded) {
    auto requirement =
        load_requirement("contracts/campaign/pcsx2-v175849-data-window.json");
    if (!requirement) return;

    for (const char* path : {
             "profiles/measured/macos-14-arm64-native.measured.json",
             "profiles/measured/macos-14-arm64-rosetta-x86_64.measured.json",
             "profiles/measured/windows-server-2025-x86_64.measured.json",
             "profiles/measured/wine-9.0-on-linux-x86_64.measured.json",
         }) {
        auto profile = load_profile(path);
        if (!profile) continue;

        const auto result = analyze(*requirement, *profile);
        RS_CHECK_MESSAGE(result.overall == SupportLevel::Unknown,
                         std::string(path) +
                             " invented a decisive PCSX2 verdict");

        const Finding* f =
            find(result, ids::kDisplacementConstraintNotEvaluable);
        RS_CHECK_MESSAGE(f != nullptr,
                         std::string(path) + " dropped RS-VM-0024");
        if (f != nullptr) {
            RS_CHECK(f->confidence == Confidence::Hypothesis);
        }
    }
}

// ---------------------------------------------------------------------------
// Report rendering must not lose the confidence qualifier.
// ---------------------------------------------------------------------------
RS_TEST(reports_carry_the_confidence_and_the_origin_warning) {
    auto requirement = load_requirement("contracts/emulator-fixed-guest-mapping.json");
    auto profile = load_profile(kMacosFixture);
    if (!requirement || !profile) return;

    const auto result = analyze(*requirement, *profile);
    const std::string markdown =
        reports::render_markdown(result, *requirement, *profile);
    const std::string text = reports::render_text(result, *requirement, *profile);

    RS_CHECK(markdown.find("PREDICTIVE") != std::string::npos);
    RS_CHECK(markdown.find("did not come from your machine") != std::string::npos);
    RS_CHECK_MESSAGE(markdown.find("PROVEN") == std::string::npos,
                     "the report used the word PROVEN for a fixture-derived "
                     "finding");
    RS_CHECK(text.find("PREDICTIVE") != std::string::npos);
    RS_CHECK(text.find("not measured on a real host") != std::string::npos);
}

RS_TEST(exit_codes_match_the_documented_mapping) {
    RS_CHECK_EQ(reports::exit_code_for(SupportLevel::Supported), 0);
    RS_CHECK_EQ(reports::exit_code_for(SupportLevel::Unsupported), 1);
    RS_CHECK_EQ(reports::exit_code_for(SupportLevel::ConditionallySupported), 2);
    RS_CHECK_EQ(reports::exit_code_for(SupportLevel::Unknown), 3);
}

RS_TEST_MAIN("incidents")

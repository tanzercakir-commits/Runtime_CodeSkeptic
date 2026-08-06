// SPDX-License-Identifier: Apache-2.0
//
// Requirement bundles.
//
// Bundle support shipped without tests, which is how a feature quietly stops
// working. These cover the shape a static extractor actually produces: many
// requirements at once, some of them malformed, each carrying notes about
// what the producer could not establish.
//
// Nothing here needs an extractor to be installed. The bundle is a file
// format, and the checked-in fixture is a real one.
#include <string>

#include "fixtures.hpp"
#include "runtimeskeptic/core/io.hpp"
#include "runtimeskeptic/core/json.hpp"
#include "runtimeskeptic/vm/analyzer.hpp"
#include "runtimeskeptic/vm/requirement.hpp"
#include "test_support.hpp"

using namespace rs;
using namespace rs::vm;
using namespace rs::test;

namespace {

json::Value parse_or_null(const std::string& text) {
    auto parsed = json::parse(text);
    if (!parsed.ok()) return json::Value();
    return *parsed.value;
}

const char* kOneRequirement = R"({
  "schema": "runtime-skeptic.application-requirements.v1",
  "name": "single",
  "operation": "virtual_memory_map",
  "assumption_evidence": "specified_guarantee",
  "request": { "size": 4096 },
  "failure_sink": { "kind": "error_return" }
})";

std::string bundle_of(const std::string& requirements) {
    return R"({
      "schema": "runtime-skeptic.application-requirements-bundle.v1",
      "producer": { "tool": "TestExtractor", "version": "1.2.3",
                    "rule": "runtime-assumption" },
      "requirements": [)" + requirements + "]}";
}

}  // namespace

RS_TEST(a_lone_requirement_is_accepted_as_a_bundle_of_one) {
    // Callers should never have to branch on which shape they were handed.
    std::string error;
    auto bundle = load_requirements(parse_or_null(kOneRequirement), error);
    RS_CHECK_MESSAGE(bundle.has_value(), error);
    if (!bundle) return;
    RS_CHECK_EQ(bundle->requirements.size(), std::size_t{1});
    RS_CHECK_EQ(bundle->requirements.front().name, std::string("single"));
    RS_CHECK(bundle->rejected.empty());
}

RS_TEST(a_bundle_carries_its_producer) {
    // Which tool produced a requirement decides how much to trust it, so the
    // provenance has to survive the round trip.
    std::string error;
    auto bundle =
        load_requirements(parse_or_null(bundle_of(kOneRequirement)), error);
    RS_CHECK_MESSAGE(bundle.has_value(), error);
    if (!bundle) return;
    RS_CHECK_EQ(bundle->producer_tool, std::string("TestExtractor"));
    RS_CHECK_EQ(bundle->producer_version, std::string("1.2.3"));
    RS_CHECK_EQ(bundle->producer_rule, std::string("runtime-assumption"));
}

RS_TEST(one_malformed_entry_does_not_discard_the_rest) {
    // An extraction run over a large codebase produces hundreds of entries.
    // Throwing all of them away because one is wrong would make the tool
    // useless exactly when it is most needed.
    const std::string mixed =
        std::string(kOneRequirement) + ", {\"schema\":\"nonsense\"}, " +
        kOneRequirement;
    std::string error;
    auto bundle = load_requirements(parse_or_null(bundle_of(mixed)), error);
    RS_CHECK_MESSAGE(bundle.has_value(), error);
    if (!bundle) return;

    RS_CHECK_EQ(bundle->requirements.size(), std::size_t{2});
    RS_CHECK_EQ(bundle->rejected.size(), std::size_t{1});
    // And the rejection must say which entry, so it can be found.
    RS_CHECK(bundle->rejected.front().find("requirements[1]") !=
             std::string::npos);
}

RS_TEST(a_rejected_entry_is_reported_rather_than_silently_dropped) {
    std::string error;
    auto bundle = load_requirements(
        parse_or_null(bundle_of("{\"schema\":\"nonsense\"}")), error);
    RS_CHECK(bundle.has_value());
    if (!bundle) return;
    RS_CHECK(bundle->requirements.empty());
    RS_CHECK_MESSAGE(!bundle->rejected.empty(),
                     "a bundle of one bad entry produced neither a "
                     "requirement nor a complaint");
}

RS_TEST(an_unknown_schema_is_refused_outright) {
    std::string error;
    RS_CHECK(!load_requirements(
                  parse_or_null(R"({"schema":"some.other.format.v1"})"), error)
                  .has_value());
    RS_CHECK(!error.empty());
    // A missing schema is not guessed at either.
    RS_CHECK(!load_requirements(parse_or_null("{}"), error).has_value());
}

RS_TEST(a_bundle_without_a_requirements_array_is_an_error) {
    std::string error;
    RS_CHECK(!load_requirements(
                  parse_or_null(
                      R"({"schema":"runtime-skeptic.application-requirements-bundle.v1"})"),
                  error)
                  .has_value());
}

RS_TEST(an_empty_bundle_parses_but_yields_nothing) {
    // Distinct from a parse failure: the producer ran and found nothing. The
    // CLI turns this into an error rather than a clean exit 0, because an
    // empty inventory is not a clean bill.
    std::string error;
    auto bundle = load_requirements(parse_or_null(bundle_of("")), error);
    RS_CHECK_MESSAGE(bundle.has_value(), error);
    if (bundle) RS_CHECK(bundle->requirements.empty());
}

RS_TEST(extraction_limitations_survive_into_the_analysis) {
    // What a producer could not establish is a limit on the verdict too. If
    // this drops on the floor, a gap reads as a clean bill.
    const char* with_limits = R"({
      "schema": "runtime-skeptic.application-requirements.v1",
      "name": "limited",
      "operation": "virtual_memory_map",
      "assumption_evidence": "statically_inferred",
      "request": { "size": 4096 },
      "failure_sink": { "kind": "error_return" },
      "extraction_limitations": [
        "requested address is not a compile-time constant",
        "translation_layer_available defaults to false"
      ]
    })";

    std::string error;
    auto bundle = load_requirements(parse_or_null(with_limits), error);
    RS_CHECK_MESSAGE(bundle.has_value(), error);
    if (!bundle) return;
    RS_CHECK_EQ(bundle->requirements.front().extraction_limitations.size(),
                std::size_t{2});

    const auto result = analyze(bundle->requirements.front(), permissive_host());
    std::size_t carried = 0;
    for (const auto& limitation : result.analyzer_limitations) {
        if (limitation.find("requirement extraction:") != std::string::npos) {
            ++carried;
        }
    }
    RS_CHECK_MESSAGE(carried == 2,
                     "extraction limitations did not reach the analysis");
}

RS_TEST(statically_inferred_requirements_cannot_yield_proven_findings) {
    // The confidence ceiling has to hold across the whole pipeline: an
    // extracted requirement is evidence about the program, not a declaration,
    // so nothing derived from it may claim proof.
    const char* extracted = R"({
      "schema": "runtime-skeptic.application-requirements.v1",
      "name": "extracted",
      "operation": "virtual_memory_map",
      "assumption_evidence": "statically_inferred",
      "request": { "address": "0x1000", "size": 4096,
                   "exact_address_required": true },
      "assumptions": { "guest_host_identity_required": true },
      "failure_sink": { "kind": "fatal_assert" }
    })";

    std::string error;
    auto bundle = load_requirements(parse_or_null(extracted), error);
    RS_CHECK(bundle.has_value());
    if (!bundle) return;

    // 0x1000 is below the fixture's measured minimum: a real contradiction.
    const auto result = analyze(bundle->requirements.front(), permissive_host());
    RS_CHECK(result.overall == SupportLevel::Unsupported);
    for (const auto& f : result.findings) {
        RS_CHECK_MESSAGE(f.confidence != Confidence::Proven,
                         f.id + " claimed PROVEN from a statically inferred "
                                "requirement");
    }
}

RS_TEST(the_checked_in_extractor_bundle_parses_and_evaluates) {
    // A real document produced by a static extractor, checked in as a
    // fixture so this format stays supported whether or not any extractor is
    // installed.
    std::string error;
    auto text = io::read_file(
        std::string(RS_REPO_ROOT) + "/contracts/generated/"
                                    "producer-neutral-demo-bundle.json",
        error);
    RS_CHECK_MESSAGE(text.has_value(), error);
    if (!text) return;

    auto bundle = load_requirements(parse_or_null(*text), error);
    RS_CHECK_MESSAGE(bundle.has_value(), error);
    if (!bundle) return;
    RS_CHECK(bundle->requirements.size() >= 2);
    RS_CHECK(bundle->rejected.empty());

    // Every entry must be analyzable, and every entry must be honest about
    // being inferred rather than declared.
    for (const auto& requirement : bundle->requirements) {
        RS_CHECK(requirement.assumption_evidence ==
                 EvidenceClass::StaticallyInferred);
        const auto result = analyze(requirement, permissive_host());
        RS_CHECK(!result.requirement_id.empty());
        for (const auto& f : result.findings) {
            RS_CHECK_MESSAGE(f.confidence != Confidence::Proven,
                             f.id + " claimed PROVEN from extracted input");
        }
    }
}

RS_TEST(the_extractor_bundle_finds_the_identity_requirement) {
    // The point of the whole exercise: the extractor recorded the caller's
    // address-identity requirement, which no error code would ever report.
    std::string error;
    auto text = io::read_file(
        std::string(RS_REPO_ROOT) + "/contracts/generated/"
                                    "producer-neutral-demo-bundle.json",
        error);
    RS_CHECK_MESSAGE(text.has_value(), error);
    if (!text) return;
    auto bundle = load_requirements(parse_or_null(*text), error);
    RS_CHECK_MESSAGE(bundle.has_value(), error);
    if (!bundle) return;

    bool found_identity = false;
    bool found_fatal_sink = false;
    for (const auto& requirement : bundle->requirements) {
        if (requirement.assumptions.guest_host_identity_required) {
            found_identity = true;
            if (requirement.failure_sink.kind == FailureSinkKind::FatalAssert) {
                found_fatal_sink = true;
            }
        }
    }
    RS_CHECK_MESSAGE(found_identity,
                     "no extracted requirement records address identity");
    RS_CHECK_MESSAGE(found_fatal_sink,
                     "the identity requirement lost its fatal failure sink");
}

RS_TEST_MAIN("bundle")

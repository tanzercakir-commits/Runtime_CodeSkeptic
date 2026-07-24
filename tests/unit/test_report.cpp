// SPDX-License-Identifier: Apache-2.0
#include "runtimeskeptic/reports/report.hpp"

#include <string>

#include "fixtures.hpp"
#include "test_support.hpp"

using namespace rs;
using namespace rs::vm;
using namespace rs::test;

namespace {

bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

RS_TEST(text_report_states_the_verdict_and_the_evidence) {
    const Requirement r = exact_mapping_requirement();
    const EnvironmentProfile p = host_with_reserved_band();
    const std::string out = reports::render_text(analyze(r, p), r, p);

    RS_CHECK(contains(out, "UNSUPPORTED"));
    RS_CHECK(contains(out, "RS-VM-0001"));
    RS_CHECK(contains(out, "PROVEN"));
    RS_CHECK(contains(out, "Evidence chain"));
    RS_CHECK(contains(out, "Will not work"));
    RS_CHECK(contains(out, "profile_id"));
}

RS_TEST(markdown_report_is_self_contained) {
    const Requirement r = exact_mapping_requirement();
    const EnvironmentProfile p = host_with_reserved_band();
    const std::string out = reports::render_markdown(analyze(r, p), r, p);

    RS_CHECK(contains(out, "# RuntimeSkeptic compatibility report"));
    RS_CHECK(contains(out, "**Verdict:** `UNSUPPORTED`"));
    RS_CHECK(contains(out, "| ID | Severity | Confidence | Title |"));
    RS_CHECK(contains(out, "requirement_id"));
    RS_CHECK(contains(out, "Remediation classes"));
}

RS_TEST(quiet_mode_drops_detail_but_keeps_the_verdict) {
    const Requirement r = exact_mapping_requirement();
    const EnvironmentProfile p = host_with_reserved_band();
    reports::Options quiet;
    quiet.verbose = false;

    const auto result = analyze(r, p);
    const std::string loud = reports::render_text(result, r, p);
    const std::string silent = reports::render_text(result, r, p, quiet);

    RS_CHECK(contains(silent, "UNSUPPORTED"));
    RS_CHECK(!contains(silent, "Evidence chain"));
    RS_CHECK(silent.size() < loud.size());
}

RS_TEST(supported_verdict_renders_without_findings) {
    const Requirement r = plain_anonymous_mapping();
    const EnvironmentProfile p = permissive_host();
    const std::string out = reports::render_text(analyze(r, p), r, p);
    RS_CHECK(contains(out, "SUPPORTED"));
    RS_CHECK(contains(out, "No findings."));
}

RS_TEST(non_measured_origin_is_always_surfaced) {
    Requirement r = exact_mapping_requirement();
    EnvironmentProfile p = host_with_reserved_band();
    p.origin = ProfileOrigin::HandAuthoredFixture;

    const auto result = analyze(r, p);
    RS_CHECK(contains(reports::render_text(result, r, p),
                      "not measured on a real host"));
    RS_CHECK(contains(reports::render_markdown(result, r, p),
                      "did not come from your machine"));
}

RS_TEST(rendering_is_deterministic) {
    const Requirement r = exact_mapping_requirement();
    const EnvironmentProfile p = host_with_reserved_band();
    const auto result = analyze(r, p);
    RS_CHECK_EQ(reports::render_markdown(result, r, p),
                reports::render_markdown(result, r, p));
    RS_CHECK_EQ(reports::render_text(result, r, p),
                reports::render_text(result, r, p));
}

RS_TEST(color_output_adds_escapes_without_changing_content) {
    const Requirement r = exact_mapping_requirement();
    const EnvironmentProfile p = host_with_reserved_band();
    reports::Options colored;
    colored.color = true;

    const auto result = analyze(r, p);
    const std::string plain = reports::render_text(result, r, p);
    const std::string fancy = reports::render_text(result, r, p, colored);

    RS_CHECK(!contains(plain, "\033["));
    RS_CHECK(contains(fancy, "\033["));
    RS_CHECK(contains(fancy, "RS-VM-0001"));
}

RS_TEST_MAIN("report")

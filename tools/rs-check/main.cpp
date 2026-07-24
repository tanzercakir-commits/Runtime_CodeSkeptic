// SPDX-License-Identifier: Apache-2.0
//
//   rs-check REQUIREMENT.json --profile PROFILE.json [--format text|json|markdown]
//
// Answers one question: can this host satisfy this requirement?
#include <iostream>
#include <vector>
#include <string>

#include "runtimeskeptic/core/io.hpp"
#include "runtimeskeptic/core/json.hpp"
#include "runtimeskeptic/reports/report.hpp"
#include "runtimeskeptic/version.hpp"
#include "runtimeskeptic/vm/analyzer.hpp"

namespace {

void print_usage() {
    std::cout <<
        R"(rs-check - compare an application requirement against a host profile

USAGE
  rs-check REQUIREMENT.json --profile PROFILE.json [OPTIONS]

REQUIREMENT.json is either a single
runtime-skeptic.application-requirements.v1 document, or a
runtime-skeptic.application-requirements-bundle.v1 containing many - the
shape CodeSkeptic's --runtime-assumptions mode emits. Every requirement in a
bundle is evaluated, and the run's verdict is the worst of them.

OPTIONS
  --profile FILE       environment profile from rs-env-probe (required)
  --format FORMAT      text (default), json, or markdown
  --output FILE        write the report here (default: stdout)
  --no-unknowns        do not emit informational findings for unknown facts.
                       The verdict still becomes UNKNOWN; only the explanation
                       is suppressed.
  --quiet              suppress evidence chains and remediation classes
  --color              colorize the text report
  -h, --help           show this help

EXIT CODES
  0   SUPPORTED
  1   UNSUPPORTED
  2   CONDITIONALLY_SUPPORTED
  3   UNKNOWN
  64  usage error
  65  input could not be read or did not satisfy its schema
  70  internal error

  A CI job that should fail on proven contradictions but tolerate unknowns
  treats 1 as failure and 2 and 3 as warnings.
)";
}

std::optional<rs::json::Value> load_json(const std::string& path,
                                         const char* what) {
    std::string error;
    auto text = rs::io::read_file(path, error);
    if (!text) {
        std::cerr << "rs-check: " << error << "\n";
        return std::nullopt;
    }
    auto parsed = rs::json::parse(*text);
    if (!parsed.ok()) {
        std::cerr << "rs-check: " << what << " '" << path
                  << "' is not valid JSON: " << parsed.error->to_string() << "\n";
        return std::nullopt;
    }
    return parsed.value;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace rs;

    std::string requirement_path;
    std::string profile_path;
    std::string output_path = "-";
    std::string format = "text";
    reports::Options report_options;
    vm::AnalysisOptions analysis_options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage();
            return reports::exit_code::kSupported;
        }
        if (arg == "--profile") {
            if (i + 1 >= argc) {
                std::cerr << "rs-check: --profile requires a path\n";
                return reports::exit_code::kUsage;
            }
            profile_path = argv[++i];
        } else if (arg == "--format" || arg == "-f") {
            if (i + 1 >= argc) {
                std::cerr << "rs-check: --format requires a value\n";
                return reports::exit_code::kUsage;
            }
            format = argv[++i];
        } else if (arg == "--output" || arg == "-o") {
            if (i + 1 >= argc) {
                std::cerr << "rs-check: --output requires a path\n";
                return reports::exit_code::kUsage;
            }
            output_path = argv[++i];
        } else if (arg == "--no-unknowns") {
            analysis_options.report_unknowns = false;
        } else if (arg == "--quiet") {
            report_options.verbose = false;
        } else if (arg == "--color") {
            report_options.color = true;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "rs-check: unrecognized option '" << arg << "'\n";
            return reports::exit_code::kUsage;
        } else if (requirement_path.empty()) {
            requirement_path = arg;
        } else {
            std::cerr << "rs-check: unexpected extra argument '" << arg << "'\n";
            return reports::exit_code::kUsage;
        }
    }

    if (requirement_path.empty() || profile_path.empty()) {
        print_usage();
        return reports::exit_code::kUsage;
    }
    if (format != "text" && format != "json" && format != "markdown") {
        std::cerr << "rs-check: unknown format '" << format << "'\n";
        return reports::exit_code::kUsage;
    }

    auto requirement_json = load_json(requirement_path, "requirement");
    if (!requirement_json) return reports::exit_code::kInput;
    auto profile_json = load_json(profile_path, "profile");
    if (!profile_json) return reports::exit_code::kInput;

    std::string error;
    auto bundle = vm::load_requirements(*requirement_json, error);
    if (!bundle) {
        std::cerr << "rs-check: requirement '" << requirement_path
                  << "' is invalid: " << error << "\n";
        return reports::exit_code::kInput;
    }
    for (const auto& rejected : bundle->rejected) {
        std::cerr << "rs-check: skipped " << rejected << "\n";
    }
    if (bundle->requirements.empty()) {
        std::cerr << "rs-check: '" << requirement_path
                  << "' contains no usable requirements. An empty bundle is "
                     "not a clean result.\n";
        return reports::exit_code::kInput;
    }
    auto profile = vm::EnvironmentProfile::from_json(*profile_json, error);
    if (!profile) {
        std::cerr << "rs-check: profile '" << profile_path
                  << "' is invalid: " << error << "\n";
        return reports::exit_code::kInput;
    }

    // A bundle is evaluated requirement by requirement, and the run's verdict
    // is the worst of them: one proven contradiction condemns the build even
    // if a hundred other mappings are fine.
    SupportLevel overall = SupportLevel::Supported;
    std::vector<vm::AnalysisResult> results;
    results.reserve(bundle->requirements.size());
    std::string rendered;

    for (const auto& requirement : bundle->requirements) {
        vm::AnalysisResult result =
            vm::analyze(requirement, *profile, analysis_options);
        overall = combine(overall, result.overall);

        if (format == "markdown") {
            if (!rendered.empty()) rendered += "\n---\n\n";
            rendered += reports::render_markdown(result, requirement, *profile,
                                                 report_options);
        } else if (format == "text") {
            if (!rendered.empty()) rendered += "\n";
            rendered +=
                reports::render_text(result, requirement, *profile, report_options);
        }
        results.push_back(std::move(result));
    }

    if (format == "json") {
        json::Value document = json::Value::object();
        document["schema"] = std::string("runtime-skeptic.compatibility-run.v1");
        document["overall"] = std::string(rs::to_string(overall));
        document["requirement_count"] =
            static_cast<unsigned long long>(results.size());
        if (!bundle->producer_tool.empty()) {
            json::Value producer = json::Value::object();
            producer["tool"] = bundle->producer_tool;
            producer["version"] = bundle->producer_version;
            producer["rule"] = bundle->producer_rule;
            document["producer"] = producer;
        }
        json::Value entries = json::Value::array();
        for (const auto& result : results) entries.push_back(result.to_json());
        document["results"] = entries;
        json::Value skipped = json::Value::array();
        for (const auto& r : bundle->rejected) skipped.push_back(json::Value(r));
        document["rejected_requirements"] = skipped;
        rendered = json::serialize_pretty(document);
    }

    if (!io::write_file(output_path, rendered, error)) {
        std::cerr << "rs-check: " << error << "\n";
        return reports::exit_code::kInput;
    }

    if (results.size() > 1 && output_path != "-") {
        std::cerr << "rs-check: evaluated " << results.size()
                  << " requirement(s); overall " << rs::to_string(overall)
                  << "\n";
    }
    return reports::exit_code_for(overall);
}

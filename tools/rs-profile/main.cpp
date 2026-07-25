// SPDX-License-Identifier: Apache-2.0
//
//   rs-profile verify PROFILE.json
//   rs-profile diff   A.json B.json
//   rs-profile impact OLD.json NEW.json CONTRACT...
//   rs-profile id     PROFILE.json
//
// `verify` answers "is this profile well-formed, and how much does it
// actually know?". `diff` answers "did the platform change?" - the Phase 1
// exit criterion that repeated runs on a stable host are equivalent, and the
// Risk 3 mitigation for platform behavior drift.
//
// `impact` answers the question a team actually asks when a kernel ships:
// "does any of this matter to US?". `diff` reports 138 differences between two
// measured macOS lanes; `impact` reports the two contracts whose verdict those
// 138 differences actually moved. It lives here rather than in rs-check
// because it compares ENVIRONMENTS - the same question as diff, one level up.
#include <iostream>
#include <string>
#include <vector>

#include "runtimeskeptic/core/io.hpp"
#include "runtimeskeptic/core/json.hpp"
#include "runtimeskeptic/reports/report.hpp"
#include "runtimeskeptic/version.hpp"
#include "runtimeskeptic/vm/impact.hpp"
#include "runtimeskeptic/vm/profile.hpp"

namespace {

using rs::json::Value;

void print_usage() {
    std::cout <<
        R"(rs-profile - inspect and compare environment profiles

USAGE
  rs-profile verify PROFILE.json
  rs-profile diff   A.json B.json
  rs-profile impact OLD.json NEW.json CONTRACT.json...
  rs-profile id     PROFILE.json

IMPACT OPTIONS
  --format text|json   default text
  --no-unknowns        suppress informational findings for unknown facts.
                       Changes which finding IDS are reported, never a verdict.

EXIT CODES
  verify : 0 valid, 65 invalid
  diff   : 0 identical facts, 1 facts differ, 65 invalid input
  impact : 0 no contract regressed, 1 at least one regressed,
           65 invalid input or an unreadable contract
  id     : 0 success, 65 invalid

WHAT IMPACT REPORTS
  A contract whose verdict was UNKNOWN on both profiles is listed as NEVER
  ANSWERED, not as unchanged. It has not stayed the same - nobody has ever
  been able to answer it, and counting it as unchanged turns a hole into
  reassurance.
)";
}

std::optional<Value> load_json(const std::string& path) {
    std::string error;
    auto text = rs::io::read_file(path, error);
    if (!text) {
        std::cerr << "rs-profile: " << error << "\n";
        return std::nullopt;
    }
    auto parsed = rs::json::parse(*text);
    if (!parsed.ok()) {
        std::cerr << "rs-profile: '" << path
                  << "' is not valid JSON: " << parsed.error->to_string() << "\n";
        return std::nullopt;
    }
    return parsed.value;
}

// Recursively reports every path at which two documents differ.
void diff_values(const Value& a, const Value& b, const std::string& path,
                 std::vector<std::string>& out) {
    if (a.is_object() && b.is_object()) {
        const auto& oa = a.as_object();
        const auto& ob = b.as_object();
        auto ia = oa.begin();
        auto ib = ob.begin();
        while (ia != oa.end() || ib != ob.end()) {
            if (ib == ob.end() || (ia != oa.end() && ia->first < ib->first)) {
                out.push_back(path + "/" + ia->first + ": only in A");
                ++ia;
            } else if (ia == oa.end() || ib->first < ia->first) {
                out.push_back(path + "/" + ib->first + ": only in B");
                ++ib;
            } else {
                diff_values(ia->second, ib->second, path + "/" + ia->first, out);
                ++ia;
                ++ib;
            }
        }
        return;
    }
    if (a.is_array() && b.is_array()) {
        const auto& aa = a.as_array();
        const auto& ab = b.as_array();
        const std::size_t n = aa.size() < ab.size() ? aa.size() : ab.size();
        for (std::size_t i = 0; i < n; ++i) {
            diff_values(aa[i], ab[i], path + "/" + std::to_string(i), out);
        }
        for (std::size_t i = n; i < aa.size(); ++i) {
            out.push_back(path + "/" + std::to_string(i) + ": only in A");
        }
        for (std::size_t i = n; i < ab.size(); ++i) {
            out.push_back(path + "/" + std::to_string(i) + ": only in B");
        }
        return;
    }
    if (!(a == b)) {
        auto render = [](const Value& v) {
            auto canonical = rs::json::serialize_canonical(v);
            return canonical ? *canonical : std::string("(uncanonicalizable)");
        };
        out.push_back(path + ": " + render(a) + " -> " + render(b));
    }
}

// Counts how many Fact<T> nodes in the document are unknown. A profile is
// only as useful as the number of questions it can answer.
void count_facts(const Value& v, std::size_t& total, std::size_t& unknown) {
    if (v.is_object()) {
        const Value* evidence = v.find("evidence");
        const bool is_fact = evidence != nullptr && evidence->is_string() &&
                             v.contains("value");
        if (is_fact) {
            ++total;
            if (evidence->as_string() == "unknown") ++unknown;
        }
        for (const auto& [key, child] : v.as_object()) {
            (void)key;
            count_facts(child, total, unknown);
        }
        return;
    }
    if (v.is_array()) {
        for (const auto& child : v.as_array()) count_facts(child, total, unknown);
    }
}

int command_verify(const std::string& path) {
    auto document = load_json(path);
    if (!document) return rs::reports::exit_code::kInput;

    std::string error;
    auto profile = rs::vm::EnvironmentProfile::from_json(*document, error);
    if (!profile) {
        std::cerr << "rs-profile: invalid profile: " << error << "\n";
        return rs::reports::exit_code::kInput;
    }

    // Round-trip: parse -> serialize -> parse -> serialize must be stable.
    // This is what makes profile_id meaningful.
    const Value rebuilt = profile->to_json();
    auto first = rs::json::serialize_canonical(rebuilt);
    if (!first) {
        std::cerr << "rs-profile: profile has no canonical form\n";
        return rs::reports::exit_code::kInput;
    }
    auto reparsed = rs::json::parse(*first);
    auto second = reparsed.ok() ? rs::json::serialize_canonical(*reparsed.value)
                                : std::nullopt;
    const bool stable = second.has_value() && *first == *second;

    std::size_t total = 0;
    std::size_t unknown = 0;
    count_facts(profile->facts_json(), total, unknown);

    std::cout << "profile        : "
              << (profile->profile_name.empty() ? "(unnamed)"
                                                : profile->profile_name)
              << "\n";
    std::cout << "schema         : " << profile->schema << "\n";
    std::cout << "origin         : " << rs::vm::to_string(profile->origin) << "\n";
    std::cout << "platform       : " << rs::vm::to_string(profile->platform.os)
              << " / " << rs::vm::to_string(profile->platform.process_arch)
              << " / translation "
              << rs::vm::to_string(profile->platform.translation_mode) << "\n";
    std::cout << "profile_id     : " << profile->profile_id() << "\n";
    std::cout << "canonical form : " << (stable ? "stable" : "NOT STABLE") << "\n";
    std::cout << "facts known    : " << (total - unknown) << " of " << total
              << "\n";
    std::cout << "unavailable ranges: " << profile->vm.unavailable_ranges.size()
              << "\n";
    std::cout << "available ranges  : " << profile->vm.available_ranges.size()
              << "\n";

    if (profile->origin != rs::vm::ProfileOrigin::Measured) {
        std::cout << "\nNOTE: origin is '" << rs::vm::to_string(profile->origin)
                  << "'. Findings derived from this profile are not evidence "
                     "about a real host.\n";
    }
    if (unknown > 0) {
        std::cout << "\n" << unknown
                  << " fact(s) are unknown. rs-check will answer UNKNOWN for "
                     "any request that depends on them, which is the correct "
                     "answer, not a bug.\n";
    }
    for (const auto& warning : profile->run.warnings) {
        std::cout << "warning: " << warning << "\n";
    }

    return stable ? rs::reports::exit_code::kSupported
                  : rs::reports::exit_code::kInput;
}

int command_diff(const std::string& path_a, const std::string& path_b) {
    auto doc_a = load_json(path_a);
    auto doc_b = load_json(path_b);
    if (!doc_a || !doc_b) return rs::reports::exit_code::kInput;

    std::string error;
    auto a = rs::vm::EnvironmentProfile::from_json(*doc_a, error);
    if (!a) {
        std::cerr << "rs-profile: '" << path_a << "' is invalid: " << error << "\n";
        return rs::reports::exit_code::kInput;
    }
    auto b = rs::vm::EnvironmentProfile::from_json(*doc_b, error);
    if (!b) {
        std::cerr << "rs-profile: '" << path_b << "' is invalid: " << error << "\n";
        return rs::reports::exit_code::kInput;
    }

    std::vector<std::string> differences;
    // Only the fact subtree is compared. Timestamps and run ids differ on
    // every run by construction and say nothing about the platform.
    diff_values(a->facts_json(), b->facts_json(), "", differences);

    std::cout << "A: " << path_a << "  " << a->profile_id() << "\n";
    std::cout << "B: " << path_b << "  " << b->profile_id() << "\n\n";

    if (differences.empty()) {
        std::cout << "Facts are identical. The platform behaviour these two "
                     "profiles describe is the same.\n";
        return rs::reports::exit_code::kSupported;
    }

    std::cout << differences.size() << " difference(s):\n";
    for (const auto& d : differences) std::cout << "  " << d << "\n";
    return rs::reports::exit_code::kUnsupported;
}

int command_id(const std::string& path) {
    auto document = load_json(path);
    if (!document) return rs::reports::exit_code::kInput;
    std::string error;
    auto profile = rs::vm::EnvironmentProfile::from_json(*document, error);
    if (!profile) {
        std::cerr << "rs-profile: invalid profile: " << error << "\n";
        return rs::reports::exit_code::kInput;
    }
    std::cout << profile->profile_id() << "\n";
    return rs::reports::exit_code::kSupported;
}

std::optional<rs::vm::EnvironmentProfile> load_profile(const std::string& path) {
    auto document = load_json(path);
    if (!document) return std::nullopt;
    std::string error;
    auto profile = rs::vm::EnvironmentProfile::from_json(*document, error);
    if (!profile) {
        std::cerr << "rs-profile: '" << path << "' is not a valid profile: "
                  << error << "\n";
        return std::nullopt;
    }
    return profile;
}

int command_impact(const std::vector<std::string>& args) {
    std::string format = "text";
    rs::vm::AnalysisOptions options;
    std::vector<std::string> positional;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--no-unknowns") {
            options.report_unknowns = false;
        } else if (a == "--format" && i + 1 < args.size()) {
            format = args[++i];
        } else if (!a.empty() && a[0] == '-') {
            std::cerr << "rs-profile: unknown option '" << a << "'\n";
            return rs::reports::exit_code::kUsage;
        } else {
            positional.push_back(a);
        }
    }
    if (positional.size() < 3) {
        std::cerr << "rs-profile: impact needs OLD.json NEW.json and at least "
                     "one contract\n";
        return rs::reports::exit_code::kUsage;
    }
    if (format != "text" && format != "json") {
        std::cerr << "rs-profile: unknown format '" << format << "'\n";
        return rs::reports::exit_code::kUsage;
    }

    auto before = load_profile(positional[0]);
    if (!before) return rs::reports::exit_code::kInput;
    auto after = load_profile(positional[1]);
    if (!after) return rs::reports::exit_code::kInput;

    const std::vector<std::string> contracts(positional.begin() + 2,
                                             positional.end());
    const rs::vm::ImpactReport report =
        rs::vm::compare_contracts(contracts, *before, *after, options);

    if (format == "json") {
        std::cout << rs::json::serialize_pretty(report.to_json()) << "\n";
    } else {
        std::cout << "before : "
                  << (before->profile_name.empty() ? "(unnamed)"
                                                   : before->profile_name)
                  << "  " << report.before_profile_id.substr(0, 12) << "\n";
        std::cout << "after  : "
                  << (after->profile_name.empty() ? "(unnamed)"
                                                  : after->profile_name)
                  << "  " << report.after_profile_id.substr(0, 12) << "\n\n";

        if (report.before_profile_id == report.after_profile_id) {
            std::cout << "The two profiles have the same profile_id: every "
                         "fact is identical, so no\nverdict can differ. "
                         "Nothing below is a measurement of anything.\n\n";
        }

        // Regressions first, then improvements, then the holes. Unchanged
        // contracts are counted and not listed - that is the one class where
        // silence is the right output.
        for (auto want : {rs::vm::VerdictChange::Regressed,
                          rs::vm::VerdictChange::Improved,
                          rs::vm::VerdictChange::NeverAnswered}) {
            for (const auto& c : report.contracts) {
                if (c.change != want) continue;
                std::cout << rs::vm::to_string(c.change) << "  " << c.path
                          << "\n";
                for (const auto& r : c.requirements) {
                    if (r.change == rs::vm::VerdictChange::Unchanged) continue;
                    std::cout << "    " << rs::to_string(r.before) << " -> "
                              << rs::to_string(r.after) << "   "
                              << (r.requirement_name.empty()
                                      ? "(unnamed requirement)"
                                      : r.requirement_name)
                              << "\n";
                    if (!r.ids_appeared.empty()) {
                        std::cout << "      findings appeared    :";
                        for (const auto& id : r.ids_appeared)
                            std::cout << " " << id;
                        std::cout << "\n";
                    }
                    if (!r.ids_disappeared.empty()) {
                        std::cout << "      findings disappeared :";
                        for (const auto& id : r.ids_disappeared)
                            std::cout << " " << id;
                        std::cout << "\n";
                    }
                }
            }
        }

        for (const auto& u : report.unreadable) {
            std::cout << "UNREADABLE  " << u << "\n";
        }

        std::cout << "\n" << report.contracts.size() << " contract(s): "
                  << report.count(rs::vm::VerdictChange::Regressed)
                  << " regressed, "
                  << report.count(rs::vm::VerdictChange::Improved)
                  << " improved, "
                  << report.count(rs::vm::VerdictChange::NeverAnswered)
                  << " never answered, "
                  << report.count(rs::vm::VerdictChange::Unchanged)
                  << " unchanged";
        if (!report.unreadable.empty()) {
            std::cout << ", " << report.unreadable.size() << " unreadable";
        }
        std::cout << "\n";
    }

    // An unreadable contract is an input error, not a clean run. A pipeline
    // that treats "the file did not parse" as "nothing regressed" is exactly
    // the failure this tool exists to prevent one level up.
    if (!report.unreadable.empty()) return rs::reports::exit_code::kInput;
    return report.any_regression() ? rs::reports::exit_code::kUnsupported
                                   : rs::reports::exit_code::kSupported;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return rs::reports::exit_code::kUsage;
    }
    const std::string command = argv[1];
    if (command == "-h" || command == "--help" || command == "help") {
        print_usage();
        return rs::reports::exit_code::kSupported;
    }
    if (command == "verify" && argc == 3) return command_verify(argv[2]);
    if (command == "diff" && argc == 4) return command_diff(argv[2], argv[3]);
    if (command == "id" && argc == 3) return command_id(argv[2]);
    if (command == "impact") {
        return command_impact(std::vector<std::string>(argv + 2, argv + argc));
    }

    print_usage();
    return rs::reports::exit_code::kUsage;
}

// SPDX-License-Identifier: Apache-2.0
#include "runtimeskeptic/reports/report.hpp"

#include <sstream>
#include <string>

namespace rs::reports {
namespace {

struct Palette {
    const char* reset = "";
    const char* bold = "";
    const char* dim = "";
    const char* red = "";
    const char* yellow = "";
    const char* green = "";
    const char* blue = "";
};

Palette palette_for(bool color) {
    if (!color) return Palette{};
    Palette p;
    p.reset = "\033[0m";
    p.bold = "\033[1m";
    p.dim = "\033[2m";
    p.red = "\033[31m";
    p.yellow = "\033[33m";
    p.green = "\033[32m";
    p.blue = "\033[34m";
    return p;
}

const char* verdict_color(const Palette& p, SupportLevel level) {
    switch (level) {
        case SupportLevel::Supported: return p.green;
        case SupportLevel::ConditionallySupported: return p.yellow;
        case SupportLevel::Unknown: return p.blue;
        case SupportLevel::Unsupported: return p.red;
    }
    return p.reset;
}

const char* severity_color(const Palette& p, Severity s) {
    switch (s) {
        case Severity::Critical:
        case Severity::High: return p.red;
        case Severity::Medium: return p.yellow;
        case Severity::Low:
        case Severity::Info: return p.dim;
    }
    return p.reset;
}

std::string upper(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

// Wraps at `width` on spaces, indenting continuation lines by `indent`.
std::string wrap(const std::string& text, std::size_t width,
                 const std::string& indent) {
    if (text.empty()) return text;
    std::ostringstream out;
    std::size_t line_len = indent.size();
    bool first_word = true;
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t end = text.find(' ', i);
        if (end == std::string::npos) end = text.size();
        const std::string word = text.substr(i, end - i);
        if (!first_word && line_len + 1 + word.size() > width) {
            out << "\n" << indent;
            line_len = indent.size();
            first_word = true;
        }
        if (!first_word) {
            out << ' ';
            ++line_len;
        }
        out << word;
        line_len += word.size();
        first_word = false;
        i = end + 1;
    }
    return out.str();
}

void field(std::ostringstream& out, const Palette& p, const char* label,
           const std::string& value) {
    if (value.empty()) return;
    out << "  " << p.dim << label << p.reset << "\n";
    out << "    " << wrap(value, 78, "    ") << "\n";
}

std::string profile_summary(const vm::EnvironmentProfile& profile) {
    std::string s(vm::to_string(profile.platform.os));
    if (!profile.platform.os_version.empty()) {
        s += " " + profile.platform.os_version;
    }
    s += " / process " + std::string(vm::to_string(profile.platform.process_arch));
    if (profile.platform.translation_mode != vm::TranslationMode::None &&
        profile.platform.translation_mode != vm::TranslationMode::Unknown) {
        s += " via " + std::string(vm::to_string(profile.platform.translation_mode));
    }
    return s;
}

}  // namespace

int exit_code_for(SupportLevel level) {
    switch (level) {
        case SupportLevel::Supported: return exit_code::kSupported;
        case SupportLevel::Unsupported: return exit_code::kUnsupported;
        case SupportLevel::ConditionallySupported: return exit_code::kConditional;
        case SupportLevel::Unknown: return exit_code::kUnknown;
    }
    return exit_code::kInternal;
}

std::string render_text(const vm::AnalysisResult& result,
                        const vm::Requirement& requirement,
                        const vm::EnvironmentProfile& profile,
                        const Options& options) {
    const Palette p = palette_for(options.color);
    std::ostringstream out;

    out << p.bold << "RuntimeSkeptic - virtual memory compatibility" << p.reset
        << "\n\n";

    out << "  requirement : "
        << (requirement.name.empty() ? "(unnamed)" : requirement.name) << "\n";
    out << "  host profile: "
        << (profile.profile_name.empty() ? "(unnamed)" : profile.profile_name)
        << "  [" << profile_summary(profile) << "]\n";
    out << "  origin      : " << vm::to_string(profile.origin);
    if (profile.origin != vm::ProfileOrigin::Measured) {
        out << p.yellow << "  (not measured on a real host)" << p.reset;
    }
    out << "\n\n";

    out << "  " << p.bold << verdict_color(p, result.overall)
        << upper(rs::to_string(result.overall)) << p.reset << "\n\n";

    if (result.findings.empty()) {
        out << "  No findings.\n";
    }

    for (const auto& f : result.findings) {
        if (!options.include_info && f.severity == Severity::Info) continue;

        out << severity_color(p, f.severity) << p.bold << f.id << p.reset << "  "
            << f.title << "\n";
        out << "  " << p.dim << "severity " << rs::to_string(f.severity)
            << "  |  confidence " << p.reset << severity_color(p, f.severity)
            << rs::to_string(f.confidence) << p.reset << p.dim << "  |  impact "
            << rs::to_string(f.support_impact) << p.reset << "\n\n";

        field(out, p, "Required", f.required);
        field(out, p, "Host capability", f.host_capability);
        if (!f.adapter_capability.empty()) {
            field(out, p, "Adapter capability", f.adapter_capability);
        }
        field(out, p, "Modeled outcome", f.modeled_fallback);
        field(out, p, "Failure sink", f.failure_sink);
        field(out, p, "Conclusion", f.conclusion);

        if (options.verbose && !f.evidence.empty()) {
            out << "  " << p.dim << "Evidence chain" << p.reset << "\n";
            for (const auto& step : f.evidence.steps()) {
                out << "    " << p.dim << "[" << rs::to_string(step.layer) << "]"
                    << p.reset << " " << step.claim << "\n";
                out << "      " << p.dim << rs::to_string(step.evidence);
                if (!step.source.empty()) out << " <- " << step.source;
                out << p.reset << "\n";
                if (!step.detail.empty()) {
                    out << "      " << p.dim << step.detail << p.reset << "\n";
                }
            }
        }

        if (options.verbose && !f.remediations.empty()) {
            out << "  " << p.dim << "Remediation classes" << p.reset << "\n";
            for (const auto& r : f.remediations) {
                out << "    - " << vm::to_string(r.kind) << "\n";
                out << "      " << wrap(std::string(vm::describe(r.kind)), 78,
                                        "      ")
                    << "\n";
                if (!r.detail.empty()) {
                    out << "      " << wrap(r.detail, 78, "      ") << "\n";
                }
            }
        }

        if (options.verbose && !f.rejected_fixes.empty()) {
            out << "  " << p.dim << "Will not work" << p.reset << "\n";
            for (const auto& r : f.rejected_fixes) {
                out << "    x " << r.proposal << "\n";
                out << "      " << wrap(r.reason, 78, "      ") << "\n";
            }
        }

        if (!f.source_locations.empty()) {
            out << "  " << p.dim << "Source" << p.reset << "\n";
            for (const auto& l : f.source_locations) {
                out << "    " << l.to_string() << "\n";
            }
        }
        out << "\n";
    }

    if (options.verbose && !result.satisfied.empty()) {
        out << "  " << p.dim << "Checked and satisfied" << p.reset << "\n";
        for (const auto& c : result.satisfied) {
            out << "    " << p.green << "ok" << p.reset << "  " << c.constraint
                << "\n";
            out << "        " << p.dim << c.host_fact << "  ["
                << rs::to_string(c.evidence) << "]" << p.reset << "\n";
        }
        out << "\n";
    }

    if (!result.analyzer_limitations.empty()) {
        out << p.yellow << "  Analyzer limitations" << p.reset << "\n";
        for (const auto& l : result.analyzer_limitations) {
            out << "    " << wrap("- " + l, 78, "    ") << "\n";
        }
        out << "\n";
    }

    out << p.dim << "  profile_id     " << result.profile_id << "\n";
    out << "  requirement_id " << result.requirement_id << p.reset << "\n";
    return out.str();
}

std::string render_markdown(const vm::AnalysisResult& result,
                            const vm::Requirement& requirement,
                            const vm::EnvironmentProfile& profile,
                            const Options& options) {
    std::ostringstream out;

    out << "# RuntimeSkeptic compatibility report\n\n";
    out << "**Verdict:** `" << upper(rs::to_string(result.overall)) << "`\n\n";

    out << "| | |\n|---|---|\n";
    out << "| Requirement | "
        << (requirement.name.empty() ? "(unnamed)" : requirement.name) << " |\n";
    out << "| Component | "
        << (requirement.component.empty() ? "-" : requirement.component) << " |\n";
    out << "| Host profile | "
        << (profile.profile_name.empty() ? "(unnamed)" : profile.profile_name)
        << " |\n";
    out << "| Platform | " << profile_summary(profile) << " |\n";
    out << "| Profile origin | `" << vm::to_string(profile.origin) << "` |\n";
    out << "| profile_id | `" << result.profile_id << "` |\n";
    out << "| requirement_id | `" << result.requirement_id << "` |\n\n";

    if (profile.origin != vm::ProfileOrigin::Measured) {
        out << "> **This verdict did not come from your machine.** The profile "
               "origin is `"
            << vm::to_string(profile.origin)
            << "`. Run `rs-env-probe vm` on the target host and re-run the "
               "analysis before acting on it.\n\n";
    }

    if (result.findings.empty()) {
        out << "No findings.\n";
    } else {
        out << "## Findings\n\n";
        out << "| ID | Severity | Confidence | Title |\n";
        out << "|---|---|---|---|\n";
        for (const auto& f : result.findings) {
            if (!options.include_info && f.severity == Severity::Info) continue;
            out << "| `" << f.id << "` | " << rs::to_string(f.severity) << " | `"
                << rs::to_string(f.confidence) << "` | " << f.title << " |\n";
        }
        out << "\n";
    }

    for (const auto& f : result.findings) {
        if (!options.include_info && f.severity == Severity::Info) continue;

        out << "### " << f.id << " - " << f.title << "\n\n";
        out << "- **Severity:** " << rs::to_string(f.severity) << "\n";
        out << "- **Confidence:** `" << rs::to_string(f.confidence) << "`\n";
        out << "- **Support impact:** `" << rs::to_string(f.support_impact)
            << "`\n\n";

        auto section = [&](const char* label, const std::string& value) {
            if (value.empty()) return;
            out << "**" << label << "**\n\n" << value << "\n\n";
        };
        section("Required", f.required);
        section("Host capability", f.host_capability);
        section("Adapter capability", f.adapter_capability);
        section("Modeled outcome", f.modeled_fallback);
        section("Failure sink", f.failure_sink);
        section("Conclusion", f.conclusion);

        if (!f.evidence.empty()) {
            out << "**Evidence chain**\n\n";
            out << "| Layer | Evidence | Claim | Source |\n|---|---|---|---|\n";
            for (const auto& step : f.evidence.steps()) {
                out << "| `" << rs::to_string(step.layer) << "` | `"
                    << rs::to_string(step.evidence) << "` | " << step.claim
                    << " | " << (step.source.empty() ? "-" : step.source)
                    << " |\n";
            }
            out << "\n";
        }

        if (!f.remediations.empty()) {
            out << "**Remediation classes**\n\n";
            for (const auto& r : f.remediations) {
                out << "- `" << vm::to_string(r.kind) << "` - "
                    << vm::describe(r.kind);
                if (!r.detail.empty()) out << " (" << r.detail << ")";
                out << "\n";
            }
            out << "\n";
        }

        if (!f.rejected_fixes.empty()) {
            out << "**Fixes that will not work**\n\n";
            for (const auto& r : f.rejected_fixes) {
                out << "- ~~" << r.proposal << "~~ - " << r.reason << "\n";
            }
            out << "\n";
        }

        if (!f.source_locations.empty()) {
            out << "**Source locations**\n\n";
            for (const auto& l : f.source_locations) {
                out << "- `" << l.to_string() << "`\n";
            }
            out << "\n";
        }
    }

    if (!result.satisfied.empty()) {
        out << "## Checked and satisfied\n\n";
        out << "A quiet report is ambiguous unless it says what it looked at.\n\n";
        out << "| Constraint | Host fact | Evidence |\n|---|---|---|\n";
        for (const auto& c : result.satisfied) {
            out << "| " << c.constraint << " | " << c.host_fact << " | `"
                << rs::to_string(c.evidence) << "` |\n";
        }
        out << "\n";
    }

    if (!result.analyzer_limitations.empty()) {
        out << "## Analyzer limitations\n\n";
        for (const auto& l : result.analyzer_limitations) {
            out << "- " << l << "\n";
        }
        out << "\n";
    }

    return out.str();
}

}  // namespace rs::reports

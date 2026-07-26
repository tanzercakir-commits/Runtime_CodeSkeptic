// SPDX-License-Identifier: Apache-2.0
#include "runtimeskeptic/vm/impact.hpp"

#include <algorithm>
#include <iterator>   // back_inserter: libstdc++ and libc++ hand it over
                      // transitively, MSVC does not. See tools/guards/check_includes.py
#include <set>

#include "runtimeskeptic/core/io.hpp"

namespace rs::vm {

std::string_view to_string(VerdictChange c) {
    switch (c) {
        case VerdictChange::Regressed:     return "REGRESSED";
        case VerdictChange::Improved:      return "IMPROVED";
        case VerdictChange::Unchanged:     return "unchanged";
        case VerdictChange::NeverAnswered: return "NEVER ANSWERED";
    }
    return "unchanged";
}

namespace {

// Ordered by how much a human is needed, NOT by the enum's numeric value.
// A regression outranks everything; an improvement is still a change and
// still wants an eye on it; an unanswered contract outranks "unchanged"
// because it is a hole, not a result.
int attention(VerdictChange c) {
    switch (c) {
        case VerdictChange::Regressed:     return 3;
        case VerdictChange::Improved:      return 2;
        case VerdictChange::NeverAnswered: return 1;
        case VerdictChange::Unchanged:     return 0;
    }
    return 0;
}

VerdictChange classify(SupportLevel before, SupportLevel after) {
    if (before == after) {
        // The one case that must not collapse into "unchanged".
        return before == SupportLevel::Unknown ? VerdictChange::NeverAnswered
                                               : VerdictChange::Unchanged;
    }
    // SupportLevel is ordered Supported(0) < ConditionallySupported(1) <
    // Unknown(2) < Unsupported(3), and that order is the aggregation order:
    // UNKNOWN deliberately outranks CONDITIONAL, because not knowing is worse
    // than knowing about a condition.
    return static_cast<int>(after) > static_cast<int>(before)
               ? VerdictChange::Regressed
               : VerdictChange::Improved;
}

std::set<std::string> finding_ids(const AnalysisResult& r) {
    std::set<std::string> ids;
    for (const auto& f : r.findings) ids.insert(f.id);
    return ids;
}

}  // namespace

std::size_t ImpactReport::count(VerdictChange c) const {
    return static_cast<std::size_t>(
        std::count_if(contracts.begin(), contracts.end(),
                      [c](const ContractImpact& e) { return e.change == c; }));
}

ContractImpact compare_contract(const std::string& path,
                                const std::vector<Requirement>& requirements,
                                const EnvironmentProfile& before,
                                const EnvironmentProfile& after,
                                const AnalysisOptions& options) {
    ContractImpact out;
    out.path = path;
    out.requirements.reserve(requirements.size());

    for (std::size_t i = 0; i < requirements.size(); ++i) {
        const AnalysisResult a = analyze(requirements[i], before, options);
        const AnalysisResult b = analyze(requirements[i], after, options);

        RequirementImpact r;
        r.index = i;
        r.requirement_name = requirements[i].name;
        r.before = a.overall;
        r.after = b.overall;
        r.change = classify(a.overall, b.overall);

        const std::set<std::string> ids_a = finding_ids(a);
        const std::set<std::string> ids_b = finding_ids(b);
        std::set_difference(ids_b.begin(), ids_b.end(), ids_a.begin(),
                            ids_a.end(), std::back_inserter(r.ids_appeared));
        std::set_difference(ids_a.begin(), ids_a.end(), ids_b.begin(),
                            ids_b.end(), std::back_inserter(r.ids_disappeared));

        if (attention(r.change) > attention(out.change)) out.change = r.change;
        out.requirements.push_back(std::move(r));
    }

    // An empty contract cannot be unchanged, because nothing was compared.
    if (requirements.empty()) out.change = VerdictChange::NeverAnswered;
    return out;
}

ImpactReport compare_contracts(const std::vector<std::string>& paths,
                               const EnvironmentProfile& before,
                               const EnvironmentProfile& after,
                               const AnalysisOptions& options) {
    ImpactReport report;
    report.before_profile_id = before.profile_id();
    report.after_profile_id = after.profile_id();
    report.before_profile_name = before.profile_name;
    report.after_profile_name = after.profile_name;

    for (const auto& path : paths) {
        std::string error;
        auto text = io::read_file(path, error);
        if (!text) {
            report.unreadable.push_back(path + ": " + error);
            continue;
        }
        auto parsed = json::parse(*text);
        if (!parsed.ok()) {
            report.unreadable.push_back(
                path + ": not valid JSON: " + parsed.error->to_string());
            continue;
        }
        auto bundle = load_requirements(*parsed.value, error);
        if (!bundle) {
            report.unreadable.push_back(path + ": " + error);
            continue;
        }
        if (bundle->requirements.empty()) {
            report.unreadable.push_back(
                path + ": contains no usable requirements");
            continue;
        }
        report.contracts.push_back(compare_contract(
            path, bundle->requirements, before, after, options));
    }
    return report;
}

json::Value RequirementImpactToJson(const RequirementImpact& r) {
    json::Value v = json::Value::object();
    v["index"] = static_cast<unsigned long long>(r.index);
    v["requirement"] = r.requirement_name;
    v["before"] = std::string(rs::to_string(r.before));
    v["after"] = std::string(rs::to_string(r.after));
    v["change"] = std::string(to_string(r.change));
    json::Value appeared = json::Value::array();
    for (const auto& id : r.ids_appeared) appeared.push_back(json::Value(id));
    json::Value gone = json::Value::array();
    for (const auto& id : r.ids_disappeared) gone.push_back(json::Value(id));
    v["findings_appeared"] = appeared;
    v["findings_disappeared"] = gone;
    return v;
}

json::Value ImpactReport::to_json() const {
    json::Value doc = json::Value::object();
    doc["schema"] = std::string("runtime-skeptic.profile-impact.v1");

    json::Value profiles = json::Value::object();
    profiles["before_id"] = before_profile_id;
    profiles["after_id"] = after_profile_id;
    profiles["before_name"] = before_profile_name;
    profiles["after_name"] = after_profile_name;
    doc["profiles"] = profiles;

    json::Value summary = json::Value::object();
    summary["contracts"] = static_cast<unsigned long long>(contracts.size());
    summary["regressed"] =
        static_cast<unsigned long long>(count(VerdictChange::Regressed));
    summary["improved"] =
        static_cast<unsigned long long>(count(VerdictChange::Improved));
    summary["never_answered"] =
        static_cast<unsigned long long>(count(VerdictChange::NeverAnswered));
    summary["unchanged"] =
        static_cast<unsigned long long>(count(VerdictChange::Unchanged));
    summary["unreadable"] =
        static_cast<unsigned long long>(unreadable.size());
    doc["summary"] = summary;

    json::Value entries = json::Value::array();
    for (const auto& c : contracts) {
        json::Value e = json::Value::object();
        e["contract"] = c.path;
        e["change"] = std::string(to_string(c.change));
        json::Value reqs = json::Value::array();
        for (const auto& r : c.requirements) {
            reqs.push_back(RequirementImpactToJson(r));
        }
        e["requirements"] = reqs;
        entries.push_back(e);
    }
    doc["contracts"] = entries;

    json::Value bad = json::Value::array();
    for (const auto& u : unreadable) bad.push_back(json::Value(u));
    doc["unreadable"] = bad;
    return doc;
}

}  // namespace rs::vm

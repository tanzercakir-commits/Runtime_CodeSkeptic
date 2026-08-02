// SPDX-License-Identifier: Apache-2.0
#include "runtimeskeptic/reports/bundle.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "runtimeskeptic/core/io.hpp"
#include "runtimeskeptic/core/json.hpp"
#include "runtimeskeptic/core/sha256.hpp"
#include "runtimeskeptic/reports/report.hpp"
#include "runtimeskeptic/version.hpp"
#include "runtimeskeptic/vm/profile.hpp"
#include "runtimeskeptic/vm/requirement.hpp"

namespace rs::bundle {
namespace {

// The bundle's file names, in one place. Names appear in the manifest, in
// hashes.txt, and in the replay reader; three literals is three chances to drift.
constexpr const char* kManifest = "manifest.json";
constexpr const char* kProfileFile = "environment_profile.json";
constexpr const char* kRequirementFile = "application_requirements.json";
constexpr const char* kFindingsFile = "findings.json";
constexpr const char* kReportFile = "report.md";
constexpr const char* kHashesFile = "hashes.txt";

std::string join(const std::string& dir, const char* name) {
    if (dir.empty()) return name;
    return dir.back() == '/' ? dir + name : dir + "/" + name;
}

std::string hex_of(const std::string& text) {
    hash::Sha256 h;
    h.update(text);
    return h.hex_digest();
}

// One analysis, run from two input documents. Everything write and replay both
// need. Returns false and sets `error` on a parse or schema failure - the same
// failures rs-check reports, because it is the same path.
struct Run {
    std::vector<vm::AnalysisResult> results;
    SupportLevel overall = SupportLevel::Supported;
    vm::RequirementBundle bundle;
    vm::EnvironmentProfile profile;
    std::vector<vm::Requirement> requirements;   // parallel to results
};

bool run_analysis(const std::string& requirement_text,
                  const std::string& profile_text,
                  const vm::AnalysisOptions& options, Run& out,
                  std::string& error) {
    auto req_parsed = json::parse(requirement_text);
    if (!req_parsed.ok()) {
        error = "requirement is not valid JSON: " + req_parsed.error->to_string();
        return false;
    }
    auto prof_parsed = json::parse(profile_text);
    if (!prof_parsed.ok()) {
        error = "profile is not valid JSON: " + prof_parsed.error->to_string();
        return false;
    }

    auto bundle = vm::load_requirements(*req_parsed.value, error);
    if (!bundle) return false;
    if (bundle->requirements.empty()) {
        error = "the requirement document contains no usable requirements";
        return false;
    }
    auto profile = vm::EnvironmentProfile::from_json(*prof_parsed.value, error);
    if (!profile) return false;

    out.bundle = std::move(*bundle);
    out.profile = std::move(*profile);
    out.overall = SupportLevel::Supported;
    for (const auto& requirement : out.bundle.requirements) {
        vm::AnalysisResult result =
            vm::analyze(requirement, out.profile, options);
        out.overall = combine(out.overall, result.overall);
        out.results.push_back(std::move(result));
        out.requirements.push_back(requirement);
    }
    return true;
}

// The finding IDs across a run, sorted and de-duplicated. Sorted because the
// replay compares two of these for equality and the analyzer's emission order is
// not part of the verdict; de-duplicated because a bundle asserting one contract
// twice must not read as two.
std::vector<std::string> finding_ids(const Run& run) {
    std::set<std::string> ids;
    for (const auto& result : run.results) {
        for (const auto& f : result.findings) {
            if (!f.id.empty()) ids.insert(f.id);
        }
    }
    return {ids.begin(), ids.end()};
}

json::Value id_array(const std::vector<std::string>& ids) {
    json::Value a = json::Value::array();
    for (const auto& id : ids) a.push_back(json::Value(id));
    return a;
}

std::string render_report(const Run& run, const reports::Options& ro) {
    std::string out;
    for (std::size_t i = 0; i < run.results.size(); ++i) {
        if (!out.empty()) out += "\n---\n\n";
        out += reports::render_markdown(run.results[i], run.requirements[i],
                                        run.profile, ro);
    }
    return out;
}

}  // namespace

bool write_bundle(const std::string& dir, const Inputs& inputs,
                  const vm::AnalysisOptions& options, std::string& error,
                  ReplayOutcome* replay_out) {
    Run run;
    if (!run_analysis(inputs.requirement_text, inputs.profile_text, options, run,
                      error)) {
        return false;
    }

    if (!io::make_directories(dir, error)) return false;

    // The two inputs go in VERBATIM. A re-serialised profile could differ from
    // the bytes the host produced, and then the bundle would record something
    // that was never run.
    const std::string findings =
        reports::render_run_json(run.results, run.overall, run.bundle);
    reports::Options ro;
    const std::string report = render_report(run, ro);

    struct FileOut {
        const char* name;
        const std::string* text;
    };
    const std::vector<FileOut> files = {
        {kProfileFile, &inputs.profile_text},
        {kRequirementFile, &inputs.requirement_text},
        {kFindingsFile, &findings},
        {kReportFile, &report},
    };

    std::string hashes_txt;
    json::Value input_hashes = json::Value::object();
    for (const auto& f : files) {
        if (!io::write_file(join(dir, f.name), *f.text, error)) return false;
        const std::string h = hex_of(*f.text);
        // sha256sum(1) format: "<hex>  <name>", two spaces, so `sha256sum -c
        // hashes.txt` works on the bundle with no bespoke verifier.
        hashes_txt += h + "  " + f.name + "\n";
        input_hashes[f.name] = h;
    }
    if (!io::write_file(join(dir, kHashesFile), hashes_txt, error)) return false;

    // SELF-CERTIFY. Re-run from the FILES ON DISK, not the in-memory Run, so the
    // thing certified is the bundle and not the process that made it. A canonical
    // round-trip that lost a fact would show up here as `diverged`, which is the
    // whole reason to replay at write time rather than assert reproducibility.
    ReplayOutcome self;
    self.recorded_overall = std::string(to_string(run.overall));
    self.recorded_finding_ids = finding_ids(run);
    {
        auto rt_profile = io::read_file(join(dir, kProfileFile), error);
        auto rt_requirement = io::read_file(join(dir, kRequirementFile), error);
        Run replay_run;
        if (rt_profile && rt_requirement &&
            run_analysis(*rt_requirement, *rt_profile, options, replay_run,
                         error)) {
            self.replayed_overall = std::string(to_string(replay_run.overall));
            self.replayed_finding_ids = finding_ids(replay_run);
            self.reproduced =
                self.replayed_overall == self.recorded_overall &&
                self.replayed_finding_ids == self.recorded_finding_ids;
            self.detail = self.reproduced
                              ? "re-running the analysis from the written bundle "
                                "reproduced the verdict and every finding ID"
                              : "the written bundle did not reproduce its own "
                                "verdict; a fact was lost writing or reading it";
        } else {
            self.detail = "the written bundle could not be re-analysed: " + error;
        }
        error.clear();
    }

    // -- the manifest -------------------------------------------------------
    const auto& plat = run.profile.platform;
    json::Value manifest = json::Value::object();
    manifest["schema"] = std::string("runtime-skeptic.analysis-bundle.v1");
    manifest["tool_version"] = std::string(kToolVersion);

    json::Value schemas = json::Value::object();
    schemas["profile"] = std::string(kProfileSchemaVersion);
    schemas["requirement"] = std::string(kRequirementSchemaVersion);
    schemas["result"] = std::string(kResultSchemaVersion);
    manifest["schema_versions"] = schemas;

    // Host identity at an appropriate privacy level: the coarse key that changes
    // the ANSWER - os, the arch the process runs as, whether a translator is in
    // the way - and nothing that merely names the machine. The same three fields
    // tools/campaign/host_key.py keys expectations on.
    json::Value host = json::Value::object();
    host["os"] = std::string(to_string(plat.os));
    host["process_arch"] = std::string(to_string(plat.process_arch));
    host["translation_mode"] = std::string(to_string(plat.translation_mode));
    host["profile_id"] = run.profile.profile_id();
    host["profile_name"] = run.profile.profile_name;
    host["profile_origin"] = std::string(to_string(run.profile.origin));
    manifest["host"] = host;
    manifest["process_architecture"] = std::string(to_string(plat.process_arch));

    json::Value opts = json::Value::object();
    opts["report_unknowns"] = options.report_unknowns;
    manifest["analysis_options"] = opts;

    json::Value in = json::Value::object();
    json::Value in_req = json::Value::object();
    in_req["file"] = std::string(kRequirementFile);
    in_req["sha256"] = *input_hashes.find(kRequirementFile);
    if (!inputs.requirement_source.empty()) {
        in_req["source"] = inputs.requirement_source;
    }
    in["requirement"] = in_req;
    json::Value in_prof = json::Value::object();
    in_prof["file"] = std::string(kProfileFile);
    in_prof["sha256"] = *input_hashes.find(kProfileFile);
    in_prof["profile_id"] = run.profile.profile_id();
    if (!inputs.profile_source.empty()) {
        in_prof["source"] = inputs.profile_source;
    }
    in["profile"] = in_prof;
    manifest["inputs"] = in;

    json::Value outs = json::Value::object();
    json::Value out_find = json::Value::object();
    out_find["file"] = std::string(kFindingsFile);
    out_find["sha256"] = *input_hashes.find(kFindingsFile);
    outs["findings"] = out_find;
    json::Value out_rep = json::Value::object();
    out_rep["file"] = std::string(kReportFile);
    out_rep["sha256"] = *input_hashes.find(kReportFile);
    outs["report"] = out_rep;
    manifest["outputs"] = outs;

    manifest["overall"] = self.recorded_overall;
    manifest["finding_ids"] = id_array(self.recorded_finding_ids);

    // §17's suggested layout also lists a static-assumptions document, a runtime
    // trace and an extracted-contracts directory. They come from producers this
    // project has not built (Phase 4 monitor, Phase 5 static extractor). Absent
    // FILES read as "there was no trace"; this reads as "this tool does not make
    // one yet", which is the true statement.
    json::Value absent = json::Value::object();
    absent["static_assumptions.json"] =
        std::string("no static contract extractor exists yet (ROADMAP Phase 5)");
    absent["runtime_trace.jsonl"] =
        std::string("no runtime monitor exists yet (ROADMAP Phase 4)");
    manifest["absent_components"] = absent;

    json::Value replay = json::Value::object();
    replay["status"] = std::string(self.reproduced ? "reproduced" : "diverged");
    replay["method"] = std::string(
        "the analysis was re-run from the written bundle files at creation "
        "time, reading only the bundle");
    replay["recorded_overall"] = self.recorded_overall;
    replay["replayed_overall"] = self.replayed_overall;
    replay["finding_ids_match"] =
        self.replayed_finding_ids == self.recorded_finding_ids;
    replay["detail"] = self.detail;
    manifest["replay"] = replay;

    if (!io::write_file(join(dir, kManifest), json::serialize_pretty(manifest),
                        error)) {
        return false;
    }

    if (replay_out) *replay_out = std::move(self);
    return true;
}

std::optional<ReplayOutcome> replay_bundle(const std::string& dir,
                                           std::string& error) {
    auto manifest_text = io::read_file(join(dir, kManifest), error);
    if (!manifest_text) return std::nullopt;
    auto manifest_parsed = json::parse(*manifest_text);
    if (!manifest_parsed.ok()) {
        error = "manifest is not valid JSON: " +
                manifest_parsed.error->to_string();
        return std::nullopt;
    }
    const json::Value& m = *manifest_parsed.value;
    if (!m.is_object() || m.find("schema") == nullptr ||
        m.find("schema")->as_string() != "runtime-skeptic.analysis-bundle.v1") {
        error = "'" + dir + "' does not contain a v1 analysis bundle manifest";
        return std::nullopt;
    }

    // analysis-bundle.v1 lists these fields as REQUIRED on the manifest. The
    // round-1 fix required only inputs/outputs, so a manifest stripped of
    // tool_version, host, analysis_options and replay still "reproduced" - the
    // tamper check never noticed fields it did not look for. Require every one
    // the schema does, so an incomplete manifest is rejected, not replayed.
    for (const char* key :
         {"tool_version", "schema_versions", "host", "process_architecture",
          "analysis_options", "inputs", "outputs", "overall", "finding_ids",
          "replay"}) {
        if (m.find(key) == nullptr) {
            error = "incomplete bundle: manifest is missing required field '" +
                    std::string(key) + "', which analysis-bundle.v1 requires";
            return std::nullopt;
        }
    }

    // A bundle is only replayable if its manifest is COMPLETE. analysis-bundle.v1
    // requires inputs{requirement,profile} and outputs{findings,report}, each
    // naming a file and its sha256 - and those hash sections are the ones the
    // tamper check runs on. A stripped manifest that omitted them was passing
    // replay vacuously: check_hash found no nodes, tampered_files stayed empty,
    // and a three-file bundle "reproduced" its own (often empty) verdict with
    // exit 0. Require them, so an incomplete bundle is rejected rather than
    // certified. (check_hash then verifies each; a named file that is missing
    // reads as tampered, which is the honest outcome for a gutted bundle.)
    auto require_hash_node = [&](const json::Value* section, const char* sname,
                                 const char* key) -> bool {
        if (section == nullptr || !section->is_object()) {
            error = "incomplete bundle: manifest has no '" + std::string(sname) +
                    "' section, which analysis-bundle.v1 requires";
            return false;
        }
        const json::Value* n = section->find(key);
        if (n == nullptr || !n->is_object() || n->find("file") == nullptr ||
            n->find("sha256") == nullptr) {
            error = "incomplete bundle: manifest '" + std::string(sname) + "." +
                    key + "' must name a file and its sha256";
            return false;
        }
        return true;
    };
    const json::Value* in_sec = m.find("inputs");
    const json::Value* out_sec = m.find("outputs");
    if (!require_hash_node(in_sec, "inputs", "requirement") ||
        !require_hash_node(in_sec, "inputs", "profile") ||
        !require_hash_node(out_sec, "outputs", "findings") ||
        !require_hash_node(out_sec, "outputs", "report")) {
        return std::nullopt;
    }

    ReplayOutcome out;
    if (const json::Value* rec = m.find("overall")) {
        out.recorded_overall = rec->as_string();
    }
    if (const json::Value* ids = m.find("finding_ids");
        ids != nullptr && ids->is_array()) {
        for (const auto& id : ids->as_array()) {
            out.recorded_finding_ids.push_back(id.as_string());
        }
    }

    // Options as the bundle recorded them: a replay that silently used defaults
    // would reproduce a DIFFERENT analysis and call the match a success.
    vm::AnalysisOptions options;
    if (const json::Value* o = m.find("analysis_options")) {
        if (const json::Value* ru = o->find("report_unknowns")) {
            options.report_unknowns = ru->as_bool();
        }
    }

    // Tamper check: every hashed file must still hash to what the manifest says.
    // A bundle edited after sealing is not the bundle whose verdict was recorded,
    // and a replay that ignored that would certify the edit.
    auto check_hash = [&](const json::Value* node) {
        if (node == nullptr) return;
        const json::Value* file = node->find("file");
        const json::Value* want = node->find("sha256");
        if (file == nullptr || want == nullptr) return;
        std::string read_error;
        auto text = io::read_file(join(dir, file->as_string().c_str()),
                                  read_error);
        if (!text || hex_of(*text) != want->as_string()) {
            out.tampered_files.push_back(file->as_string());
        }
    };
    if (const json::Value* in = m.find("inputs")) {
        check_hash(in->find("requirement"));
        check_hash(in->find("profile"));
    }
    if (const json::Value* outs = m.find("outputs")) {
        check_hash(outs->find("findings"));
        check_hash(outs->find("report"));
    }

    auto profile_text = io::read_file(join(dir, kProfileFile), error);
    auto requirement_text = io::read_file(join(dir, kRequirementFile), error);
    if (!profile_text || !requirement_text) return std::nullopt;

    Run run;
    if (!run_analysis(*requirement_text, *profile_text, options, run, error)) {
        return std::nullopt;
    }
    out.replayed_overall = std::string(to_string(run.overall));
    out.replayed_finding_ids = finding_ids(run);
    out.reproduced = out.tampered_files.empty() &&
                     out.replayed_overall == out.recorded_overall &&
                     out.replayed_finding_ids == out.recorded_finding_ids;

    if (!out.tampered_files.empty()) {
        out.detail = "the bundle was edited after it was sealed: " +
                     std::to_string(out.tampered_files.size()) +
                     " file(s) no longer match their recorded hash";
    } else if (out.reproduced) {
        out.detail = "re-running the analysis from the bundle alone reproduced "
                     "its recorded verdict and every finding ID";
    } else {
        out.detail = "the bundle did not reproduce its recorded verdict";
    }
    return out;
}

}  // namespace rs::bundle

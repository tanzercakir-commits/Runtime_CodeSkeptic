// SPDX-License-Identifier: Apache-2.0
//
// The evidence bundle (ROADMAP §17).
//
// The bundle's whole promise is that a verdict survives leaving the machine, so
// the tests are about the round trip and nothing else: a bundle that writes but
// cannot be replayed is worse than no bundle, because it looks like proof. Every
// case here writes a real directory to a temporary path and reads it back with
// the same code a third party would - there is no in-memory shortcut, because
// the shortcut is exactly what would hide a serialization bug.
#include <filesystem>
#include <fstream>
#include <string>

#include "fixtures.hpp"
#include "runtimeskeptic/core/io.hpp"
#include "runtimeskeptic/core/json.hpp"
#include "runtimeskeptic/core/sha256.hpp"
#include "runtimeskeptic/reports/bundle.hpp"
#include "runtimeskeptic/vm/analyzer.hpp"
#include "test_support.hpp"

using namespace rs;

namespace {

namespace fs = std::filesystem;

// A directory that removes itself, so a failing test cannot leave litter and a
// passing one needs no teardown. Named by the test's own line so two cases in
// one process cannot collide.
struct TempDir {
    fs::path path;
    explicit TempDir(const std::string& tag)
        : path(fs::temp_directory_path() /
               ("rs-bundle-" + tag)) {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    std::string dir() const { return path.string(); }
};

std::string to_text(const json::Value& v) { return json::serialize_pretty(v); }

// A profile and a requirement as the JSON TEXT the bundle consumes, built from
// the in-code fixtures so the test owns no brittle string literals.
bundle::Inputs inputs_that_produce_a_finding() {
    // A 32-bit pointer store against a host whose addresses run high: RS-VM-0013,
    // UNSUPPORTED, the seventh demonstration. Deterministic and profile-driven,
    // so the replay has something real to reproduce.
    vm::EnvironmentProfile profile = test::permissive_host();
    vm::Requirement requirement;
    requirement.name = "32-bit pointer store";
    requirement.operation = vm::OperationKind::VirtualMemoryMap;
    requirement.request.size = 65536;
    requirement.assumptions.pointer_storage_width_bits = 32;

    bundle::Inputs in;
    in.profile_text = to_text(profile.to_json());
    in.requirement_text = to_text(requirement.to_json());
    in.profile_source = "test:permissive_host";
    in.requirement_source = "test:32-bit-pointer-store";
    return in;
}

std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(in)),
                  std::istreambuf_iterator<char>());
    return s;
}

json::Value read_manifest(const std::string& dir) {
    auto text = read(fs::path(dir) / "manifest.json");
    return json::parse(text).value.value();
}

}  // namespace

RS_TEST(a_bundle_has_every_file_the_spec_names) {
    TempDir tmp("files");
    std::string error;
    RS_CHECK_MESSAGE(
        bundle::write_bundle(tmp.dir(), inputs_that_produce_a_finding(), {},
                             error),
        "write_bundle failed: " + error);

    for (const char* name :
         {"manifest.json", "environment_profile.json",
          "application_requirements.json", "findings.json", "report.md",
          "hashes.txt"}) {
        RS_CHECK_MESSAGE(fs::exists(tmp.path / name),
                         std::string("bundle is missing ") + name);
    }
}

RS_TEST(a_fresh_bundle_certifies_its_own_replay) {
    TempDir tmp("selfcert");
    std::string error;
    bundle::ReplayOutcome self;
    RS_CHECK(bundle::write_bundle(tmp.dir(), inputs_that_produce_a_finding(), {},
                                  error, &self));
    RS_CHECK_MESSAGE(self.reproduced,
                     "the bundle did not reproduce at write time: " + self.detail);

    // And the manifest says so, in the field a reader would check.
    const json::Value m = read_manifest(tmp.dir());
    RS_CHECK(m.find("replay")->find("status")->as_string() == "reproduced");
    RS_CHECK(m.find("overall")->as_string() == "UNSUPPORTED");
    RS_CHECK(!m.find("finding_ids")->as_array().empty());
}

RS_TEST(replay_from_the_bundle_alone_reproduces) {
    TempDir tmp("replay");
    std::string error;
    RS_CHECK(bundle::write_bundle(tmp.dir(), inputs_that_produce_a_finding(), {},
                                  error));

    auto outcome = bundle::replay_bundle(tmp.dir(), error);
    RS_CHECK_MESSAGE(outcome.has_value(), "replay could not read the bundle: " + error);
    RS_CHECK_MESSAGE(outcome->reproduced,
                     "replay from the bundle alone diverged: " + outcome->detail);
    RS_CHECK(outcome->recorded_overall == outcome->replayed_overall);
    RS_CHECK(outcome->recorded_finding_ids == outcome->replayed_finding_ids);
    RS_CHECK(outcome->tampered_files.empty());
}

RS_TEST(editing_a_stored_file_is_caught_as_tampering) {
    TempDir tmp("tamper");
    std::string error;
    RS_CHECK(bundle::write_bundle(tmp.dir(), inputs_that_produce_a_finding(), {},
                                  error));

    // Change the stored profile after sealing. The manifest still says what the
    // verdict was, and the hash no longer matches the bytes.
    {
        std::ofstream out(tmp.path / "environment_profile.json",
                          std::ios::binary | std::ios::app);
        out << "\n";   // one trailing byte is enough to change the hash
    }

    auto outcome = bundle::replay_bundle(tmp.dir(), error);
    RS_CHECK(outcome.has_value());
    RS_CHECK_MESSAGE(!outcome->reproduced,
                     "an edited bundle was accepted as reproduced");
    RS_CHECK_MESSAGE(!outcome->tampered_files.empty(),
                     "the edit was not reported as tampering");
}

RS_TEST(an_incomplete_manifest_is_rejected_not_replayed_vacuously) {
    TempDir tmp("incomplete");
    std::string error;
    RS_CHECK(bundle::write_bundle(tmp.dir(), inputs_that_produce_a_finding(), {},
                                  error));

    // Independent review 2026-08-02, A3: a manifest stripped of its inputs/
    // outputs hash sections gave the tamper check no nodes, so tampered_files
    // stayed empty and a three-file bundle "reproduced" its own verdict with
    // exit 0. Overwrite the manifest with exactly the reviewer's shape - schema,
    // overall, empty finding_ids, and nothing else - and replay must REJECT it.
    {
        std::ofstream out(tmp.path / "manifest.json",
                          std::ios::binary | std::ios::trunc);
        out << R"({"schema":"runtime-skeptic.analysis-bundle.v1",)"
               R"("overall":"SUPPORTED","finding_ids":[]})";
    }

    auto outcome = bundle::replay_bundle(tmp.dir(), error);
    RS_CHECK_MESSAGE(!outcome.has_value(),
                     "an incomplete bundle was replayed instead of rejected");
    RS_CHECK_MESSAGE(error.find("incomplete") != std::string::npos,
                     "the rejection did not name the bundle as incomplete");
}

RS_TEST(a_manifest_that_lies_about_its_verdict_is_caught) {
    TempDir tmp("liar");
    std::string error;
    RS_CHECK(bundle::write_bundle(tmp.dir(), inputs_that_produce_a_finding(), {},
                                  error));

    // Rewrite ONLY the manifest's recorded verdict. findings.json still hashes
    // correctly, so this is not caught as tampering - it is caught by the replay
    // re-deriving the real verdict and finding it does not match what the
    // manifest claims. The two checks cover different lies.
    {
        json::Value m = read_manifest(tmp.dir());
        m["overall"] = std::string("SUPPORTED");
        std::string err;
        io::write_file((tmp.path / "manifest.json").string(),
                       json::serialize_pretty(m), err);
    }

    auto outcome = bundle::replay_bundle(tmp.dir(), error);
    RS_CHECK(outcome.has_value());
    RS_CHECK(outcome->recorded_overall == "SUPPORTED");
    RS_CHECK(outcome->replayed_overall == "UNSUPPORTED");
    RS_CHECK_MESSAGE(!outcome->reproduced,
                     "a manifest claiming the wrong verdict was accepted");
}

RS_TEST(the_stored_hashes_match_the_stored_files) {
    TempDir tmp("hashes");
    std::string error;
    RS_CHECK(bundle::write_bundle(tmp.dir(), inputs_that_produce_a_finding(), {},
                                  error));

    // hashes.txt is sha256sum(1) format. Re-derive each line's hash from the
    // file it names and require a match, which is what `sha256sum -c` does.
    const std::string hashes = read(tmp.path / "hashes.txt");
    std::size_t start = 0;
    int lines = 0;
    while (start < hashes.size()) {
        const std::size_t nl = hashes.find('\n', start);
        const std::string line = hashes.substr(start, nl - start);
        start = nl == std::string::npos ? hashes.size() : nl + 1;
        if (line.empty()) continue;
        const std::string want = line.substr(0, 64);
        const std::string name = line.substr(66);   // "<64 hex><two spaces>name"
        hash::Sha256 h;
        h.update(read(tmp.path / name));
        RS_CHECK_MESSAGE(h.hex_digest() == want,
                         "hashes.txt disagrees with " + name);
        ++lines;
    }
    RS_CHECK(lines == 4);   // profile, requirement, findings, report
}

RS_TEST(a_directory_that_is_not_a_bundle_is_refused_not_crashed) {
    TempDir tmp("empty");
    std::error_code ec;
    fs::create_directories(tmp.path, ec);
    std::string error;
    auto outcome = bundle::replay_bundle(tmp.dir(), error);
    RS_CHECK_MESSAGE(!outcome.has_value(),
                     "an empty directory was accepted as a bundle");
    RS_CHECK(!error.empty());
}

RS_TEST_MAIN("evidence bundle")

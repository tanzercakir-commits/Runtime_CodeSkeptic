// SPDX-License-Identifier: Apache-2.0
#include "runtimeskeptic/core/schema_registry.hpp"

#include "runtimeskeptic/core/json.hpp"
#include "runtimeskeptic/core/schema.hpp"
#include "runtimeskeptic/core/schemas_embedded.hpp"

namespace rs::schema {
namespace {

// The two requirement shapes rs-check accepts, keyed by their `schema` const.
constexpr const char* kRequirementConst = "runtime-skeptic.application-requirements.v1";
constexpr const char* kBundleConst = "runtime-skeptic.application-requirements-bundle.v1";

// Parsed once, on first use. The embedded strings are our own valid JSON; a
// parse failure here is a build-time corruption, reported as an internal error.
struct Parsed {
    json::Value requirement;
    json::Value bundle;
    json::Value profile;
    json::Value analysis;
    Store bundle_store;  // lets the bundle's cross-file $ref reach `requirement`
    bool ok = false;
    std::string error;
};

const Parsed& schemas() {
    static const Parsed p = [] {
        Parsed out;
        auto load = [&](const char* text, json::Value& into, const char* what) {
            auto r = json::parse(text);
            if (!r.ok()) {
                out.error = std::string("embedded schema ") + what +
                            " did not parse";
                return false;
            }
            into = *r.value;
            return true;
        };
        if (!load(embedded::kApplicationRequirements, out.requirement,
                  "application-requirements") ||
            !load(embedded::kApplicationRequirementsBundle, out.bundle,
                  "application-requirements-bundle") ||
            !load(embedded::kEnvironmentProfile, out.profile,
                  "environment-profile") ||
            !load(embedded::kAnalysisBundle, out.analysis, "analysis-bundle")) {
            return out;
        }
        // The bundle schema refers to the requirement schema by file name.
        out.bundle_store.add("application-requirements.v1.json", &out.requirement);
        out.ok = true;
        return out;
    }();
    return p;
}

}  // namespace

bool validate_requirement_input(const json::Value& doc, std::string& error) {
    const Parsed& s = schemas();
    if (!s.ok) { error = s.error; return false; }

    const json::Value* schema = doc.find("schema");
    const std::string declared =
        (schema != nullptr && schema->is_string()) ? schema->as_string()
                                                    : std::string();
    if (declared == kBundleConst) {
        return validate(doc, s.bundle, s.bundle_store, error);
    }
    // Default to the single-requirement schema. It requires `schema` to equal
    // the requirement const, so an absent, mistyped or unrecognized `schema`
    // field is refused there with the precise violation - the same verdict
    // jsonschema reaches for a document meant to be a single requirement.
    return validate(doc, s.requirement, error);
}

bool validate_profile(const json::Value& doc, std::string& error) {
    const Parsed& s = schemas();
    if (!s.ok) { error = s.error; return false; }
    return validate(doc, s.profile, error);
}

bool validate_analysis_manifest(const json::Value& doc, std::string& error) {
    const Parsed& s = schemas();
    if (!s.ok) { error = s.error; return false; }
    return validate(doc, s.analysis, error);
}

bool validate_by_schema_name(const std::string& schema_basename,
                             const json::Value& doc, std::string& error) {
    const Parsed& s = schemas();
    if (!s.ok) { error = s.error; return false; }
    if (schema_basename == "application-requirements.v1.json") {
        return validate(doc, s.requirement, error);
    }
    if (schema_basename == "application-requirements-bundle.v1.json") {
        return validate(doc, s.bundle, s.bundle_store, error);
    }
    if (schema_basename == "environment-profile.v1.json") {
        return validate(doc, s.profile, error);
    }
    if (schema_basename == "analysis-bundle.v1.json") {
        return validate(doc, s.analysis, error);
    }
    error = "unknown schema " + schema_basename;
    return false;
}

}  // namespace rs::schema

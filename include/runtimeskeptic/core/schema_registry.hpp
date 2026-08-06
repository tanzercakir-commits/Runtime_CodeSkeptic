// SPDX-License-Identifier: Apache-2.0
//
// The published contract, gating every tool input. The schemas are embedded at
// build time (schemas_embedded.hpp) and parsed once; each helper validates a
// document against the right one before any hand-parser touches it. This is the
// single place a wrong type, a null container or an unexpected nested field is
// refused - the parsers no longer have to remember to, because the schema does.
#ifndef RUNTIMESKEPTIC_CORE_SCHEMA_REGISTRY_HPP
#define RUNTIMESKEPTIC_CORE_SCHEMA_REGISTRY_HPP

#include <string>

#include "runtimeskeptic/core/json.hpp"

namespace rs::schema {

// A requirement input: either a single runtime-skeptic.application-requirements
// .v1 document or an ...-bundle.v1 of them. Dispatches on the top-level `schema`
// const so the error names the real fault. Returns true if valid; else sets
// `error`.
bool validate_requirement_input(const json::Value& doc, std::string& error);

// An environment profile (runtime-skeptic.environment-profile.v1).
bool validate_profile(const json::Value& doc, std::string& error);

// An analysis-bundle manifest (runtime-skeptic.analysis-bundle.v1), the
// document rs-replay reads back.
bool validate_analysis_manifest(const json::Value& doc, std::string& error);

// One header, event or footer record from runtime-trace-record.v1 JSONL.
bool validate_runtime_trace_record(const json::Value& doc, std::string& error);

// A runtime-overhead.v1 benchmark artifact.
bool validate_runtime_overhead(const json::Value& doc, std::string& error);

// Validate against a schema named by its file basename (e.g.
// "environment-profile.v1.json"). Used by the dev tool rs-validate so the
// boundary matrix can compare this validator to Python's jsonschema directly.
// Returns false with `error` set to "unknown schema ..." for an unknown name.
bool validate_by_schema_name(const std::string& schema_basename,
                             const json::Value& doc, std::string& error);

}  // namespace rs::schema

#endif  // RUNTIMESKEPTIC_CORE_SCHEMA_REGISTRY_HPP

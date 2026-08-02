// SPDX-License-Identifier: Apache-2.0
//
//   rs-validate SCHEMA_BASENAME DOC.json
//
// A DEVELOPMENT tool, not shipped. It answers exactly one question - does this
// document satisfy this schema, by the same rs::schema validator the tools gate
// their inputs with? The boundary matrix (tools/audit/boundary_matrix.py) runs
// it beside Python's jsonschema for every mutation; if the two ever disagree,
// the validator is not a faithful stand-in for the published contract and the
// matrix fails. That is the proof that gating the tools through this validator
// enforces the schema and nothing more or less.
//
// SCHEMA_BASENAME is the file name under schemas/, e.g.
// "environment-profile.v1.json".
//
// EXIT CODES
//   0   valid
//   65  invalid against the schema (or the document is not JSON)
//   64  usage error
//   70  internal error (an embedded schema failed to parse)
#include <iostream>
#include <string>

#include "runtimeskeptic/core/io.hpp"
#include "runtimeskeptic/core/json.hpp"
#include "runtimeskeptic/core/schema_registry.hpp"

int main(int argc, char** argv) {
    using namespace rs;

    if (argc != 3) {
        std::cerr << "usage: rs-validate SCHEMA_BASENAME DOC.json\n";
        return 64;
    }
    const std::string schema_name = argv[1];
    const std::string doc_path = argv[2];

    std::string error;
    auto text = io::read_file(doc_path, error);
    if (!text) {
        std::cerr << "rs-validate: " << error << "\n";
        return 65;
    }
    auto parsed = json::parse(*text);
    if (!parsed.ok()) {
        std::cerr << "rs-validate: not valid JSON: "
                  << parsed.error->to_string() << "\n";
        return 65;
    }

    if (!schema::validate_by_schema_name(schema_name, *parsed.value, error)) {
        // Distinguish an unknown schema name (a usage/internal fault) from a
        // document that simply does not conform.
        if (error.rfind("unknown schema", 0) == 0) {
            std::cerr << "rs-validate: " << error << "\n";
            return 64;
        }
        if (error.rfind("embedded schema", 0) == 0) {
            std::cerr << "rs-validate: internal: " << error << "\n";
            return 70;
        }
        std::cerr << "rs-validate: invalid: " << error << "\n";
        return 65;
    }
    return 0;
}

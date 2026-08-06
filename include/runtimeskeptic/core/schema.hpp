// SPDX-License-Identifier: Apache-2.0
//
// A small JSON Schema validator - the SUBSET our own schemas/*.json use, no
// more. Four rounds of hand-written type checks in the parsers each left a
// different gap (a wrong type read as absent, a null container, a nested
// manifest field), because a hand check can only enforce the fields someone
// remembered. This reads the schema itself, so it cannot forget one.
//
// Supported keywords: type (string or array, incl "null"/"integer"), required,
// properties, additionalProperties (bool or schema), enum, const, items, $ref
// (local #/$defs/... and cross-file, resolved through a Store), pattern,
// minimum, maximum, minItems, maxItems, uniqueItems, anyOf, oneOf, allOf. "integer" means an Int or UInt value (this
// project's schemas never use doubles; the canonical writer rejects them). It
// is validated against Python's jsonschema as an oracle by
// tools/audit/boundary_matrix.py - if that reports 0 disagreements, this agrees
// with jsonschema for every mutation the matrix covers.
#ifndef RUNTIMESKEPTIC_CORE_SCHEMA_HPP
#define RUNTIMESKEPTIC_CORE_SCHEMA_HPP

#include <string>
#include <utility>
#include <vector>

#include "runtimeskeptic/core/json.hpp"

namespace rs::schema {

// A registry of sibling schemas that a cross-file $ref can resolve to. A ref
// like "application-requirements.v1.json" (as the bundle schema uses) is looked
// up here by the exact string it appears as, and by a schema's own $id. Local
// "#/..." refs never consult it. The pointers must outlive every validate call
// that uses the Store.
class Store {
public:
    // Register `schema` under `key`, and also under its own "$id" if present.
    void add(std::string key, const json::Value* schema);
    // The schema for `ref` (the base, before any '#') or nullptr.
    const json::Value* find(const std::string& ref) const;

private:
    std::vector<std::pair<std::string, const json::Value*>> entries_;
};

// Validate `doc` against `schema`. On the first violation sets `error` (with a
// slash-separated location) and returns false; returns true when `doc`
// conforms. Local `$ref` is resolved within `schema` itself (#/$defs/NAME);
// cross-file `$ref` needs the Store overload below.
bool validate(const json::Value& doc, const json::Value& schema,
              std::string& error);

// As above, but cross-file `$ref` is resolved through `store`. When a ref
// crosses into another schema, that schema becomes the root for its own local
// `#/` refs.
bool validate(const json::Value& doc, const json::Value& schema,
              const Store& store, std::string& error);

}  // namespace rs::schema

#endif  // RUNTIMESKEPTIC_CORE_SCHEMA_HPP

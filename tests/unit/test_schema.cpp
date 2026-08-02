// SPDX-License-Identifier: Apache-2.0
//
// The JSON Schema validator, keyword by keyword, plus the exact 2026-08-02
// re-test findings it exists to close (a null container, a wrong-typed value, a
// nested field). The boundary matrix proves the validator equals Python's
// jsonschema across hundreds of mutations, but it only runs where jsonschema
// and the built binaries are present; this suite guards the validator itself on
// every platform, with no external oracle.
#include "runtimeskeptic/core/schema.hpp"

#include <string>

#include "runtimeskeptic/core/json.hpp"
#include "runtimeskeptic/core/schema_registry.hpp"
#include "test_support.hpp"

using namespace rs;

namespace {

json::Value J(const std::string& text) {
    auto p = json::parse(text);
    RS_CHECK_MESSAGE(p.ok(), "test JSON did not parse: " + text);
    return *p.value;
}

// Validate `doc` against `schema`, both given as JSON text. Returns true iff
// the document conforms.
bool ok(const std::string& doc, const std::string& schema) {
    std::string error;
    return schema::validate(J(doc), J(schema), error);
}

}  // namespace

RS_TEST(type_integer_accepts_ints_rejects_everything_else) {
    const char* s = R"({"type":"integer"})";
    RS_CHECK(ok("0", s));
    RS_CHECK(ok("42", s));
    RS_CHECK(ok("-7", s));
    RS_CHECK(ok("18446744073709551615", s));  // u64max, still an integer
    RS_CHECK(!ok("1.5", s));                   // a double is not an integer
    RS_CHECK(!ok("\"5\"", s));
    RS_CHECK(!ok("true", s));
    RS_CHECK(!ok("null", s));
    RS_CHECK(!ok("{}", s));
    RS_CHECK(!ok("[]", s));
}

RS_TEST(type_string_boolean_object_array_null) {
    RS_CHECK(ok("\"x\"", R"({"type":"string"})"));
    RS_CHECK(!ok("5", R"({"type":"string"})"));
    RS_CHECK(ok("true", R"({"type":"boolean"})"));
    RS_CHECK(!ok("\"true\"", R"({"type":"boolean"})"));
    RS_CHECK(ok("{}", R"({"type":"object"})"));
    RS_CHECK(ok("[]", R"({"type":"array"})"));
    RS_CHECK(ok("null", R"({"type":"null"})"));
    RS_CHECK(!ok("0", R"({"type":"null"})"));
}

RS_TEST(type_union_array_incl_null) {
    const char* s = R"({"type":["integer","null"]})";
    RS_CHECK(ok("5", s));
    RS_CHECK(ok("null", s));
    RS_CHECK(!ok("\"5\"", s));
    RS_CHECK(!ok("1.5", s));
}

RS_TEST(required_and_properties) {
    const char* s = R"({"type":"object","required":["a"],
                        "properties":{"a":{"type":"integer"}}})";
    RS_CHECK(ok(R"({"a":1})", s));
    RS_CHECK(!ok(R"({})", s));           // missing required
    RS_CHECK(!ok(R"({"a":"x"})", s));    // wrong type
}

RS_TEST(additional_properties_bool_and_schema) {
    const char* closed = R"({"type":"object","additionalProperties":false,
                            "properties":{"a":{"type":"integer"}}})";
    RS_CHECK(ok(R"({"a":1})", closed));
    RS_CHECK(!ok(R"({"a":1,"b":2})", closed));  // extra key refused

    const char* open = R"({"type":"object","additionalProperties":true,
                          "properties":{"a":{"type":"integer"}}})";
    RS_CHECK(ok(R"({"a":1,"b":"anything"})", open));

    const char* typed = R"({"type":"object","additionalProperties":{"type":"string"}})";
    RS_CHECK(ok(R"({"x":"s"})", typed));
    RS_CHECK(!ok(R"({"x":5})", typed));  // extra value must be a string
}

RS_TEST(enum_and_const) {
    const char* e = R"({"enum":["a","b","c"]})";
    RS_CHECK(ok("\"b\"", e));
    RS_CHECK(!ok("\"z\"", e));
    const char* c = R"({"const":"fixed"})";
    RS_CHECK(ok("\"fixed\"", c));
    RS_CHECK(!ok("\"other\"", c));
}

RS_TEST(items_applies_to_each_element) {
    const char* s = R"({"type":"array","items":{"type":"integer"}})";
    RS_CHECK(ok("[1,2,3]", s));
    RS_CHECK(ok("[]", s));
    RS_CHECK(!ok("[1,\"x\"]", s));
}

RS_TEST(pattern_applies_to_strings_only) {
    const char* s = R"({"type":["string","integer","null"],"pattern":"^0x[0-9a-f]+$"})";
    RS_CHECK(ok("\"0x1f\"", s));
    RS_CHECK(!ok("\"nothex\"", s));
    RS_CHECK(ok("4096", s));   // pattern is ignored for a non-string
    RS_CHECK(ok("null", s));
}

RS_TEST(minimum_and_maximum) {
    const char* s = R"({"type":"integer","minimum":1,"maximum":18446744073709551615})";
    RS_CHECK(ok("1", s));
    RS_CHECK(!ok("0", s));             // below minimum
    RS_CHECK(!ok("-1", s));            // negative < a non-negative minimum
    RS_CHECK(ok("18446744073709551615", s));  // exactly u64max
    const char* z = R"({"type":"integer","minimum":0})";
    RS_CHECK(ok("0", z));
    RS_CHECK(!ok("-1", z));
}

RS_TEST(any_of_and_all_of) {
    const char* a = R"({"anyOf":[{"type":"integer"},{"type":"string"}]})";
    RS_CHECK(ok("5", a));
    RS_CHECK(ok("\"x\"", a));
    RS_CHECK(!ok("true", a));
    // allOf: a value must satisfy every branch. Here: an object that is both
    // non-empty-by-required and has a typed property.
    const char* all = R"({"allOf":[{"required":["a"]},
                          {"properties":{"a":{"type":"integer"}}}]})";
    RS_CHECK(ok(R"({"a":1})", all));
    RS_CHECK(!ok(R"({"a":"x"})", all));  // second branch fails
    RS_CHECK(!ok(R"({})", all));          // first branch fails
}

RS_TEST(local_ref_into_defs) {
    const char* s = R"({"type":"array","items":{"$ref":"#/$defs/id"},
                        "$defs":{"id":{"type":"string","pattern":"^RS-[0-9]+$"}}})";
    RS_CHECK(ok(R"(["RS-1","RS-42"])", s));
    RS_CHECK(!ok(R"(["RS-1","nope"])", s));
}

RS_TEST(cross_file_ref_through_store) {
    // A "parent" schema whose array items $ref a sibling file, resolved through
    // a Store - the shape the requirements-bundle schema uses to reach the
    // requirement schema. The sibling's own local #/$defs must resolve under the
    // sibling's root, not the parent's.
    json::Value child = J(R"({"type":"object","required":["k"],
        "properties":{"k":{"$ref":"#/$defs/kind"}},
        "$defs":{"kind":{"enum":["x","y"]}}})");
    json::Value parent = J(R"({"type":"array",
        "items":{"$ref":"child.json"}})");
    schema::Store store;
    store.add("child.json", &child);
    std::string error;
    RS_CHECK(schema::validate(J(R"([{"k":"x"},{"k":"y"}])"), parent, store, error));
    RS_CHECK(!schema::validate(J(R"([{"k":"z"}])"), parent, store, error));
    RS_CHECK(!schema::validate(J(R"([{}])"), parent, store, error));  // missing k
}

RS_TEST(if_then_enforces_the_address_dependency) {
    // The one cross-field rule in the project's schemas: a request that demands
    // an exact address must carry one.
    const char* s = R"({"type":"object",
        "if":{"required":["exact"],"properties":{"exact":{"const":true}}},
        "then":{"required":["address"]}})";
    RS_CHECK(ok(R"({"exact":true,"address":"0x1000"})", s));
    RS_CHECK(!ok(R"({"exact":true})", s));            // then-branch requires address
    RS_CHECK(ok(R"({"exact":false})", s));            // if-condition false
    RS_CHECK(ok(R"({})", s));                          // exact absent
}

// --- the embedded contract, through the registry: the exact re-test findings.

RS_TEST(registry_accepts_the_real_base_documents) {
    std::string error;
    RS_CHECK_MESSAGE(schema::validate_requirement_input(J(R"({
        "schema":"runtime-skeptic.application-requirements.v1",
        "operation":"virtual_memory_map",
        "assumption_evidence":"specified_guarantee",
        "request":{"size":65536}})"), error), error);
    RS_CHECK_MESSAGE(schema::validate_profile(J(R"({
        "schema":"runtime-skeptic.environment-profile.v1",
        "origin":"measured",
        "platform":{"os":"linux","host_arch":"x86_64","process_arch":"x86_64"},
        "virtual_memory":{"page_size":{"value":4096,"evidence":"measured_capability"}}})"),
        error), error);
}

RS_TEST(registry_rejects_the_round4_findings) {
    std::string error;
    // A null container where an object/array is required (assumptions:null).
    RS_CHECK(!schema::validate_requirement_input(J(R"({
        "schema":"runtime-skeptic.application-requirements.v1",
        "operation":"virtual_memory_map","assumption_evidence":"specified_guarantee",
        "request":{"size":65536},"assumptions":null})"), error));
    // A null nested leaf (source_locations[].line:null).
    RS_CHECK(!schema::validate_requirement_input(J(R"({
        "schema":"runtime-skeptic.application-requirements.v1",
        "operation":"virtual_memory_map","assumption_evidence":"specified_guarantee",
        "request":{"size":65536},
        "source_locations":[{"file":"a.c","line":null}]})"), error));
    // A null fact container in a profile (page_size:null).
    RS_CHECK(!schema::validate_profile(J(R"({
        "schema":"runtime-skeptic.environment-profile.v1","origin":"measured",
        "platform":{"os":"linux","host_arch":"x86_64","process_arch":"x86_64"},
        "virtual_memory":{"page_size":null}})"), error));
    // exact_address_required with no address (the if/then rule).
    RS_CHECK(!schema::validate_requirement_input(J(R"({
        "schema":"runtime-skeptic.application-requirements.v1",
        "operation":"virtual_memory_map","assumption_evidence":"specified_guarantee",
        "request":{"size":65536,"exact_address_required":true}})"), error));
}

RS_TEST(registry_manifest_validates_nested_fields) {
    // The 28/111 nested-manifest class: a wrong-typed nested field must fail.
    std::string error;
    RS_CHECK(!schema::validate_analysis_manifest(J(R"({
        "schema":"runtime-skeptic.analysis-bundle.v1","tool_version":"x",
        "schema_versions":{},"host":{"os":5},"process_architecture":"x86_64",
        "analysis_options":{},"inputs":{},"outputs":{},"overall":"SUPPORTED",
        "finding_ids":[],"replay":{}})"), error));
}

RS_TEST_MAIN("schema")

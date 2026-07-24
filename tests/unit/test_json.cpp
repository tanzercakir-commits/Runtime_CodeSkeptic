// SPDX-License-Identifier: Apache-2.0
#include "runtimeskeptic/core/json.hpp"

#include <string>

#include "test_support.hpp"

using namespace rs;

namespace {

std::string canonical(const std::string& text) {
    auto parsed = json::parse(text);
    if (!parsed.ok()) return "<parse error: " + parsed.error->to_string() + ">";
    auto out = json::serialize_canonical(*parsed.value);
    return out.value_or("<no canonical form>");
}

}  // namespace

RS_TEST(parses_scalars) {
    RS_CHECK(json::parse("null").ok());
    RS_CHECK(json::parse("true").ok());
    RS_CHECK_EQ(canonical(" 42 "), std::string("42"));
    RS_CHECK_EQ(canonical("-42"), std::string("-42"));
    RS_CHECK_EQ(canonical("\"hi\""), std::string("\"hi\""));
}

RS_TEST(rejects_malformed_input) {
    RS_CHECK(!json::parse("{").ok());
    RS_CHECK(!json::parse("[1,]").ok());
    RS_CHECK(!json::parse("{\"a\":1,}").ok());
    RS_CHECK(!json::parse("01").ok());          // leading zero
    RS_CHECK(!json::parse("{\"a\":1} junk").ok());
    RS_CHECK(!json::parse("\"unterminated").ok());
    // A duplicate key is a schema-level ambiguity, so it is an error rather
    // than a last-one-wins silent choice.
    RS_CHECK(!json::parse("{\"a\":1,\"a\":2}").ok());
}

RS_TEST(canonical_form_sorts_keys) {
    RS_CHECK_EQ(canonical("{\"b\":1,\"a\":2}"), std::string("{\"a\":2,\"b\":1}"));
    RS_CHECK_EQ(canonical("{\"z\":{\"y\":1,\"x\":2}}"),
                std::string("{\"z\":{\"x\":2,\"y\":1}}"));
}

RS_TEST(canonical_form_is_idempotent) {
    const std::string input =
        "{\"schema\":\"x\",\"b\":[3,2,1],\"a\":{\"n\":null,\"t\":true}}";
    const std::string once = canonical(input);
    const std::string twice = canonical(once);
    RS_CHECK_EQ(once, twice);
}

RS_TEST(canonical_form_rejects_doubles) {
    // Floating point has no stable cross-platform textual form, so a document
    // containing one has no canonical serialization at all. Silently rounding
    // would break profile_id stability.
    auto parsed = json::parse("{\"x\":1.5}");
    RS_CHECK(parsed.ok());
    RS_CHECK(!json::serialize_canonical(*parsed.value).has_value());
}

RS_TEST(pretty_form_round_trips_to_same_canonical) {
    const std::string input = "{\"b\":[1,2,{\"d\":4,\"c\":3}],\"a\":\"x\"}";
    auto parsed = json::parse(input);
    RS_CHECK(parsed.ok());
    const std::string pretty = json::serialize_pretty(*parsed.value);
    RS_CHECK_EQ(canonical(pretty), canonical(input));
}

RS_TEST(escapes_control_characters) {
    auto parsed = json::parse("\"a\\u0001b\\n\"");
    RS_CHECK(parsed.ok());
    auto out = json::serialize_canonical(*parsed.value);
    RS_CHECK(out.has_value());
    RS_CHECK_EQ(*out, std::string("\"a\\u0001b\\n\""));
}

RS_TEST(handles_surrogate_pairs) {
    auto parsed = json::parse("\"\\ud83d\\ude00\"");  // U+1F600
    RS_CHECK(parsed.ok());
    RS_CHECK_EQ(parsed.value->as_string().size(), std::size_t{4});
    RS_CHECK(!json::parse("\"\\ud83d\"").ok());       // unpaired high
    RS_CHECK(!json::parse("\"\\ude00\"").ok());       // unpaired low
}

RS_TEST(preserves_full_uint64_range) {
    // 2^64-1 must survive a round trip. This is why addresses could have been
    // numbers - but see the hex-string tests below for why they are not.
    auto parsed = json::parse("18446744073709551615");
    RS_CHECK(parsed.ok());
    RS_CHECK_EQ(parsed.value->as_uint(), UINT64_MAX);
    auto out = json::serialize_canonical(*parsed.value);
    RS_CHECK_EQ(*out, std::string("18446744073709551615"));
}

RS_TEST(hex_addresses_round_trip) {
    RS_CHECK_EQ(json::to_hex(0), std::string("0x0"));
    RS_CHECK_EQ(json::to_hex(0x1000000000ull), std::string("0x1000000000"));
    RS_CHECK_EQ(*json::from_hex("0x1000000000"), 0x1000000000ull);
    RS_CHECK_EQ(*json::from_hex("0X1000"), 0x1000ull);
    RS_CHECK(!json::from_hex("1000").has_value());     // no prefix
    RS_CHECK(!json::from_hex("0x").has_value());       // no digits
    RS_CHECK(!json::from_hex("0xzz").has_value());     // bad digit
    // Overflow must be rejected rather than wrapping.
    RS_CHECK(!json::from_hex("0x10000000000000000").has_value());
    RS_CHECK_EQ(*json::from_hex("0xffffffffffffffff"), UINT64_MAX);
}

RS_TEST(rejects_excessive_nesting) {
    std::string deep;
    for (int i = 0; i < 200; ++i) deep += "[";
    for (int i = 0; i < 200; ++i) deep += "]";
    RS_CHECK(!json::parse(deep).ok());
}

RS_TEST(int_and_uint_compare_equal_when_same_value) {
    const json::Value a(static_cast<long long>(5));
    const json::Value b(static_cast<unsigned long long>(5));
    RS_CHECK(a == b);
    const json::Value c(static_cast<long long>(-5));
    RS_CHECK(!(c == b));
}

RS_TEST_MAIN("json")

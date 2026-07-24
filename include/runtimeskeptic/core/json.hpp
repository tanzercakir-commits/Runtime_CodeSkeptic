// SPDX-License-Identifier: Apache-2.0
//
// Minimal, dependency-free JSON value type with a *canonical* serializer.
//
// Why a hand-written JSON implementation instead of a third-party library:
//
//   1. Canonicalization is a product requirement, not a convenience.
//      Environment profiles are hashed and compared across machines and
//      releases (ROADMAP Phase 1 exit criteria). We must own every byte of
//      the output: key order, escaping, integer formatting.
//   2. The probe runs close to the operating-system boundary and must stay
//      easy to audit (ROADMAP section 18).
//   3. Reproducible builds without network access in CI.
//
// Canonical form rules (see docs/architecture/determinism.md):
//   - object keys sorted by UTF-8 code unit, ascending
//   - no insignificant whitespace
//   - integers emitted without exponent or fraction
//   - doubles are REJECTED in canonical mode (schemas must not use them)
//   - strings escaped with the shortest legal escape; control characters
//     use \u00XX; forward slash is NOT escaped
//
#ifndef RUNTIMESKEPTIC_CORE_JSON_HPP
#define RUNTIMESKEPTIC_CORE_JSON_HPP

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rs::json {

class Value;

// std::map keeps keys sorted, which is exactly the canonical order we want.
using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;

// GCC's -Wshadow flags the scoped enumerators `Type::Array` / `Type::Object`
// as shadowing the aliases above. A scoped enumerator cannot actually be
// confused with a namespace-scope name, and Clang does not warn, so the
// diagnostic is suppressed here rather than distorting the names.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#endif
enum class Type {
    Null,
    Bool,
    Int,     // signed 64-bit
    UInt,    // unsigned 64-bit (addresses, sizes)
    Double,  // tolerated on input, rejected by the canonical writer
    String,
    Array,
    Object,
};
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

class Value {
public:
    Value() = default;                       // null
    Value(std::nullptr_t) {}                 // NOLINT(google-explicit-constructor)
    Value(bool v);                           // NOLINT
    Value(int v);                            // NOLINT
    Value(long long v);                      // NOLINT
    Value(unsigned long long v);             // NOLINT
    Value(double v);                         // NOLINT
    Value(const char* v);                    // NOLINT
    Value(std::string v);                    // NOLINT
    Value(Array v);                          // NOLINT
    Value(Object v);                         // NOLINT

    Type type() const { return type_; }

    bool is_null() const { return type_ == Type::Null; }
    bool is_bool() const { return type_ == Type::Bool; }
    bool is_number() const;
    bool is_string() const { return type_ == Type::String; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }

    // Accessors. They do not throw; callers use the checked helpers below or
    // the typed getters in schema.hpp which produce diagnostics.
    bool as_bool(bool fallback = false) const;
    std::int64_t as_int(std::int64_t fallback = 0) const;
    std::uint64_t as_uint(std::uint64_t fallback = 0) const;
    double as_double(double fallback = 0.0) const;
    const std::string& as_string() const;
    const Array& as_array() const;
    const Object& as_object() const;

    Array& array_ref();
    Object& object_ref();

    // Object member lookup; returns nullptr when absent or when *this is not
    // an object.
    const Value* find(std::string_view key) const;
    bool contains(std::string_view key) const { return find(key) != nullptr; }

    // Convenience mutators for building documents.
    Value& operator[](const std::string& key);
    void push_back(Value v);

    static Value object() { return Value(Object{}); }
    static Value array() { return Value(Array{}); }

    friend bool operator==(const Value& a, const Value& b);
    friend bool operator!=(const Value& a, const Value& b) { return !(a == b); }

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    std::int64_t int_ = 0;
    std::uint64_t uint_ = 0;
    double double_ = 0.0;
    std::string string_;
    Array array_;
    Object object_;
};

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

struct ParseError {
    std::size_t offset = 0;
    std::size_t line = 1;
    std::size_t column = 1;
    std::string message;

    std::string to_string() const;
};

struct ParseResult {
    std::optional<Value> value;
    std::optional<ParseError> error;

    bool ok() const { return value.has_value(); }
};

ParseResult parse(std::string_view text);

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

// Byte-exact canonical form. Deterministic across platforms and toolchains.
// Returns an empty optional when the document contains a value that has no
// canonical representation (currently: doubles, NaN, infinities).
std::optional<std::string> serialize_canonical(const Value& v);

// Human-oriented pretty form. Key order is still sorted, so a pretty document
// canonicalizes to the same bytes after a parse round-trip.
std::string serialize_pretty(const Value& v, unsigned indent = 2);

// ---------------------------------------------------------------------------
// Address helpers
// ---------------------------------------------------------------------------
//
// All virtual addresses cross the JSON boundary as lowercase hex strings
// ("0x1000000000") rather than numbers. JSON numbers are not safely
// interoperable above 2^53 (JavaScript consumers silently lose precision),
// and an address is an opaque identifier rather than an arithmetic quantity
// at the schema level.

std::string to_hex(std::uint64_t value);
std::optional<std::uint64_t> from_hex(std::string_view text);

}  // namespace rs::json

#endif  // RUNTIMESKEPTIC_CORE_JSON_HPP

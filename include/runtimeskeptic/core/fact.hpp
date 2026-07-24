// SPDX-License-Identifier: Apache-2.0
//
// A `Fact<T>` is a value that knows how it came to be known.
//
// The whole project rests on refusing to answer questions we have not
// measured or specified. A bare `size_t page_size` invites that mistake,
// because a default-constructed 0 looks like data. `Fact<T>` makes the
// unknown state unavoidable at the call site: `value()` is only meaningful
// after `is_known()` returns true, and `is_known()` is false unless an
// explicit evidence class other than Unknown was attached.
#ifndef RUNTIMESKEPTIC_CORE_FACT_HPP
#define RUNTIMESKEPTIC_CORE_FACT_HPP

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

#include "runtimeskeptic/core/evidence.hpp"
#include "runtimeskeptic/core/json.hpp"

namespace rs {

// Tag type: a virtual address. Distinct from a plain uint64 so that the
// serializer emits a hex string for addresses and a number for sizes and
// counts, without the call site having to remember which is which.
struct Address {
    std::uint64_t value = 0;

    Address() = default;
    explicit Address(std::uint64_t v) : value(v) {}

    friend bool operator==(const Address& a, const Address& b) {
        return a.value == b.value;
    }
};

template <typename T>
class Fact {
public:
    Fact() = default;

    static Fact known(T value, EvidenceClass evidence, std::string source,
                      std::string note = {}) {
        Fact f;
        // Guard against a caller passing Unknown alongside a real value: that
        // combination would silently create a fact that reports itself as
        // unknown while carrying data.
        if (evidence == EvidenceClass::Unknown) {
            f.note_ = "rejected: value supplied with unknown evidence class";
            return f;
        }
        f.value_ = std::move(value);
        f.known_ = true;
        f.evidence_ = evidence;
        f.source_ = std::move(source);
        f.note_ = std::move(note);
        return f;
    }

    static Fact unknown(std::string reason = {}) {
        Fact f;
        f.note_ = std::move(reason);
        return f;
    }

    bool is_known() const { return known_; }
    const T& value() const { return value_; }
    T value_or(T fallback) const { return known_ ? value_ : std::move(fallback); }
    EvidenceClass evidence() const {
        return known_ ? evidence_ : EvidenceClass::Unknown;
    }
    const std::string& source() const { return source_; }
    const std::string& note() const { return note_; }

    json::Value to_json() const {
        json::Value v = json::Value::object();
        v["value"] = known_ ? encode(value_) : json::Value();
        v["evidence"] = std::string(rs::to_string(evidence()));
        if (!source_.empty()) v["source"] = source_;
        if (!note_.empty()) v["note"] = note_;
        return v;
    }

private:
    static json::Value encode(const T& v) {
        if constexpr (std::is_same_v<T, Address>) {
            return json::Value(json::to_hex(v.value));
        } else if constexpr (std::is_same_v<T, bool>) {
            return json::Value(v);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return json::Value(v);
        } else if constexpr (std::is_enum_v<T>) {
            // Unqualified call plus a using-declaration: argument-dependent
            // lookup finds to_string() overloads declared alongside domain
            // enums (rs::vm::*), while the using-declaration covers the core
            // enums declared in namespace rs.
            using rs::to_string;
            return json::Value(std::string(to_string(v)));
        } else if constexpr (std::is_unsigned_v<T>) {
            return json::Value(static_cast<unsigned long long>(v));
        } else {
            return json::Value(static_cast<long long>(v));
        }
    }

    T value_{};
    bool known_ = false;
    EvidenceClass evidence_ = EvidenceClass::Unknown;
    std::string source_;
    std::string note_;
};

// Parses a fact from JSON. Returns an unknown fact when the node is absent,
// null, or declares evidence "unknown". `error` is set only for malformed
// input (wrong type, unrecognized evidence class), which callers treat as a
// hard schema failure rather than as missing data.
template <typename T, typename Reader>
Fact<T> fact_from_json(const json::Value* node, Reader read_value,
                       std::string& error) {
    if (node == nullptr || node->is_null()) return Fact<T>::unknown("absent");
    if (!node->is_object()) {
        error = "fact must be an object with 'value' and 'evidence'";
        return Fact<T>::unknown("malformed");
    }
    const json::Value* evidence_node = node->find("evidence");
    if (evidence_node == nullptr || !evidence_node->is_string()) {
        error = "fact requires an explicit 'evidence' string";
        return Fact<T>::unknown("malformed");
    }
    EvidenceClass evidence = EvidenceClass::Unknown;
    if (!evidence_class_from_string(evidence_node->as_string(), evidence)) {
        error = "unrecognized evidence class: " + evidence_node->as_string();
        return Fact<T>::unknown("malformed");
    }

    std::string source;
    if (const json::Value* s = node->find("source"); s != nullptr) {
        source = s->as_string();
    }
    std::string note;
    if (const json::Value* n = node->find("note"); n != nullptr) {
        note = n->as_string();
    }

    const json::Value* value_node = node->find("value");
    if (evidence == EvidenceClass::Unknown || value_node == nullptr ||
        value_node->is_null()) {
        return Fact<T>::unknown(note.empty() ? "declared unknown" : note);
    }

    T value{};
    if (!read_value(*value_node, value, error)) {
        return Fact<T>::unknown("malformed");
    }
    return Fact<T>::known(std::move(value), evidence, std::move(source),
                          std::move(note));
}

// ---------------------------------------------------------------------------
// Readers for the common fact payload types
// ---------------------------------------------------------------------------

inline bool read_bool(const json::Value& v, bool& out, std::string& error) {
    if (!v.is_bool()) {
        error = "expected a boolean value";
        return false;
    }
    out = v.as_bool();
    return true;
}

inline bool read_uint(const json::Value& v, std::uint64_t& out,
                      std::string& error) {
    if (v.type() != json::Type::UInt && v.type() != json::Type::Int) {
        error = "expected an integer value";
        return false;
    }
    if (v.type() == json::Type::Int && v.as_int() < 0) {
        error = "expected a non-negative integer";
        return false;
    }
    out = v.as_uint();
    return true;
}

inline bool read_address(const json::Value& v, Address& out,
                         std::string& error) {
    if (v.is_string()) {
        auto parsed = json::from_hex(v.as_string());
        if (!parsed) {
            error = "address must be a hex string like \"0x1000\"";
            return false;
        }
        out.value = *parsed;
        return true;
    }
    if (v.type() == json::Type::UInt || v.type() == json::Type::Int) {
        out.value = v.as_uint();
        return true;
    }
    error = "address must be a hex string";
    return false;
}

inline bool read_string(const json::Value& v, std::string& out,
                        std::string& error) {
    if (!v.is_string()) {
        error = "expected a string value";
        return false;
    }
    out = v.as_string();
    return true;
}

inline bool read_support_level(const json::Value& v, SupportLevel& out,
                               std::string& error) {
    if (!v.is_string() || !support_level_from_string(v.as_string(), out)) {
        error = "expected one of SUPPORTED / UNSUPPORTED / "
                "CONDITIONALLY_SUPPORTED / UNKNOWN";
        return false;
    }
    return true;
}

}  // namespace rs

#endif  // RUNTIMESKEPTIC_CORE_FACT_HPP

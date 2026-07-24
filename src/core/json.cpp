// SPDX-License-Identifier: Apache-2.0
#include "runtimeskeptic/core/json.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace rs::json {
namespace {

const std::string kEmptyString;
const Array kEmptyArray;
const Object kEmptyObject;

bool is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

void append_escaped(std::string& out, std::string_view s) {
    out.push_back('"');
    for (char raw : s) {
        const auto c = static_cast<unsigned char>(raw);
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    // Bytes >= 0x20 pass through unchanged, including valid
                    // UTF-8 continuation bytes. The canonical form is UTF-8.
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
}

void encode_utf8(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    ParseResult run() {
        skip_ws();
        Value v;
        if (!parse_value(v, 0)) return fail_result();
        skip_ws();
        if (pos_ != text_.size()) {
            set_error("trailing content after top-level value");
            return fail_result();
        }
        ParseResult r;
        r.value = std::move(v);
        return r;
    }

private:
    static constexpr int kMaxDepth = 128;

    std::string_view text_;
    std::size_t pos_ = 0;
    ParseError error_;
    bool failed_ = false;

    ParseResult fail_result() {
        ParseResult r;
        r.error = error_;
        return r;
    }

    void set_error(std::string message) {
        if (failed_) return;
        failed_ = true;
        error_.offset = pos_;
        error_.message = std::move(message);
        std::size_t line = 1;
        std::size_t col = 1;
        for (std::size_t i = 0; i < pos_ && i < text_.size(); ++i) {
            if (text_[i] == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
        }
        error_.line = line;
        error_.column = col;
    }

    bool eof() const { return pos_ >= text_.size(); }
    char peek() const { return text_[pos_]; }

    void skip_ws() {
        while (!eof() && is_ws(text_[pos_])) ++pos_;
    }

    bool expect(char c) {
        if (eof() || text_[pos_] != c) {
            set_error(std::string("expected '") + c + "'");
            return false;
        }
        ++pos_;
        return true;
    }

    bool literal(std::string_view lit) {
        if (text_.compare(pos_, lit.size(), lit) != 0) {
            set_error("invalid literal");
            return false;
        }
        pos_ += lit.size();
        return true;
    }

    bool parse_value(Value& out, int depth) {
        if (depth > kMaxDepth) {
            set_error("maximum nesting depth exceeded");
            return false;
        }
        if (eof()) {
            set_error("unexpected end of input");
            return false;
        }
        switch (peek()) {
            case '{': return parse_object(out, depth);
            case '[': return parse_array(out, depth);
            case '"': {
                std::string s;
                if (!parse_string(s)) return false;
                out = Value(std::move(s));
                return true;
            }
            case 't':
                if (!literal("true")) return false;
                out = Value(true);
                return true;
            case 'f':
                if (!literal("false")) return false;
                out = Value(false);
                return true;
            case 'n':
                if (!literal("null")) return false;
                out = Value();
                return true;
            default: return parse_number(out);
        }
    }

    bool parse_object(Value& out, int depth) {
        if (!expect('{')) return false;
        Object obj;
        skip_ws();
        if (!eof() && peek() == '}') {
            ++pos_;
            out = Value(std::move(obj));
            return true;
        }
        for (;;) {
            skip_ws();
            std::string key;
            if (!parse_string(key)) return false;
            skip_ws();
            if (!expect(':')) return false;
            skip_ws();
            Value v;
            if (!parse_value(v, depth + 1)) return false;
            if (obj.find(key) != obj.end()) {
                set_error("duplicate object key: " + key);
                return false;
            }
            obj.emplace(std::move(key), std::move(v));
            skip_ws();
            if (eof()) {
                set_error("unterminated object");
                return false;
            }
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            if (peek() == '}') {
                ++pos_;
                break;
            }
            set_error("expected ',' or '}'");
            return false;
        }
        out = Value(std::move(obj));
        return true;
    }

    bool parse_array(Value& out, int depth) {
        if (!expect('[')) return false;
        Array arr;
        skip_ws();
        if (!eof() && peek() == ']') {
            ++pos_;
            out = Value(std::move(arr));
            return true;
        }
        for (;;) {
            skip_ws();
            Value v;
            if (!parse_value(v, depth + 1)) return false;
            arr.push_back(std::move(v));
            skip_ws();
            if (eof()) {
                set_error("unterminated array");
                return false;
            }
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            if (peek() == ']') {
                ++pos_;
                break;
            }
            set_error("expected ',' or ']'");
            return false;
        }
        out = Value(std::move(arr));
        return true;
    }

    bool parse_hex4(std::uint32_t& out) {
        if (pos_ + 4 > text_.size()) {
            set_error("truncated \\u escape");
            return false;
        }
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = text_[pos_++];
            v <<= 4;
            if (c >= '0' && c <= '9') {
                v |= static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                v |= static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                v |= static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                set_error("invalid hex digit in \\u escape");
                return false;
            }
        }
        out = v;
        return true;
    }

    bool parse_string(std::string& out) {
        if (!expect('"')) return false;
        out.clear();
        for (;;) {
            if (eof()) {
                set_error("unterminated string");
                return false;
            }
            char c = text_[pos_++];
            if (c == '"') return true;
            if (c != '\\') {
                if (static_cast<unsigned char>(c) < 0x20) {
                    set_error("unescaped control character in string");
                    return false;
                }
                out.push_back(c);
                continue;
            }
            if (eof()) {
                set_error("unterminated escape");
                return false;
            }
            char e = text_[pos_++];
            switch (e) {
                case '"':  out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/'); break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u': {
                    std::uint32_t cp = 0;
                    if (!parse_hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        // High surrogate: a low surrogate must follow.
                        if (pos_ + 1 < text_.size() && text_[pos_] == '\\' &&
                            text_[pos_ + 1] == 'u') {
                            pos_ += 2;
                            std::uint32_t low = 0;
                            if (!parse_hex4(low)) return false;
                            if (low < 0xDC00 || low > 0xDFFF) {
                                set_error("invalid low surrogate");
                                return false;
                            }
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            set_error("unpaired high surrogate");
                            return false;
                        }
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        set_error("unpaired low surrogate");
                        return false;
                    }
                    encode_utf8(out, cp);
                    break;
                }
                default:
                    set_error("invalid escape character");
                    return false;
            }
        }
    }

    bool parse_number(Value& out) {
        const std::size_t start = pos_;
        bool negative = false;
        if (!eof() && peek() == '-') {
            negative = true;
            ++pos_;
        }
        if (eof() || peek() < '0' || peek() > '9') {
            set_error("invalid number");
            return false;
        }
        if (peek() == '0') {
            ++pos_;
        } else {
            while (!eof() && peek() >= '0' && peek() <= '9') ++pos_;
        }
        bool is_double = false;
        if (!eof() && peek() == '.') {
            is_double = true;
            ++pos_;
            if (eof() || peek() < '0' || peek() > '9') {
                set_error("expected digit after decimal point");
                return false;
            }
            while (!eof() && peek() >= '0' && peek() <= '9') ++pos_;
        }
        if (!eof() && (peek() == 'e' || peek() == 'E')) {
            is_double = true;
            ++pos_;
            if (!eof() && (peek() == '+' || peek() == '-')) ++pos_;
            if (eof() || peek() < '0' || peek() > '9') {
                set_error("expected digit in exponent");
                return false;
            }
            while (!eof() && peek() >= '0' && peek() <= '9') ++pos_;
        }

        const std::string token(text_.substr(start, pos_ - start));
        if (is_double) {
            out = Value(std::strtod(token.c_str(), nullptr));
            return true;
        }
        errno = 0;
        if (negative) {
            char* end = nullptr;
            long long v = std::strtoll(token.c_str(), &end, 10);
            if (errno == ERANGE) {
                set_error("integer out of range");
                return false;
            }
            out = Value(v);
        } else {
            char* end = nullptr;
            unsigned long long v = std::strtoull(token.c_str(), &end, 10);
            if (errno == ERANGE) {
                set_error("integer out of range");
                return false;
            }
            out = Value(v);
        }
        return true;
    }
};

bool write_canonical(const Value& v, std::string& out) {
    switch (v.type()) {
        case Type::Null: out += "null"; return true;
        case Type::Bool: out += v.as_bool() ? "true" : "false"; return true;
        case Type::Int: {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%lld",
                          static_cast<long long>(v.as_int()));
            out += buf;
            return true;
        }
        case Type::UInt: {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%llu",
                          static_cast<unsigned long long>(v.as_uint()));
            out += buf;
            return true;
        }
        case Type::Double:
            // Deliberate: floating point has no stable cross-platform textual
            // form we are willing to depend on for hashed artifacts.
            return false;
        case Type::String: append_escaped(out, v.as_string()); return true;
        case Type::Array: {
            out.push_back('[');
            bool first = true;
            for (const auto& item : v.as_array()) {
                if (!first) out.push_back(',');
                first = false;
                if (!write_canonical(item, out)) return false;
            }
            out.push_back(']');
            return true;
        }
        case Type::Object: {
            out.push_back('{');
            bool first = true;
            for (const auto& [key, item] : v.as_object()) {
                if (!first) out.push_back(',');
                first = false;
                append_escaped(out, key);
                out.push_back(':');
                if (!write_canonical(item, out)) return false;
            }
            out.push_back('}');
            return true;
        }
    }
    return false;
}

void write_pretty(const Value& v, std::string& out, unsigned indent,
                  unsigned depth) {
    const std::string pad(static_cast<std::size_t>(indent) * depth, ' ');
    const std::string pad_inner(static_cast<std::size_t>(indent) * (depth + 1), ' ');
    switch (v.type()) {
        case Type::Null:
        case Type::Bool:
        case Type::Int:
        case Type::UInt:
        case Type::String:
            if (!write_canonical(v, out)) out += "null";
            return;
        case Type::Double: {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.17g", v.as_double());
            out += buf;
            return;
        }
        case Type::Array: {
            const Array& arr = v.as_array();
            if (arr.empty()) {
                out += "[]";
                return;
            }
            out += "[\n";
            for (std::size_t i = 0; i < arr.size(); ++i) {
                out += pad_inner;
                write_pretty(arr[i], out, indent, depth + 1);
                if (i + 1 < arr.size()) out.push_back(',');
                out.push_back('\n');
            }
            out += pad;
            out.push_back(']');
            return;
        }
        case Type::Object: {
            const Object& obj = v.as_object();
            if (obj.empty()) {
                out += "{}";
                return;
            }
            out += "{\n";
            std::size_t i = 0;
            for (const auto& [key, item] : obj) {
                out += pad_inner;
                append_escaped(out, key);
                out += ": ";
                write_pretty(item, out, indent, depth + 1);
                if (++i < obj.size()) out.push_back(',');
                out.push_back('\n');
            }
            out += pad;
            out.push_back('}');
            return;
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Value
// ---------------------------------------------------------------------------

Value::Value(bool v) : type_(Type::Bool), bool_(v) {}
Value::Value(int v) : type_(Type::Int), int_(v) {}
Value::Value(long long v) : type_(Type::Int), int_(v) {}
Value::Value(unsigned long long v) : type_(Type::UInt), uint_(v) {}
Value::Value(double v) : type_(Type::Double), double_(v) {}
Value::Value(const char* v) : type_(Type::String), string_(v ? v : "") {}
Value::Value(std::string v) : type_(Type::String), string_(std::move(v)) {}
Value::Value(Array v) : type_(Type::Array), array_(std::move(v)) {}
Value::Value(Object v) : type_(Type::Object), object_(std::move(v)) {}

bool Value::is_number() const {
    return type_ == Type::Int || type_ == Type::UInt || type_ == Type::Double;
}

bool Value::as_bool(bool fallback) const {
    return type_ == Type::Bool ? bool_ : fallback;
}

std::int64_t Value::as_int(std::int64_t fallback) const {
    switch (type_) {
        case Type::Int: return int_;
        case Type::UInt: return static_cast<std::int64_t>(uint_);
        case Type::Double: return static_cast<std::int64_t>(double_);
        default: return fallback;
    }
}

std::uint64_t Value::as_uint(std::uint64_t fallback) const {
    switch (type_) {
        case Type::UInt: return uint_;
        case Type::Int: return int_ < 0 ? fallback : static_cast<std::uint64_t>(int_);
        case Type::Double:
            return double_ < 0 ? fallback : static_cast<std::uint64_t>(double_);
        default: return fallback;
    }
}

double Value::as_double(double fallback) const {
    switch (type_) {
        case Type::Double: return double_;
        case Type::Int: return static_cast<double>(int_);
        case Type::UInt: return static_cast<double>(uint_);
        default: return fallback;
    }
}

const std::string& Value::as_string() const {
    return type_ == Type::String ? string_ : kEmptyString;
}

const Array& Value::as_array() const {
    return type_ == Type::Array ? array_ : kEmptyArray;
}

const Object& Value::as_object() const {
    return type_ == Type::Object ? object_ : kEmptyObject;
}

Array& Value::array_ref() {
    if (type_ != Type::Array) {
        type_ = Type::Array;
        array_.clear();
    }
    return array_;
}

Object& Value::object_ref() {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        object_.clear();
    }
    return object_;
}

const Value* Value::find(std::string_view key) const {
    if (type_ != Type::Object) return nullptr;
    auto it = object_.find(std::string(key));
    return it == object_.end() ? nullptr : &it->second;
}

Value& Value::operator[](const std::string& key) {
    return object_ref()[key];
}

void Value::push_back(Value v) {
    array_ref().push_back(std::move(v));
}

bool operator==(const Value& a, const Value& b) {
    if (a.type_ != b.type_) {
        // Int/UInt hold the same mathematical value when both are
        // non-negative; treat them as equal so round-trips compare cleanly.
        const bool a_num = a.type_ == Type::Int || a.type_ == Type::UInt;
        const bool b_num = b.type_ == Type::Int || b.type_ == Type::UInt;
        if (a_num && b_num) {
            if (a.as_int(-1) < 0 || b.as_int(-1) < 0) return false;
            return a.as_uint() == b.as_uint();
        }
        return false;
    }
    switch (a.type_) {
        case Type::Null: return true;
        case Type::Bool: return a.bool_ == b.bool_;
        case Type::Int: return a.int_ == b.int_;
        case Type::UInt: return a.uint_ == b.uint_;
        case Type::Double: return a.double_ == b.double_;
        case Type::String: return a.string_ == b.string_;
        case Type::Array: return a.array_ == b.array_;
        case Type::Object: return a.object_ == b.object_;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Free functions
// ---------------------------------------------------------------------------

std::string ParseError::to_string() const {
    return "line " + std::to_string(line) + ", column " + std::to_string(column) +
           ": " + message;
}

ParseResult parse(std::string_view text) {
    Parser p(text);
    return p.run();
}

std::optional<std::string> serialize_canonical(const Value& v) {
    std::string out;
    out.reserve(1024);
    if (!write_canonical(v, out)) return std::nullopt;
    return out;
}

std::string serialize_pretty(const Value& v, unsigned indent) {
    std::string out;
    out.reserve(2048);
    write_pretty(v, out, indent, 0);
    out.push_back('\n');
    return out;
}

std::string to_hex(std::uint64_t value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%llx",
                  static_cast<unsigned long long>(value));
    return buf;
}

std::optional<std::uint64_t> from_hex(std::string_view text) {
    if (text.size() < 3) return std::nullopt;
    if (text[0] != '0' || (text[1] != 'x' && text[1] != 'X')) return std::nullopt;
    std::uint64_t value = 0;
    for (std::size_t i = 2; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '_') continue;  // tolerated as a digit separator on input
        std::uint64_t digit;
        if (c >= '0' && c <= '9') {
            digit = static_cast<std::uint64_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = static_cast<std::uint64_t>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            digit = static_cast<std::uint64_t>(c - 'A' + 10);
        } else {
            return std::nullopt;
        }
        if (value > (UINT64_MAX - digit) / 16) return std::nullopt;  // overflow
        value = value * 16 + digit;
    }
    return value;
}

}  // namespace rs::json

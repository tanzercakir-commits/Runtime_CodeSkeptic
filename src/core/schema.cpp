// SPDX-License-Identifier: Apache-2.0
#include "runtimeskeptic/core/schema.hpp"

#include <regex>
#include <string>

namespace rs::schema {
namespace {

using json::Value;

std::string join(const std::string& path, const std::string& key) {
    return path.empty() ? key : path + "/" + key;
}

std::string where(const std::string& path) {
    return path.empty() ? "<root>" : path;
}

// One JSON type name against a value. "integer" is Int or UInt only - this
// project's schemas never carry doubles, and a fractional number cannot be a
// size, address, page count or line.
bool matches_type(const Value& v, const std::string& t) {
    if (t == "object") return v.is_object();
    if (t == "array") return v.is_array();
    if (t == "string") return v.is_string();
    if (t == "boolean") return v.is_bool();
    if (t == "null") return v.is_null();
    if (t == "integer")
        return v.type() == json::Type::Int || v.type() == json::Type::UInt;
    if (t == "number") return v.is_number();
    return false;  // unknown type name in a schema: fail closed
}

// A single schema evaluation carries the root it resolves local refs against
// and the store it resolves cross-file refs through. `root` changes when a ref
// crosses into another file, so that file's own #/ refs resolve correctly.
struct Context {
    const Value* root;
    const Store* store;
};

// Resolve a $ref to its target schema and the root to evaluate it under.
// Handles local "#/a/b" pointers and cross-file "file.json" (optionally with a
// "#/a/b" fragment) via the store. Returns false if it cannot be resolved.
bool resolve_ref(const std::string& ref, const Context& ctx,
                 const Value*& target, const Value*& new_root) {
    std::string base = ref;
    std::string fragment;
    if (auto hash = ref.find('#'); hash != std::string::npos) {
        base = ref.substr(0, hash);
        fragment = ref.substr(hash + 1);  // leading '/' included, or empty
    }

    // Pick the document the fragment walks: the current root for a pure "#/..."
    // ref, otherwise the sibling schema named by `base`.
    const Value* doc = ctx.root;
    if (!base.empty()) {
        doc = ctx.store != nullptr ? ctx.store->find(base) : nullptr;
        if (doc == nullptr) return false;
    }
    new_root = doc;

    // No fragment: the whole document is the target.
    if (fragment.empty() || fragment == "/") {
        target = doc;
        return true;
    }

    // Walk a JSON Pointer ("/a/b/c") from the document root.
    const Value* node = doc;
    std::size_t pos = 0;
    while (pos < fragment.size()) {
        if (fragment[pos] != '/') return false;
        std::size_t next = fragment.find('/', pos + 1);
        std::string token = fragment.substr(pos + 1, next == std::string::npos
                                                          ? std::string::npos
                                                          : next - (pos + 1));
        // JSON Pointer escaping: ~1 -> '/', ~0 -> '~'.
        for (std::size_t i = 0; i + 1 < token.size();) {
            if (token[i] == '~') {
                token.replace(i, 2, token[i + 1] == '1' ? "/" : "~");
            }
            ++i;
        }
        if (!node->is_object()) return false;
        node = node->find(token);
        if (node == nullptr) return false;
        pos = (next == std::string::npos) ? fragment.size() : next;
    }
    target = node;
    return true;
}

bool check(const Value& v, const Value& sub, const Context& ctx,
           const std::string& path, std::string& error);

// value >= minimum, for the small non-negative minimums our schemas use (0/1).
bool at_least(const Value& v, const Value& bound) {
    if (v.type() == json::Type::Int && v.as_int() < 0) return false;  // < any >=0
    return v.as_uint() >= bound.as_uint();
}
// value <= maximum. Our maximum is 2^64-1, which every representable integer
// satisfies; a larger literal parses as a double and fails the integer type.
bool at_most(const Value& v, const Value& bound) {
    if (v.type() == json::Type::Int && v.as_int() < 0) return true;  // < any >=0
    if (bound.type() == json::Type::Int && bound.as_int() < 0) return false;
    return v.as_uint() <= bound.as_uint();
}

bool check_type(const Value& v, const Value& sub, const std::string& path,
                std::string& error) {
    const Value* t = sub.find("type");
    if (t == nullptr) return true;
    bool ok = false;
    std::string names;
    if (t->is_string()) {
        ok = matches_type(v, t->as_string());
        names = t->as_string();
    } else if (t->is_array()) {
        for (const auto& item : t->as_array()) {
            if (item.is_string()) {
                if (!names.empty()) names += "|";
                names += item.as_string();
                if (matches_type(v, item.as_string())) ok = true;
            }
        }
    }
    if (!ok) {
        error = where(path) + ": must be " + names;
        return false;
    }
    return true;
}

bool check_object(const Value& v, const Value& sub, const Context& ctx,
                  const std::string& path, std::string& error) {
    if (!v.is_object()) return true;  // type handled elsewhere
    const Value* required = sub.find("required");
    if (required != nullptr && required->is_array()) {
        for (const auto& r : required->as_array()) {
            if (r.is_string() && v.find(r.as_string()) == nullptr) {
                error = join(path, r.as_string()) + ": required property missing";
                return false;
            }
        }
    }
    const Value* props = sub.find("properties");
    const Value* addl = sub.find("additionalProperties");
    for (const auto& [key, child] : v.as_object()) {
        const Value* pschema =
            props != nullptr ? props->find(key) : nullptr;
        if (pschema != nullptr) {
            if (!check(child, *pschema, ctx, join(path, key), error)) {
                return false;
            }
            continue;
        }
        // Not a named property: additionalProperties decides.
        if (addl == nullptr) continue;              // absent == true == allow
        if (addl->is_bool()) {
            if (!addl->as_bool()) {
                error = join(path, key) +
                        ": additional property not allowed by the schema";
                return false;
            }
            continue;
        }
        if (addl->is_object()) {  // a schema every extra property must satisfy
            if (!check(child, *addl, ctx, join(path, key), error)) return false;
        }
    }
    return true;
}

bool check(const Value& v, const Value& sub, const Context& ctx,
           const std::string& path, std::string& error) {
    // $ref: validate against the referenced schema, under the root that schema
    // belongs to. In these documents a $ref carries at most a `description`
    // sibling, which is not a constraint - except allOf/properties siblings,
    // handled below by evaluating every keyword on `sub` regardless of $ref.
    if (const Value* ref = sub.find("$ref"); ref != nullptr && ref->is_string()) {
        const Value* target = nullptr;
        const Value* new_root = nullptr;
        if (!resolve_ref(ref->as_string(), ctx, target, new_root)) {
            error = where(path) + ": unresolved $ref " + ref->as_string();
            return false;
        }
        Context sub_ctx{new_root, ctx.store};
        if (!check(v, *target, sub_ctx, path, error)) return false;
        // fall through: keywords sitting alongside $ref still apply
    }

    if (!check_type(v, sub, path, error)) return false;

    if (const Value* c = sub.find("const"); c != nullptr) {
        if (!(v == *c)) {
            error = where(path) + ": does not equal the required constant";
            return false;
        }
    }
    if (const Value* en = sub.find("enum"); en != nullptr && en->is_array()) {
        bool found = false;
        for (const auto& option : en->as_array()) {
            if (v == option) { found = true; break; }
        }
        if (!found) {
            error = where(path) + ": not one of the allowed values";
            return false;
        }
    }

    if (!check_object(v, sub, ctx, path, error)) return false;

    if (v.is_array()) {
        if (const Value* items = sub.find("items"); items != nullptr) {
            std::size_t i = 0;
            for (const auto& item : v.as_array()) {
                if (!check(item, *items, ctx,
                           join(path, std::to_string(i)), error)) {
                    return false;
                }
                ++i;
            }
        }
    }

    if (v.is_string()) {
        if (const Value* pat = sub.find("pattern");
            pat != nullptr && pat->is_string()) {
            try {
                if (!std::regex_search(v.as_string(),
                                       std::regex(pat->as_string()))) {
                    error = where(path) + ": does not match pattern " +
                            pat->as_string();
                    return false;
                }
            } catch (const std::regex_error&) {
                error = where(path) + ": invalid pattern in schema";
                return false;
            }
        }
    }

    if (v.type() == json::Type::Int || v.type() == json::Type::UInt) {
        if (const Value* mn = sub.find("minimum");
            mn != nullptr && mn->is_number() && !at_least(v, *mn)) {
            error = where(path) + ": below the minimum";
            return false;
        }
        if (const Value* mx = sub.find("maximum");
            mx != nullptr && mx->is_number() && !at_most(v, *mx)) {
            error = where(path) + ": above the maximum";
            return false;
        }
    }

    if (const Value* all = sub.find("allOf"); all != nullptr && all->is_array()) {
        for (const auto& option : all->as_array()) {
            if (!check(v, option, ctx, path, error)) return false;
        }
    }

    // if/then/else: when `if` validates, `then` must; otherwise `else` must.
    // The project uses exactly one such rule - a request that demands an exact
    // address must carry one - so the published schema states the cross-field
    // constraint the parser has always enforced, and jsonschema agrees.
    if (const Value* iff = sub.find("if"); iff != nullptr) {
        std::string ignored;
        const bool cond = check(v, *iff, ctx, path, ignored);
        const Value* branch = sub.find(cond ? "then" : "else");
        if (branch != nullptr && !check(v, *branch, ctx, path, error)) {
            return false;
        }
    }

    if (const Value* any = sub.find("anyOf"); any != nullptr && any->is_array()) {
        bool ok = false;
        for (const auto& option : any->as_array()) {
            std::string ignored;
            if (check(v, option, ctx, path, ignored)) { ok = true; break; }
        }
        if (!ok) {
            error = where(path) + ": does not match any allowed alternative";
            return false;
        }
    }

    return true;
}

}  // namespace

void Store::add(std::string key, const Value* schema) {
    entries_.emplace_back(std::move(key), schema);
    if (schema != nullptr) {
        if (const Value* id = schema->find("$id");
            id != nullptr && id->is_string()) {
            entries_.emplace_back(id->as_string(), schema);
        }
    }
}

const Value* Store::find(const std::string& ref) const {
    for (const auto& [key, schema] : entries_) {
        if (key == ref) return schema;
    }
    return nullptr;
}

bool validate(const Value& doc, const Value& schema, std::string& error) {
    Store empty;
    Context ctx{&schema, &empty};
    return check(doc, schema, ctx, "", error);
}

bool validate(const Value& doc, const Value& schema, const Store& store,
              std::string& error) {
    Context ctx{&schema, &store};
    return check(doc, schema, ctx, "", error);
}

}  // namespace rs::schema

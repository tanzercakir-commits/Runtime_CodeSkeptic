// SPDX-License-Identifier: Apache-2.0
#include "runtimeskeptic/vm/requirement.hpp"

#include <algorithm>
#include <array>
#include <utility>

#include "runtimeskeptic/core/sha256.hpp"

namespace rs::vm {
namespace {

template <typename Enum, std::size_t N>
bool lookup(const std::array<std::pair<std::string_view, Enum>, N>& table,
            std::string_view s, Enum& out) {
    for (const auto& [name, value] : table) {
        if (name == s) {
            out = value;
            return true;
        }
    }
    return false;
}

template <typename Enum, std::size_t N>
std::string_view name_of(
    const std::array<std::pair<std::string_view, Enum>, N>& table, Enum value) {
    for (const auto& [name, v] : table) {
        if (v == value) return name;
    }
    return "unknown";
}

constexpr std::array<std::pair<std::string_view, OperationKind>, 5> kOperation{{
    {"virtual_memory_map", OperationKind::VirtualMemoryMap},
    {"virtual_memory_protect", OperationKind::VirtualMemoryProtect},
    {"virtual_memory_reserve", OperationKind::VirtualMemoryReserve},
    {"virtual_memory_commit", OperationKind::VirtualMemoryCommit},
    {"unknown", OperationKind::Unknown},
}};

constexpr std::array<std::pair<std::string_view, FailureSinkKind>, 7> kSink{{
    {"fatal_assert", FailureSinkKind::FatalAssert},
    {"process_exit", FailureSinkKind::ProcessExit},
    {"error_return", FailureSinkKind::ErrorReturn},
    {"retry_loop", FailureSinkKind::RetryLoop},
    {"unchecked", FailureSinkKind::Unchecked},
    {"none", FailureSinkKind::None},
    {"unknown", FailureSinkKind::Unknown},
}};

constexpr std::array<std::pair<std::string_view, FallbackKind>, 6> kFallback{{
    {"relocate", FallbackKind::Relocate},
    {"smaller_size", FallbackKind::SmallerSize},
    {"weaker_protection", FallbackKind::WeakerProtection},
    {"non_executable", FallbackKind::NonExecutable},
    {"none", FallbackKind::None},
    {"unknown", FallbackKind::Unknown},
}};

std::optional<std::uint64_t> read_optional_uint(const json::Value* node) {
    if (node == nullptr || node->is_null()) return std::nullopt;
    if (node->type() != json::Type::UInt && node->type() != json::Type::Int) {
        return std::nullopt;
    }
    return node->as_uint();
}

bool read_flag(const json::Value* parent, const char* key, bool& out,
               std::string& error) {
    if (parent == nullptr) return true;
    const json::Value* node = parent->find(key);
    if (node == nullptr || node->is_null()) return true;
    if (!node->is_bool()) {
        error = std::string(key) + " must be a boolean";
        return false;
    }
    out = node->as_bool();
    return true;
}

}  // namespace

std::string_view to_string(OperationKind v) { return name_of(kOperation, v); }
std::string_view to_string(FailureSinkKind v) { return name_of(kSink, v); }
std::string_view to_string(FallbackKind v) { return name_of(kFallback, v); }

bool operation_kind_from_string(std::string_view s, OperationKind& out) {
    return lookup(kOperation, s, out);
}
bool failure_sink_from_string(std::string_view s, FailureSinkKind& out) {
    return lookup(kSink, s, out);
}
bool fallback_from_string(std::string_view s, FallbackKind& out) {
    return lookup(kFallback, s, out);
}

std::string Protection::to_string() const {
    std::string s;
    s.push_back(read ? 'r' : '-');
    s.push_back(write ? 'w' : '-');
    s.push_back(execute ? 'x' : '-');
    return s;
}

json::Value Protection::to_json() const {
    json::Value v = json::Value::object();
    v["read"] = read;
    v["write"] = write;
    v["execute"] = execute;
    return v;
}

json::Value SourceLocation::to_json() const {
    json::Value v = json::Value::object();
    v["file"] = file;
    v["line"] = static_cast<unsigned long long>(line);
    if (!symbol.empty()) v["symbol"] = symbol;
    return v;
}

std::string SourceLocation::to_string() const {
    std::string s = file;
    if (line != 0) s += ":" + std::to_string(line);
    if (!symbol.empty()) s += " (" + symbol + ")";
    return s;
}

json::Value MappingRequest::to_json() const {
    json::Value v = json::Value::object();
    v["address"] = address ? json::Value(json::to_hex(*address)) : json::Value();
    v["size"] = static_cast<unsigned long long>(size);
    v["exact_address_required"] = exact_address_required;
    v["required_alignment"] =
        required_alignment ? json::Value(static_cast<unsigned long long>(*required_alignment))
                           : json::Value();
    v["required_page_size"] =
        required_page_size ? json::Value(static_cast<unsigned long long>(*required_page_size))
                           : json::Value();
    v["protection"] = protection.to_json();
    v["write_then_execute"] = write_then_execute;
    v["simultaneous_write_execute"] = simultaneous_write_execute;
    v["file_backed"] = file_backed;
    v["file_length"] = file_length
                           ? json::Value(static_cast<unsigned long long>(*file_length))
                           : json::Value();
    v["file_offset"] = static_cast<unsigned long long>(file_offset);
    v["accesses_beyond_eof"] = accesses_beyond_eof;
    v["reserve_then_commit"] = reserve_then_commit;
    return v;
}

json::Value Assumptions::to_json() const {
    json::Value v = json::Value::object();
    v["guest_host_identity_required"] = guest_host_identity_required;
    v["translation_layer_available"] = translation_layer_available;
    v["pointer_storage_width_bits"] =
        pointer_storage_width_bits
            ? json::Value(static_cast<unsigned long long>(*pointer_storage_width_bits))
            : json::Value();
    v["retries_on_failure"] = retries_on_failure;
    v["max_retries"] = max_retries
                           ? json::Value(static_cast<unsigned long long>(*max_retries))
                           : json::Value();
    return v;
}

json::Value FailureSink::to_json() const {
    json::Value v = json::Value::object();
    v["kind"] = std::string(rs::vm::to_string(kind));
    v["location"] = location ? location->to_json() : json::Value();
    if (!description.empty()) v["description"] = description;
    return v;
}

bool Requirement::permits(FallbackKind kind) const {
    return std::find(permitted_fallbacks.begin(), permitted_fallbacks.end(),
                     kind) != permitted_fallbacks.end();
}

json::Value Requirement::to_json() const {
    json::Value v = json::Value::object();
    v["schema"] = schema;
    v["name"] = name;
    v["component"] = component;
    v["operation"] = std::string(rs::vm::to_string(operation));
    v["request"] = request.to_json();
    v["assumptions"] = assumptions.to_json();

    json::Value posts = json::Value::array();
    for (const auto& p : required_postconditions) posts.push_back(json::Value(p));
    v["required_postconditions"] = posts;

    json::Value fallbacks = json::Value::array();
    for (auto f : permitted_fallbacks) {
        fallbacks.push_back(json::Value(std::string(rs::vm::to_string(f))));
    }
    v["permitted_fallbacks"] = fallbacks;

    v["failure_sink"] = failure_sink.to_json();

    json::Value locs = json::Value::array();
    for (const auto& l : source_locations) locs.push_back(l.to_json());
    v["source_locations"] = locs;

    v["assumption_evidence"] = std::string(rs::to_string(assumption_evidence));

    json::Value limitations = json::Value::array();
    for (const auto& l : extraction_limitations) {
        limitations.push_back(json::Value(l));
    }
    v["extraction_limitations"] = limitations;
    return v;
}

std::string Requirement::requirement_id() const {
    auto canonical = json::serialize_canonical(to_json());
    if (!canonical) return "sha256:unavailable";
    return rs::hash::sha256_uri(*canonical);
}

std::optional<Requirement> Requirement::from_json(const json::Value& v,
                                                   std::string& error) {
    if (!v.is_object()) {
        error = "requirement document must be a JSON object";
        return std::nullopt;
    }
    const json::Value* schema = v.find("schema");
    if (schema == nullptr || !schema->is_string()) {
        error = "requirement requires a 'schema' string";
        return std::nullopt;
    }
    if (schema->as_string() != kRequirementSchema) {
        error = "unsupported requirement schema: " + schema->as_string() +
                " (expected " + kRequirementSchema + ")";
        return std::nullopt;
    }

    Requirement r;
    r.schema = schema->as_string();
    if (const json::Value* n = v.find("name"); n != nullptr) r.name = n->as_string();
    if (const json::Value* n = v.find("component"); n != nullptr) {
        r.component = n->as_string();
    }

    const json::Value* op = v.find("operation");
    if (op == nullptr || !op->is_string() ||
        !operation_kind_from_string(op->as_string(), r.operation)) {
        error = "requirement requires a recognized 'operation'";
        return std::nullopt;
    }
    if (r.operation == OperationKind::Unknown) {
        error = "operation 'unknown' cannot be analyzed";
        return std::nullopt;
    }

    const json::Value* req = v.find("request");
    if (req == nullptr || !req->is_object()) {
        error = "requirement requires a 'request' object";
        return std::nullopt;
    }

    if (const json::Value* addr = req->find("address");
        addr != nullptr && !addr->is_null()) {
        if (addr->is_string()) {
            auto parsed = json::from_hex(addr->as_string());
            if (!parsed) {
                error = "request.address must be a hex string like \"0x1000\"";
                return std::nullopt;
            }
            r.request.address = *parsed;
        } else if (addr->type() == json::Type::UInt ||
                   addr->type() == json::Type::Int) {
            r.request.address = addr->as_uint();
        } else {
            error = "request.address must be a hex string";
            return std::nullopt;
        }
    }

    const json::Value* size = req->find("size");
    if (size == nullptr ||
        (size->type() != json::Type::UInt && size->type() != json::Type::Int)) {
        error = "request.size is required and must be an integer";
        return std::nullopt;
    }
    r.request.size = size->as_uint();
    if (r.request.size == 0) {
        error = "request.size must be greater than zero";
        return std::nullopt;
    }

    if (!read_flag(req, "exact_address_required", r.request.exact_address_required,
                   error)) {
        return std::nullopt;
    }
    if (r.request.exact_address_required && !r.request.address) {
        error = "exact_address_required is true but no request.address was given";
        return std::nullopt;
    }

    r.request.required_alignment = read_optional_uint(req->find("required_alignment"));
    r.request.required_page_size = read_optional_uint(req->find("required_page_size"));

    if (const json::Value* prot = req->find("protection"); prot != nullptr) {
        if (!read_flag(prot, "read", r.request.protection.read, error)) {
            return std::nullopt;
        }
        if (!read_flag(prot, "write", r.request.protection.write, error)) {
            return std::nullopt;
        }
        if (!read_flag(prot, "execute", r.request.protection.execute, error)) {
            return std::nullopt;
        }
    }

    if (!read_flag(req, "write_then_execute", r.request.write_then_execute, error) ||
        !read_flag(req, "simultaneous_write_execute",
                   r.request.simultaneous_write_execute, error) ||
        !read_flag(req, "file_backed", r.request.file_backed, error) ||
        !read_flag(req, "accesses_beyond_eof", r.request.accesses_beyond_eof,
                   error) ||
        !read_flag(req, "reserve_then_commit", r.request.reserve_then_commit,
                   error)) {
        return std::nullopt;
    }
    r.request.file_length = read_optional_uint(req->find("file_length"));
    r.request.file_offset = read_optional_uint(req->find("file_offset")).value_or(0);

    if (const json::Value* a = v.find("assumptions"); a != nullptr) {
        if (!read_flag(a, "guest_host_identity_required",
                       r.assumptions.guest_host_identity_required, error) ||
            !read_flag(a, "translation_layer_available",
                       r.assumptions.translation_layer_available, error) ||
            !read_flag(a, "retries_on_failure", r.assumptions.retries_on_failure,
                       error)) {
            return std::nullopt;
        }
        r.assumptions.pointer_storage_width_bits =
            read_optional_uint(a->find("pointer_storage_width_bits"));
        r.assumptions.max_retries = read_optional_uint(a->find("max_retries"));
    }

    if (const json::Value* posts = v.find("required_postconditions");
        posts != nullptr && posts->is_array()) {
        for (const auto& p : posts->as_array()) {
            if (p.is_string()) r.required_postconditions.push_back(p.as_string());
        }
    }

    if (const json::Value* fbs = v.find("permitted_fallbacks");
        fbs != nullptr && fbs->is_array()) {
        for (const auto& f : fbs->as_array()) {
            FallbackKind kind = FallbackKind::Unknown;
            if (!f.is_string() || !fallback_from_string(f.as_string(), kind)) {
                error = "unrecognized entry in permitted_fallbacks";
                return std::nullopt;
            }
            r.permitted_fallbacks.push_back(kind);
        }
    }

    if (const json::Value* sink = v.find("failure_sink"); sink != nullptr) {
        const json::Value* kind = sink->find("kind");
        if (kind == nullptr || !kind->is_string() ||
            !failure_sink_from_string(kind->as_string(), r.failure_sink.kind)) {
            error = "failure_sink.kind is required and must be recognized";
            return std::nullopt;
        }
        if (const json::Value* d = sink->find("description"); d != nullptr) {
            r.failure_sink.description = d->as_string();
        }
        if (const json::Value* loc = sink->find("location");
            loc != nullptr && loc->is_object()) {
            SourceLocation sl;
            if (const json::Value* f = loc->find("file"); f != nullptr) {
                sl.file = f->as_string();
            }
            if (const json::Value* l = loc->find("line"); l != nullptr) {
                sl.line = l->as_uint();
            }
            if (const json::Value* s = loc->find("symbol"); s != nullptr) {
                sl.symbol = s->as_string();
            }
            r.failure_sink.location = sl;
        }
    }

    if (const json::Value* locs = v.find("source_locations");
        locs != nullptr && locs->is_array()) {
        for (const auto& item : locs->as_array()) {
            if (!item.is_object()) continue;
            SourceLocation sl;
            if (const json::Value* f = item.find("file"); f != nullptr) {
                sl.file = f->as_string();
            }
            if (const json::Value* l = item.find("line"); l != nullptr) {
                sl.line = l->as_uint();
            }
            if (const json::Value* s = item.find("symbol"); s != nullptr) {
                sl.symbol = s->as_string();
            }
            r.source_locations.push_back(sl);
        }
    }

    const json::Value* ae = v.find("assumption_evidence");
    if (ae == nullptr || !ae->is_string() ||
        !evidence_class_from_string(ae->as_string(), r.assumption_evidence)) {
        error = "requirement requires an 'assumption_evidence' class "
                "(specified_guarantee for a hand-declared contract, "
                "statically_inferred for an extracted one)";
        return std::nullopt;
    }

    if (const json::Value* limitations = v.find("extraction_limitations");
        limitations != nullptr && limitations->is_array()) {
        for (const auto& item : limitations->as_array()) {
            if (item.is_string()) {
                r.extraction_limitations.push_back(item.as_string());
            }
        }
    }

    return r;
}

std::optional<RequirementBundle> load_requirements(const json::Value& v,
                                                   std::string& error) {
    if (!v.is_object()) {
        error = "document must be a JSON object";
        return std::nullopt;
    }
    const json::Value* schema = v.find("schema");
    if (schema == nullptr || !schema->is_string()) {
        error = "document requires a 'schema' string";
        return std::nullopt;
    }

    // A lone requirement is treated as a bundle of one, so callers never
    // branch on which shape they were given.
    if (schema->as_string() == kRequirementSchema) {
        auto single = Requirement::from_json(v, error);
        if (!single) return std::nullopt;
        RequirementBundle bundle;
        bundle.schema = kRequirementSchema;
        bundle.requirements.push_back(std::move(*single));
        return bundle;
    }

    if (schema->as_string() != kRequirementBundleSchema) {
        error = "unsupported schema: " + schema->as_string() + " (expected " +
                kRequirementSchema + " or " + kRequirementBundleSchema + ")";
        return std::nullopt;
    }

    RequirementBundle bundle;
    bundle.schema = schema->as_string();
    if (const json::Value* producer = v.find("producer"); producer != nullptr) {
        auto str = [&](const char* key) -> std::string {
            const json::Value* n = producer->find(key);
            return n == nullptr ? std::string() : n->as_string();
        };
        bundle.producer_tool = str("tool");
        bundle.producer_version = str("version");
        bundle.producer_rule = str("rule");
    }

    const json::Value* requirements = v.find("requirements");
    if (requirements == nullptr || !requirements->is_array()) {
        error = "bundle requires a 'requirements' array";
        return std::nullopt;
    }

    std::size_t index = 0;
    for (const auto& item : requirements->as_array()) {
        std::string item_error;
        auto parsed = Requirement::from_json(item, item_error);
        if (parsed) {
            bundle.requirements.push_back(std::move(*parsed));
        } else {
            // Rejected rather than fatal: one malformed entry must not throw
            // away the rest of a large extraction run.
            bundle.rejected.push_back("requirements[" + std::to_string(index) +
                                      "]: " + item_error);
        }
        ++index;
    }
    return bundle;
}

}  // namespace rs::vm

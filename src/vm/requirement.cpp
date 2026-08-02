// SPDX-License-Identifier: Apache-2.0
#include "runtimeskeptic/vm/requirement.hpp"

#include <algorithm>
#include <array>
#include <utility>

#include "runtimeskeptic/core/schema_registry.hpp"
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

constexpr std::array<std::pair<std::string_view, SizeRelation>, 3> kSizeRelation{{
    {"equal", SizeRelation::Equal},
    {"at_most", SizeRelation::AtMost},
    {"at_least", SizeRelation::AtLeast},
}};

constexpr std::array<std::pair<std::string_view, FallbackKind>, 6> kFallback{{
    {"relocate", FallbackKind::Relocate},
    {"smaller_size", FallbackKind::SmallerSize},
    {"weaker_protection", FallbackKind::WeakerProtection},
    {"non_executable", FallbackKind::NonExecutable},
    {"none", FallbackKind::None},
    {"unknown", FallbackKind::Unknown},
}};

// Reads an optional unsigned integer. Absent or null means "not given" and is
// fine. A value of the WRONG TYPE is a schema violation, not an absent field:
// the old version returned nullopt for it, which silently dropped
// `required_page_size:"16384"` (a string) and let the analysis proceed as if no
// page size were required and pass. A negative is rejected here too, in one
// place, so as_uint() can never fabricate a 0 or a giant unsigned from a -1.
bool read_optional_uint(const json::Value* node, const char* label,
                        std::optional<std::uint64_t>& out, std::string& error) {
    if (node == nullptr || node->is_null()) {
        out = std::nullopt;
        return true;
    }
    if (node->type() != json::Type::UInt && node->type() != json::Type::Int) {
        error = std::string(label) + " must be an integer";
        return false;
    }
    if (node->type() == json::Type::Int && node->as_int() < 0) {
        error = std::string(label) + " must not be negative";
        return false;
    }
    out = node->as_uint();
    return true;
}

// A negative integer in a size / count / address / page-size field is not a
// smaller value to accept - as_uint() would turn -1 into a fabricated 0 or a
// giant unsigned, and the report would then present a number the document never
// gave as a PROVEN fact. Reject it with the field named. Returns false on a
// negative, true otherwise (including absent or non-integer, which other checks
// handle).
bool reject_negative(const json::Value* node, const char* key,
                     std::string& error) {
    if (node != nullptr && node->type() == json::Type::Int &&
        node->as_int() < 0) {
        error = std::string(key) + " must not be negative";
        return false;
    }
    return true;
}

bool read_flag(const json::Value* parent, const char* key, bool& out,
               std::string& error) {
    if (parent == nullptr) return true;
    const json::Value* node = parent->find(key);
    if (node == nullptr) return true;  // absent keeps the default
    // The schema types these flags `boolean`, not nullable: `null` is not a way
    // to say "absent", it is a wrong type. as_bool() on a null would coerce to
    // false, so a program that declared it needs write+execute could read as
    // needing neither. is_bool() is false for null, so this catches it.
    if (!node->is_bool()) {
        error = std::string(key) + " must be a boolean";
        return false;
    }
    out = node->as_bool();
    return true;
}

// How far past end-of-file the program actually reads. Absent means
// `whole_page_past_end`, the risky one, so an older contract that predates the
// field keeps the verdict it had rather than being quietly upgraded to safe.
bool read_eof_access_extent(const json::Value* parent,
                            MappingRequest::EofAccessExtent& out,
                            std::string& error) {
    if (parent == nullptr) return true;
    const json::Value* node = parent->find("eof_access_extent");
    if (node == nullptr || node->is_null()) return true;
    if (!node->is_string()) {
        error = "eof_access_extent must be a string";
        return false;
    }
    const std::string& s = node->as_string();
    if (s == "whole_page_past_end") {
        out = MappingRequest::EofAccessExtent::WholePagePastEnd;
        return true;
    }
    if (s == "within_final_partial_page") {
        out = MappingRequest::EofAccessExtent::WithinFinalPartialPage;
        return true;
    }
    error = "unrecognized eof_access_extent: " + s;
    return false;
}

// A string array where the schema types it so. Present-but-not-an-array, or a
// non-string entry, is a schema violation - the old readers skipped bad entries
// and ignored a non-array whole, so `required_postconditions: "x"` and
// `["a", 5]` both passed.
bool read_string_array_field(const json::Value* node, const char* label,
                             std::vector<std::string>& out, std::string& error) {
    if (node == nullptr) return true;  // absent is fine; null is a wrong type
    if (!node->is_array()) {
        error = std::string(label) + " must be an array of strings";
        return false;
    }
    for (const auto& item : node->as_array()) {
        if (!item.is_string()) {
            error = std::string(label) + " entries must all be strings";
            return false;
        }
        out.push_back(item.as_string());
    }
    return true;
}

// A source location: file/symbol are strings, line a non-negative integer. The
// old reader coerced each - a number `file`, a string `line`, a negative `line`
// all slipped through as_string()/as_uint().
bool read_source_location(const json::Value& v, const std::string& label,
                          SourceLocation& out, std::string& error) {
    if (!v.is_object()) {
        error = label + " must be an object";
        return false;
    }
    if (const json::Value* f = v.find("file"); f != nullptr && !f->is_null()) {
        if (!f->is_string()) {
            error = label + ".file must be a string";
            return false;
        }
        out.file = f->as_string();
    }
    if (const json::Value* l = v.find("line"); l != nullptr && !l->is_null()) {
        if ((l->type() != json::Type::UInt && l->type() != json::Type::Int) ||
            (l->type() == json::Type::Int && l->as_int() < 0)) {
            error = label + ".line must be a non-negative integer";
            return false;
        }
        out.line = l->as_uint();
    }
    if (const json::Value* s = v.find("symbol"); s != nullptr && !s->is_null()) {
        if (!s->is_string()) {
            error = label + ".symbol must be a string";
            return false;
        }
        out.symbol = s->as_string();
    }
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

std::string_view to_string(SizeRelation v) { return name_of(kSizeRelation, v); }
bool size_relation_from_string(std::string_view s, SizeRelation& out) {
    return lookup(kSizeRelation, s, out);
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
    v["address_min"] =
        address_min ? json::Value(json::to_hex(*address_min)) : json::Value();
    v["address_max"] =
        address_max ? json::Value(json::to_hex(*address_max)) : json::Value();
    v["max_displacement_bytes"] =
        max_displacement_bytes
            ? json::Value(static_cast<unsigned long long>(*max_displacement_bytes))
            : json::Value();
    v["displacement_reference"] = displacement_reference;
    v["commit_is_checked_call"] = commit_is_checked_call;
    v["required_page_size_relation"] =
        std::string(rs::vm::to_string(required_page_size_relation));
    v["validates_returned_address"] = validates_returned_address;
    v["protection"] = protection.to_json();
    v["write_then_execute"] = write_then_execute;
    v["simultaneous_write_execute"] = simultaneous_write_execute;
    v["file_backed"] = file_backed;
    v["file_length"] = file_length
                           ? json::Value(static_cast<unsigned long long>(*file_length))
                           : json::Value();
    v["file_offset"] = static_cast<unsigned long long>(file_offset);
    v["accesses_beyond_eof"] = accesses_beyond_eof;
    v["relies_on_unmapped_beyond_size"] = relies_on_unmapped_beyond_size;
    v["eof_access_extent"] =
        std::string(eof_access_extent == EofAccessExtent::WithinFinalPartialPage
                        ? "within_final_partial_page"
                        : "whole_page_past_end");
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

namespace {

// Every key the reader understands, so anything else can be reported rather
// than dropped. Kept adjacent to from_json on purpose: a field added below
// without a name added here shows up as unrecognized in the tool's own tests,
// which is a louder reminder than a comment.
constexpr const char* kKnownTopLevel[] = {
    "schema", "name", "component", "operation", "request", "assumptions",
    "required_postconditions", "permitted_fallbacks", "failure_sink",
    "assumption_evidence", "source_locations", "extraction_limitations",
    "producer", "rule",
};
constexpr const char* kKnownRequest[] = {
    "address", "size", "exact_address_required", "protection", "file_backed",
    "file_length", "file_offset", "accesses_beyond_eof", "eof_access_extent",
    "relies_on_unmapped_beyond_size",
    "required_alignment", "required_page_size", "required_page_size_relation",
    "write_then_execute", "simultaneous_write_execute", "reserve_then_commit",
    "commit_is_checked_call", "validates_returned_address", "address_min",
    "address_max", "max_displacement_bytes", "displacement_reference",
};
constexpr const char* kKnownAssumptions[] = {
    "guest_host_identity_required", "translation_layer_available",
    "retries_on_failure", "max_retries", "pointer_storage_width_bits",
};

template <std::size_t N>
void collect_unrecognized(const json::Value& node,
                          const char* const (&known)[N],
                          const std::string& prefix,
                          std::vector<std::string>& out) {
    if (!node.is_object()) return;
    for (const auto& entry : node.as_object()) {
        bool recognized = false;
        for (std::size_t i = 0; i < N; ++i) {
            if (entry.first == known[i]) { recognized = true; break; }
        }
        // Anything namespaced with x_ is an extension by convention and is
        // deliberately not reported: x_campaign and x_groundtruth carry this
        // project's own provenance notes.
        if (!recognized && entry.first.rfind("x_", 0) != 0) {
            out.push_back(prefix + entry.first);
        }
    }
}

}  // namespace

std::optional<Requirement> Requirement::from_json(const json::Value& v,
                                                   std::string& error) {
    if (!v.is_object()) {
        error = "requirement document must be a JSON object";
        return std::nullopt;
    }
    // The published schema IS the contract; the whole document is validated
    // against it here, before a single field is read. Four rounds of per-field
    // checks below each missed a different case (a wrong type read as absent, a
    // null container, a nested field); the schema lists every field once, so
    // none is forgotten. A bundle validates each item through this same path, so
    // one malformed entry is dropped, not the whole batch.
    if (std::string schema_error;
        !rs::schema::validate_requirement_input(v, schema_error)) {
        error = schema_error;
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
    // The published schema types `name` and `component` as strings. A number or
    // object here is a schema violation, not something to coerce to "" in
    // silence - which let a malformed document analyze on with the field gone
    // and still return a verdict.
    if (const json::Value* n = v.find("name"); n != nullptr) {
        if (!n->is_string()) {
            error = "name must be a string";
            return std::nullopt;
        }
        r.name = n->as_string();
    }
    if (const json::Value* n = v.find("component"); n != nullptr) {
        if (!n->is_string()) {
            error = "component must be a string";
            return std::nullopt;
        }
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
            if (!reject_negative(addr, "request.address", error)) {
                return std::nullopt;
            }
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
    if (!reject_negative(size, "request.size", error)) return std::nullopt;
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

    if (!read_optional_uint(req->find("required_alignment"),
                            "request.required_alignment",
                            r.request.required_alignment, error)) {
        return std::nullopt;
    }

    auto read_optional_address = [&](const char* key,
                                     std::optional<std::uint64_t>& out) -> bool {
        const json::Value* node = req->find(key);
        if (node == nullptr || node->is_null()) return true;
        if (node->is_string()) {
            auto parsed = json::from_hex(node->as_string());
            if (!parsed) {
                error = std::string("request.") + key +
                        " must be a hex string like \"0x80000000\"";
                return false;
            }
            out = *parsed;
            return true;
        }
        if (node->type() == json::Type::UInt || node->type() == json::Type::Int) {
            if (node->type() == json::Type::Int && node->as_int() < 0) {
                error = std::string("request.") + key + " must not be negative";
                return false;
            }
            out = node->as_uint();
            return true;
        }
        error = std::string("request.") + key + " must be a hex string";
        return false;
    };
    if (!read_optional_address("address_min", r.request.address_min)) {
        return std::nullopt;
    }
    if (!read_optional_address("address_max", r.request.address_max)) {
        return std::nullopt;
    }
    if (!read_optional_uint(req->find("max_displacement_bytes"),
                            "request.max_displacement_bytes",
                            r.request.max_displacement_bytes, error)) {
        return std::nullopt;
    }
    if (const json::Value* ref = req->find("displacement_reference");
        ref != nullptr && ref->is_string()) {
        r.request.displacement_reference = ref->as_string();
    }
    if (!read_optional_uint(req->find("required_page_size"),
                            "request.required_page_size",
                            r.request.required_page_size, error)) {
        return std::nullopt;
    }
    if (const json::Value* rel = req->find("required_page_size_relation");
        rel != nullptr && !rel->is_null()) {
        if (!rel->is_string() ||
            !size_relation_from_string(rel->as_string(),
                                       r.request.required_page_size_relation)) {
            error = "request.required_page_size_relation must be one of "
                    "equal / at_most / at_least";
            return std::nullopt;
        }
    }

    if (const json::Value* prot = req->find("protection");
        prot != nullptr && !prot->is_null()) {
        // The schema types protection as an object of read/write/execute
        // booleans. A string like "rwx" is a schema violation; left to
        // read_flag it silently found none of the keys and the request became
        // no-protection, so a program needing RWX read as needing nothing.
        if (!prot->is_object()) {
            error = "request.protection must be an object with read/write/"
                    "execute booleans";
            return std::nullopt;
        }
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
        !read_flag(req, "relies_on_unmapped_beyond_size",
                   r.request.relies_on_unmapped_beyond_size, error) ||
        !read_eof_access_extent(req, r.request.eof_access_extent, error) ||
        !read_flag(req, "validates_returned_address",
                   r.request.validates_returned_address, error) ||
        !read_flag(req, "commit_is_checked_call",
                   r.request.commit_is_checked_call, error) ||
        !read_flag(req, "reserve_then_commit", r.request.reserve_then_commit,
                   error)) {
        return std::nullopt;
    }
    if (!read_optional_uint(req->find("file_length"), "request.file_length",
                            r.request.file_length, error)) {
        return std::nullopt;
    }
    std::optional<std::uint64_t> file_offset;
    if (!read_optional_uint(req->find("file_offset"), "request.file_offset",
                            file_offset, error)) {
        return std::nullopt;
    }
    r.request.file_offset = file_offset.value_or(0);

    if (const json::Value* a = v.find("assumptions"); a != nullptr) {
        if (!read_flag(a, "guest_host_identity_required",
                       r.assumptions.guest_host_identity_required, error) ||
            !read_flag(a, "translation_layer_available",
                       r.assumptions.translation_layer_available, error) ||
            !read_flag(a, "retries_on_failure", r.assumptions.retries_on_failure,
                       error)) {
            return std::nullopt;
        }
        if (!read_optional_uint(a->find("pointer_storage_width_bits"),
                                "assumptions.pointer_storage_width_bits",
                                r.assumptions.pointer_storage_width_bits,
                                error)) {
            return std::nullopt;
        }
        if (!read_optional_uint(a->find("max_retries"), "assumptions.max_retries",
                                r.assumptions.max_retries, error)) {
            return std::nullopt;
        }
    }

    if (!read_string_array_field(v.find("required_postconditions"),
                                 "required_postconditions",
                                 r.required_postconditions, error)) {
        return std::nullopt;
    }

    if (const json::Value* fbs = v.find("permitted_fallbacks"); fbs != nullptr) {
        if (!fbs->is_array()) {
            error = "permitted_fallbacks must be an array";
            return std::nullopt;
        }
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
            if (!d->is_string()) {
                error = "failure_sink.description must be a string";
                return std::nullopt;
            }
            r.failure_sink.description = d->as_string();
        }
        // location is anyOf[sourceLocation, null]: null ("producer could not
        // determine one") is fine; a present non-null one must be well-typed.
        if (const json::Value* loc = sink->find("location");
            loc != nullptr && !loc->is_null()) {
            SourceLocation sl;
            if (!read_source_location(*loc, "failure_sink.location", sl, error)) {
                return std::nullopt;
            }
            r.failure_sink.location = sl;
        }
    }

    if (const json::Value* locs = v.find("source_locations"); locs != nullptr) {
        if (!locs->is_array()) {
            error = "source_locations must be an array";
            return std::nullopt;
        }
        std::size_t i = 0;
        for (const auto& item : locs->as_array()) {
            SourceLocation sl;
            if (!read_source_location(
                    item, "source_locations[" + std::to_string(i) + "]", sl,
                    error)) {
                return std::nullopt;
            }
            r.source_locations.push_back(sl);
            ++i;
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

    if (!read_string_array_field(v.find("extraction_limitations"),
                                 "extraction_limitations",
                                 r.extraction_limitations, error)) {
        return std::nullopt;
    }

    collect_unrecognized(v, kKnownTopLevel, "", r.unrecognized_fields);
    if (req != nullptr) {
        collect_unrecognized(*req, kKnownRequest, "request.",
                             r.unrecognized_fields);
    }
    if (const json::Value* assume = v.find("assumptions"); assume != nullptr) {
        collect_unrecognized(*assume, kKnownAssumptions, "assumptions.",
                             r.unrecognized_fields);
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

// SPDX-License-Identifier: Apache-2.0
#include "runtimeskeptic/vm/profile.hpp"

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

constexpr std::array<std::pair<std::string_view, OperatingSystem>, 5> kOs{{
    {"linux", OperatingSystem::Linux},
    {"macos", OperatingSystem::MacOS},
    {"windows", OperatingSystem::Windows},
    {"other", OperatingSystem::Other},
    {"unknown", OperatingSystem::Unknown},
}};

constexpr std::array<std::pair<std::string_view, Architecture>, 6> kArch{{
    {"x86_64", Architecture::X86_64},
    {"aarch64", Architecture::Aarch64},
    {"x86", Architecture::X86},
    {"arm", Architecture::Arm},
    {"other", Architecture::Other},
    {"unknown", Architecture::Unknown},
}};

constexpr std::array<std::pair<std::string_view, TranslationMode>, 6> kTranslation{{
    {"none", TranslationMode::None},
    {"rosetta2", TranslationMode::Rosetta2},
    {"wow64", TranslationMode::Wow64},
    {"qemu_user", TranslationMode::QemuUser},
    {"other", TranslationMode::Other},
    {"unknown", TranslationMode::Unknown},
}};

constexpr std::array<std::pair<std::string_view, ReserveCommitModel>, 3> kRc{{
    {"posix_lazy", ReserveCommitModel::PosixLazy},
    {"windows_reserve_commit", ReserveCommitModel::WindowsReserveCommit},
    {"unknown", ReserveCommitModel::Unknown},
}};

constexpr std::array<std::pair<std::string_view, BeyondEofBehavior>, 4> kEof{{
    {"sigbus", BeyondEofBehavior::Sigbus},
    {"error", BeyondEofBehavior::Error},
    {"zero_fill", BeyondEofBehavior::ZeroFill},
    {"unknown", BeyondEofBehavior::Unknown},
}};

constexpr std::array<std::pair<std::string_view, ProfileOrigin>, 4> kOrigin{{
    {"measured", ProfileOrigin::Measured},
    {"hand_authored_fixture", ProfileOrigin::HandAuthoredFixture},
    {"synthetic", ProfileOrigin::Synthetic},
    {"unknown", ProfileOrigin::Unknown},
}};

json::Value string_array(const std::vector<std::string>& items) {
    json::Value arr = json::Value::array();
    for (const auto& s : items) arr.push_back(json::Value(s));
    return arr;
}

std::vector<std::string> read_string_array(const json::Value* node) {
    std::vector<std::string> out;
    if (node == nullptr || !node->is_array()) return out;
    for (const auto& item : node->as_array()) {
        if (item.is_string()) out.push_back(item.as_string());
    }
    return out;
}

bool read_ranges(const json::Value* node, std::vector<ClassifiedRange>& out,
                 const char* field, std::string& error) {
    if (node == nullptr) return true;
    if (!node->is_array()) {
        error = std::string(field) + " must be an array";
        return false;
    }
    for (const auto& item : node->as_array()) {
        std::string local_error;
        auto range = ClassifiedRange::from_json(item, local_error);
        if (!range) {
            error = std::string(field) + ": " + local_error;
            return false;
        }
        if (range->evidence == EvidenceClass::Unknown) {
            error = std::string(field) +
                    ": a range with evidence 'unknown' is meaningless; omit it "
                    "instead (absence of observation is not evidence)";
            return false;
        }
        out.push_back(*range);
    }
    std::sort(out.begin(), out.end(),
              [](const ClassifiedRange& a, const ClassifiedRange& b) {
                  return a.range < b.range;
              });
    return true;
}

// Ranges are sorted at serialization time, not only at parse time. Producers
// (the probe, hand-authored fixtures) discover ranges in whatever order their
// experiments run in; if that order reached the canonical form, profile_id
// would depend on probe internals rather than on what was measured, and a
// write/read round trip would change the identity of a profile.
json::Value ranges_json(std::vector<ClassifiedRange> ranges) {
    std::sort(ranges.begin(), ranges.end(),
              [](const ClassifiedRange& a, const ClassifiedRange& b) {
                  if (!(a.range == b.range)) return a.range < b.range;
                  return static_cast<int>(a.evidence) < static_cast<int>(b.evidence);
              });
    json::Value arr = json::Value::array();
    for (const auto& r : ranges) arr.push_back(r.to_json());
    return arr;
}

}  // namespace

std::string_view to_string(OperatingSystem v) { return name_of(kOs, v); }
std::string_view to_string(Architecture v) { return name_of(kArch, v); }
std::string_view to_string(TranslationMode v) { return name_of(kTranslation, v); }
std::string_view to_string(ReserveCommitModel v) { return name_of(kRc, v); }
std::string_view to_string(BeyondEofBehavior v) { return name_of(kEof, v); }
std::string_view to_string(ProfileOrigin v) { return name_of(kOrigin, v); }

bool operating_system_from_string(std::string_view s, OperatingSystem& out) {
    return lookup(kOs, s, out);
}
bool architecture_from_string(std::string_view s, Architecture& out) {
    return lookup(kArch, s, out);
}
bool translation_mode_from_string(std::string_view s, TranslationMode& out) {
    return lookup(kTranslation, s, out);
}
bool reserve_commit_model_from_string(std::string_view s, ReserveCommitModel& out) {
    return lookup(kRc, s, out);
}
bool beyond_eof_behavior_from_string(std::string_view s, BeyondEofBehavior& out) {
    return lookup(kEof, s, out);
}
bool profile_origin_from_string(std::string_view s, ProfileOrigin& out) {
    return lookup(kOrigin, s, out);
}

unsigned pointer_width_bits(Architecture arch) {
    switch (arch) {
        case Architecture::X86_64:
        case Architecture::Aarch64:
            return 64;
        case Architecture::X86:
        case Architecture::Arm:
            return 32;
        case Architecture::Other:
        case Architecture::Unknown:
            return 0;
    }
    return 0;
}

// ---------------------------------------------------------------------------

json::Value PlatformInfo::to_json() const {
    json::Value v = json::Value::object();
    v["os"] = std::string(to_string(os));
    v["os_version"] = os_version;
    v["kernel_version"] = kernel_version;
    v["host_arch"] = std::string(to_string(host_arch));
    v["process_arch"] = std::string(to_string(process_arch));
    v["translation_mode"] = std::string(to_string(translation_mode));
    const unsigned bits = pointer_width_bits(process_arch);
    v["pointer_width_bits"] =
        bits == 0 ? json::Value() : json::Value(static_cast<unsigned long long>(bits));
    return v;
}

json::Value ProtectionModel::to_json() const {
    json::Value v = json::Value::object();
    v["write_execute_simultaneous"] = write_execute_simultaneous.to_json();
    v["write_then_execute_transition"] = write_then_execute_transition.to_json();
    v["anonymous_executable_mapping"] = anonymous_executable_mapping.to_json();
    v["jit_entitlement_required"] = jit_entitlement_required.to_json();
    return v;
}

json::Value VirtualMemoryModel::to_json() const {
    json::Value v = json::Value::object();
    v["page_size"] = page_size.to_json();
    v["allocation_granularity"] = allocation_granularity.to_json();
    v["min_map_address"] = min_map_address.to_json();
    v["max_user_address"] = max_user_address.to_json();
    v["anonymous_mapping_supported"] = anonymous_mapping_supported.to_json();
    v["exact_mapping"] = exact_mapping.to_json();
    v["exact_mapping_failure_codes"] = string_array(exact_mapping_failure_codes);
    v["hinted_mapping_may_relocate"] = hinted_mapping_may_relocate.to_json();
    v["fixed_noreplace_available"] = fixed_noreplace_available.to_json();
    v["reserve_commit_model"] = reserve_commit_model.to_json();
    v["file_map_beyond_eof"] = file_map_beyond_eof.to_json();
    v["protection"] = protection.to_json();
    v["unavailable_ranges"] = ranges_json(unavailable_ranges);
    v["available_ranges"] = ranges_json(available_ranges);
    return v;
}

json::Value ProbeRun::to_json() const {
    json::Value v = json::Value::object();
    v["tool_version"] = tool_version;
    v["probe_version"] = probe_version;
    v["run_id"] = run_id;
    v["timestamp_utc"] = timestamp_utc;
    v["probe_binary_hash"] = probe_binary_hash;
    v["duration_ms"] = static_cast<unsigned long long>(duration_ms);
    v["warnings"] = string_array(warnings);
    return v;
}

// The canonical fact subtree. `profile_name` is deliberately EXCLUDED: it is
// a human label, not a platform fact, and renaming a profile must not change
// its identity. `probe_run` and `notes` are excluded for the same reason.
json::Value EnvironmentProfile::facts_json() const {
    json::Value v = json::Value::object();
    v["schema"] = schema;
    v["origin"] = std::string(to_string(origin));
    v["platform"] = platform.to_json();
    v["virtual_memory"] = vm.to_json();
    return v;
}

std::string EnvironmentProfile::profile_id() const {
    auto canonical = json::serialize_canonical(facts_json());
    if (!canonical) return "sha256:unavailable";
    return rs::hash::sha256_uri(*canonical);
}

json::Value EnvironmentProfile::to_json() const {
    json::Value v = facts_json();
    v["profile_name"] = profile_name;
    v["profile_id"] = profile_id();
    v["probe_run"] = run.to_json();
    v["notes"] = string_array(notes);
    return v;
}

std::optional<EnvironmentProfile> EnvironmentProfile::from_json(
    const json::Value& v, std::string& error) {
    if (!v.is_object()) {
        error = "profile document must be a JSON object";
        return std::nullopt;
    }
    const json::Value* schema = v.find("schema");
    if (schema == nullptr || !schema->is_string()) {
        error = "profile requires a 'schema' string";
        return std::nullopt;
    }
    if (schema->as_string() != kProfileSchema) {
        error = "unsupported profile schema: " + schema->as_string() +
                " (expected " + kProfileSchema + ")";
        return std::nullopt;
    }

    EnvironmentProfile p;
    p.schema = schema->as_string();

    if (const json::Value* origin = v.find("origin"); origin != nullptr) {
        if (!origin->is_string() ||
            !profile_origin_from_string(origin->as_string(), p.origin)) {
            error = "unrecognized profile origin";
            return std::nullopt;
        }
    } else {
        error = "profile requires an explicit 'origin' "
                "(measured / hand_authored_fixture / synthetic)";
        return std::nullopt;
    }

    if (const json::Value* name = v.find("profile_name"); name != nullptr) {
        p.profile_name = name->as_string();
    }

    // -- platform ----------------------------------------------------------
    const json::Value* platform = v.find("platform");
    if (platform == nullptr || !platform->is_object()) {
        error = "profile requires a 'platform' object";
        return std::nullopt;
    }
    auto read_enum = [&](const char* key, auto parser, auto& out) -> bool {
        const json::Value* node = platform->find(key);
        if (node == nullptr) return true;  // stays Unknown
        if (!node->is_string() || !parser(node->as_string(), out)) {
            error = std::string("platform.") + key + ": unrecognized value";
            return false;
        }
        return true;
    };
    if (!read_enum("os", operating_system_from_string, p.platform.os)) {
        return std::nullopt;
    }
    if (!read_enum("host_arch", architecture_from_string, p.platform.host_arch)) {
        return std::nullopt;
    }
    if (!read_enum("process_arch", architecture_from_string,
                   p.platform.process_arch)) {
        return std::nullopt;
    }
    if (!read_enum("translation_mode", translation_mode_from_string,
                   p.platform.translation_mode)) {
        return std::nullopt;
    }
    if (const json::Value* n = platform->find("os_version"); n != nullptr) {
        p.platform.os_version = n->as_string();
    }
    if (const json::Value* n = platform->find("kernel_version"); n != nullptr) {
        p.platform.kernel_version = n->as_string();
    }

    // -- virtual memory ----------------------------------------------------
    const json::Value* mem = v.find("virtual_memory");
    if (mem == nullptr || !mem->is_object()) {
        error = "profile requires a 'virtual_memory' object";
        return std::nullopt;
    }

    std::string local_error;
    p.vm.page_size = fact_from_json<std::uint64_t>(mem->find("page_size"),
                                                   read_uint, local_error);
    if (!local_error.empty()) {
        error = "virtual_memory.page_size: " + local_error;
        return std::nullopt;
    }
    p.vm.allocation_granularity = fact_from_json<std::uint64_t>(
        mem->find("allocation_granularity"), read_uint, local_error);
    if (!local_error.empty()) {
        error = "virtual_memory.allocation_granularity: " + local_error;
        return std::nullopt;
    }
    p.vm.min_map_address = fact_from_json<Address>(mem->find("min_map_address"),
                                                   read_address, local_error);
    if (!local_error.empty()) {
        error = "virtual_memory.min_map_address: " + local_error;
        return std::nullopt;
    }
    p.vm.max_user_address = fact_from_json<Address>(mem->find("max_user_address"),
                                                    read_address, local_error);
    if (!local_error.empty()) {
        error = "virtual_memory.max_user_address: " + local_error;
        return std::nullopt;
    }
    p.vm.anonymous_mapping_supported = fact_from_json<bool>(
        mem->find("anonymous_mapping_supported"), read_bool, local_error);
    if (!local_error.empty()) {
        error = "virtual_memory.anonymous_mapping_supported: " + local_error;
        return std::nullopt;
    }
    p.vm.exact_mapping = fact_from_json<SupportLevel>(
        mem->find("exact_mapping"), read_support_level, local_error);
    if (!local_error.empty()) {
        error = "virtual_memory.exact_mapping: " + local_error;
        return std::nullopt;
    }
    p.vm.exact_mapping_failure_codes =
        read_string_array(mem->find("exact_mapping_failure_codes"));

    p.vm.hinted_mapping_may_relocate = fact_from_json<bool>(
        mem->find("hinted_mapping_may_relocate"), read_bool, local_error);
    if (!local_error.empty()) {
        error = "virtual_memory.hinted_mapping_may_relocate: " + local_error;
        return std::nullopt;
    }
    p.vm.fixed_noreplace_available = fact_from_json<bool>(
        mem->find("fixed_noreplace_available"), read_bool, local_error);
    if (!local_error.empty()) {
        error = "virtual_memory.fixed_noreplace_available: " + local_error;
        return std::nullopt;
    }

    auto read_rc = [](const json::Value& node, ReserveCommitModel& out,
                      std::string& err) -> bool {
        if (!node.is_string() ||
            !reserve_commit_model_from_string(node.as_string(), out)) {
            err = "unrecognized reserve/commit model";
            return false;
        }
        return true;
    };
    p.vm.reserve_commit_model = fact_from_json<ReserveCommitModel>(
        mem->find("reserve_commit_model"), read_rc, local_error);
    if (!local_error.empty()) {
        error = "virtual_memory.reserve_commit_model: " + local_error;
        return std::nullopt;
    }

    auto read_eof = [](const json::Value& node, BeyondEofBehavior& out,
                       std::string& err) -> bool {
        if (!node.is_string() ||
            !beyond_eof_behavior_from_string(node.as_string(), out)) {
            err = "unrecognized beyond-EOF behavior";
            return false;
        }
        return true;
    };
    p.vm.file_map_beyond_eof = fact_from_json<BeyondEofBehavior>(
        mem->find("file_map_beyond_eof"), read_eof, local_error);
    if (!local_error.empty()) {
        error = "virtual_memory.file_map_beyond_eof: " + local_error;
        return std::nullopt;
    }

    if (const json::Value* prot = mem->find("protection"); prot != nullptr) {
        auto read_prot_bool = [&](const char* key, Fact<bool>& out) -> bool {
            out = fact_from_json<bool>(prot->find(key), read_bool, local_error);
            if (!local_error.empty()) {
                error = std::string("virtual_memory.protection.") + key + ": " +
                        local_error;
                return false;
            }
            return true;
        };
        if (!read_prot_bool("write_execute_simultaneous",
                            p.vm.protection.write_execute_simultaneous)) {
            return std::nullopt;
        }
        if (!read_prot_bool("write_then_execute_transition",
                            p.vm.protection.write_then_execute_transition)) {
            return std::nullopt;
        }
        if (!read_prot_bool("anonymous_executable_mapping",
                            p.vm.protection.anonymous_executable_mapping)) {
            return std::nullopt;
        }
        if (!read_prot_bool("jit_entitlement_required",
                            p.vm.protection.jit_entitlement_required)) {
            return std::nullopt;
        }
    }

    if (!read_ranges(mem->find("unavailable_ranges"), p.vm.unavailable_ranges,
                     "virtual_memory.unavailable_ranges", error)) {
        return std::nullopt;
    }
    if (!read_ranges(mem->find("available_ranges"), p.vm.available_ranges,
                     "virtual_memory.available_ranges", error)) {
        return std::nullopt;
    }

    // -- run metadata (optional) ------------------------------------------
    if (const json::Value* run = v.find("probe_run"); run != nullptr) {
        auto str = [&](const char* key) -> std::string {
            const json::Value* n = run->find(key);
            return n == nullptr ? std::string() : n->as_string();
        };
        p.run.tool_version = str("tool_version");
        p.run.probe_version = str("probe_version");
        p.run.run_id = str("run_id");
        p.run.timestamp_utc = str("timestamp_utc");
        p.run.probe_binary_hash = str("probe_binary_hash");
        if (const json::Value* d = run->find("duration_ms"); d != nullptr) {
            p.run.duration_ms = d->as_uint();
        }
        p.run.warnings = read_string_array(run->find("warnings"));
    }
    p.notes = read_string_array(v.find("notes"));

    return p;
}

unsigned EnvironmentProfile::process_pointer_width() const {
    return pointer_width_bits(platform.process_arch);
}

RangeVerdict EnvironmentProfile::query_range(const AddressRange& range) const {
    RangeVerdict verdict;

    if (range.empty()) {
        verdict.level = SupportLevel::Unsupported;
        verdict.evidence = EvidenceClass::SpecifiedGuarantee;
        verdict.reason = "empty or wrapping address range";
        return verdict;
    }

    // 1. Hard bounds first: they are the strongest and cheapest facts.
    if (vm.min_map_address.is_known() &&
        range.start < vm.min_map_address.value().value) {
        verdict.level = SupportLevel::Unsupported;
        verdict.evidence = vm.min_map_address.evidence();
        verdict.reason = "start address is below the lowest mappable address (" +
                         json::to_hex(vm.min_map_address.value().value) + ")";
        return verdict;
    }
    if (vm.max_user_address.is_known() &&
        range.end > vm.max_user_address.value().value) {
        verdict.level = SupportLevel::Unsupported;
        verdict.evidence = vm.max_user_address.evidence();
        verdict.reason =
            "range extends past the end of the user address space (" +
            json::to_hex(vm.max_user_address.value().value) + ")";
        return verdict;
    }

    // 2. Explicitly unavailable ranges.
    for (const auto& r : vm.unavailable_ranges) {
        if (r.range.intersects(range)) {
            verdict.level = SupportLevel::Unsupported;
            verdict.evidence = r.evidence;
            verdict.reason = "requested range intersects unavailable range " +
                             r.range.to_string() +
                             (r.note.empty() ? "" : " (" + r.note + ")");
            verdict.conflicting_range = r.range;
            return verdict;
        }
    }

    // 3. Explicitly available ranges, but only if one fully contains the
    //    request. Partial containment tells us nothing about the remainder.
    for (const auto& r : vm.available_ranges) {
        if (r.range.contains(range)) {
            verdict.level = SupportLevel::Supported;
            verdict.evidence = r.evidence;
            verdict.reason = "requested range lies inside observed available "
                             "range " + r.range.to_string();
            return verdict;
        }
    }

    // 4. Nothing established either way.
    verdict.level = SupportLevel::Unknown;
    verdict.evidence = EvidenceClass::Unknown;
    verdict.reason =
        "no probe observation covers this range; absence of observation is not "
        "evidence of availability";
    return verdict;
}

}  // namespace rs::vm

// SPDX-License-Identifier: Apache-2.0
//
// Environment profile: what the *host* can actually do.
//
// Schema: runtime-skeptic.environment-profile.v1  (schemas/environment-profile.v1.json)
//
// Two invariants govern this type:
//
//   1. Every capability is a Fact<T>. Nothing is knowable by default.
//   2. "Not observed" is not "impossible" (ROADMAP 10.1). A range that the
//      probe never tested does not appear in `unavailable_ranges`; it simply
//      is not in `available_ranges` either, and queries return UNKNOWN.
//
// The `profile_id` is a SHA-256 over the canonical JSON of the *facts*
// subtree only. Run metadata (timestamps, durations, run ids) is excluded, so
// two probe runs on an unchanged host produce an identical profile_id. That
// is the Phase 1 exit criterion "repeated runs on the same stable host
// produce equivalent canonical profiles".
#ifndef RUNTIMESKEPTIC_VM_PROFILE_HPP
#define RUNTIMESKEPTIC_VM_PROFILE_HPP

#include <optional>
#include <string>
#include <vector>

#include "runtimeskeptic/core/evidence.hpp"
#include "runtimeskeptic/core/fact.hpp"
#include "runtimeskeptic/core/json.hpp"
#include "runtimeskeptic/vm/address_range.hpp"

namespace rs::vm {

inline constexpr const char* kProfileSchema =
    "runtime-skeptic.environment-profile.v1";

enum class OperatingSystem { Linux, MacOS, Windows, Other, Unknown };
enum class Architecture { X86_64, Aarch64, X86, Arm, Other, Unknown };
enum class TranslationMode { None, Rosetta2, Wow64, QemuUser, Other, Unknown };
enum class ReserveCommitModel { PosixLazy, WindowsReserveCommit, Unknown };
enum class BeyondEofBehavior { Sigbus, Error, ZeroFill, Unknown };

// How the profile came into existence. This is NOT an evidence class; each
// individual fact still carries its own. Origin exists so a report can say
// "this verdict came from a hand-authored fixture, not from your machine".
enum class ProfileOrigin { Measured, HandAuthoredFixture, Synthetic, Unknown };

std::string_view to_string(OperatingSystem v);
std::string_view to_string(Architecture v);
std::string_view to_string(TranslationMode v);
std::string_view to_string(ReserveCommitModel v);
std::string_view to_string(BeyondEofBehavior v);
std::string_view to_string(ProfileOrigin v);

bool operating_system_from_string(std::string_view s, OperatingSystem& out);
bool architecture_from_string(std::string_view s, Architecture& out);
bool translation_mode_from_string(std::string_view s, TranslationMode& out);
bool reserve_commit_model_from_string(std::string_view s, ReserveCommitModel& out);
bool beyond_eof_behavior_from_string(std::string_view s, BeyondEofBehavior& out);
bool profile_origin_from_string(std::string_view s, ProfileOrigin& out);

unsigned pointer_width_bits(Architecture arch);

// ---------------------------------------------------------------------------

struct PlatformInfo {
    OperatingSystem os = OperatingSystem::Unknown;
    std::string os_version;
    std::string kernel_version;
    Architecture host_arch = Architecture::Unknown;
    Architecture process_arch = Architecture::Unknown;
    TranslationMode translation_mode = TranslationMode::Unknown;

    json::Value to_json() const;
};

struct ProtectionModel {
    // Can a single mapping hold PROT_WRITE and PROT_EXEC at the same time?
    Fact<bool> write_execute_simultaneous;
    // Is the RW -> RX transition permitted (the normal JIT sequence)?
    Fact<bool> write_then_execute_transition;
    // Can anonymous memory be mapped executable at all?
    Fact<bool> anonymous_executable_mapping;
    // Does the platform demand an entitlement/policy opt-in for JIT memory?
    Fact<bool> jit_entitlement_required;

    json::Value to_json() const;
};

struct VirtualMemoryModel {
    Fact<std::uint64_t> page_size;
    Fact<std::uint64_t> allocation_granularity;
    Fact<Address> min_map_address;
    // Exclusive upper bound of the usable user-mode virtual address space.
    Fact<Address> max_user_address;

    // Can a mapping be placed at an exactly specified address at all?
    // CONDITIONALLY_SUPPORTED is the common answer: possible, but only for
    // ranges that are free and within bounds.
    Fact<SupportLevel> exact_mapping;
    std::vector<std::string> exact_mapping_failure_codes;

    // Does a non-exact address hint silently relocate?
    Fact<bool> hinted_mapping_may_relocate;
    // Is a non-destructive fixed mapping primitive available
    // (MAP_FIXED_NOREPLACE, VirtualAlloc2 with a placeholder)? Without it, an
    // exact-address request must either clobber existing mappings or relocate.
    Fact<bool> fixed_noreplace_available;

    Fact<ReserveCommitModel> reserve_commit_model;
    Fact<BeyondEofBehavior> file_map_beyond_eof;

    ProtectionModel protection;

    // Measured or specified. Never populated by "we did not try".
    std::vector<ClassifiedRange> unavailable_ranges;
    std::vector<ClassifiedRange> available_ranges;

    json::Value to_json() const;
};

// Everything in here is excluded from the canonical hash.
struct ProbeRun {
    std::string tool_version;
    std::string probe_version;
    std::string run_id;
    std::string timestamp_utc;
    std::string probe_binary_hash;
    std::uint64_t duration_ms = 0;
    std::vector<std::string> warnings;

    json::Value to_json() const;
};

struct RangeVerdict {
    SupportLevel level = SupportLevel::Unknown;
    EvidenceClass evidence = EvidenceClass::Unknown;
    std::string reason;
    std::optional<AddressRange> conflicting_range;
};

class EnvironmentProfile {
public:
    std::string schema = kProfileSchema;
    ProfileOrigin origin = ProfileOrigin::Unknown;
    std::string profile_name;
    PlatformInfo platform;
    VirtualMemoryModel vm;
    ProbeRun run;
    std::vector<std::string> notes;

    // Canonical fact subtree: schema + origin + platform + virtual_memory.
    json::Value facts_json() const;

    // Full document, including run metadata and the derived profile_id.
    json::Value to_json() const;

    // "sha256:<hex>" over serialize_canonical(facts_json()).
    std::string profile_id() const;

    static std::optional<EnvironmentProfile> from_json(const json::Value& v,
                                                       std::string& error);

    // Can `range` be mapped at exactly this location, as far as we know?
    RangeVerdict query_range(const AddressRange& range) const;

    // Bits of usable virtual address, derived from process_arch. Unknown
    // architectures yield 0, which callers must treat as "do not reason".
    unsigned process_pointer_width() const;
};

}  // namespace rs::vm

#endif  // RUNTIMESKEPTIC_VM_PROFILE_HPP

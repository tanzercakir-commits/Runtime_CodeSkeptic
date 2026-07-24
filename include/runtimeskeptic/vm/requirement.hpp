// SPDX-License-Identifier: Apache-2.0
//
// Application requirement: what the *program* assumes.
//
// Schema: runtime-skeptic.application-requirements.v1
//
// In Phase 3 this document is written by hand (ROADMAP: "accept manually
// authored application requirements"). In Phase 5 CodeSkeptic emits it.
// The `assumption_evidence` field records which of the two produced it,
// because a hand-declared contract is authoritative about intent while a
// statically inferred one is not.
#ifndef RUNTIMESKEPTIC_VM_REQUIREMENT_HPP
#define RUNTIMESKEPTIC_VM_REQUIREMENT_HPP

#include <optional>
#include <string>
#include <vector>

#include "runtimeskeptic/core/evidence.hpp"
#include "runtimeskeptic/core/json.hpp"
#include "runtimeskeptic/vm/address_range.hpp"

namespace rs::vm {

inline constexpr const char* kRequirementSchema =
    "runtime-skeptic.application-requirements.v1";

enum class OperationKind {
    VirtualMemoryMap,
    VirtualMemoryProtect,
    VirtualMemoryReserve,
    VirtualMemoryCommit,
    Unknown,
};

// What the program does when the operation does not deliver what it needs.
// This is the "failure sink" of ROADMAP 10.5. It decides severity: a fatal
// assert turns a semantic mismatch into a crash, an unchecked outcome turns
// it into silent corruption.
enum class FailureSinkKind {
    FatalAssert,   // assert()/abort() on postcondition violation
    ProcessExit,   // explicit exit / fatal error path
    ErrorReturn,   // propagates an error to the caller: recoverable
    RetryLoop,     // retries, possibly forever
    Unchecked,     // does not check at all: silent misbehavior
    None,
    Unknown,
};

// What the program is willing to accept instead of the exact request.
enum class FallbackKind {
    Relocate,             // a different address is acceptable
    SmallerSize,
    WeakerProtection,
    NonExecutable,
    None,
    Unknown,
};

std::string_view to_string(OperationKind v);
std::string_view to_string(FailureSinkKind v);
std::string_view to_string(FallbackKind v);
bool operation_kind_from_string(std::string_view s, OperationKind& out);
bool failure_sink_from_string(std::string_view s, FailureSinkKind& out);
bool fallback_from_string(std::string_view s, FallbackKind& out);

struct Protection {
    bool read = false;
    bool write = false;
    bool execute = false;

    bool write_and_execute() const { return write && execute; }
    std::string to_string() const;
    json::Value to_json() const;
};

struct SourceLocation {
    std::string file;
    std::uint64_t line = 0;
    std::string symbol;

    json::Value to_json() const;
    std::string to_string() const;
};

struct MappingRequest {
    // Absent when the program does not care where the mapping lands.
    std::optional<std::uint64_t> address;
    std::uint64_t size = 0;

    // The program requires the mapping to be placed at exactly `address`.
    bool exact_address_required = false;

    // Additional alignment the program relies on, beyond page alignment.
    std::optional<std::uint64_t> required_alignment;

    // The program hard-codes a page size (common in emulators that mirror a
    // guest MMU). A host with a different page size breaks it.
    std::optional<std::uint64_t> required_page_size;

    Protection protection;

    // The program writes code into the mapping and then executes it.
    bool write_then_execute = false;
    // The program requires W and X to be live simultaneously (no flip step).
    bool simultaneous_write_execute = false;

    bool file_backed = false;
    // For file-backed mappings: the length of the backing file and the offset
    // the mapping starts at. When `file_offset + size` exceeds `file_length`
    // the mapping extends past end-of-file, whose behavior is host-specific.
    std::optional<std::uint64_t> file_length;
    std::uint64_t file_offset = 0;
    // Does the program actually touch the bytes past end-of-file? A mapping
    // that merely extends past EOF is harmless until it is accessed.
    bool accesses_beyond_eof = false;

    // The program reserves address space first and commits later, and relies
    // on the reservation being a distinct, observable state.
    bool reserve_then_commit = false;

    std::optional<AddressRange> range() const {
        if (!address) return std::nullopt;
        return AddressRange::from_base_size(*address, size);
    }

    json::Value to_json() const;
};

struct Assumptions {
    // The program requires guest addresses to equal host addresses.
    bool guest_host_identity_required = false;
    // Is an address translation layer present that could repair a relocation?
    bool translation_layer_available = false;
    // Pointer width the program stores the returned address in. A 32-bit
    // storage slot silently truncates a high host address.
    std::optional<std::uint64_t> pointer_storage_width_bits;
    // The program retries the operation after failure.
    bool retries_on_failure = false;
    std::optional<std::uint64_t> max_retries;

    json::Value to_json() const;
};

struct FailureSink {
    FailureSinkKind kind = FailureSinkKind::Unknown;
    std::optional<SourceLocation> location;
    std::string description;

    json::Value to_json() const;
};

class Requirement {
public:
    std::string schema = kRequirementSchema;
    std::string name;
    std::string component;
    OperationKind operation = OperationKind::Unknown;

    MappingRequest request;
    Assumptions assumptions;

    // Free-form postcondition statements, carried through to the report so a
    // human sees the requirement in the program's own words.
    std::vector<std::string> required_postconditions;

    std::vector<FallbackKind> permitted_fallbacks;
    FailureSink failure_sink;
    std::vector<SourceLocation> source_locations;

    // How we know the program requires this.
    EvidenceClass assumption_evidence = EvidenceClass::Unknown;

    // What the producer of this document could NOT establish. A static
    // extractor knows things about itself that no consumer can infer - that
    // an address was not a compile-time constant, that a flags expression was
    // computed at runtime. Carrying those forward keeps a silent gap from
    // reading as a clean bill.
    std::vector<std::string> extraction_limitations;

    bool permits(FallbackKind kind) const;

    json::Value to_json() const;
    std::string requirement_id() const;  // sha256 over the canonical document

    static std::optional<Requirement> from_json(const json::Value& v,
                                                std::string& error);
};

// ---------------------------------------------------------------------------
// Bundles
// ---------------------------------------------------------------------------
//
// A static analyzer does not find one requirement, it finds all of them. The
// bundle is the interchange format between CodeSkeptic's
// `--runtime-assumptions` mode and rs-check (ROADMAP 12.1: "Integration
// should occur through versioned artifacts rather than direct source
// dependencies" - so neither project links against the other).

inline constexpr const char* kRequirementBundleSchema =
    "runtime-skeptic.application-requirements-bundle.v1";

struct RequirementBundle {
    std::string schema = kRequirementBundleSchema;
    std::string producer_tool;
    std::string producer_version;
    std::string producer_rule;
    std::vector<Requirement> requirements;

    // Requirements the bundle contained but that could not be parsed. They
    // are reported rather than dropped: a producer emitting documents this
    // consumer rejects is a fact worth surfacing.
    std::vector<std::string> rejected;
};

// Accepts either a single requirement document or a bundle, so a caller
// never has to know which one it was handed.
std::optional<RequirementBundle> load_requirements(const json::Value& v,
                                                   std::string& error);

}  // namespace rs::vm

#endif  // RUNTIMESKEPTIC_VM_REQUIREMENT_HPP

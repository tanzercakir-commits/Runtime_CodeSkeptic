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
// How a program's page-size requirement relates to the host's.
//
// Equality was the only option in the first draft, and it produced confident
// false positives: a jemalloc built with --with-lg-page=16 checks
// `if (os_page > PAGE) error;` - it demands the host page be AT MOST 64 KiB
// and runs perfectly on a 4 KiB kernel. Reporting that as an impossibility,
// and advising the user to find "a host whose page size is 65536", was worse
// than saying nothing.
enum class SizeRelation {
    Equal,    // the host page size must be exactly this
    AtMost,   // the host page size must not exceed this
    AtLeast,  // the host page size must be at least this
};

std::string_view to_string(SizeRelation v);
bool size_relation_from_string(std::string_view s, SizeRelation& out);

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

    // The program relies on the bytes past its requested size staying
    // unmapped: guard-page schemes, fault-based bounds checks, anything that
    // treats "I asked for N" as "byte N faults". This is RS-VM-0005's own
    // precondition, which the rule used to assume for every caller - and the
    // false-positive campaign measured what that assumption costs: 42% of all
    // real mappings pass unrounded sizes, because that is simply how mmap is
    // called, and none of them was ever shown to care. The precedent is
    // `accesses_beyond_eof` below: a behavioral claim belongs to the caller,
    // not to the rule's imagination. Absent means "no reliance declared", and
    // the rule then records the rounding as information instead of a
    // condition.
    bool relies_on_unmapped_beyond_size = false;

    // Additional alignment the program relies on, beyond page alignment.
    std::optional<std::uint64_t> required_alignment;

    // The returned address must fall within these bounds.
    //
    // Added because the campaign found three unrelated projects expressing
    // exactly this and having nowhere to put it: LuaJIT needs its heap below
    // 2^31 (LJ_ALLOC_MBITS), Box64's box32 mode needs guest allocations below
    // 2^32, and Box64's dynarec buffer needs to be ABOVE 2^32 - two opposite
    // bounds in the same process. Without the field, authors reached for
    // `guest_host_identity_required`, which means something far stronger, and
    // the analyzer then reported a self-contradiction it had manufactured
    // itself.
    std::optional<std::uint64_t> address_min;
    std::optional<std::uint64_t> address_max;  // exclusive

    // The mapping must land within `max_displacement` bytes of some other
    // region, named informally by `reference`.
    //
    // This is the constraint every JIT that emits a relative branch lives
    // under: LuaJIT's machine-code area must sit inside a +/-2 GiB window
    // centred on its exit handler, V8's code range likewise around the
    // embedded blob, and the same holds for SpiderMonkey and .NET. v0.1
    // cannot evaluate it - the reference address is not knowable from a host
    // profile - but carrying it lets the analyzer say so explicitly instead
    // of returning a confident SUPPORTED that simply ignored the hard part.
    std::optional<std::uint64_t> max_displacement_bytes;
    std::string displacement_reference;

    // For reserve/commit: is the later commit a CHECKED call site?
    //
    // Windows MEM_RESERVE/MEM_COMMIT and the POSIX
    // mmap(PROT_NONE)-then-mmap(MAP_FIXED)-over-it idiom look alike in a
    // boolean but differ in the only way that matters. In the POSIX form the
    // commit IS a call whose result the program tests, so the warning that
    // "failures move from a checked call site to an unchecked memory access"
    // is false for it.
    bool commit_is_checked_call = false;

    // The program hard-codes a page size (common in emulators that mirror a
    // guest MMU). A host with a different page size breaks it.
    std::optional<std::uint64_t> required_page_size;
    SizeRelation required_page_size_relation = SizeRelation::Equal;

    // The program validates the address it got back against its own
    // constraint and rejects a bad one, rather than storing it and hoping.
    // LuaJIT, Box64 and jemalloc all do this; without the field they look
    // like programs that silently truncate.
    bool validates_returned_address = false;

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

    // HOW FAR past the end. These are two different questions and conflating
    // them produced a false positive on every host.
    //
    // POSIX requires a conforming system to zero-fill the partial page at the
    // end of a mapped object. A program that reads only between the end of the
    // file and the end of its final page is therefore portable and CANNOT
    // fault - on Linux, on macOS, anywhere. Only a reference to a page lying
    // ENTIRELY past the end is implementation-defined, and that is what
    // `file_map_beyond_eof` measures: Linux and native macOS arm64 raise
    // SIGBUS, the same Apple machine under Rosetta 2 returns zeroes.
    //
    // Until this field existed both were spelled `accesses_beyond_eof: true`,
    // so the safe one was judged against a fact about the dangerous one and
    // came back UNSUPPORTED on a sigbus host. Found by
    // tests/groundtruth/, whose first version made the same mistake in its own
    // case program - it read inside the partial page while claiming to measure
    // the other thing, contradicted the analyzer, and was wrong.
    enum class EofAccessExtent {
        WholePagePastEnd = 0,       // implementation-defined; the risky one
        WithinFinalPartialPage = 1, // POSIX-guaranteed zero-fill; always safe
    };
    EofAccessExtent eof_access_extent = EofAccessExtent::WholePagePastEnd;

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

    // Keys the reader did not recognise, collected rather than discarded.
    //
    // A typo used to change a contract's meaning in silence. `address_is_hint`
    // was invented while writing a ground-truth contract, accepted without a
    // word, and ignored - and the failure mode is worse than a wasted field:
    // misspell `accesses_beyond_eof` and the requirement quietly says false,
    // the rule that would have caught it never runs, and the report is a clean
    // bill of health for a claim nobody evaluated.
    //
    // That is the exact shape of the thing this project exists to object to, so
    // it is not dropped and not fatal either: unknown keys travel into
    // analyzer_limitations, where a reader sees that part of their document was
    // not understood. Rejecting outright would make every future schema
    // addition a breaking change for older tools.
    std::vector<std::string> unrecognized_fields;

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

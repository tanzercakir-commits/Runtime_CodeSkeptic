// SPDX-License-Identifier: Apache-2.0
#include "runtimeskeptic/vm/finding.hpp"

#include <array>
#include <utility>

namespace rs::vm {
namespace {

constexpr std::array<std::pair<RemediationClass, const char*>, 12> kRemediationNames{{
    {RemediationClass::AddAddressTranslationLayer, "add_address_translation_layer"},
    {RemediationClass::RelaxExactAddressRequirement,
     "relax_exact_address_requirement"},
    {RemediationClass::ChooseDifferentBaseAddress, "choose_different_base_address"},
    {RemediationClass::ReserveAddressSpaceEarlier, "reserve_address_space_earlier"},
    {RemediationClass::MatchHostPageSize, "match_host_page_size"},
    {RemediationClass::UseSeparateWriteAndExecuteMappings,
     "use_separate_write_and_execute_mappings"},
    {RemediationClass::RequestPlatformEntitlement, "request_platform_entitlement"},
    {RemediationClass::HandleRelocationInCaller, "handle_relocation_in_caller"},
    {RemediationClass::TreatErrorAsPermanent, "treat_error_as_permanent"},
    {RemediationClass::WidenPointerStorage, "widen_pointer_storage"},
    {RemediationClass::SelectDifferentHostConfiguration,
     "select_different_host_configuration"},
    {RemediationClass::NoKnownRemediationOnThisHost,
     "no_known_remediation_on_this_host"},
}};

constexpr std::array<std::pair<RemediationClass, const char*>, 12> kRemediationDesc{{
    {RemediationClass::AddAddressTranslationLayer,
     "Introduce a guest-to-host address translation layer so the guest address "
     "no longer has to equal the host address."},
    {RemediationClass::RelaxExactAddressRequirement,
     "Remove the exact-address postcondition and make the caller tolerate the "
     "address the operating system actually returned."},
    {RemediationClass::ChooseDifferentBaseAddress,
     "Select a base address inside a range the host can actually provide."},
    {RemediationClass::ReserveAddressSpaceEarlier,
     "Reserve the address range during process startup, before the loader and "
     "allocator fragment the address space."},
    {RemediationClass::MatchHostPageSize,
     "Derive page size at runtime instead of hard-coding it, or build a "
     "variant for this host page size."},
    {RemediationClass::UseSeparateWriteAndExecuteMappings,
     "Map the same physical pages twice - once writable, once executable - "
     "instead of requiring write and execute permissions simultaneously."},
    {RemediationClass::RequestPlatformEntitlement,
     "Declare the platform entitlement or policy opt-in that unlocks the "
     "requested memory capability."},
    {RemediationClass::HandleRelocationInCaller,
     "Detect relocation at the call site and rebase the dependent pointers "
     "rather than asserting."},
    {RemediationClass::TreatErrorAsPermanent,
     "Classify this failure as permanent and stop retrying; the operation "
     "cannot succeed on this host regardless of attempt count."},
    {RemediationClass::WidenPointerStorage,
     "Store the returned address in a full-width pointer or uintptr_t."},
    {RemediationClass::SelectDifferentHostConfiguration,
     "Run on a host, kernel configuration or process mode whose measured "
     "capabilities satisfy the requirement."},
    {RemediationClass::NoKnownRemediationOnThisHost,
     "No remediation class known to this analyzer preserves the requested "
     "semantics on this host profile."},
}};

const std::vector<FindingDefinition>& registry_storage() {
    static const std::vector<FindingDefinition> kRegistry = {
        {ids::kExactAddressUnavailable,
         "Exact virtual-memory mapping cannot be satisfied", Severity::Critical,
         "The program requires a mapping at an exact address that the host "
         "cannot provide, and no translation layer can repair the difference."},
        {ids::kAddressBelowMinimum,
         "Requested address is below the lowest mappable address",
         Severity::Critical,
         "The host forbids mappings below a policy minimum (for example "
         "vm.mmap_min_addr on Linux)."},
        {ids::kAddressAboveUserSpace,
         "Requested range extends past the end of the user address space",
         Severity::Critical,
         "The requested range does not fit inside the user-mode portion of the "
         "virtual address space on this host."},
        {ids::kAddressAlignmentMismatch,
         "Requested address does not satisfy host allocation granularity",
         Severity::High,
         "An exact mapping request must be aligned to the host allocation "
         "granularity; this one is not."},
        {ids::kSizeGranularityMismatch,
         "Requested size is not a multiple of the host allocation granularity",
         Severity::Medium,
         "The host will round the reservation, so the mapping covers more "
         "address space than the program asked for."},
        {ids::kPageSizeMismatch, "Host page size differs from the required page size",
         Severity::Critical,
         "The program hard-codes a page size that this host does not use."},
        {ids::kHintedMappingMayRelocate,
         "Address hint may relocate while the caller assumes identity",
         Severity::High,
         "The request is a hint rather than an exact placement, the host may "
         "return a different address, and the caller treats the returned "
         "address as if it equalled the request."},
        {ids::kIdentityWithoutTranslation,
         "Guest/host address identity required without a translation layer",
         Severity::High,
         "The program requires guest addresses to equal host addresses and no "
         "translation layer exists to absorb a mismatch."},
        {ids::kWriteExecuteRestricted,
         "Simultaneous write and execute permission is restricted",
         Severity::Critical,
         "The host enforces write-xor-execute; the program requires a mapping "
         "that is writable and executable at the same time."},
        {ids::kExecutableMappingUnsupported,
         "Anonymous executable mapping is not available", Severity::Critical,
         "The host does not permit executable anonymous memory in this process "
         "configuration."},
        {ids::kJitEntitlementRequired,
         "Executable memory requires a platform entitlement", Severity::High,
         "The host grants executable memory only to processes that carry an "
         "explicit entitlement or policy opt-in."},
        {ids::kReserveCommitSemanticMismatch,
         "Reserve/commit semantics differ from the program's model",
         Severity::Medium,
         "The program models reservation and commitment as distinct states; "
         "this host does not provide that distinction."},
        {ids::kPointerTruncation,
         "Returned address does not fit the caller's pointer storage",
         Severity::Critical,
         "The address the host would return exceeds the width of the variable "
         "the program stores it in."},
        {ids::kInvalidFallbackContract,
         "Permitted fallback contradicts a required postcondition",
         Severity::High,
         "The requirement document permits a fallback that cannot satisfy one "
         "of its own required postconditions. The contradiction is internal to "
         "the requirement and holds on every host."},
        {ids::kPermanentErrorRetried,
         "Retry loop targets a permanently impossible operation", Severity::High,
         "The program retries after failure, but the failure is structural on "
         "this host: no number of attempts can succeed."},
        {ids::kExactMappingNonDestructiveUnavailable,
         "No non-destructive exact-mapping primitive is available",
         Severity::High,
         "The host lacks a way to request an exact address without either "
         "overwriting an existing mapping or silently relocating."},
        {ids::kRequiredFactUnknown,
         "A platform fact this request depends on was never established",
         Severity::Info,
         "The profile contains no observation covering something the request "
         "depends on - a range, a page size, a protection capability. Reported "
         "as UNKNOWN rather than as support. The finding text names the "
         "specific fact; the id stays stable so CI can filter it."},
        {ids::kFileMappingBeyondEof,
         "File-backed mapping extends beyond end of file", Severity::High,
         "Accessing the portion of a file mapping past end-of-file has "
         "host-specific behavior that the program does not handle."},
        {ids::kAddressBoundUnsatisfiable,
         "The host cannot place the mapping inside the program's address bound",
         Severity::Critical,
         "The program needs the returned address inside a bounded window - "
         "below 2^31, above 2^32, and so on - and this host's usable address "
         "space does not intersect it."},
        {ids::kDisplacementConstraintNotEvaluable,
         "A relative-displacement constraint was carried but not evaluated",
         Severity::Info,
         "The mapping must land within a bounded distance of another region. "
         "A host profile does not know where that region will be, so v0.1 "
         "cannot decide this. Reported rather than ignored: the alternative "
         "was a confident verdict that silently dropped the hard part."},
        {ids::kAddressBoundIsTight,
         "The program can use only a small part of this host's address space",
         Severity::Low,
         "The address bound is satisfiable today but covers a small fraction "
         "of what the host offers. Success then depends on the allocator "
         "handing out a low address, which degrades as the process fragments "
         "and can change with a different ASLR configuration."},
        {ids::kAnonymousMappingUnavailable,
         "Anonymous memory mapping is unavailable on this host", Severity::Critical,
         "The host cannot create an ordinary anonymous mapping at all. Nothing "
         "further about placement or protection can matter."},
        {ids::kSizeExceedsAddressSpace,
         "Requested size does not fit in the usable address space",
         Severity::Critical,
         "The reservation is larger than the whole user-mode virtual address "
         "space this host provides, so it cannot succeed at any address."},
        {ids::kReservationAlignmentUnguaranteed,
         "Reservation alignment exceeds what the mapping API guarantees",
         Severity::High,
         "The program needs the reservation itself aligned more strictly than "
         "page granularity. The mapping call does not provide that; it has to "
         "be achieved by over-allocating and trimming, and nothing in the "
         "profile can confirm the program does so."},
        {ids::kSizeExceedsGrantedReservation,
         "Requested reservation is larger than any this host granted",
         Severity::Critical,
         "The request fits the address space, and the host still refuses a "
         "reservation this large. Fitting is necessary, not sufficient: whether a "
         "reservation is granted also depends on overcommit policy, RLIMIT_AS and "
         "how much contiguous space is free. Measured, not assumed."},
        {ids::kReservationGrantabilityUnknown,
         "Whether a reservation of this size is grantable was never established",
         Severity::Medium,
         "The request fits the address space and is larger than any reservation "
         "this project has observed a real program make, and the profile does not "
         "record the largest reservation the host grants. Fitting is not "
         "sufficient, so nothing here supports a positive answer."},
        {ids::kAddressHintNotHonourable,
         "Address hint points into a range the host cannot provide", Severity::Low,
         "The requested address is a hint rather than a demand, and it falls "
         "in a range the profile records as unavailable. The mapping will "
         "succeed somewhere else, so this is a wasted hint rather than a "
         "failure - unless something downstream quietly assumes the hint was "
         "taken."},
    };
    return kRegistry;
}

}  // namespace

std::string_view to_string(RemediationClass v) {
    for (const auto& [kind, name] : kRemediationNames) {
        if (kind == v) return name;
    }
    return "unknown";
}

std::string_view describe(RemediationClass v) {
    for (const auto& [kind, text] : kRemediationDesc) {
        if (kind == v) return text;
    }
    return "";
}

json::Value Remediation::to_json() const {
    json::Value v = json::Value::object();
    v["class"] = std::string(rs::vm::to_string(kind));
    v["description"] = std::string(describe(kind));
    if (!detail.empty()) v["detail"] = detail;
    return v;
}

json::Value RejectedFix::to_json() const {
    json::Value v = json::Value::object();
    v["proposal"] = proposal;
    v["reason"] = reason;
    return v;
}

json::Value Finding::to_json() const {
    json::Value v = json::Value::object();
    v["id"] = id;
    v["title"] = title;
    v["domain"] = domain;
    v["severity"] = std::string(rs::to_string(severity));
    v["confidence"] = std::string(rs::to_string(confidence));
    v["support_impact"] = std::string(rs::to_string(support_impact));
    v["structural_impossibility"] = structural_impossibility;
    v["required"] = required;
    v["host_capability"] = host_capability;
    v["adapter_capability"] = adapter_capability;
    v["modeled_fallback"] = modeled_fallback;
    v["failure_sink"] = failure_sink;
    v["conclusion"] = conclusion;
    v["evidence_chain"] = evidence.to_json();

    json::Value locs = json::Value::array();
    for (const auto& l : source_locations) locs.push_back(l.to_json());
    v["source_locations"] = locs;

    json::Value rem = json::Value::array();
    for (const auto& r : remediations) rem.push_back(r.to_json());
    v["remediation_classes"] = rem;

    json::Value rejected = json::Value::array();
    for (const auto& r : rejected_fixes) rejected.push_back(r.to_json());
    v["rejected_fixes"] = rejected;

    v["profile_id"] = profile_id;
    v["requirement_id"] = requirement_id;
    return v;
}

const std::vector<FindingDefinition>& finding_registry() {
    return registry_storage();
}

const FindingDefinition* find_definition(std::string_view id) {
    for (const auto& def : registry_storage()) {
        if (id == def.id) return &def;
    }
    return nullptr;
}

}  // namespace rs::vm

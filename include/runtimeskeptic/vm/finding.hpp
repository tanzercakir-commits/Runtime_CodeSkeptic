// SPDX-License-Identifier: Apache-2.0
//
// A finding is the unit of output. ROADMAP 10.7 fixes its required content;
// this header encodes that list as struct fields so a finding cannot be
// constructed without them.
//
// Note on remediation: a finding carries *classes* of remedy, not patches.
// The tool knows which architectural moves remain valid; it does not know the
// program well enough to write the fix. It also records which superficial
// fixes are provably useless, because that is often the more valuable half.
#ifndef RUNTIMESKEPTIC_VM_FINDING_HPP
#define RUNTIMESKEPTIC_VM_FINDING_HPP

#include <string>
#include <vector>

#include "runtimeskeptic/core/evidence.hpp"
#include "runtimeskeptic/core/json.hpp"
#include "runtimeskeptic/vm/requirement.hpp"

namespace rs::vm {

enum class RemediationClass {
    AddAddressTranslationLayer,
    RelaxExactAddressRequirement,
    ChooseDifferentBaseAddress,
    ReserveAddressSpaceEarlier,
    MatchHostPageSize,
    UseSeparateWriteAndExecuteMappings,
    RequestPlatformEntitlement,
    HandleRelocationInCaller,
    TreatErrorAsPermanent,
    WidenPointerStorage,
    SelectDifferentHostConfiguration,
    NoKnownRemediationOnThisHost,
};

std::string_view to_string(RemediationClass v);
std::string_view describe(RemediationClass v);

struct Remediation {
    RemediationClass kind = RemediationClass::NoKnownRemediationOnThisHost;
    std::string detail;

    json::Value to_json() const;
};

// A fix that looks plausible but cannot work, with the reason. Emitting these
// is a first-class feature: it stops teams from burning days on a retry loop
// or a bigger buffer when the contradiction is structural.
struct RejectedFix {
    std::string proposal;
    std::string reason;

    json::Value to_json() const;
};

struct Finding {
    std::string id;        // RS-VM-0001
    std::string title;
    std::string domain = "virtual_memory";
    Severity severity = Severity::Medium;
    Confidence confidence = Confidence::Hypothesis;

    // How this finding contributes to the overall verdict.
    SupportLevel support_impact = SupportLevel::Unknown;

    // Does this finding mean "no execution can succeed", as opposed to "one
    // legal outcome fails" or "the contract is internally confused"?
    //
    // Rules that reason ABOUT other findings need this. Asking "did anything
    // prove impossibility?" by testing `confidence == Proven` looked
    // equivalent and is not: confidence is clamped by evidence, so a
    // statically extracted requirement can never reach Proven, and the
    // question "is this retry loop futile?" would go permanently unanswered
    // for exactly the inputs a static extractor produces.
    bool structural_impossibility = false;

    std::string required;             // what the program requires
    std::string host_capability;      // what the environment provides
    std::string adapter_capability;   // what the compatibility layer provides
    std::string modeled_fallback;     // what actually happens instead
    std::string failure_sink;         // where the mismatch lands
    std::string conclusion;

    EvidenceChain evidence;
    std::vector<SourceLocation> source_locations;
    std::vector<Remediation> remediations;
    std::vector<RejectedFix> rejected_fixes;

    std::string profile_id;
    std::string requirement_id;

    json::Value to_json() const;
};

// ---------------------------------------------------------------------------
// Finding registry
// ---------------------------------------------------------------------------
//
// IDs are permanent once published. See docs/findings/registry.md.
// The ROADMAP's illustrative example uses "RS-VM-0041"; the real registry
// starts at 0001 and the exact-address contradiction is RS-VM-0001.

struct FindingDefinition {
    const char* id;
    const char* title;
    Severity default_severity;
    const char* summary;
};

const std::vector<FindingDefinition>& finding_registry();
const FindingDefinition* find_definition(std::string_view id);

// Registry identifiers, referenced by the rule engine.
namespace ids {
inline constexpr const char* kExactAddressUnavailable = "RS-VM-0001";
inline constexpr const char* kAddressBelowMinimum = "RS-VM-0002";
inline constexpr const char* kAddressAboveUserSpace = "RS-VM-0003";
inline constexpr const char* kAddressAlignmentMismatch = "RS-VM-0004";
inline constexpr const char* kSizeGranularityMismatch = "RS-VM-0005";
inline constexpr const char* kPageSizeMismatch = "RS-VM-0006";
inline constexpr const char* kHintedMappingMayRelocate = "RS-VM-0007";
inline constexpr const char* kIdentityWithoutTranslation = "RS-VM-0008";
inline constexpr const char* kWriteExecuteRestricted = "RS-VM-0009";
inline constexpr const char* kExecutableMappingUnsupported = "RS-VM-0010";
inline constexpr const char* kJitEntitlementRequired = "RS-VM-0011";
inline constexpr const char* kReserveCommitSemanticMismatch = "RS-VM-0012";
inline constexpr const char* kPointerTruncation = "RS-VM-0013";
inline constexpr const char* kInvalidFallbackContract = "RS-VM-0014";
inline constexpr const char* kPermanentErrorRetried = "RS-VM-0015";
inline constexpr const char* kExactMappingNonDestructiveUnavailable = "RS-VM-0016";
// One id covers every "we never established this" outcome. The finding text
// names the specific fact; keeping a single id lets CI filter the whole class
// with one rule instead of chasing a new id per capability.
inline constexpr const char* kRequiredFactUnknown = "RS-VM-0017";
inline constexpr const char* kFileMappingBeyondEof = "RS-VM-0018";
inline constexpr const char* kAnonymousMappingUnavailable = "RS-VM-0019";
inline constexpr const char* kAddressHintNotHonourable = "RS-VM-0020";
inline constexpr const char* kSizeExceedsAddressSpace = "RS-VM-0021";
inline constexpr const char* kReservationAlignmentUnguaranteed = "RS-VM-0022";
}  // namespace ids

}  // namespace rs::vm

#endif  // RUNTIMESKEPTIC_VM_FINDING_HPP

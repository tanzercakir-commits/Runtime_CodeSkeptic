// SPDX-License-Identifier: Apache-2.0
#include "runtimeskeptic/vm/analyzer.hpp"

#include <algorithm>
#include <string>

namespace rs::vm {
namespace {

std::string dec(std::uint64_t v) { return std::to_string(v); }

// A finding whose failure sink is fatal is worse than the same finding whose
// failure sink returns an error. Severity therefore depends on the program,
// not only on the platform.
Severity adjust_severity(Severity base, FailureSinkKind sink) {
    // The registry (section 3.2) documents this table and ends with: "`info`
    // is never raised". The two fatal cases did not honor that until T-019 -
    // an informational note under a fatal-assert sink came out `critical`,
    // which is how a rounding fact would have read as an abort. Unchecked and
    // RetryLoop already preserved Info; now all sinks do, and the code matches
    // the sentence that was already published about it.
    switch (sink) {
        case FailureSinkKind::FatalAssert:
        case FailureSinkKind::ProcessExit:
            return base == Severity::Info ? Severity::Info : Severity::Critical;
        case FailureSinkKind::Unchecked:
            // Silent misbehavior is not milder than a crash; it is usually
            // worse, because the failure surfaces far from its cause.
            return base == Severity::Info ? Severity::Info : Severity::Critical;
        case FailureSinkKind::RetryLoop:
            return base == Severity::Info ? Severity::Info : Severity::High;
        case FailureSinkKind::ErrorReturn:
            return base == Severity::Critical ? Severity::High : base;
        case FailureSinkKind::None:
            // The program declared that nothing happens when this fails - it
            // degrades and carries on. mimalloc's huge-page loop keeps the
            // contiguous prefix it did get and warns. Leaving such a finding
            // at Critical made a graceful fallback look like an abort.
            return static_cast<int>(base) < static_cast<int>(Severity::Medium)
                       ? Severity::Medium
                       : base;
        case FailureSinkKind::Unknown:
            return base;
    }
    return base;
}

std::string describe_sink(const FailureSink& sink) {
    std::string s(rs::vm::to_string(sink.kind));
    if (sink.location) s += " at " + sink.location->to_string();
    if (!sink.description.empty()) s += " - " + sink.description;
    return s;
}

class Analysis {
public:
    Analysis(const Requirement& requirement, const EnvironmentProfile& profile,
             const AnalysisOptions& options)
        : req_(requirement), profile_(profile), options_(options) {}

    AnalysisResult run();

private:
    const Requirement& req_;
    const EnvironmentProfile& profile_;
    const AnalysisOptions& options_;
    AnalysisResult result_;

    // Creates a finding pre-filled from the registry. `desired` is what the
    // rule believes; the engine clamps it against the evidence chain in
    // `emit()`.
    Finding start(const char* id, Confidence desired, SupportLevel impact) {
        Finding f;
        const FindingDefinition* def = find_definition(id);
        f.id = id;
        f.title = def != nullptr ? def->title : "(unregistered finding)";
        f.severity = def != nullptr ? def->default_severity : Severity::Medium;
        f.confidence = desired;
        f.support_impact = impact;
        f.failure_sink = describe_sink(req_.failure_sink);
        f.source_locations = req_.source_locations;
        f.profile_id = result_.profile_id;
        f.requirement_id = result_.requirement_id;
        f.adapter_capability =
            req_.assumptions.translation_layer_available
                ? "guest-to-host address translation available"
                : "no guest-to-host address translation";
        return f;
    }

    // Records that a constraint was examined and holds. The evidence class
    // travels with it: "satisfied, on a measured fact" and "satisfied, on a
    // guess" are different statements.
    void satisfied(std::string constraint, std::string host_fact,
                   EvidenceClass evidence) {
        result_.satisfied.push_back(
            SatisfiedCheck{std::move(constraint), std::move(host_fact), evidence});
    }

    void emit(Finding f) {
        // The sink adjustment models what happens when this finding's failure
        // SURFACES - a fatal assert turns any real defect into an abort. A
        // finding emitted on a SUPPORTED verdict describes a fact, not a
        // failure; there is nothing for the sink to catch, and escalating it
        // turned RS-VM-0020's wasted-hint line and RS-VM-0005's rounding note
        // into "Critical" for any caller whose UNRELATED failure path is an
        // assert. Found while giving RS-VM-0005 its quiet form (T-019).
        if (f.support_impact != SupportLevel::Supported) {
            f.severity = adjust_severity(f.severity, req_.failure_sink.kind);
        }
        // The single place where overclaiming is prevented.
        f.confidence = clamp_confidence(f.confidence, f.evidence.weakest_class());
        result_.overall = combine(result_.overall, f.support_impact);
        result_.findings.push_back(std::move(f));
    }

    // The application's own claim about what it requires. Every finding starts
    // from one of these so the report always says who asked for what.
    void add_application_claim(Finding& f, std::string claim) {
        f.evidence.add(Layer::Application, req_.assumption_evidence,
                       std::move(claim),
                       req_.source_locations.empty()
                           ? (req_.name.empty() ? "requirement document"
                                                : req_.name)
                           : req_.source_locations.front().to_string());
    }

    // "Has any rule concluded that no execution can succeed?"
    //
    // This used to test `confidence == Proven`, which looked equivalent and
    // was not. Confidence is clamped by evidence, so a statically extracted
    // requirement can never reach Proven - and every document a static
    // extractor produces is in that class. The retry rule was therefore dead
    // on exactly the input it was written for. Structural impossibility is a
    // property of the reasoning, not of how well-attested the facts are.
    bool established_impossibility() const {
        return std::any_of(result_.findings.begin(), result_.findings.end(),
                           [](const Finding& f) {
                               return f.structural_impossibility;
                           });
    }

    bool already_emitted(const char* id) const {
        return std::any_of(
            result_.findings.begin(), result_.findings.end(),
            [id](const Finding& f) { return f.id == id; });
    }

    // The weakest evidence class among the findings that proved impossibility.
    // A rule that reasons *about* other findings must inherit their evidence
    // rather than inventing its own: the retry rule knows nothing first-hand.
    EvidenceClass proven_unsupported_evidence() const {
        EvidenceClass w = EvidenceClass::SpecifiedGuarantee;
        bool found = false;
        for (const auto& f : result_.findings) {
            if (f.structural_impossibility) {
                w = rs::weakest(w, f.evidence.weakest_class());
                found = true;
            }
        }
        return found ? w : EvidenceClass::Unknown;
    }

    // ---- rules ----------------------------------------------------------
    void rule_internal_fallback_contradiction();
    void rule_baseline_mapping_capability();
    void rule_size_feasibility();
    void rule_reservation_grantable(std::uint64_t usable);
    void rule_address_bounds();
    void rule_displacement_constraint();
    void rule_reservation_alignment();
    void rule_page_size();
    void rule_address_alignment();
    void rule_size_granularity();
    void rule_range_availability();
    void rule_exact_mapping_capability();
    void rule_non_destructive_exact_mapping();
    void rule_hinted_relocation();
    void rule_identity_without_translation();
    void rule_write_execute();
    void rule_executable_mapping();
    void rule_jit_entitlement();
    void rule_reserve_commit();
    void rule_pointer_truncation();
    void rule_file_mapping_beyond_eof();
    void rule_retry_of_permanent_error();
};

// ---------------------------------------------------------------------------
// RS-VM-0014: the requirement contradicts itself.
//
// Checked first and without consulting the profile, because a self-
// contradictory contract is host-independent. ROADMAP MVP demonstration 7,
// "invalid fallback reported as success".
// ---------------------------------------------------------------------------
void Analysis::rule_internal_fallback_contradiction() {
    if (!req_.permits(FallbackKind::Relocate)) return;
    // A program that CHECKS the address it got back and rejects a bad one is
    // not accepting relocation as success - it is trying again. jemalloc
    // unmaps a relocated result and retries with over-allocate-and-trim
    // (pages.c:155). Reading that as a self-contradiction condemned the normal
    // shape of allocator code.
    if (req_.request.validates_returned_address) return;

    const bool requires_identity =
        req_.request.exact_address_required ||
        req_.assumptions.guest_host_identity_required;
    if (!requires_identity) return;

    Finding f = start(ids::kInvalidFallbackContract, Confidence::Proven,
                      SupportLevel::Unsupported);
    f.required =
        "the returned address must equal the requested address"
        + std::string(req_.assumptions.guest_host_identity_required
                          ? " (guest/host identity)"
                          : " (exact address)");
    f.host_capability = "not consulted: the contradiction is internal to the "
                        "requirement and holds on every host";
    f.modeled_fallback = "relocation is listed as a permitted fallback";
    f.conclusion =
        "The requirement permits relocation and simultaneously demands address "
        "identity. Any relocated mapping satisfies the fallback while violating "
        "the postcondition, so a 'successful' fallback still fails the caller.";

    add_application_claim(f, "exact address / identity is required");
    f.evidence.add(Layer::Application, req_.assumption_evidence,
                   "relocation is declared an acceptable fallback",
                   "requirement.permitted_fallbacks");
    f.evidence.add(Layer::Analyzer, EvidenceClass::SpecifiedGuarantee,
                   "a relocated mapping cannot satisfy "
                   "returned_address == requested_address",
                   "analyzer: definition of relocation");

    f.remediations.push_back({RemediationClass::RelaxExactAddressRequirement, ""});
    f.remediations.push_back({RemediationClass::HandleRelocationInCaller, ""});
    f.rejected_fixes.push_back(
        {"Keep both and let the runtime decide",
         "The two clauses are mutually exclusive; no runtime choice satisfies "
         "both."});
    emit(std::move(f));
}

// ---------------------------------------------------------------------------
// RS-VM-0019 / RS-VM-0017: can this host map memory at all?
//
// This rule exists because of a bug found while checking a real emulator's
// requirements against a hand-authored profile. A request that did not care
// where its mapping landed came out SUPPORTED with exit code 0 against a
// profile that had established nothing at all - not one available range, not
// one measurement. No rule objected, and "no objections" was being reported
// as support.
//
// That is the exact failure this project exists to catch, committed by the
// analyzer itself. SUPPORTED must rest on a fact that says mapping works
// here, not on the absence of facts that say it does not.
// ---------------------------------------------------------------------------
void Analysis::rule_baseline_mapping_capability() {
    if (req_.request.file_backed) return;  // a different capability

    if (!profile_.vm.anonymous_mapping_supported.is_known()) {
        if (!options_.report_unknowns) {
            result_.overall = combine(result_.overall, SupportLevel::Unknown);
            return;
        }
        Finding f = start(ids::kRequiredFactUnknown, Confidence::Hypothesis,
                          SupportLevel::Unknown);
        f.required = "the host can create an anonymous mapping of " +
                     dec(req_.request.size) + " bytes";
        f.host_capability =
            "unknown: this profile does not record whether anonymous mapping "
            "works at all";
        f.conclusion =
            "Nothing in this profile positively establishes that a mapping can "
            "succeed on this host, so no request can be reported as SUPPORTED. "
            "Run rs-env-probe on the target host.";
        add_application_claim(f, "program creates an anonymous mapping of " +
                                     dec(req_.request.size) + " bytes");
        f.evidence.add(Layer::OperatingSystem, EvidenceClass::Unknown,
                       "anonymous_mapping_supported is absent from the profile",
                       profile_.profile_name);
        emit(std::move(f));
        return;
    }

    if (profile_.vm.anonymous_mapping_supported.value()) return;

    Finding f = start(ids::kAnonymousMappingUnavailable, Confidence::Proven,
                      SupportLevel::Unsupported);
    f.structural_impossibility = true;
    f.required = "an anonymous mapping of " + dec(req_.request.size) + " bytes";
    f.host_capability = "this host refused an anonymous mapping outright";
    f.conclusion = "No placement, protection or size adjustment can help: the "
                   "operation itself is unavailable in this configuration.";
    add_application_claim(f, "program creates an anonymous mapping");
    f.evidence.add(Layer::OperatingSystem,
                   profile_.vm.anonymous_mapping_supported.evidence(),
                   "anonymous mapping is unavailable", profile_.profile_name);
    f.remediations.push_back(
        {RemediationClass::SelectDifferentHostConfiguration, ""});
    emit(std::move(f));
}

// ---------------------------------------------------------------------------
// RS-VM-0021: does the request even fit?
//
// Found by running real projects through the analyzer. QEMU's aarch64
// user-mode reserves MAX_RESERVED_VA = (1ul << 52) - 1 - four petabytes - and
// the tool called it SUPPORTED with no findings, because every placement rule
// began with `if (!request.address) return;` and nothing ever compared the
// SIZE against the address space. A request for eight exabytes was
// indistinguishable from a request for one page.
//
// Address-less-but-enormous is not an exotic case. It is how emulators,
// allocators and JITs make their largest requests.
// ---------------------------------------------------------------------------
void Analysis::rule_size_feasibility() {
    if (req_.request.size == 0) return;
    if (!profile_.vm.max_user_address.is_known()) return;

    const std::uint64_t top = profile_.vm.max_user_address.value().value;
    const std::uint64_t bottom =
        profile_.vm.min_map_address.is_known()
            ? profile_.vm.min_map_address.value().value
            : 0;
    if (top <= bottom) return;
    const std::uint64_t usable = top - bottom;
    if (req_.request.size <= usable) {
        satisfied("a reservation of " + dec(req_.request.size) +
                      " bytes fits the address space",
                  "the usable address space is " + dec(usable) + " bytes",
                  profile_.vm.max_user_address.evidence());
        // AND THAT IS WHERE THIS RULE USED TO STOP, which was the bug.
        //
        // Fitting is necessary, not sufficient - and the rule's own rejected-fix
        // text asserted the opposite: "the limit is the width of the address
        // space, not the amount of free memory in it". On a 4-level host QEMU's
        // 4 PiB request did not fit, the verdict was UNSUPPORTED, the kernel
        // refused, and the prediction held for the wrong reason. On a 56-bit
        // host it fits, the verdict became SUPPORTED, and the kernel refused
        // anyway with ENOMEM. A false positive in the dangerous direction.
        rule_reservation_grantable(usable);
        return;
    }

    Finding f = start(ids::kSizeExceedsAddressSpace, Confidence::Proven,
                      SupportLevel::Unsupported);
    f.structural_impossibility = true;
    f.required = "a single reservation of " + dec(req_.request.size) +
                 " bytes (" + json::to_hex(req_.request.size) + ")";
    f.host_capability = "the entire usable user address space is " +
                        dec(usable) + " bytes, from " + json::to_hex(bottom) +
                        " to " + json::to_hex(top);
    f.modeled_fallback =
        "the reservation fails regardless of where it is attempted; there is "
        "no address at which it could fit";
    f.conclusion =
        "The request is larger than the address space itself. Placement, "
        "alignment and protection are all irrelevant.";
    add_application_claim(f, "program reserves " + dec(req_.request.size) +
                                 " bytes in one call");
    f.evidence.add(Layer::OperatingSystem,
                   profile_.vm.max_user_address.evidence(),
                   "usable user address space is " + dec(usable) + " bytes",
                   profile_.profile_name);
    f.evidence.add(Layer::Analyzer, EvidenceClass::SpecifiedGuarantee,
                   dec(req_.request.size) + " > " + dec(usable),
                   "analyzer: size arithmetic");
    f.remediations.push_back(
        {RemediationClass::SelectDifferentHostConfiguration,
         "a host with a wider virtual address space, or a build configured for "
         "a smaller reservation"});
    f.rejected_fixes.push_back(
        {"Retry, or wait for memory pressure to ease",
         "This limit is the width of the address space. A reservation that DOES "
         "fit can still be refused for accounting reasons - see RS-VM-0026 - but "
         "this one cannot fit at any address."});
    emit(std::move(f));
}

// ---------------------------------------------------------------------------
// RS-VM-0026 / RS-VM-0027: it fits, but is it granted?
//
// Reached only when the request fits the address space. `max_single_reservation`
// is the largest power-of-two reservation the host actually granted, so:
//
//   size > 2 * granted     the next power of two above `granted` was REFUSED and
//                          this is larger still. Measured, UNSUPPORTED.
//   granted < size <= 2*granted
//                          between the largest success and the smallest failure.
//                          Nothing measured this size. CONDITIONAL, not a guess
//                          dressed as either answer.
//   size <= granted        the host granted at least this much. Nothing to say.
//
// WHICH `granted`, AND WHY THE HINTLESS ONE IS THE DEFAULT.
//
// The profile carries two: `max_single_reservation`, probed with a NULL hint, and
// `max_single_reservation_hinted`, probed above the platform's default mmap search
// window. On x86-64 Linux they differ on a 5-level-paging host, because
// `find_start_end()` opens the full address space only for a hint above
// `DEFAULT_MAP_WINDOW` - so the hintless figure means "the largest grant inside
// the default window", which is exactly what an UNHINTED CALLER GETS.
//
// This rule therefore compares against the hintless figure unless the requirement
// names an address above the hintless probe's own reach, which is the only way a
// caller reaches the wider window. A program that asks for 4 PiB with
// `addr = NULL` is bounded by the default window whatever the hardware can do,
// and answering it from the hinted number would be reporting a capability the
// caller cannot access.
//
// The fact absent is a different case, and the threshold below is the only
// constant in this rule that is not measured from the host - so it is measured
// from the corpus instead. Across 1292 requirements observed from 13 real programs
// (`campaigns/false-positive/`), the largest single request was 2103443456 bytes,
// about 1.96 GiB; the 99th percentile was 32 MiB. 4 GiB is the next power of two
// above anything this project has ever seen a real program ask for, so above it
// the analyzer has no observational basis at all and says so rather than
// defaulting to yes.
// ---------------------------------------------------------------------------
void Analysis::rule_reservation_grantable(std::uint64_t usable) {
    const std::uint64_t size = req_.request.size;

    if (!profile_.vm.max_single_reservation.is_known()) {
        constexpr std::uint64_t kLargestObservedRequest = 4ull << 30;
        if (size <= kLargestObservedRequest) return;
        Finding f = start(ids::kReservationGrantabilityUnknown,
                          Confidence::Hypothesis, SupportLevel::Unknown);
        f.required = "a single reservation of " + dec(size) + " bytes";
        f.host_capability =
            "the profile records that " + dec(usable) +
            " bytes of address space exist, and does NOT record the largest "
            "reservation this host grants";
        f.modeled_fallback =
            "unknown: fitting the address space does not establish that the "
            "kernel will account for the reservation";
        f.conclusion =
            "This is larger than any request this project has observed a real "
            "program make (1.96 GiB, across 1292 observations), and nothing in "
            "the profile bounds it. Re-probe with a build that records "
            "max_single_reservation.";
        add_application_claim(f, "program reserves " + dec(size) +
                                     " bytes in one call");
        f.evidence.add(Layer::OperatingSystem, EvidenceClass::Unknown,
                       "max_single_reservation is not recorded in this profile",
                       profile_.profile_name);
        f.remediations.push_back(
            {RemediationClass::SelectDifferentHostConfiguration,
             "measure the host with a probe that records "
             "max_single_reservation, then re-run this analysis"});
        emit(std::move(f));
        return;
    }

    std::uint64_t granted = profile_.vm.max_single_reservation.value();
    const auto& granted_fact = profile_.vm.max_single_reservation;
    bool used_hinted = false;
    // A caller that names a high address reaches the wider window; one that does
    // not is bounded by the default one whatever the hardware could give.
    if (req_.request.address &&
        profile_.vm.max_single_reservation_hinted.is_known() &&
        *req_.request.address > granted &&
        profile_.vm.max_single_reservation_hinted.value() > granted) {
        granted = profile_.vm.max_single_reservation_hinted.value();
        used_hinted = true;
    }
    const std::string granted_source =
        used_hinted ? " (measured with a hint above the default mmap window, "
                      "which this request reaches by naming a high address)"
                    : "";
    if (size <= granted) {
        satisfied("a reservation of " + dec(size) + " bytes",
                  "this host granted a reservation of " + dec(granted) +
                      " bytes" + granted_source,
                  used_hinted
                      ? profile_.vm.max_single_reservation_hinted.evidence()
                      : granted_fact.evidence());
        return;
    }

    const bool above_a_known_failure =
        granted <= (~std::uint64_t{0}) / 2 && size > 2 * granted;

    Finding f = start(ids::kSizeExceedsGrantedReservation,
                      above_a_known_failure ? Confidence::Proven
                                            : Confidence::Hypothesis,
                      above_a_known_failure ? SupportLevel::Unsupported
                                            : SupportLevel::ConditionallySupported);
    f.required = "a single reservation of " + dec(size) + " bytes (" +
                 json::to_hex(size) + ")";
    f.host_capability =
        "the largest reservation this host granted was " + dec(granted) +
        " bytes" + granted_source + ", while " + dec(usable) +
        " bytes of address space exist";
    if (above_a_known_failure) {
        f.modeled_fallback =
            "the reservation is refused; a request of " + dec(2 * granted) +
            " bytes was already refused on this host and this one is larger";
        f.conclusion =
            "The request fits the address space and the host still will not "
            "grant it. Fitting is necessary, not sufficient.";
    } else {
        f.modeled_fallback =
            "unmeasured: this size lies between the largest reservation that "
            "succeeded (" + dec(granted) + ") and the smallest that failed (" +
            dec(2 * granted) + ")";
        f.conclusion =
            "Larger than anything this host was measured to grant, smaller than "
            "anything it was measured to refuse. The honest answer is that it "
            "depends, and on what was not measured.";
    }
    add_application_claim(f, "program reserves " + dec(size) + " bytes in one call");
    f.evidence.add(Layer::OperatingSystem,
                   used_hinted
                       ? profile_.vm.max_single_reservation_hinted.evidence()
                       : granted_fact.evidence(),
                   "largest reservation granted is " + dec(granted) + " bytes" +
                       granted_source,
                   profile_.profile_name);
    f.evidence.add(Layer::Analyzer, EvidenceClass::SpecifiedGuarantee,
                   dec(size) + " > " + dec(granted), "analyzer: size arithmetic");
    f.remediations.push_back(
        {RemediationClass::ReserveAddressSpaceEarlier,
         "reserve in several smaller pieces, or lower the reservation the build "
         "asks for, or reserve before the address space fragments"});
    f.rejected_fixes.push_back(
        {"Assume it works because the address space is wide enough",
         "That is the exact reasoning this finding exists to replace: 4 PiB fits "
         "below a 56-bit max_user_address and is refused with ENOMEM."});
    emit(std::move(f));
}

// ---------------------------------------------------------------------------
// RS-VM-0023: the program needs its address inside a bounded window.
//
// Three unrelated projects in the campaign expressed exactly this and had
// nowhere to put it - LuaJIT below 2^31, Box64's box32 mode below 2^32,
// Box64's dynarec buffer ABOVE 2^32 in the same process. Authors reached for
// guest_host_identity_required instead, which means something far stronger,
// and the analyzer then reported a self-contradiction it had manufactured.
// ---------------------------------------------------------------------------
void Analysis::rule_address_bounds() {
    const auto& req = req_.request;
    if (!req.address_min && !req.address_max) return;

    const std::uint64_t lower = req.address_min.value_or(0);
    const std::uint64_t upper = req.address_max.value_or(UINT64_MAX);

    // First: is the bound self-consistent with an address the program also
    // pinned down? That needs no host at all.
    if (req.address && (*req.address < lower || *req.address >= upper)) {
        Finding f = start(ids::kAddressBoundUnsatisfiable, Confidence::Proven,
                          SupportLevel::Unsupported);
        f.structural_impossibility = true;
        f.required = "an address in [" + json::to_hex(lower) + ", " +
                     json::to_hex(upper) + ")";
        f.host_capability = "not consulted: the request names an address "
                            "outside its own declared bound";
        f.conclusion = "The requirement contradicts itself; this holds on every "
                       "host.";
        add_application_claim(f, "program requests " + json::to_hex(*req.address) +
                                     " while requiring [" + json::to_hex(lower) +
                                     ", " + json::to_hex(upper) + ")");
        f.evidence.add(Layer::Analyzer, EvidenceClass::SpecifiedGuarantee,
                       "the requested address lies outside the declared bound",
                       "analyzer: bound arithmetic");
        emit(std::move(f));
        return;
    }

    // Now the host. A bound is unsatisfiable when the usable address space
    // does not intersect it at all.
    if (req.address_min && profile_.vm.max_user_address.is_known()) {
        const std::uint64_t top = profile_.vm.max_user_address.value().value;
        if (top <= lower) {
            Finding f = start(ids::kAddressBoundUnsatisfiable, Confidence::Proven,
                              SupportLevel::Unsupported);
            f.structural_impossibility = true;
            f.required = "an address at or above " + json::to_hex(lower);
            f.host_capability = "the usable address space ends at " +
                                json::to_hex(top);
            f.conclusion = "No address on this host satisfies the lower bound.";
            add_application_claim(f, "program requires an address at or above " +
                                         json::to_hex(lower));
            f.evidence.add(Layer::OperatingSystem,
                           profile_.vm.max_user_address.evidence(),
                           "usable address space ends at " + json::to_hex(top),
                           profile_.profile_name);
            f.remediations.push_back(
                {RemediationClass::SelectDifferentHostConfiguration, ""});
            emit(std::move(f));
            return;
        }
    }
    if (req.address_max && profile_.vm.min_map_address.is_known()) {
        const std::uint64_t floor = profile_.vm.min_map_address.value().value;
        if (floor >= upper) {
            Finding f = start(ids::kAddressBoundUnsatisfiable, Confidence::Proven,
                              SupportLevel::Unsupported);
            f.structural_impossibility = true;
            f.required = "an address below " + json::to_hex(upper);
            f.host_capability = "the lowest mappable address is " +
                                json::to_hex(floor);
            f.conclusion =
                "Every address this host will grant is above the program's "
                "ceiling. This is the shape that breaks a 32-bit-addressed "
                "runtime on a host with a high load base.";
            add_application_claim(f, "program requires an address below " +
                                         json::to_hex(upper));
            f.evidence.add(Layer::OperatingSystem,
                           profile_.vm.min_map_address.evidence(),
                           "lowest mappable address is " + json::to_hex(floor),
                           profile_.profile_name);
            emit(std::move(f));
            return;
        }
    }

    // The bound is reachable in principle. Do we have positive evidence that
    // anything inside it is actually available?
    const AddressRange window{lower, upper};
    bool observed_inside = false;
    for (const auto& r : profile_.vm.available_ranges) {
        if (r.range.intersects(window)) { observed_inside = true; break; }
    }

    if (observed_inside) {
        // Satisfiable - but how comfortably? A program confined to a small
        // corner of the address space is not wrong today and not safe either:
        // it depends on the allocator handing out a low address, which decays
        // as the process fragments. ROADMAP section 11 calls this class
        // PREDICTIVE, and losing it was the cost of removing the (incorrect)
        // truncation story that used to cover the same cases.
        if (!req.address_max || !profile_.vm.max_user_address.is_known()) return;
        const std::uint64_t top = profile_.vm.max_user_address.value().value;
        if (top == 0 || upper > top / 4) return;  // uses a comfortable share

        Finding f = start(ids::kAddressBoundIsTight, Confidence::Predictive,
                          SupportLevel::ConditionallySupported);
        f.required = "every mapping below " + json::to_hex(upper);
        f.host_capability = "the host offers usable space up to " +
                            json::to_hex(top);
        f.modeled_fallback =
            "allocation succeeds while low addresses remain free and starts "
            "failing once they do not";
        // Expressed as a ratio, not a percentage: the window is routinely
        // four or five orders of magnitude smaller than the space, and integer
        // percentages of that read as a flat "0%".
        f.conclusion =
            "The program can address roughly one part in " +
            dec(upper == 0 ? 0 : top / upper) +
            " of this host's space. That is a dependency on address-space "
            "layout, not a guarantee: it degrades as the process fragments and "
            "can differ under another ASLR configuration.";
        add_application_claim(f, "program requires every mapping below " +
                                     json::to_hex(upper));
        f.evidence.add(Layer::OperatingSystem,
                       profile_.vm.max_user_address.evidence(),
                       "usable space reaches " + json::to_hex(top),
                       profile_.profile_name);
        f.evidence.add(Layer::Analyzer, EvidenceClass::HeuristicRisk,
                       "the reachable window is a small fraction of the space",
                       "analyzer: fragmentation heuristic");
        f.remediations.push_back({RemediationClass::ReserveAddressSpaceEarlier,
                                  "claim the low region at startup, before the "
                                  "allocator and loader fragment it"});
        f.remediations.push_back({RemediationClass::WidenPointerStorage,
                                  "or lift the bound entirely"});
        emit(std::move(f));
        return;
    }
    if (!options_.report_unknowns) {
        result_.overall = combine(result_.overall, SupportLevel::Unknown);
        return;
    }
    Finding f = start(ids::kRequiredFactUnknown, Confidence::Hypothesis,
                      SupportLevel::Unknown);
    f.required = "an address in [" + json::to_hex(lower) + ", " +
                 json::to_hex(upper) + ")";
    f.host_capability =
        "no probe observation falls inside that window, so whether the host "
        "can place a mapping there was never established";
    f.conclusion =
        "The bound is not refuted, but nothing supports it either. Extend the "
        "probe to sample inside the window.";
    add_application_claim(f, "program requires an address inside [" +
                                 json::to_hex(lower) + ", " +
                                 json::to_hex(upper) + ")");
    f.evidence.add(Layer::OperatingSystem, EvidenceClass::Unknown,
                   "no observation inside the required window",
                   profile_.profile_name);
    emit(std::move(f));
}

// ---------------------------------------------------------------------------
// RS-VM-0024: "within N bytes of something else".
//
// Every JIT that emits a relative branch lives under this constraint. v0.1
// cannot evaluate it, because a host profile has no idea where the reference
// region will land. The campaign's verdict on LuaJIT's machine-code window
// was not wrong, it was EMPTY: stripped of the displacement the request is
// "64 KiB anon RW, later RX", which is trivially supportable. Saying so out
// loud is the difference between an unanswered question and an unnoticed one.
// ---------------------------------------------------------------------------
void Analysis::rule_displacement_constraint() {
    if (!req_.request.max_displacement_bytes) return;

    Finding f = start(ids::kDisplacementConstraintNotEvaluable,
                      Confidence::Hypothesis, SupportLevel::Unknown);
    f.required = "the mapping must land within " +
                 dec(*req_.request.max_displacement_bytes) + " bytes of " +
                 (req_.request.displacement_reference.empty()
                      ? "another region"
                      : req_.request.displacement_reference);
    f.host_capability =
        "a host profile records what the address space looks like, not where "
        "any particular region of a future process will be placed";
    f.modeled_fallback =
        "if the two land too far apart the program cannot encode its branches "
        "and either falls back to a slower sequence or fails outright";
    f.conclusion =
        "This constraint is carried but NOT evaluated in v0.1. The rest of "
        "this verdict says nothing about it. Deciding it needs either a "
        "runtime observation of both regions or an allocation-order model.";
    add_application_claim(f, "program requires the mapping within " +
                                 dec(*req_.request.max_displacement_bytes) +
                                 " bytes of " +
                                 (req_.request.displacement_reference.empty()
                                      ? "another region"
                                      : req_.request.displacement_reference));
    f.evidence.add(Layer::Analyzer, EvidenceClass::Unknown,
                   "no rule in v0.1 evaluates relative displacement",
                   "analyzer: acknowledged gap");
    f.remediations.push_back(
        {RemediationClass::ReserveAddressSpaceEarlier,
         "reserve both regions together at startup so the displacement is "
         "chosen rather than hoped for"});
    emit(std::move(f));
}

// ---------------------------------------------------------------------------
// RS-VM-0022: alignment of the reservation itself.
//
// Also found by running real projects. `required_alignment` was only ever
// consulted when an address was also given, so the constraint that matters
// most to allocators and runtimes - "give me 4 GiB, aligned to 4 GiB, I do
// not care where" - fell straight through. Deleting the field from a V8 cage
// contract produced a byte-identical verdict, which is the definition of dead
// code.
//
// mmap guarantees page alignment and nothing more. Stronger alignment is
// reachable only by over-allocating and trimming, and whether the program
// does that is not something a host profile can answer - so this is a
// conditional finding, not a refusal.
// ---------------------------------------------------------------------------
void Analysis::rule_reservation_alignment() {
    if (!req_.request.required_alignment) return;
    if (req_.request.address) return;  // covered by rule_address_alignment
    const std::uint64_t alignment = *req_.request.required_alignment;
    if (!is_power_of_two(alignment)) {
        result_.analyzer_limitations.push_back(
            "required_alignment " + dec(alignment) +
            " is not a power of two; the alignment rules were skipped");
        return;
    }
    if (!profile_.vm.allocation_granularity.is_known()) return;
    const std::uint64_t granularity = profile_.vm.allocation_granularity.value();
    if (alignment <= granularity) return;  // the API already guarantees it

    Finding f = start(ids::kReservationAlignmentUnguaranteed, Confidence::Proven,
                      SupportLevel::ConditionallySupported);
    f.required = "a reservation of " + dec(req_.request.size) +
                 " bytes aligned to " + dec(alignment) + " bytes";
    f.host_capability = "the mapping API guarantees alignment to " +
                        dec(granularity) + " bytes and no more";
    f.modeled_fallback =
        "the reservation lands page-aligned at an address of the host's "
        "choosing; the stronger alignment holds only if the program "
        "over-allocates " + dec(alignment) + " extra bytes and trims, or "
        "retries until it gets a lucky address";
    f.conclusion =
        "Nothing in the platform delivers this alignment. If the program does "
        "not implement over-allocate-and-trim, the misalignment is silent: "
        "pointer-masking arithmetic keeps working and simply addresses the "
        "wrong object.";
    add_application_claim(f, "program requires the reservation itself to be " +
                                 dec(alignment) + "-byte aligned");
    f.evidence.add(Layer::SystemCallInterface,
                   profile_.vm.allocation_granularity.evidence(),
                   "allocation granularity is " + dec(granularity) + " bytes",
                   profile_.profile_name);
    f.evidence.add(Layer::Analyzer, EvidenceClass::SpecifiedGuarantee,
                   dec(alignment) + " > " + dec(granularity) +
                       ", so the API cannot promise it",
                   "analyzer: alignment arithmetic");
    f.remediations.push_back(
        {RemediationClass::ReserveAddressSpaceEarlier,
         "reserve " + dec(alignment) + " extra bytes and unmap the unaligned "
         "head and tail"});
    f.rejected_fixes.push_back(
        {"Assert the alignment after the call",
         "An assert catches it only when it happens to fire. Many builds "
         "compile assertions out, and the corruption that follows a silently "
         "misaligned base is not attributable to this call site."});
    emit(std::move(f));
}

// ---------------------------------------------------------------------------
// RS-VM-0006: hard-coded page size versus host page size.
// ---------------------------------------------------------------------------
void Analysis::rule_page_size() {
    if (!req_.request.required_page_size) return;
    const std::uint64_t required = *req_.request.required_page_size;

    if (!profile_.vm.page_size.is_known()) {
        if (!options_.report_unknowns) return;
        // The id (and therefore the title) stays stable across every "we never
        // established this" outcome; the specific fact is named in the body so
        // CI can filter the whole class with one rule.
        Finding f = start(ids::kRequiredFactUnknown, Confidence::Hypothesis,
                          SupportLevel::Unknown);
        f.required = "page size == " + dec(required);
        f.host_capability = "unknown: the profile carries no page-size fact";
        f.conclusion = "Run rs-env-probe on the target host to establish the "
                       "page size before trusting this analysis.";
        add_application_claim(f, "program assumes a page size of " + dec(required));
        f.evidence.add(Layer::OperatingSystem, EvidenceClass::Unknown,
                       "page size not present in profile", profile_.profile_name);
        emit(std::move(f));
        return;
    }

    const std::uint64_t actual = profile_.vm.page_size.value();
    const SizeRelation relation = req_.request.required_page_size_relation;
    const char* relation_text = relation == SizeRelation::AtMost   ? "<= "
                                : relation == SizeRelation::AtLeast ? ">= "
                                                                    : "== ";
    bool satisfied_relation = false;
    switch (relation) {
        case SizeRelation::Equal:   satisfied_relation = actual == required; break;
        case SizeRelation::AtMost:  satisfied_relation = actual <= required; break;
        case SizeRelation::AtLeast: satisfied_relation = actual >= required; break;
    }
    if (satisfied_relation) {
        satisfied("host page size " + std::string(relation_text) + dec(required),
                  "host page size is " + dec(actual),
                  profile_.vm.page_size.evidence());
        return;
    }

    Finding f = start(ids::kPageSizeMismatch, Confidence::Proven,
                      SupportLevel::Unsupported);
    f.structural_impossibility = true;
    f.required = std::string("page size ") + relation_text + dec(required);
    f.host_capability = "page size == " + dec(actual);
    f.modeled_fallback =
        actual > required
            ? "mappings are rounded up to the larger host page, so distinct "
              "guest pages share one host page and their protections collide"
            : "one guest page spans several host pages, so protection changes "
              "are not atomic with respect to the guest model";
    f.conclusion = "The host page size differs from the size the program has "
                   "compiled in. No mapping request can repair this.";

    add_application_claim(f, "program requires a page size of " + dec(required));
    f.evidence.add(Layer::OperatingSystem, profile_.vm.page_size.evidence(),
                   "host page size is " + dec(actual),
                   profile_.vm.page_size.source().empty()
                       ? profile_.profile_name
                       : profile_.vm.page_size.source());

    f.remediations.push_back({RemediationClass::MatchHostPageSize, ""});
    f.remediations.push_back({RemediationClass::SelectDifferentHostConfiguration,
                              "a host whose page size is " + dec(required)});
    f.rejected_fixes.push_back(
        {"Round the mapping size up to the host page size",
         "Rounding fixes the allocation but not the program's page-granularity "
         "model: protection and dirty tracking still operate on host pages."});
    emit(std::move(f));
}

// ---------------------------------------------------------------------------
// RS-VM-0004: exact address versus allocation granularity.
// ---------------------------------------------------------------------------
void Analysis::rule_address_alignment() {
    if (!req_.request.address) return;
    const std::uint64_t address = *req_.request.address;

    // Explicit program-declared alignment first: it is host-independent.
    if (req_.request.required_alignment) {
        const std::uint64_t alignment = *req_.request.required_alignment;
        if (!is_power_of_two(alignment)) {
            result_.analyzer_limitations.push_back(
                "required_alignment " + dec(alignment) +
                " is not a power of two; alignment rules were skipped");
        } else if (!is_aligned(address, alignment)) {
            Finding f = start(ids::kAddressAlignmentMismatch, Confidence::Proven,
                              SupportLevel::Unsupported);
            f.required = "address aligned to " + dec(alignment);
            f.host_capability = "not consulted: the request is internally "
                                "inconsistent";
            f.conclusion = "The requested address does not satisfy the "
                           "alignment the program itself declares.";
            add_application_claim(
                f, "program requires " + dec(alignment) + "-byte alignment but "
                   "requests " + json::to_hex(address));
            f.evidence.add(Layer::Analyzer, EvidenceClass::SpecifiedGuarantee,
                           json::to_hex(address) + " % " + dec(alignment) +
                               " != 0",
                           "analyzer: alignment arithmetic");
            f.remediations.push_back(
                {RemediationClass::ChooseDifferentBaseAddress, ""});
            emit(std::move(f));
            return;
        }
    }

    if (!req_.request.exact_address_required) return;
    if (!profile_.vm.allocation_granularity.is_known()) return;

    const std::uint64_t granularity = profile_.vm.allocation_granularity.value();
    if (is_aligned(address, granularity)) return;

    Finding f = start(ids::kAddressAlignmentMismatch, Confidence::Proven,
                      SupportLevel::Unsupported);
    f.structural_impossibility = true;
    f.required = "mapping placed exactly at " + json::to_hex(address);
    f.host_capability =
        "exact mappings must be aligned to the allocation granularity of " +
        dec(granularity) + " bytes";
    f.modeled_fallback =
        "the request is rejected, or the base is rounded down and the mapping "
        "starts below the requested address";
    f.conclusion = "The requested base address is not a multiple of this host's "
                   "allocation granularity, so an exact placement is impossible.";

    add_application_claim(f, "program requires an exact mapping at " +
                                 json::to_hex(address));
    f.evidence.add(Layer::OperatingSystem,
                   profile_.vm.allocation_granularity.evidence(),
                   "allocation granularity is " + dec(granularity) + " bytes",
                   profile_.vm.allocation_granularity.source().empty()
                       ? profile_.profile_name
                       : profile_.vm.allocation_granularity.source());
    f.evidence.add(Layer::Analyzer, EvidenceClass::SpecifiedGuarantee,
                   json::to_hex(address) + " % " + dec(granularity) + " != 0",
                   "analyzer: alignment arithmetic");

    f.remediations.push_back({RemediationClass::ChooseDifferentBaseAddress,
                              "round the base down to " +
                                  json::to_hex(align_down(address, granularity)
                                                   .value_or(0))});
    emit(std::move(f));
}

// ---------------------------------------------------------------------------
// RS-VM-0005: size versus allocation granularity.
//
// This rule's conclusion always named its own precondition - "only a defect
// if the program relies on the bytes past its requested size being unmapped" -
// and then fired as a CONDITION for every caller anyway, because the
// requirement model had no way to state the reliance. The false-positive
// campaign measured what that assumption costs: 42% of all real mappings
// (544 of 1292) pass unrounded sizes, because that is simply how mmap is
// called, and every one came back CONDITIONALLY_SUPPORTED. Correct, and in a
// CI gate indistinguishable from noise - and this project has already written
// down what happens to a noisy guard: it gets switched off.
//
// The decision (T-019, 2026-07-30): the precondition became a declarable fact,
// `relies_on_unmapped_beyond_size`, on the exact precedent of
// `accesses_beyond_eof` - a behavioral claim belongs to the caller, not to the
// rule's imagination. Declared, the rounding violates a stated guarantee in
// every execution: UNSUPPORTED, and the finding says which bytes. Undeclared,
// the fact is still true and still worth recording - the campaign's own
// analysis rejected deleting it - so it is emitted as information on a
// SUPPORTED verdict, the same quiet-line shape as RS-VM-0020's unhonourable
// hint. What no caller gets any more is a condition nobody stated.
// ---------------------------------------------------------------------------
void Analysis::rule_size_granularity() {
    if (!profile_.vm.allocation_granularity.is_known()) return;
    const std::uint64_t granularity = profile_.vm.allocation_granularity.value();
    if (granularity == 0 || is_aligned(req_.request.size, granularity)) return;

    const auto rounded = round_up_to(req_.request.size, granularity);
    if (!rounded) return;
    const std::uint64_t extra = *rounded - req_.request.size;

    if (req_.request.relies_on_unmapped_beyond_size) {
        // The caller stated the guarantee this host cannot give. No execution
        // satisfies it: every reservation is rounded, so bytes
        // [size, rounded) are addressable in all of them.
        Finding f = start(ids::kSizeGranularityMismatch, Confidence::Proven,
                          SupportLevel::Unsupported);
        f.severity = Severity::High;  // silent guarantee violation, not noise
        f.required = "bytes past the requested " + dec(req_.request.size) +
                     " must stay unmapped (declared: the program relies on it)";
        f.host_capability = "reservations are rounded up to " +
                            dec(granularity) + "-byte units";
        f.modeled_fallback =
            "the host reserves " + dec(*rounded) + " bytes; the " + dec(extra) +
            " bytes past the requested size are addressable and will not "
            "fault, so whatever depends on that fault never fires";
        f.conclusion =
            "The program declares it relies on the bytes past its requested "
            "size staying unmapped, and on this host they never are. The "
            "mapping call succeeds; the guarantee it was trusted for does not "
            "hold in any execution.";
        add_application_claim(f, "program requests " + dec(req_.request.size) +
                                     " bytes and relies on the remainder of "
                                     "the region staying unmapped");
        f.evidence.add(Layer::OperatingSystem,
                       profile_.vm.allocation_granularity.evidence(),
                       "allocation granularity is " + dec(granularity) +
                           " bytes",
                       profile_.profile_name);
        f.remediations.push_back(
            {RemediationClass::ReserveAddressSpaceEarlier,
             "reserve " + dec(*rounded) + " bytes and place an explicit "
             "PROT_NONE guard page after the " + dec(req_.request.size) +
             " the program uses, instead of relying on the reservation edge"});
        emit(std::move(f));
        return;
    }

    // No reliance declared. The rounding is a fact about this host worth one
    // line, not a condition on the verdict.
    Finding f = start(ids::kSizeGranularityMismatch, Confidence::Proven,
                      SupportLevel::Supported);
    f.severity = Severity::Info;
    f.required = "a mapping of " + dec(req_.request.size) +
                 " bytes, with no declared reliance on the bytes past it";
    f.host_capability = "reservations are rounded up to " + dec(granularity) +
                        "-byte units";
    f.modeled_fallback = "the host reserves " + dec(*rounded) +
                         " bytes; the extra " + dec(extra) +
                         " bytes are addressable and will not fault";
    f.conclusion =
        "The mapping succeeds and covers " + dec(*rounded) +
        " bytes rather than the requested " + dec(req_.request.size) +
        ". Recorded as information: this matters only to a program that "
        "relies on the bytes past its requested size being unmapped, and this "
        "requirement declares no such reliance "
        "(`relies_on_unmapped_beyond_size`).";
    add_application_claim(f, "program requests " + dec(req_.request.size) +
                                 " bytes");
    f.evidence.add(Layer::OperatingSystem,
                   profile_.vm.allocation_granularity.evidence(),
                   "allocation granularity is " + dec(granularity) + " bytes",
                   profile_.profile_name);
    emit(std::move(f));
}

// ---------------------------------------------------------------------------
// RS-VM-0001 / 0002 / 0003 / 0017: is the requested range available at all?
//
// This is the rule that diagnoses the ROADMAP's motivating incident.
// ---------------------------------------------------------------------------
void Analysis::rule_range_availability() {
    if (!req_.request.address) return;
    if (!req_.request.exact_address_required &&
        !req_.assumptions.guest_host_identity_required) {
        // The program does not care where the mapping lands, so availability
        // at this particular address cannot make the request fail. It can
        // still make the hint pointless, which is worth one quiet line:
        // a hint aimed at a range the host cannot provide will certainly be
        // ignored, and code downstream sometimes assumes otherwise.
        const auto hint_range = req_.request.range();
        if (!hint_range) return;
        const RangeVerdict hint_verdict = profile_.query_range(*hint_range);
        if (hint_verdict.level != SupportLevel::Unsupported) return;

        Finding f = start(ids::kAddressHintNotHonourable, Confidence::Proven,
                          SupportLevel::Supported);
        f.required = "nothing: the address is a hint and relocation is "
                     "acceptable to this caller";
        f.host_capability = hint_verdict.reason;
        f.modeled_fallback =
            "the mapping is placed at an address of the host's choosing; the "
            "hint has no effect";
        f.conclusion =
            "The request still succeeds. Check that no later code treats the "
            "hint as the address it got back.";
        add_application_claim(f, "program hints at " +
                                     json::to_hex(*req_.request.address));
        f.evidence.add(Layer::OperatingSystem, hint_verdict.evidence,
                       hint_verdict.reason, profile_.profile_name);
        f.remediations.push_back(
            {RemediationClass::ChooseDifferentBaseAddress,
             "pick a hint the host can actually honour, or drop it"});
        emit(std::move(f));
        return;
    }

    const auto range = req_.request.range();
    if (!range) {
        // address + size overflows uint64: the requested region wraps past the
        // end of the 64-bit address space, so no host can place it. That is a
        // proven impossibility, not a gap in the analysis - recording it as only
        // a limitation left the verdict SUPPORTED for a request that can never
        // be satisfied (independent review, A5 overflow). Exact placement is
        // required on this path (checked above), so the wrap is load-bearing.
        Finding f = start(ids::kAddressAboveUserSpace, Confidence::Proven,
                          SupportLevel::Unsupported);
        f.structural_impossibility = true;
        f.required = "mapping of " + dec(req_.request.size) + " bytes placed at " +
                     json::to_hex(*req_.request.address);
        f.host_capability =
            "the requested range wraps past the end of the 64-bit address space: "
            + json::to_hex(*req_.request.address) + " + " +
            dec(req_.request.size) + " bytes exceeds 2^64";
        f.modeled_fallback =
            "no mapping can be placed; a region that wraps the address space "
            "does not exist, so the call fails outright";
        f.conclusion =
            "No execution satisfying all constraints exists on any host: a region "
            "that wraps the 64-bit address space cannot be mapped.";
        add_application_claim(f, "program requires an exact mapping at " +
                                     json::to_hex(*req_.request.address) + " of " +
                                     dec(req_.request.size) + " bytes");
        f.evidence.add(Layer::OperatingSystem, EvidenceClass::SpecifiedGuarantee,
                       "address + size exceeds the 64-bit address space",
                       profile_.profile_name.empty() ? result_.profile_id
                                                      : profile_.profile_name);
        f.remediations.push_back(
            {RemediationClass::ChooseDifferentBaseAddress, ""});
        f.remediations.push_back(
            {RemediationClass::RelaxExactAddressRequirement, ""});
        emit(std::move(f));
        return;
    }

    const RangeVerdict verdict = profile_.query_range(*range);

    if (verdict.level == SupportLevel::Supported) {
        satisfied("exact placement at " + range->to_string(), verdict.reason,
                  verdict.evidence);
        return;
    }

    if (verdict.level == SupportLevel::Unknown) {
        if (!options_.report_unknowns) {
            result_.overall = combine(result_.overall, SupportLevel::Unknown);
            return;
        }
        Finding f = start(ids::kRequiredFactUnknown, Confidence::Hypothesis,
                          SupportLevel::Unknown);
        f.required = "mapping placed exactly at " + range->to_string();
        f.host_capability = verdict.reason;
        f.conclusion =
            "The analyzer cannot decide this request. Extend the probe to cover "
            "the range, or supply a profile that specifies it.";
        add_application_claim(f, "program requires an exact mapping at " +
                                     range->to_string());
        f.evidence.add(Layer::OperatingSystem, EvidenceClass::Unknown,
                       verdict.reason, profile_.profile_name);
        emit(std::move(f));
        return;
    }

    // Unsupported. Pick the finding id that names the actual obstacle.
    const char* id = ids::kExactAddressUnavailable;
    if (profile_.vm.min_map_address.is_known() &&
        range->start < profile_.vm.min_map_address.value().value) {
        id = ids::kAddressBelowMinimum;
    } else if (profile_.vm.max_user_address.is_known() &&
               range->end > profile_.vm.max_user_address.value().value) {
        id = ids::kAddressAboveUserSpace;
    }

    Finding f = start(id, Confidence::Proven, SupportLevel::Unsupported);
    f.structural_impossibility = true;
    f.required = "mapping placed exactly at " + range->to_string();
    if (req_.assumptions.guest_host_identity_required) {
        f.required += "; guest address must equal host address";
    }
    f.host_capability = verdict.reason;
    f.modeled_fallback =
        req_.assumptions.translation_layer_available
            ? "a translation layer could rebase the mapping"
            : "the mapping is placed elsewhere and reported as success, or the "
              "call fails outright";
    f.conclusion =
        req_.assumptions.translation_layer_available
            ? "The host cannot place the mapping at the requested address. A "
              "translation layer is available, so the outcome depends on "
              "whether that layer is actually engaged for this mapping."
            : "No execution satisfying all constraints exists on this host "
              "profile. The address is unavailable and no translation layer "
              "can absorb the difference.";

    add_application_claim(f, "program requires an exact mapping at " +
                                 json::to_hex(range->start) + " of " +
                                 dec(req_.request.size) + " bytes");
    if (req_.assumptions.guest_host_identity_required) {
        f.evidence.add(Layer::CompatibilityLayer, req_.assumption_evidence,
                       req_.assumptions.translation_layer_available
                           ? "a guest-to-host translation layer is available"
                           : "no guest-to-host translation layer exists, so the "
                             "guest address must equal the host address",
                       "requirement.assumptions");
    }
    f.evidence.add(Layer::OperatingSystem, verdict.evidence, verdict.reason,
                   profile_.profile_name.empty() ? result_.profile_id
                                                 : profile_.profile_name);
    if (verdict.conflicting_range) {
        f.evidence.add(Layer::Kernel, verdict.evidence,
                       "conflicting range " + verdict.conflicting_range->to_string(),
                       profile_.profile_name);
    }

    if (!req_.assumptions.translation_layer_available) {
        f.remediations.push_back(
            {RemediationClass::AddAddressTranslationLayer, ""});
    }
    f.remediations.push_back({RemediationClass::ChooseDifferentBaseAddress, ""});
    f.remediations.push_back({RemediationClass::RelaxExactAddressRequirement, ""});
    f.remediations.push_back({RemediationClass::ReserveAddressSpaceEarlier,
                              "only helps if the range is free at startup and "
                              "merely contended later"});
    f.remediations.push_back(
        {RemediationClass::SelectDifferentHostConfiguration, ""});

    f.rejected_fixes.push_back(
        {"Retry the mapping",
         "The obstacle is a property of this host's address space, not a "
         "transient resource shortage. Every attempt fails identically."});
    f.rejected_fixes.push_back(
        {"Request a smaller size at the same address",
         "The start address itself is unavailable; reducing the length does not "
         "make it available."});
    if (!req_.assumptions.translation_layer_available) {
        f.rejected_fixes.push_back(
            {"Ignore the returned address and continue",
             "The program compares the returned address against the requested "
             "one and treats a mismatch as fatal."});
    }
    emit(std::move(f));
}

// ---------------------------------------------------------------------------
// RS-VM-0001 (capability form): the host does not support exact placement.
// ---------------------------------------------------------------------------
void Analysis::rule_exact_mapping_capability() {
    if (!req_.request.exact_address_required) return;
    if (!profile_.vm.exact_mapping.is_known()) return;
    const SupportLevel level = profile_.vm.exact_mapping.value();
    if (level != SupportLevel::Unsupported) return;

    Finding f = start(ids::kExactAddressUnavailable, Confidence::Proven,
                      SupportLevel::Unsupported);
    f.structural_impossibility = true;
    f.required = "the mapping must be placed at an exactly specified address";
    f.host_capability = "this host does not support exact address placement at all";
    f.modeled_fallback = "the address is treated as a hint";
    f.conclusion = "Exact placement is unavailable as a capability on this host, "
                   "independently of which address is requested.";
    add_application_claim(f, "program requires exact address placement");
    f.evidence.add(Layer::OperatingSystem, profile_.vm.exact_mapping.evidence(),
                   "exact_mapping is UNSUPPORTED in the profile",
                   profile_.profile_name);
    f.remediations.push_back({RemediationClass::AddAddressTranslationLayer, ""});
    f.remediations.push_back({RemediationClass::RelaxExactAddressRequirement, ""});
    emit(std::move(f));
}

// ---------------------------------------------------------------------------
// RS-VM-0016: exact placement exists, but only destructively.
//
// Without MAP_FIXED_NOREPLACE (or an equivalent), the only way to demand an
// address is to overwrite whatever is already there. That is a silent
// contract degradation waiting to happen: the call succeeds and something
// else in the process loses its memory.
// ---------------------------------------------------------------------------
void Analysis::rule_non_destructive_exact_mapping() {
    if (!req_.request.exact_address_required) return;
    if (!profile_.vm.fixed_noreplace_available.is_known()) return;
    if (profile_.vm.fixed_noreplace_available.value()) return;

    Finding f = start(ids::kExactMappingNonDestructiveUnavailable,
                      Confidence::Proven, SupportLevel::ConditionallySupported);
    f.required = "an exact mapping that does not disturb existing mappings";
    f.host_capability =
        "this host offers no non-destructive exact-placement primitive";
    f.modeled_fallback =
        "a forced exact mapping silently unmaps whatever already occupied the "
        "range, or the address is downgraded to a hint and may relocate";
    f.conclusion =
        "Exact placement is only reachable through a destructive operation. "
        "Success does not imply that the range was free.";
    add_application_claim(f, "program requires exact address placement");
    f.evidence.add(Layer::SystemCallInterface,
                   profile_.vm.fixed_noreplace_available.evidence(),
                   "no non-destructive fixed-mapping primitive is available",
                   profile_.profile_name);
    f.remediations.push_back({RemediationClass::ReserveAddressSpaceEarlier, ""});
    f.rejected_fixes.push_back(
        {"Check the range with a preliminary query, then map it",
         "The check and the mapping are not atomic; another thread or the "
         "allocator can take the range in between."});
    emit(std::move(f));
}

// ---------------------------------------------------------------------------
// RS-VM-0007: hinted mapping may relocate while the caller assumes identity.
// ROADMAP MVP demonstration 2, and the Phase 6 "latent portability failure".
// ---------------------------------------------------------------------------
void Analysis::rule_hinted_relocation() {
    if (req_.request.exact_address_required) return;   // covered elsewhere
    if (!req_.request.address) return;
    if (!req_.assumptions.guest_host_identity_required) return;
    if (!profile_.vm.hinted_mapping_may_relocate.is_known()) return;
    if (!profile_.vm.hinted_mapping_may_relocate.value()) return;

    Finding f = start(ids::kHintedMappingMayRelocate, Confidence::Counterexample,
                      SupportLevel::Unsupported);
    f.required = "returned_address == requested_address";
    f.host_capability =
        "the requested address is a hint; the host may return a different one";
    f.modeled_fallback =
        "the call succeeds with a relocated address and the caller's identity "
        "assumption is violated without any error being reported";
    f.conclusion =
        "This request may succeed today and fail on the next run, kernel "
        "version or address-space layout. The observed success is weaker than "
        "the required success.";

    add_application_claim(
        f, "program requires the returned address to equal " +
               json::to_hex(*req_.request.address));
    f.evidence.add(Layer::OperatingSystem,
                   profile_.vm.hinted_mapping_may_relocate.evidence(),
                   "a hinted mapping is permitted to relocate on this host",
                   profile_.profile_name);
    f.evidence.add(Layer::Analyzer, EvidenceClass::BoundedCounterexample,
                   "a platform-legal outcome (relocation) reaches the caller's "
                   "failure sink",
                   "analyzer: outcome enumeration for hinted mappings");

    f.remediations.push_back({RemediationClass::HandleRelocationInCaller, ""});
    f.remediations.push_back({RemediationClass::AddAddressTranslationLayer, ""});
    f.rejected_fixes.push_back(
        {"It works on our machines, so ship it",
         "Relocation is permitted rather than forbidden. Absence of relocation "
         "in observed runs is not a guarantee about future runs."});
    emit(std::move(f));
}

// ---------------------------------------------------------------------------
// RS-VM-0008: identity assumption with no translation layer.
//
// This is the residual case: the program relies on guest/host address
// identity but does not demand exact placement, and nothing more specific has
// already been said. It is suppressed whenever a sharper rule covers the same
// ground, so the report never states the same problem twice.
// ---------------------------------------------------------------------------
void Analysis::rule_identity_without_translation() {
    if (!req_.assumptions.guest_host_identity_required) return;
    if (req_.assumptions.translation_layer_available) return;

    // Exact placement, when the host honours it, delivers identity by
    // construction: the call either lands at the requested address or fails.
    // If the host cannot honour it, rule_range_availability has already said
    // so, with the actual obstacle named.
    if (req_.request.exact_address_required) return;

    // The hinted-relocation rule states the same risk with a measured fact
    // behind it and a stronger confidence class.
    if (already_emitted(ids::kHintedMappingMayRelocate)) return;

    if (established_impossibility()) return;  // already stated more precisely

    Finding f = start(ids::kIdentityWithoutTranslation, Confidence::Hypothesis,
                      SupportLevel::ConditionallySupported);
    f.required = "guest addresses must equal host addresses";
    f.host_capability =
        "the host chooses addresses; nothing binds it to the guest layout";
    f.modeled_fallback =
        "any future address-space change (ASLR, loader, allocator, kernel "
        "version) can break the identity";
    f.conclusion =
        "The identity assumption currently holds but is not guaranteed by "
        "anything. It is a structural fragility rather than a present defect.";
    add_application_claim(f, "program requires guest/host address identity");
    f.evidence.add(Layer::CompatibilityLayer, req_.assumption_evidence,
                   "no translation layer is available to absorb a mismatch",
                   "requirement.assumptions");
    f.evidence.add(Layer::Analyzer, EvidenceClass::HeuristicRisk,
                   "identity is preserved by circumstance, not by contract",
                   "analyzer: structural assessment");
    f.remediations.push_back({RemediationClass::AddAddressTranslationLayer, ""});
    emit(std::move(f));
}

// ---------------------------------------------------------------------------
// RS-VM-0009: W^X.
// ---------------------------------------------------------------------------
void Analysis::rule_write_execute() {
    const bool wants_simultaneous = req_.request.simultaneous_write_execute ||
                                    req_.request.protection.write_and_execute();
    if (!wants_simultaneous) {
        // The RW -> RX flip is the well-behaved JIT pattern; check that the
        // host permits it when the program relies on it.
        if (!req_.request.write_then_execute) return;
        if (!profile_.vm.protection.write_then_execute_transition.is_known()) return;
        if (profile_.vm.protection.write_then_execute_transition.value()) return;

        Finding f = start(ids::kWriteExecuteRestricted, Confidence::Proven,
                          SupportLevel::Unsupported);
        f.required = "write code into a mapping, then make it executable";
        f.host_capability = "this host rejects the write-then-execute transition";
        f.conclusion = "Neither simultaneous nor sequential write/execute is "
                       "available for this mapping.";
        add_application_claim(f, "program writes code and then executes it");
        f.evidence.add(Layer::OperatingSystem,
                       profile_.vm.protection.write_then_execute_transition.evidence(),
                       "the RW to RX protection transition is not permitted",
                       profile_.profile_name);
        f.remediations.push_back({RemediationClass::RequestPlatformEntitlement, ""});
        f.remediations.push_back(
            {RemediationClass::UseSeparateWriteAndExecuteMappings, ""});
        emit(std::move(f));
        return;
    }

    if (!profile_.vm.protection.write_execute_simultaneous.is_known()) return;
    if (profile_.vm.protection.write_execute_simultaneous.value()) {
        satisfied("a mapping that is writable and executable at once",
                  "this host permits simultaneous write and execute",
                  profile_.vm.protection.write_execute_simultaneous.evidence());
        return;
    }

    const bool flip_available =
        profile_.vm.protection.write_then_execute_transition.is_known() &&
        profile_.vm.protection.write_then_execute_transition.value();

    Finding f = start(ids::kWriteExecuteRestricted, Confidence::Proven,
                      SupportLevel::Unsupported);
    f.structural_impossibility = true;
    f.required = "a mapping that is writable and executable at the same time";
    f.host_capability = "this host enforces write-xor-execute";
    f.modeled_fallback =
        flip_available
            ? "the protection can be flipped between writable and executable, "
              "but never both at once"
            : "the request fails";
    f.conclusion =
        "Write-xor-execute is a security policy, not a defect. The program's "
        "memory model has to change; the platform's will not.";
    add_application_claim(f, "program requires simultaneous write and execute "
                             "permission");
    f.evidence.add(Layer::OperatingSystem,
                   profile_.vm.protection.write_execute_simultaneous.evidence(),
                   "simultaneous write+execute is not available",
                   profile_.profile_name);
    if (flip_available) {
        f.evidence.add(Layer::OperatingSystem,
                       profile_.vm.protection.write_then_execute_transition.evidence(),
                       "the RW to RX transition is available",
                       profile_.profile_name);
    }
    f.remediations.push_back(
        {RemediationClass::UseSeparateWriteAndExecuteMappings, ""});
    if (flip_available) {
        f.remediations.push_back(
            {RemediationClass::UseSeparateWriteAndExecuteMappings,
             "or serialize the JIT into write, flush, then flip to executable"});
    }
    f.rejected_fixes.push_back(
        {"Disable the platform's W^X enforcement",
         "RuntimeSkeptic does not treat a security control as a defect, and "
         "disabling it is not a portable remediation."});
    emit(std::move(f));
}

// ---------------------------------------------------------------------------
// RS-VM-0010: executable anonymous memory at all.
// ---------------------------------------------------------------------------
void Analysis::rule_executable_mapping() {
    if (!req_.request.protection.execute && !req_.request.write_then_execute) return;
    if (req_.request.file_backed) return;
    if (!profile_.vm.protection.anonymous_executable_mapping.is_known()) return;
    if (profile_.vm.protection.anonymous_executable_mapping.value()) {
        satisfied("executable anonymous memory",
                  "this host permits anonymous executable mappings",
                  profile_.vm.protection.anonymous_executable_mapping.evidence());
        return;
    }

    Finding f = start(ids::kExecutableMappingUnsupported, Confidence::Proven,
                      SupportLevel::Unsupported);
    f.structural_impossibility = true;
    f.required = "executable anonymous memory";
    f.host_capability = "anonymous mappings cannot be made executable in this "
                        "process configuration";
    f.conclusion = "Code generated at runtime cannot be executed on this host "
                   "profile.";
    add_application_claim(f, "program requires executable anonymous memory");
    f.evidence.add(Layer::OperatingSystem,
                   profile_.vm.protection.anonymous_executable_mapping.evidence(),
                   "anonymous executable mapping is unavailable",
                   profile_.profile_name);
    f.remediations.push_back({RemediationClass::RequestPlatformEntitlement, ""});
    f.remediations.push_back(
        {RemediationClass::SelectDifferentHostConfiguration, ""});
    emit(std::move(f));
}

// ---------------------------------------------------------------------------
// RS-VM-0011: JIT entitlement.
// ---------------------------------------------------------------------------
void Analysis::rule_jit_entitlement() {
    if (!req_.request.protection.execute && !req_.request.write_then_execute) return;
    if (!profile_.vm.protection.jit_entitlement_required.is_known()) return;
    if (!profile_.vm.protection.jit_entitlement_required.value()) {
        satisfied("executable memory for generated code",
                  "this host does not gate executable memory on an entitlement",
                  profile_.vm.protection.jit_entitlement_required.evidence());
        return;
    }

    Finding f = start(ids::kJitEntitlementRequired, Confidence::Proven,
                      SupportLevel::ConditionallySupported);
    f.required = "executable memory for generated code";
    f.host_capability =
        "executable memory is granted only to processes carrying the platform's "
        "JIT entitlement or policy opt-in";
    f.modeled_fallback =
        "without the entitlement the mapping fails, or succeeds without execute "
        "permission and faults on first execution";
    f.conclusion = "The capability exists but is gated by process policy rather "
                   "than by the mapping call.";
    add_application_claim(f, "program generates and executes code at runtime");
    f.evidence.add(Layer::OperatingSystem,
                   profile_.vm.protection.jit_entitlement_required.evidence(),
                   "the host requires a JIT entitlement for executable memory",
                   profile_.profile_name);
    f.remediations.push_back({RemediationClass::RequestPlatformEntitlement, ""});
    emit(std::move(f));
}

// ---------------------------------------------------------------------------
// RS-VM-0012: reserve/commit.
// ---------------------------------------------------------------------------
void Analysis::rule_reserve_commit() {
    if (!req_.request.reserve_then_commit) return;
    // The POSIX idiom - reserve with PROT_NONE, then map over it with
    // MAP_FIXED and CHECK THAT CALL - is not the Windows two-phase model, and
    // the warning below is false for it. QEMU is the second kind; this rule
    // downgraded four campaign contracts on a distinction it could not see.
    if (req_.request.commit_is_checked_call) return;
    if (!profile_.vm.reserve_commit_model.is_known()) return;
    if (profile_.vm.reserve_commit_model.value() !=
        ReserveCommitModel::PosixLazy) {
        return;
    }

    Finding f = start(ids::kReserveCommitSemanticMismatch, Confidence::Proven,
                      SupportLevel::ConditionallySupported);
    f.required = "reservation and commitment are distinct, observable states";
    f.host_capability =
        "this host reserves address space lazily; there is no separate commit "
        "step and no failure point at commit time";
    f.modeled_fallback =
        "the reservation appears to succeed for the full size and the shortage "
        "surfaces later, at first touch, as a fault or an out-of-memory kill "
        "rather than as a commit failure";
    f.conclusion =
        "The program's two-phase model maps onto a one-phase host. Failures "
        "move from a checked call site to an unchecked memory access.";
    add_application_claim(f, "program reserves address space and commits it later");
    f.evidence.add(Layer::OperatingSystem,
                   profile_.vm.reserve_commit_model.evidence(),
                   "reserve/commit model is posix_lazy", profile_.profile_name);
    f.remediations.push_back(
        {RemediationClass::ReserveAddressSpaceEarlier,
         "pre-fault or explicitly populate the range if the program needs the "
         "shortage to surface at a checkable point"});
    emit(std::move(f));
}

// ---------------------------------------------------------------------------
// RS-VM-0013: pointer truncation.
// ---------------------------------------------------------------------------
void Analysis::rule_pointer_truncation() {
    std::uint64_t width = 0;
    EvidenceClass evidence = EvidenceClass::Unknown;
    std::string source;

    if (req_.assumptions.pointer_storage_width_bits) {
        width = *req_.assumptions.pointer_storage_width_bits;
        evidence = req_.assumption_evidence;
        source = "requirement.assumptions.pointer_storage_width_bits";
    } else if (profile_.process_pointer_width() != 0) {
        width = profile_.process_pointer_width();
        evidence = EvidenceClass::SpecifiedGuarantee;
        source = "profile.platform.process_arch";
    } else {
        return;
    }
    if (width == 0 || width >= 64) return;

    const std::uint64_t limit =
        width >= 64 ? UINT64_MAX : (std::uint64_t{1} << width);

    // The highest address the request could involve.
    std::uint64_t highest = 0;
    if (const auto range = req_.request.range(); range) {
        highest = range->end;
    } else if (profile_.vm.max_user_address.is_known()) {
        highest = profile_.vm.max_user_address.value().value;
    } else {
        return;
    }
    if (highest <= limit) return;

    // A program that CHECKS the address it got back does not truncate: it
    // rejects and tries again. LuaJIT, Box64 and jemalloc all do exactly
    // this, and reporting silent truncation for them was not merely the wrong
    // severity, it was the wrong mechanism - the described failure cannot
    // occur in that code.
    const bool rejects_bad_addresses =
        req_.request.validates_returned_address ||
        req_.permits(FallbackKind::Relocate) ||
        req_.assumptions.retries_on_failure;

    Finding f = start(ids::kPointerTruncation, Confidence::Proven,
                      rejects_bad_addresses ? SupportLevel::ConditionallySupported
                                            : SupportLevel::Unsupported);
    f.structural_impossibility = !rejects_bad_addresses;
    f.required = "the returned address must round-trip through " + dec(width) +
                 "-bit storage";
    f.host_capability = "addresses in this request reach " + json::to_hex(highest);
    if (rejects_bad_addresses) {
        f.modeled_fallback =
            "the program checks the returned address and rejects one it cannot "
            "store" +
            std::string(req_.assumptions.max_retries
                            ? ", retrying up to " +
                                  dec(*req_.assumptions.max_retries) + " times"
                            : ", retrying") +
            ". The reachable failure is exhaustion of those attempts, not a "
            "truncated pointer.";
        f.conclusion =
            "The program cannot use most of this host's address space, so it "
            "depends on the allocator handing it a low address. That is a "
            "probabilistic dependency on address-space layout, not a "
            "guarantee: it degrades as the process fragments and it can fail "
            "outright under a different ASLR configuration.";
    } else {
        f.modeled_fallback =
            "the high bits are discarded on assignment and the truncated "
            "pointer refers to unrelated memory";
        f.conclusion =
            "The address does not fit the storage the program keeps it in.";
    }
    add_application_claim(f, "program stores the returned address in a " +
                                 dec(width) + "-bit slot");
    f.evidence.add(Layer::Application, evidence,
                   "pointer storage width is " + dec(width) + " bits", source);
    f.evidence.add(Layer::Analyzer, EvidenceClass::SpecifiedGuarantee,
                   json::to_hex(highest) + " >= 2^" + dec(width),
                   "analyzer: width arithmetic");
    f.remediations.push_back({RemediationClass::WidenPointerStorage, ""});
    f.remediations.push_back({RemediationClass::ChooseDifferentBaseAddress,
                              "keep every mapping below " + json::to_hex(limit)});
    if (rejects_bad_addresses) {
        f.rejected_fixes.push_back(
            {"Raise the retry count",
             "More attempts help only while low addresses remain free. They do "
             "not create any, and the loop gets slower exactly as the address "
             "space fills up."});
    }
    emit(std::move(f));
}

// ---------------------------------------------------------------------------
// RS-VM-0018: file-backed mapping extending past end of file.
//
// Mapping past EOF is legal everywhere; *touching* those bytes is where hosts
// diverge. The rule therefore fires on the access, not on the mapping.
// ---------------------------------------------------------------------------
void Analysis::rule_file_mapping_beyond_eof() {
    if (!req_.request.file_backed) return;
    if (!req_.request.file_length) return;

    const std::uint64_t file_length = *req_.request.file_length;
    const std::uint64_t offset = req_.request.file_offset;
    if (offset > UINT64_MAX - req_.request.size) return;  // caught elsewhere
    const std::uint64_t mapping_end = offset + req_.request.size;
    if (mapping_end <= file_length) return;

    const std::uint64_t overhang = mapping_end - file_length;

    if (!profile_.vm.file_map_beyond_eof.is_known()) {
        if (!options_.report_unknowns) {
            result_.overall = combine(result_.overall, SupportLevel::Unknown);
            return;
        }
        Finding f = start(ids::kRequiredFactUnknown, Confidence::Hypothesis,
                          SupportLevel::Unknown);
        f.required = "defined behavior for the " + dec(overhang) +
                     " bytes mapped past end of file";
        f.host_capability =
            "unknown: the profile carries no beyond-EOF behavior fact";
        f.conclusion = "Run rs-env-probe on the target host to establish how "
                       "this platform treats accesses past end of file.";
        add_application_claim(f, "program maps " + dec(overhang) +
                                     " bytes past end of file");
        f.evidence.add(Layer::OperatingSystem, EvidenceClass::Unknown,
                       "file_map_beyond_eof not present in profile",
                       profile_.profile_name);
        emit(std::move(f));
        return;
    }

    const BeyondEofBehavior behavior = profile_.vm.file_map_beyond_eof.value();

    if (!req_.request.accesses_beyond_eof) {
        // Mapped but never touched: no fault can occur.
        return;
    }

    // Zero-fill of the trailing partial page is universal and harmless; only
    // whole pages entirely past EOF are the problem.
    //
    // This used to say "without page size we cannot tell the two apart, so we
    // stay conservative and report" - and reporting was not conservative, it
    // was wrong. POSIX requires every conforming system to zero-fill the
    // partial page at the end of a mapped object, so a program reading only
    // between the end of the file and the end of its final page cannot fault
    // anywhere. Calling that UNSUPPORTED on a sigbus host is a false positive
    // on a portable program, and it fired on every host until
    // tests/groundtruth/ ran the actual read and looked.
    if (req_.request.eof_access_extent ==
        MappingRequest::EofAccessExtent::WithinFinalPartialPage) {
        // The claim is only meaningful if a partial page exists at all. A
        // page-aligned file has none, so a contract asserting this about one is
        // describing something that cannot happen, and gets the risky path
        // rather than a free pass.
        const std::uint64_t page = profile_.vm.page_size.value_or(0);
        const bool partial_page_exists = page > 0 && (file_length % page) != 0;
        if (partial_page_exists) {
            satisfied("reads only within the final partial page of the mapped "
                      "file",
                      "POSIX requires the partial page at the end of a mapped "
                      "object to be zero-filled, on this and every conforming "
                      "host",
                      EvidenceClass::SpecifiedGuarantee);
            return;
        }
    }

    Finding f = start(ids::kFileMappingBeyondEof, Confidence::Proven,
                      behavior == BeyondEofBehavior::ZeroFill
                          ? SupportLevel::ConditionallySupported
                          : SupportLevel::Unsupported);
    f.required = "readable memory for the " + dec(overhang) +
                 " bytes past end of file";
    switch (behavior) {
        case BeyondEofBehavior::Sigbus:
            f.host_capability =
                "accessing whole pages past end of file raises a bus fault";
            f.modeled_fallback =
                "the process takes SIGBUS at the access site, far from the "
                "mapping call that appeared to succeed";
            break;
        case BeyondEofBehavior::Error:
            f.host_capability =
                "the mapping call itself refuses to extend past end of file";
            f.modeled_fallback = "the mapping call fails";
            break;
        case BeyondEofBehavior::ZeroFill:
            f.host_capability = "bytes past end of file read as zero";
            f.modeled_fallback =
                "reads succeed and return zeros; writes may or may not extend "
                "the file. The program cannot distinguish a zero byte in the "
                "file from a byte past its end.";
            break;
        case BeyondEofBehavior::Unknown:
            break;
    }
    f.conclusion =
        "The mapping call succeeds while the access does not. This is the "
        "classic shape of a silent contract degradation: the success code "
        "describes the reservation, not the readability.";
    add_application_claim(f, "program accesses " + dec(overhang) +
                                 " bytes past end of file (file length " +
                                 dec(file_length) + ", mapping ends at " +
                                 dec(mapping_end) + ")");
    f.evidence.add(Layer::OperatingSystem,
                   profile_.vm.file_map_beyond_eof.evidence(),
                   "beyond-EOF behavior is " +
                       std::string(rs::vm::to_string(behavior)),
                   profile_.profile_name);
    f.remediations.push_back(
        {RemediationClass::SelectDifferentHostConfiguration,
         "extend the file to at least " + dec(mapping_end) +
             " bytes before mapping it"});
    f.rejected_fixes.push_back(
        {"Check the mapping call's return value more carefully",
         "The mapping call succeeds. There is nothing in its result to check."});
    emit(std::move(f));
}

// ---------------------------------------------------------------------------
// RS-VM-0015: retrying something that can never succeed.
//
// Runs last: it needs to know whether any other rule proved impossibility.
// ---------------------------------------------------------------------------
void Analysis::rule_retry_of_permanent_error() {
    if (!req_.assumptions.retries_on_failure) return;
    if (!established_impossibility()) return;

    Finding f = start(ids::kPermanentErrorRetried, Confidence::Proven,
                      SupportLevel::Unsupported);
    f.required = "the operation eventually succeeds after retrying";
    f.host_capability = "the failure is structural on this host";
    f.modeled_fallback =
        req_.assumptions.max_retries
            ? "the program exhausts " + dec(*req_.assumptions.max_retries) +
                  " attempts and then takes its failure path"
            : "the program retries without bound and never makes progress";
    f.conclusion =
        "A permanently impossible operation is being treated as a retryable "
        "error. This is error-code laundering: the platform's permanent "
        "refusal is reinterpreted as transient.";
    add_application_claim(f, "program retries the operation after failure");
    f.evidence.add(Layer::Analyzer, proven_unsupported_evidence(),
                   "another rule proved this request cannot succeed on this host",
                   "analyzer: cross-rule aggregation");
    f.remediations.push_back({RemediationClass::TreatErrorAsPermanent, ""});
    f.rejected_fixes.push_back(
        {"Increase the retry count or add a backoff",
         "Neither changes the outcome; the operation is impossible on this "
         "host, not contended."});
    emit(std::move(f));
}

AnalysisResult Analysis::run() {
    result_.profile_id = profile_.profile_id();
    result_.requirement_id = req_.requirement_id();
    result_.profile_name = profile_.profile_name;
    result_.requirement_name = req_.name;
    result_.profile_origin = profile_.origin;
    result_.overall = SupportLevel::Supported;

    // Origin is not an evidence class: it describes where the *document* came
    // from, while each fact carries how that fact is established. A profile
    // that did not come off a real host is flagged regardless of how confident
    // its individual facts claim to be.
    if (profile_.origin != ProfileOrigin::Measured) {
        result_.analyzer_limitations.push_back(
            "profile origin is '" +
            std::string(rs::vm::to_string(profile_.origin)) +
            "': these facts did not come from rs-env-probe running on a real "
            "host. Re-run the analysis against a measured profile before "
            "acting on this verdict.");
    }

    // Whatever the producer could not establish about the program is a limit
    // on this analysis too, and has to travel with the verdict.
    for (const auto& limitation : req_.extraction_limitations) {
        result_.analyzer_limitations.push_back("requirement extraction: " +
                                               limitation);
    }

    // A field the reader did not understand is a part of the document that had
    // no effect on this verdict, and the reader must say so. Misspell
    // `accesses_beyond_eof` and the requirement quietly reads false, the rule
    // that would have caught it never runs, and the report is a clean bill of
    // health for a claim nobody evaluated.
    for (const auto& field : req_.unrecognized_fields) {
        result_.analyzer_limitations.push_back(
            "unrecognized requirement field '" + field +
            "' was ignored; if it was meant to constrain this request, nothing "
            "here evaluated it");
    }

    if (req_.operation != OperationKind::VirtualMemoryMap &&
        req_.operation != OperationKind::VirtualMemoryReserve &&
        req_.operation != OperationKind::VirtualMemoryCommit &&
        req_.operation != OperationKind::VirtualMemoryProtect) {
        result_.analyzer_limitations.push_back(
            "operation is outside the virtual-memory domain modelled in v0.1");
        result_.overall = SupportLevel::Unknown;
        return result_;
    }

    rule_internal_fallback_contradiction();
    rule_baseline_mapping_capability();
    rule_size_feasibility();
    rule_address_bounds();
    rule_displacement_constraint();
    rule_reservation_alignment();
    rule_page_size();
    rule_address_alignment();
    rule_size_granularity();
    rule_range_availability();
    rule_exact_mapping_capability();
    rule_non_destructive_exact_mapping();
    rule_hinted_relocation();
    rule_identity_without_translation();
    rule_write_execute();
    rule_executable_mapping();
    rule_jit_entitlement();
    rule_reserve_commit();
    rule_pointer_truncation();
    rule_file_mapping_beyond_eof();
    rule_retry_of_permanent_error();

    // Deterministic output order: severity, then confidence, then id.
    std::stable_sort(result_.findings.begin(), result_.findings.end(),
                     [](const Finding& a, const Finding& b) {
                         if (a.severity != b.severity) {
                             return static_cast<int>(a.severity) <
                                    static_cast<int>(b.severity);
                         }
                         if (a.confidence != b.confidence) {
                             return static_cast<int>(a.confidence) <
                                    static_cast<int>(b.confidence);
                         }
                         return a.id < b.id;
                     });
    return result_;
}

}  // namespace

json::Value SatisfiedCheck::to_json() const {
    json::Value v = json::Value::object();
    v["constraint"] = constraint;
    v["host_fact"] = host_fact;
    v["evidence"] = std::string(rs::to_string(evidence));
    return v;
}

json::Value AnalysisResult::to_json() const {
    json::Value v = json::Value::object();
    v["schema"] = schema;
    v["overall"] = std::string(rs::to_string(overall));
    v["profile_id"] = profile_id;
    v["profile_name"] = profile_name;
    v["profile_origin"] = std::string(rs::vm::to_string(profile_origin));
    v["requirement_id"] = requirement_id;
    v["requirement_name"] = requirement_name;

    json::Value arr = json::Value::array();
    for (const auto& f : findings) arr.push_back(f.to_json());
    v["findings"] = arr;

    json::Value checks = json::Value::array();
    for (const auto& c : satisfied) checks.push_back(c.to_json());
    v["satisfied_checks"] = checks;

    json::Value limits = json::Value::array();
    for (const auto& l : analyzer_limitations) limits.push_back(json::Value(l));
    v["analyzer_limitations"] = limits;
    return v;
}

std::size_t AnalysisResult::count_at_least(Severity threshold) const {
    return static_cast<std::size_t>(
        std::count_if(findings.begin(), findings.end(), [&](const Finding& f) {
            return static_cast<int>(f.severity) <= static_cast<int>(threshold);
        }));
}

bool AnalysisResult::has_confidence(Confidence c) const {
    return std::any_of(findings.begin(), findings.end(),
                       [&](const Finding& f) { return f.confidence == c; });
}

AnalysisResult analyze(const Requirement& requirement,
                       const EnvironmentProfile& profile,
                       const AnalysisOptions& options) {
    Analysis analysis(requirement, profile, options);
    return analysis.run();
}

}  // namespace rs::vm

// SPDX-License-Identifier: Apache-2.0
//
// In-code fixtures. Building profiles and requirements programmatically keeps
// the rule tests independent of the on-disk schema, so a schema change breaks
// the parser tests (where it belongs) rather than every rule test at once.
#ifndef RUNTIMESKEPTIC_TESTS_FIXTURES_HPP
#define RUNTIMESKEPTIC_TESTS_FIXTURES_HPP

#include <string>

#include "runtimeskeptic/vm/analyzer.hpp"
#include "runtimeskeptic/vm/profile.hpp"
#include "runtimeskeptic/vm/requirement.hpp"

namespace rs::test {

// A fully-known, permissive host: 4 KiB pages, wide address space, W^X not
// enforced. Rules should stay silent on it unless the requirement is unusual.
inline vm::EnvironmentProfile permissive_host() {
    vm::EnvironmentProfile p;
    p.origin = vm::ProfileOrigin::Synthetic;
    p.profile_name = "test-permissive";
    p.platform.os = vm::OperatingSystem::Linux;
    p.platform.os_version = "test";
    p.platform.host_arch = vm::Architecture::X86_64;
    p.platform.process_arch = vm::Architecture::X86_64;
    p.platform.translation_mode = vm::TranslationMode::None;

    const auto measured = EvidenceClass::MeasuredCapability;
    p.vm.page_size = Fact<std::uint64_t>::known(4096, measured, "fixture");
    p.vm.allocation_granularity =
        Fact<std::uint64_t>::known(4096, measured, "fixture");
    p.vm.min_map_address =
        Fact<Address>::known(Address(0x10000), measured, "fixture");
    p.vm.max_user_address =
        Fact<Address>::known(Address(0x7ffffffff000ull), measured, "fixture");
    p.vm.anonymous_mapping_supported =
        Fact<bool>::known(true, measured, "fixture");
    p.vm.exact_mapping = Fact<SupportLevel>::known(
        SupportLevel::ConditionallySupported, measured, "fixture");
    p.vm.hinted_mapping_may_relocate =
        Fact<bool>::known(true, EvidenceClass::SpecifiedGuarantee, "fixture");
    p.vm.fixed_noreplace_available = Fact<bool>::known(true, measured, "fixture");
    p.vm.reserve_commit_model = Fact<vm::ReserveCommitModel>::known(
        vm::ReserveCommitModel::PosixLazy, measured, "fixture");
    p.vm.file_map_beyond_eof = Fact<vm::BeyondEofBehavior>::known(
        vm::BeyondEofBehavior::Sigbus, measured, "fixture");

    p.vm.protection.write_execute_simultaneous =
        Fact<bool>::known(true, measured, "fixture");
    p.vm.protection.write_then_execute_transition =
        Fact<bool>::known(true, measured, "fixture");
    p.vm.protection.anonymous_executable_mapping =
        Fact<bool>::known(true, measured, "fixture");
    p.vm.protection.jit_entitlement_required =
        Fact<bool>::known(false, measured, "fixture");

    vm::ClassifiedRange available;
    available.range = vm::AddressRange{0x1000000000ull, 0x1010000000ull};
    available.evidence = measured;
    available.note = "fixture: probed free";
    p.vm.available_ranges.push_back(available);
    return p;
}

// A host that reserves a large band, the shape of the ROADMAP's motivating
// incident.
inline vm::EnvironmentProfile host_with_reserved_band() {
    vm::EnvironmentProfile p = permissive_host();
    p.profile_name = "test-reserved-band";
    p.vm.available_ranges.clear();

    vm::ClassifiedRange reserved;
    reserved.range = vm::AddressRange{0x1000000000ull, 0x7000000000ull};
    reserved.evidence = EvidenceClass::MeasuredCapability;
    reserved.note = "reserved by the platform";
    p.vm.unavailable_ranges.push_back(reserved);
    return p;
}

// A host about which nothing was established.
inline vm::EnvironmentProfile unknown_host() {
    vm::EnvironmentProfile p;
    p.origin = vm::ProfileOrigin::Synthetic;
    p.profile_name = "test-unknown";
    p.platform.os = vm::OperatingSystem::Linux;
    p.platform.process_arch = vm::Architecture::X86_64;
    return p;
}

// The emulator requirement from the ROADMAP's motivating example.
inline vm::Requirement exact_mapping_requirement(std::uint64_t address =
                                                     0x1000000000ull) {
    vm::Requirement r;
    r.name = "guest memory base mapping";
    r.component = "emulator";
    r.operation = vm::OperationKind::VirtualMemoryMap;
    r.request.address = address;
    r.request.size = 16384;
    r.request.exact_address_required = true;
    r.request.protection.read = true;
    r.request.protection.write = true;
    r.assumptions.guest_host_identity_required = true;
    r.assumptions.translation_layer_available = false;
    r.required_postconditions.push_back("returned_address == requested_address");
    r.failure_sink.kind = vm::FailureSinkKind::FatalAssert;
    r.assumption_evidence = EvidenceClass::SpecifiedGuarantee;
    return r;
}

// A benign request that any permissive host satisfies.
inline vm::Requirement plain_anonymous_mapping() {
    vm::Requirement r;
    r.name = "plain anonymous mapping";
    r.operation = vm::OperationKind::VirtualMemoryMap;
    r.request.size = 4096;
    r.request.protection.read = true;
    r.request.protection.write = true;
    r.failure_sink.kind = vm::FailureSinkKind::ErrorReturn;
    r.assumption_evidence = EvidenceClass::SpecifiedGuarantee;
    return r;
}

inline bool has_finding(const vm::AnalysisResult& result, const char* id) {
    for (const auto& f : result.findings) {
        if (f.id == id) return true;
    }
    return false;
}

inline const vm::Finding* get_finding(const vm::AnalysisResult& result,
                                      const char* id) {
    for (const auto& f : result.findings) {
        if (f.id == id) return &f;
    }
    return nullptr;
}

}  // namespace rs::test

#endif  // RUNTIMESKEPTIC_TESTS_FIXTURES_HPP

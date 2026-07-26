// SPDX-License-Identifier: Apache-2.0
//
// Windows x64 / ARM64 virtual-memory probe.
//
// The third platform family the ROADMAP names, and the first whose memory
// model differs from the other two in kind rather than in detail. Three
// differences drive everything below.
//
// 1. ALLOCATION GRANULARITY IS NOT THE PAGE SIZE. The asymmetry itself IS
//    documented, and it is the RSC-0044 mechanism in Microsoft's own words:
//
//      "If the memory is being reserved, the specified address is rounded down
//       to the nearest multiple of the allocation granularity. If the memory is
//       already reserved and is being committed, the address is rounded down to
//       the next page boundary."          - VirtualAlloc, learn.microsoft.com
//
//    So a reservation is rounded to one unit and a commit to another, and the
//    gap between them is stranded - which is how a 4 KiB-page allocator lost up
//    to 60 KiB per allocation until a 32-bit browser ran out of address space.
//    On Linux and macOS the two numbers are equal and the distinction is
//    invisible. Here it is the whole point, which is why the profile carries
//    both.
//
//    WHAT IS *NOT* DOCUMENTED IS THE VALUE. Neither the `SYSTEM_INFO` page nor
//    the `VirtualAlloc` page states 64 KiB, or says the number is
//    architecture-dependent; both say only "use GetSystemInfo". 64 KiB is what
//    implementations return, not a guarantee. So `allocation_granularity` is
//    `measured_capability` and never `specified_guarantee`, and a host reporting
//    something else is a FINDING rather than a bug in this probe.
//
// 2. RESERVE AND COMMIT ARE SEPARATE OPERATIONS THE PROGRAM PERFORMS. Not a
//    lazy-commit heuristic the kernel applies behind an ordinary mapping.
//    `reserve_commit_model` has had the value `WindowsReserveCommit` since the
//    model was written and no probe has ever established it. `RS-VM-0012`
//    exists for the mismatch and has never been confirmed by execution against
//    a host that actually has this model. This probe is what would confirm it.
//
// 3. THERE IS NO DESTRUCTIVE EXACT PLACEMENT. `VirtualAlloc` with a base
//    address FAILS when the range is not free; it never unmaps what is there.
//    That is the inverse of POSIX, where `MAP_FIXED` clobbers silently and
//    `MAP_FIXED_NOREPLACE` had to be added later - and then reverted from the
//    ELF loader (RSC-0051, RSC-0052). So `fixed_noreplace_available` is TRUE
//    here by construction, and the probe records it as a specified guarantee
//    only after demonstrating it, because a fact this project has not measured
//    is a fact it does not have.
//
// THE T-013 RULE APPLIES HERE BEFORE IT IS TESTED, NOT AFTER.
//
// `VirtualQuery` walks the address space and reports, region by region, what
// is free and what is not. That is far better information than the Linux
// probe's sampling - and it is information about THIS PROCESS. Where our own
// image, heap and stacks landed is ASLR's choice, remade every run. Recording
// a free region because our libraries happened not to be there would make
// `profile_id` a function of our load address, which is exactly the mistake
// `min_map_address` made once and the arena scan nearly made again.
//
// So the walk is used for two things only:
//   - STRUCTURAL BOUNDS, which come from `GetSystemInfo` and are identical in
//     every process on the machine.
//   - COUNTS, which go to `notes` - outside the facts subtree, outside the
//     hash - so a human can see what the walk found without it becoming a fact.
//
// Nothing derived from where this process sits reaches `available_ranges`.
#include "runtimeskeptic/probe/vm_probe.hpp"

#if defined(RS_PLATFORM_WINDOWS)

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on

#include "runtimeskeptic/core/sha256.hpp"

namespace rs::probe {
namespace {

using rs::Address;
using vm::AddressRange;
using vm::ClassifiedRange;

constexpr const char* kSourceProbe = "rs-env-probe vm (windows)";

std::string last_error_name(DWORD code) {
    switch (code) {
        case ERROR_INVALID_ADDRESS:      return "ERROR_INVALID_ADDRESS";
        case ERROR_INVALID_PARAMETER:    return "ERROR_INVALID_PARAMETER";
        case ERROR_NOT_ENOUGH_MEMORY:    return "ERROR_NOT_ENOUGH_MEMORY";
        case ERROR_COMMITMENT_LIMIT:     return "ERROR_COMMITMENT_LIMIT";
        case ERROR_ACCESS_DENIED:        return "ERROR_ACCESS_DENIED";
        case ERROR_NOACCESS:             return "ERROR_NOACCESS";
        case ERROR_DYNAMIC_CODE_BLOCKED: return "ERROR_DYNAMIC_CODE_BLOCKED";
        default: return "error " + std::to_string(static_cast<unsigned long>(code));
    }
}

// A reservation that always releases itself, so no probe path can leak address
// space even on an early return.
class Reservation {
public:
    Reservation(void* base, SIZE_T size, DWORD type, DWORD protect)
        : size_(size) {
        SetLastError(0);
        address_ = ::VirtualAlloc(base, size, type, protect);
        error_ = address_ != nullptr ? 0 : ::GetLastError();
    }
    ~Reservation() { release(); }
    Reservation(const Reservation&) = delete;
    Reservation& operator=(const Reservation&) = delete;

    void release() {
        if (address_ != nullptr) {
            ::VirtualFree(address_, 0, MEM_RELEASE);
            address_ = nullptr;
        }
    }
    bool ok() const { return address_ != nullptr; }
    void* address() const { return address_; }
    std::uint64_t base() const {
        return reinterpret_cast<std::uint64_t>(address_);
    }
    DWORD error() const { return error_; }

private:
    void* address_ = nullptr;
    SIZE_T size_ = 0;
    DWORD error_ = 0;
};

std::string utc_timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm tm_buf{};
    ::gmtime_s(&tm_buf, &now);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return buffer;
}

vm::Architecture architecture_from_system_info(const SYSTEM_INFO& info) {
    switch (info.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: return vm::Architecture::X86_64;
        case PROCESSOR_ARCHITECTURE_ARM64: return vm::Architecture::Aarch64;
        case PROCESSOR_ARCHITECTURE_INTEL: return vm::Architecture::X86;
        case PROCESSOR_ARCHITECTURE_ARM:   return vm::Architecture::Arm;
        default: return vm::Architecture::Other;
    }
}

std::string os_version_string() {
    // `GetVersionEx` lies to unmanifested processes by design. `RtlGetVersion`
    // does not, and is the documented way to get the real numbers. Reading it
    // dynamically avoids linking ntdll import machinery into a tool that only
    // wants a version string; failure leaves the field empty rather than
    // guessing, per the probe's rule.
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) return {};
    auto fn = reinterpret_cast<RtlGetVersionFn>(
        reinterpret_cast<void*>(::GetProcAddress(ntdll, "RtlGetVersion")));
    if (fn == nullptr) return {};
    RTL_OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (fn(&info) != 0) return {};
    return std::to_string(info.dwMajorVersion) + "." +
           std::to_string(info.dwMinorVersion) + "." +
           std::to_string(info.dwBuildNumber);
}

// -------------------------------------------------------------------------
// Is this actually Windows?
//
// Wine implements the Win32 API on a POSIX kernel well enough that every probe
// below runs and returns plausible values - it reports `10.0.19043`, a 64 KiB
// allocation granularity and a working reserve/commit model, because that is
// what Win32 programs expect and Wine's job is to provide it. A profile
// produced there and labelled `windows` would be a measurement of Wine wearing
// the name of the platform it emulates, which is the exact confusion this
// project exists to prevent: `docs/PLAN.md` counts platform FAMILIES, and Wine
// on Linux is not a third one.
//
// `wine_get_version` in ntdll is Wine's own documented way of saying so, and
// it is absent on real Windows. Cheap, exact, and the difference between a
// profile that is honest about its host and one that is not.
// -------------------------------------------------------------------------
std::string wine_version() {
    HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) return {};
    using WineGetVersionFn = const char*(CDECL*)(void);
    auto fn = reinterpret_cast<WineGetVersionFn>(
        reinterpret_cast<void*>(::GetProcAddress(ntdll, "wine_get_version")));
    if (fn == nullptr) return {};
    const char* v = fn();
    return v == nullptr ? std::string("unknown") : std::string(v);
}

// -------------------------------------------------------------------------
// Is the process running under emulation? Windows on ARM runs x86-64 binaries
// through a translation layer, which is the same shape as Rosetta 2 and is
// worth recording for the same reason: two profiles from one machine can
// differ, and a contract judged against the wrong one is judged wrongly.
// -------------------------------------------------------------------------
vm::TranslationMode detect_translation(const SYSTEM_INFO& native,
                                       vm::Architecture process_arch) {
    USHORT process_machine = 0;
    USHORT native_machine = 0;
    using IsWow64Process2Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
    HMODULE kernel32 = ::GetModuleHandleW(L"kernel32.dll");
    auto fn = kernel32 == nullptr ? nullptr
        : reinterpret_cast<IsWow64Process2Fn>(reinterpret_cast<void*>(
              ::GetProcAddress(kernel32, "IsWow64Process2")));
    if (fn != nullptr &&
        fn(::GetCurrentProcess(), &process_machine, &native_machine)) {
        if (process_machine == IMAGE_FILE_MACHINE_UNKNOWN) {
            return vm::TranslationMode::None;   // running natively
        }
        return vm::TranslationMode::Wow64;
    }
    // No IsWow64Process2: fall back to comparing the architectures we have.
    // An honest Unknown is better than a guess when they agree for a reason
    // we cannot see.
    const vm::Architecture host = architecture_from_system_info(native);
    if (host == vm::Architecture::Unknown ||
        process_arch == vm::Architecture::Unknown) {
        return vm::TranslationMode::Unknown;
    }
    return host == process_arch ? vm::TranslationMode::None
                                : vm::TranslationMode::Wow64;
}

// -------------------------------------------------------------------------
// Is `lpMaximumApplicationAddress` inclusive?
//
// It matters by a page in every direction, and the documentation does not say.
// `SYSTEM_INFO` describes the field as "A pointer to the highest memory address
// accessible to applications and DLLs" - which reads inclusive in ordinary
// English and is nowhere stated to be. The memory-limits page gives the x64
// user-mode bound as "128 TB" and no hexadecimal at all.
//
// The model's `max_user_address` is an EXCLUSIVE bound, so the conversion needs
// an answer, and `+ 1` was written from that reading. A reading is not a
// measurement. `docs/domains/shadps4-case-study.md` already has a section on
// exactly this hazard - shadPS4's maxima are inclusive, RuntimeSkeptic's ranges
// are half-open, and getting it backwards silently moves every boundary.
//
// So ask the kernel. One page at the reported maximum, page-aligned down: if it
// can be reserved, that address is usable and the field is inclusive. The result
// is recorded either way, and when the experiment cannot be run the fact is left
// derived-by-reading rather than presented as measured.
// -------------------------------------------------------------------------
enum class MaxAddressKind { Inclusive, Exclusive, Undetermined };

MaxAddressKind probe_max_address_inclusivity(std::uint64_t reported_max,
                                             std::uint64_t page_size,
                                             std::vector<std::string>& warnings) {
    if (reported_max < page_size) return MaxAddressKind::Undetermined;
    const std::uint64_t page = reported_max & ~(page_size - 1);

    // VirtualQuery, not VirtualAlloc, and the first version of this used the
    // wrong one.
    //
    // A reservation attempt at the top page came back ERROR_INVALID_ADDRESS and
    // was read as "the field is exclusive". That inference does not hold:
    // Windows keeps a no-access guard region at the very top of user space, so
    // a refusal there is equally consistent with "in bounds and guarded". The
    // experiment had two explanations and picked one.
    //
    // VirtualQuery separates them. It reports on an address without touching
    // it, and it fails - returns 0 - only when the address is OUTSIDE the
    // process's virtual address space. So:
    //
    //   query succeeds  ->  the address exists for this process, whatever its
    //                       state; the field names a usable address and is
    //                       therefore INCLUSIVE
    //   query fails     ->  the address is not part of the address space; the
    //                       field is already an EXCLUSIVE bound
    //
    // A reservation is then attempted only as corroboration, and its failure is
    // reported rather than interpreted.
    MEMORY_BASIC_INFORMATION info{};
    const SIZE_T queried = ::VirtualQuery(reinterpret_cast<LPCVOID>(page), &info,
                                          sizeof(info));
    if (queried == 0) {
        warnings.push_back(
            "VirtualQuery(" + json::to_hex(page) + ") failed with " +
            last_error_name(::GetLastError()) +
            "; the page containing lpMaximumApplicationAddress is not part of "
            "this process's address space, so the field is an exclusive bound");
        return MaxAddressKind::Exclusive;
    }

    const char* state = info.State == MEM_FREE ? "MEM_FREE"
                      : info.State == MEM_RESERVE ? "MEM_RESERVE"
                      : info.State == MEM_COMMIT ? "MEM_COMMIT" : "unknown";
    Reservation at_max(reinterpret_cast<void*>(page),
                       static_cast<SIZE_T>(page_size),
                       MEM_RESERVE, PAGE_NOACCESS);
    const std::string corroboration =
        at_max.ok() && at_max.base() == page
            ? "and a one-page reservation there succeeded"
            : "and a one-page reservation there failed with " +
                  last_error_name(at_max.error()) +
                  " - consistent with the system's top-of-space guard region, "
                  "which is why the reservation is not what decides this";
    warnings.push_back(
        "lpMaximumApplicationAddress is INCLUSIVE: VirtualQuery(" +
        json::to_hex(page) + ") succeeded reporting " + state + " over " +
        std::to_string(static_cast<unsigned long long>(info.RegionSize)) +
        " bytes, so the address is part of this process's space, " +
        corroboration);
    return MaxAddressKind::Inclusive;
}

// -------------------------------------------------------------------------
// Exact placement.
//
// `VirtualAlloc(base, ...)` is already non-destructive: it fails rather than
// replacing. The probe demonstrates that rather than asserting it, by
// reserving a range and then asking for the same range again.
// -------------------------------------------------------------------------
struct ExactPlacement {
    bool attempted = false;
    bool exact_supported = false;
    bool non_destructive_demonstrated = false;
    bool hint_relocates = false;
    std::vector<std::string> failure_codes;
};

ExactPlacement probe_exact_placement(std::uint64_t granularity,
                                     std::uint64_t probe_length,
                                     std::vector<std::string>& warnings) {
    ExactPlacement out;
    out.attempted = true;

    // Let the OS choose a range, note where it put it, release it, then ask
    // for that exact address back. A range the OS just handed us is the
    // safest possible target: nothing else can be there.
    std::uint64_t candidate = 0;
    {
        Reservation scout(nullptr, static_cast<SIZE_T>(probe_length),
                          MEM_RESERVE, PAGE_NOACCESS);
        if (!scout.ok()) {
            warnings.push_back(
                "could not reserve a scout range while probing exact "
                "placement: " + last_error_name(scout.error()));
            return out;
        }
        candidate = scout.base();
    }
    if (candidate == 0 || candidate % granularity != 0) {
        warnings.push_back(
            "scout reservation was not allocation-granularity aligned; exact "
            "placement was left unestablished");
        return out;
    }

    Reservation exact(reinterpret_cast<void*>(candidate),
                      static_cast<SIZE_T>(probe_length), MEM_RESERVE,
                      PAGE_NOACCESS);
    if (!exact.ok()) {
        // The range was free a moment ago. A refusal here means exact
        // placement is unavailable or unreliable, which is a real finding.
        out.failure_codes.push_back(last_error_name(exact.error()));
        return out;
    }
    if (exact.base() != candidate) {
        // Windows should never do this - a base address is a requirement, not
        // a hint. Recording it rather than assuming it cannot happen.
        warnings.push_back(
            "VirtualAlloc with an explicit base returned a different address; "
            "exact placement was left unestablished");
        return out;
    }
    out.exact_supported = true;

    // Now the collision test, while the range is still held: asking again for
    // an occupied range must FAIL. If it succeeded, exact placement would be
    // destructive and `fixed_noreplace_available` would be false.
    {
        Reservation collide(reinterpret_cast<void*>(candidate),
                            static_cast<SIZE_T>(probe_length), MEM_RESERVE,
                            PAGE_NOACCESS);
        if (!collide.ok()) {
            out.non_destructive_demonstrated = true;
            out.failure_codes.push_back(last_error_name(collide.error()));
        } else {
            warnings.push_back(
                "a second reservation over an occupied range SUCCEEDED; exact "
                "placement on this host may be destructive");
        }
    }

    // A hint: on Windows there is no hint. Passing a base is a requirement.
    // The fact is recorded as false only because it was demonstrated above -
    // a request at an occupied address failed rather than relocating.
    out.hint_relocates = false;
    return out;
}

// -------------------------------------------------------------------------
// Reserve/commit. The fact the model has carried since it was written and no
// probe has ever established.
// -------------------------------------------------------------------------
bool probe_reserve_then_commit(std::uint64_t page_size,
                               std::uint64_t probe_length,
                               std::vector<std::string>& warnings) {
    Reservation reserved(nullptr, static_cast<SIZE_T>(probe_length),
                         MEM_RESERVE, PAGE_NOACCESS);
    if (!reserved.ok()) {
        warnings.push_back("reserve/commit probe could not reserve: " +
                           last_error_name(reserved.error()));
        return false;
    }

    // A reservation alone must not be writable. If it were, the model would
    // not be reserve-then-commit at all.
    void* committed = ::VirtualAlloc(reserved.address(),
                                     static_cast<SIZE_T>(page_size),
                                     MEM_COMMIT, PAGE_READWRITE);
    if (committed == nullptr) {
        warnings.push_back("reserve succeeded but commit failed: " +
                           last_error_name(::GetLastError()));
        return false;
    }
    // Touch it, so "committed" means what it says.
    volatile char* p = static_cast<volatile char*>(committed);
    p[0] = 1;
    const bool ok = p[0] == 1;
    ::VirtualFree(committed, static_cast<SIZE_T>(page_size), MEM_DECOMMIT);
    return ok;
}

// -------------------------------------------------------------------------
// Protection. Windows permits PAGE_EXECUTE_READWRITE unless the process opts
// into Arbitrary Code Guard, so the interesting answer is which of these the
// runner actually allows - and ACG is exactly the kind of policy that makes a
// specified guarantee wrong on a particular host.
// -------------------------------------------------------------------------
struct ProtectionOutcome {
    bool rwx_attempted = false;
    bool rwx_allowed = false;
    bool rw_then_rx_attempted = false;
    bool rw_then_rx_allowed = false;
    bool anon_exec_attempted = false;
    bool anon_exec_allowed = false;
    std::vector<std::string> codes;
};

ProtectionOutcome probe_protection(std::uint64_t page_size,
                                   std::vector<std::string>& warnings) {
    ProtectionOutcome out;
    const SIZE_T len = static_cast<SIZE_T>(page_size);

    {
        out.rwx_attempted = true;
        Reservation rwx(nullptr, len, MEM_RESERVE | MEM_COMMIT,
                        PAGE_EXECUTE_READWRITE);
        out.rwx_allowed = rwx.ok();
        if (!rwx.ok()) out.codes.push_back(last_error_name(rwx.error()));
    }
    {
        out.anon_exec_attempted = true;
        Reservation rx(nullptr, len, MEM_RESERVE | MEM_COMMIT,
                       PAGE_EXECUTE_READ);
        out.anon_exec_allowed = rx.ok();
        if (!rx.ok()) out.codes.push_back(last_error_name(rx.error()));
    }
    {
        out.rw_then_rx_attempted = true;
        Reservation rw(nullptr, len, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!rw.ok()) {
            warnings.push_back("could not commit RW memory for the W->X "
                               "transition probe: " +
                               last_error_name(rw.error()));
            out.rw_then_rx_attempted = false;
        } else {
            DWORD old = 0;
            const BOOL changed =
                ::VirtualProtect(rw.address(), len, PAGE_EXECUTE_READ, &old);
            out.rw_then_rx_allowed = changed != FALSE;
            if (changed == FALSE) {
                out.codes.push_back(last_error_name(::GetLastError()));
            }
        }
    }
    return out;
}

// -------------------------------------------------------------------------
// The address-space walk. Counts only - see the header comment.
// -------------------------------------------------------------------------
struct WalkSummary {
    std::size_t free_regions = 0;
    std::size_t reserved_regions = 0;
    std::size_t committed_regions = 0;
    std::uint64_t largest_free_bytes = 0;
    bool completed = false;
};

WalkSummary walk_address_space(std::uint64_t min_address,
                               std::uint64_t max_address) {
    WalkSummary out;
    std::uint64_t address = min_address;
    // A bound on iterations so a pathological layout cannot hang the probe.
    for (unsigned steps = 0; steps < 1000000 && address < max_address; ++steps) {
        MEMORY_BASIC_INFORMATION info{};
        const SIZE_T n = ::VirtualQuery(reinterpret_cast<LPCVOID>(address),
                                        &info, sizeof(info));
        if (n == 0) break;
        switch (info.State) {
            case MEM_FREE:
                ++out.free_regions;
                out.largest_free_bytes = std::max<std::uint64_t>(
                    out.largest_free_bytes, info.RegionSize);
                break;
            case MEM_RESERVE: ++out.reserved_regions; break;
            case MEM_COMMIT:  ++out.committed_regions; break;
            default: break;
        }
        const std::uint64_t next =
            reinterpret_cast<std::uint64_t>(info.BaseAddress) + info.RegionSize;
        if (next <= address) break;   // no forward progress; stop rather than spin
        address = next;
        out.completed = address >= max_address;
    }
    return out;
}

}  // namespace

std::string probe_platform_name() { return "windows"; }

Result probe_virtual_memory(const Options& options) {
    Result result;
    result.implemented = true;

    vm::EnvironmentProfile& profile = result.profile;
    profile.origin = vm::ProfileOrigin::Measured;
    profile.run.probe_version = kProbeVersion;
    profile.run.timestamp_utc = utc_timestamp();

    std::vector<std::string> warnings;
    const auto started = ::GetTickCount64();

    SYSTEM_INFO info{};
    ::GetSystemInfo(&info);
    SYSTEM_INFO native{};
    ::GetNativeSystemInfo(&native);

    profile.platform.os = vm::OperatingSystem::Windows;
    profile.platform.os_version = os_version_string();
    profile.platform.kernel_version = profile.platform.os_version;
    profile.platform.host_arch = architecture_from_system_info(native);
    profile.platform.process_arch = architecture_from_system_info(info);
    profile.platform.translation_mode =
        detect_translation(native, profile.platform.process_arch);

    // Say so, in the facts, before anything else is recorded. A Wine profile
    // that reads as a Windows profile would be counted as the third platform
    // family and it is not one.
    const std::string wine = wine_version();
    if (!wine.empty()) {
        profile.platform.os_version += " (Wine " + wine + ")";
        profile.platform.kernel_version = profile.platform.os_version;
        profile.platform.translation_mode = vm::TranslationMode::Other;
        profile.notes.push_back(
            "THIS IS NOT WINDOWS. ntdll exports wine_get_version, so these "
            "facts were measured through Wine " + wine + " on a POSIX kernel. "
            "Wine reproduces the Win32 memory model faithfully enough that "
            "every probe below succeeds, which is precisely why the profile "
            "must say where it came from: it does not count as the third "
            "platform family, and a contract judged against it has been judged "
            "against an emulation of the platform it names.");
    }

    const auto measured = EvidenceClass::MeasuredCapability;
    const std::uint64_t page_size = info.dwPageSize;
    const std::uint64_t granularity = info.dwAllocationGranularity;

    // THE FACT THAT MAKES THIS PLATFORM DIFFERENT. On Linux and macOS these
    // two are equal; here they are not, and every rule that reasons about
    // alignment has to know which one it means.
    profile.vm.page_size = Fact<std::uint64_t>::known(
        page_size, measured,
        std::string(kSourceProbe) + ": GetSystemInfo().dwPageSize");
    profile.vm.allocation_granularity = Fact<std::uint64_t>::known(
        granularity, measured,
        std::string(kSourceProbe) + ": GetSystemInfo().dwAllocationGranularity");

    // Structural bounds, identical in every process on the machine. Not
    // derived from where this process sits - which is the whole T-013 rule.
    const std::uint64_t min_address =
        reinterpret_cast<std::uint64_t>(info.lpMinimumApplicationAddress);
    const std::uint64_t max_address =
        reinterpret_cast<std::uint64_t>(info.lpMaximumApplicationAddress);
    profile.vm.min_map_address = Fact<Address>::known(
        Address(min_address), measured,
        std::string(kSourceProbe) + ": GetSystemInfo().lpMinimumApplicationAddress");
    if (max_address > min_address) {
        // The model's bound is exclusive; Win32's field is documented only as
        // "the highest memory address accessible", with no statement either
        // way. So the conversion is MEASURED rather than read off the doc.
        const MaxAddressKind kind =
            probe_max_address_inclusivity(max_address, page_size, warnings);
        switch (kind) {
            case MaxAddressKind::Inclusive:
                profile.vm.max_user_address = Fact<Address>::known(
                    Address(max_address + 1), measured,
                    std::string(kSourceProbe) +
                        ": lpMaximumApplicationAddress + 1; VirtualQuery at "
                        "the reported maximum succeeded, so that address is "
                        "part of the process's address space and the field is "
                        "inclusive. See probe_run.warnings for what the "
                        "reservation attempt did and why it is not what "
                        "decided this");
                break;
            case MaxAddressKind::Exclusive:
                profile.vm.max_user_address = Fact<Address>::known(
                    Address(max_address), measured,
                    std::string(kSourceProbe) +
                        ": lpMaximumApplicationAddress used unchanged; "
                        "VirtualQuery at the reported maximum FAILED, so that "
                        "address is not part of the process's address space "
                        "and the field is already an exclusive bound");
                break;
            case MaxAddressKind::Undetermined:
                // Deliberately unknown. A one-page difference at the top of the
                // address space decides RS-VM-0003 for anything near it, and
                // guessing is what this project exists not to do.
                profile.notes.push_back(
                    "max_user_address was left UNKNOWN: "
                    "lpMaximumApplicationAddress reported " +
                    json::to_hex(max_address) +
                    " but whether it is inclusive could not be established by "
                    "experiment, and the Win32 documentation does not say. "
                    "Recording either value would be a guess about a boundary "
                    "that decides RS-VM-0003.");
                break;
        }
    }

    const std::uint64_t probe_length =
        std::max<std::uint64_t>(granularity,
                                std::min<std::uint64_t>(
                                    options.max_test_mapping_bytes, 4u << 20));

    // Ordinary anonymous mapping. Looks trivial; recorded because a profile
    // that asserts nothing lets a don't-care request come out SUPPORTED.
    {
        Reservation anon(nullptr, static_cast<SIZE_T>(page_size),
                         MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        profile.vm.anonymous_mapping_supported = Fact<bool>::known(
            anon.ok(), measured,
            std::string(kSourceProbe) + ": VirtualAlloc(MEM_RESERVE|MEM_COMMIT)");
        if (!anon.ok()) {
            warnings.push_back("a plain anonymous commit failed: " +
                               last_error_name(anon.error()));
        }
    }

    // Reserve then commit. The model's `WindowsReserveCommit` value has never
    // been established by a probe on any host; this is the one that can.
    if (probe_reserve_then_commit(page_size, probe_length, warnings)) {
        profile.vm.reserve_commit_model = Fact<vm::ReserveCommitModel>::known(
            vm::ReserveCommitModel::WindowsReserveCommit, measured,
            std::string(kSourceProbe) +
                ": MEM_RESERVE then MEM_COMMIT then write, both observed");
    } else {
        warnings.push_back(
            "reserve-then-commit could not be demonstrated; the model was left "
            "unknown rather than assumed from the platform name");
    }

    if (options.scan_address_space) {
        const ExactPlacement placement =
            probe_exact_placement(granularity, probe_length, warnings);
        if (placement.attempted && placement.exact_supported) {
            profile.vm.exact_mapping = Fact<SupportLevel>::known(
                SupportLevel::ConditionallySupported, measured,
                std::string(kSourceProbe) +
                    ": VirtualAlloc at an explicit base succeeded on a free "
                    "range and failed on an occupied one");
            profile.vm.hinted_mapping_may_relocate = Fact<bool>::known(
                false, measured,
                std::string(kSourceProbe) +
                    ": a base address is a requirement on this platform, not a "
                    "hint; a request at an occupied range was refused rather "
                    "than relocated");
        }
        if (placement.non_destructive_demonstrated) {
            profile.vm.fixed_noreplace_available = Fact<bool>::known(
                true, measured,
                std::string(kSourceProbe) +
                    ": a second VirtualAlloc over an occupied range was refused, "
                    "so exact placement here cannot clobber an existing "
                    "reservation");
        }
        for (const auto& code : placement.failure_codes) {
            profile.vm.exact_mapping_failure_codes.push_back(code);
        }

        const WalkSummary walk = walk_address_space(min_address, max_address);
        // Counts only, and into notes: everything this walk saw about what is
        // occupied is a fact about THIS process, not about the host.
        profile.notes.push_back(
            "VirtualQuery walk: " + std::to_string(walk.free_regions) +
            " free, " + std::to_string(walk.reserved_regions) + " reserved, " +
            std::to_string(walk.committed_regions) + " committed region(s); "
            "largest free run " + std::to_string(walk.largest_free_bytes) +
            " bytes" + (walk.completed ? "" : " (walk did not reach the top)") +
            ". Recorded as a note and NOT as available_ranges: which regions "
            "are free is a property of this process's ASLR layout, and a "
            "profile that hashed it would differ between two runs on one "
            "machine.");
    }

    const ProtectionOutcome protection = probe_protection(page_size, warnings);
    if (protection.rwx_attempted) {
        profile.vm.protection.write_execute_simultaneous = Fact<bool>::known(
            protection.rwx_allowed, measured,
            std::string(kSourceProbe) + ": VirtualAlloc(PAGE_EXECUTE_READWRITE)");
    }
    if (protection.anon_exec_attempted) {
        profile.vm.protection.anonymous_executable_mapping = Fact<bool>::known(
            protection.anon_exec_allowed, measured,
            std::string(kSourceProbe) + ": VirtualAlloc(PAGE_EXECUTE_READ)");
    }
    if (protection.rw_then_rx_attempted) {
        profile.vm.protection.write_then_execute_transition = Fact<bool>::known(
            protection.rw_then_rx_allowed, measured,
            std::string(kSourceProbe) +
                ": VirtualProtect(PAGE_READWRITE -> PAGE_EXECUTE_READ)");
    }
    // Windows has no entitlement gate equivalent to macOS's MAP_JIT. Arbitrary
    // Code Guard is the nearest analogue and it is a per-process opt-in, so a
    // probe that is not running under it cannot speak for one that is. Left
    // unknown deliberately: "we are not gated" is not "nothing is gated".
    for (const auto& code : protection.codes) {
        profile.vm.exact_mapping_failure_codes.push_back(code);
    }

    // File mapping past end of file. Windows sizes the section object at
    // CreateFileMapping, so a view beyond the file is refused at creation
    // rather than faulting on access - a third value distinct from both
    // SIGBUS and zero-fill, and the reason BeyondEofBehavior has an `Error`
    // case at all.
    if (options.run_faulting_tests) {
        wchar_t temp_dir[MAX_PATH + 1] = {};
        wchar_t temp_file[MAX_PATH + 1] = {};
        if (::GetTempPathW(MAX_PATH, temp_dir) != 0 &&
            ::GetTempFileNameW(temp_dir, L"rsp", 0, temp_file) != 0) {
            HANDLE file = ::CreateFileW(
                temp_file, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
                nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                // One byte of file, then ask for a whole page of view.
                const char byte = 'x';
                DWORD written = 0;
                ::WriteFile(file, &byte, 1, &written, nullptr);
                ::FlushFileBuffers(file);

                HANDLE section = ::CreateFileMappingW(
                    file, nullptr, PAGE_READONLY, 0,
                    static_cast<DWORD>(page_size), nullptr);
                if (section == nullptr) {
                    profile.vm.file_map_beyond_eof =
                        Fact<vm::BeyondEofBehavior>::known(
                            vm::BeyondEofBehavior::Error, measured,
                            std::string(kSourceProbe) +
                                ": CreateFileMapping sized past end of file was "
                                "refused with " +
                                last_error_name(::GetLastError()));
                } else {
                    // The section grew the file to its own size, which is
                    // Windows behaviour and means nothing is "past the end"
                    // any more. Recording what happened rather than forcing
                    // the observation into one of the POSIX answers.
                    LARGE_INTEGER size{};
                    const bool got = ::GetFileSizeEx(file, &size) != FALSE;
                    warnings.push_back(
                        "CreateFileMapping sized past end of file SUCCEEDED and "
                        "the file is now " +
                        (got ? std::to_string(size.QuadPart) : std::string("?")) +
                        " bytes; on this platform a section extends the file "
                        "rather than leaving a region past the end, so "
                        "file_map_beyond_eof was left unknown");
                    ::CloseHandle(section);
                }
                ::CloseHandle(file);
            }
        }
    }

    profile.run.duration_ms = ::GetTickCount64() - started;
    for (auto& w : warnings) profile.run.warnings.push_back(std::move(w));
    profile.profile_name =
        (wine.empty() ? "windows-" : "wine-on-posix-") +
        std::string(profile.platform.process_arch == vm::Architecture::Aarch64
                        ? "arm64" : "x86_64");
    return result;
}

}  // namespace rs::probe

#endif  // RS_PLATFORM_WINDOWS

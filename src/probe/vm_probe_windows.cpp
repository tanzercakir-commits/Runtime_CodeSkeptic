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
//    `reserve_commit_model` is measured here as
//    `WindowsReserveCommit`. The bounded Windows Job Object CTest confirms
//    reservation succeeds while commitment can fail synchronously; the
//    paired Linux cgroup lane confirms POSIX-lazy failure moves to first
//    touch. Together they execute the `RS-VM-0012` mismatch.
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
#include <limits>
#include <string>
#include <utility>
#include <vector>

// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on

#include "runtimeskeptic/core/sha256.hpp"
#include "runtimeskeptic/probe/arena_walk.hpp"
#include "runtimeskeptic/probe/windows_regions.hpp"

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
// The largest single reservation the kernel actually grants, as a power of two.
//
// The third platform to answer this. `RS-VM-0021` compared a request against the
// width of the address space and, when it fitted, said nothing more; a
// 5-level-paging Linux runner refused a 4 PiB reservation with ENOMEM while the
// analyzer said SUPPORTED. Fitting is necessary, not sufficient, and without this
// fact every analysis of a request above 4 GiB answers UNKNOWN via `RS-VM-0027`.
//
// A POWER OF TWO on purpose: the exact figure moves between two runs as the
// process's own mappings shift, and a fact that moves is a fact about the probe -
// `check_reproducible.sh` would fail and `profile_id` would stop naming the host.
//
// `MEM_RESERVE | PAGE_NOACCESS` is the Win32 spelling of the question the other
// two probes ask with `PROT_NONE | MAP_NORESERVE`: address space, not commit. On
// Windows that distinction is not a flag but the whole memory model - a reserve
// costs no pagefile, and it is the commit that can fail for accounting reasons.
// So this measures a genuinely narrower thing here than on POSIX, and the note
// says which.
std::uint64_t find_max_single_reservation() {
    std::uint64_t largest = 0;
    for (unsigned bit = 20; bit < 63; ++bit) {   // from 1 MiB
        const std::uint64_t size = std::uint64_t{1} << bit;
        if (size > static_cast<std::uint64_t>(
                       std::numeric_limits<SIZE_T>::max())) {
            break;
        }
        Reservation r(nullptr, static_cast<SIZE_T>(size), MEM_RESERVE,
                      PAGE_NOACCESS);
        if (r.ok()) largest = size;
    }
    return largest;
}

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
// WHY THIS ALSO REPORTS WHERE, AND WHY THAT IS SAFE.
//
// The counts alone answered the question "did the walk work". They do not answer
// the question the Windows arena needs answered: 128 TiB is far too wide to walk
// contiguously, so an arena must choose REGIONS, and choosing them from what this
// author remembers about Windows ASLR is the guess that the macOS arena's five
// wrong versions were all made of. The macOS bounds were settled by a runner
// printing three addresses; these are the same three, for Windows.
//
// Occupancy per 1 TiB bucket IS a fact about this process and would poison
// `profile_id` - so it goes into `notes`, which is outside the facts subtree and
// outside the hash, exactly like the counts already there. The rule this project
// keeps relearning is not "record less", it is that the RECORDED SET must not
// move with the probe's own layout. A note is not the recorded set.
struct WalkSummary {
    std::size_t free_regions = 0;
    std::size_t reserved_regions = 0;
    std::size_t committed_regions = 0;
    std::uint64_t largest_free_bytes = 0;
    bool completed = false;

    // Note-only, all of it. See above.
    std::uint64_t lowest_occupied = 0;
    std::uint64_t highest_occupied_end = 0;
    std::uint64_t occupied_bytes = 0;
    // Bucket index -> occupied bytes, one bucket per TiB, non-empty only.
    std::vector<std::pair<std::uint64_t, std::uint64_t>> occupied_by_tib;
};

constexpr std::uint64_t kWinTiB = 1ull << 40;

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
        const std::uint64_t base =
            reinterpret_cast<std::uint64_t>(info.BaseAddress);
        const std::uint64_t size = static_cast<std::uint64_t>(info.RegionSize);
        switch (info.State) {
            case MEM_FREE:
                ++out.free_regions;
                out.largest_free_bytes =
                    std::max<std::uint64_t>(out.largest_free_bytes, size);
                break;
            case MEM_RESERVE:
            case MEM_COMMIT: {
                if (info.State == MEM_RESERVE) {
                    ++out.reserved_regions;
                } else {
                    ++out.committed_regions;
                }
                if (out.lowest_occupied == 0) out.lowest_occupied = base;
                out.highest_occupied_end =
                    std::max(out.highest_occupied_end, base + size);
                out.occupied_bytes += size;
                // A region may span buckets; attribute each part to its own.
                for (std::uint64_t at = base; at < base + size;) {
                    const std::uint64_t bucket = at / kWinTiB;
                    const std::uint64_t bucket_end =
                        std::min((bucket + 1) * kWinTiB, base + size);
                    const std::uint64_t part = bucket_end - at;
                    if (!out.occupied_by_tib.empty() &&
                        out.occupied_by_tib.back().first == bucket) {
                        out.occupied_by_tib.back().second += part;
                    } else {
                        out.occupied_by_tib.emplace_back(bucket, part);
                    }
                    at = bucket_end;
                }
                break;
            }
            default: break;
        }
        const std::uint64_t next = base + size;
        if (next <= address) break;   // no forward progress; stop rather than spin
        address = next;
        out.completed = address >= max_address;
    }
    return out;
}

// -------------------------------------------------------------------------
// The allocation arena, third and last of three, and the only one whose bounds
// were measured before they were written.
//
// WHERE IT IS, AND WHY THAT IS NOT A GUESS.
//
// `a8bc15f` published a note from a Windows Server 2025 runner saying where the
// process actually sat, in 1 TiB buckets across the 128 TiB user space:
//
//   occupied [0x7ffe0000, 0x7ff9fa967000), 4349464576 bytes in 3 bucket(s):
//     0x0            =       6299648      <- lowest is KUSER_SHARED_DATA
//     0x10000000000  =       2633728
//     0x7f0000000000 =    4340531200      <- image, DLLs, stacks, heaps: 99.8%
//   largest free run 139217018867712 bytes  (126.6 TiB, contiguous)
//
// So high-entropy ASLR puts essentially everything in the TOP TiB, and the arena
// is that TiB: `[max_user_address rounded down to a TiB, max_user_address)`.
// Derived from `lpMaximumApplicationAddress`, a system constant identical in
// every process on the machine - NOT from where this process landed. The note
// chose the shape; it does not supply a bound.
//
// The macOS arena needed five wrong versions and six runner round trips to reach
// bounds that were, in the end, two constants. This one is written after the
// measurement instead of before it, which is the entire difference.
//
// WHY THE PLATFORM BAND CAVEAT IS SATISFIED. `arena_walk.hpp` treats a refusal
// covered by a region of this task as HELD, which is sound only while no
// system-wide band lies inside the arena. On Windows there is exactly one
// obvious candidate - KUSER_SHARED_DATA, mapped read-only into every x64
// process - and the runner put its number in the note: `0x7ffe0000`, in bucket
// 0, some 127 TiB BELOW this arena's floor. It is outside by measurement rather
// than by assumption, which is more than the macOS arena can say about its
// commpage.
//
// WHY 64 MiB WINDOWS. 1 TiB in 64 MiB windows is 16,384 placements, within a
// rounding error of the macOS arena's 15,360 - a cost this project has already
// paid and measured. 4 MiB windows would be 262,144. The window is a multiple of
// the 64 KiB allocation granularity, which is what a base address must be
// aligned to here; `dwPageSize` is 4096 and is the wrong quantum for placement,
// which is RSC-0044's entire subject.
constexpr std::uint64_t kArenaSpan = kWinTiB;
constexpr std::uint64_t kArenaWindow = 64ull << 20;

// TWO ARENAS, NOT ONE, AND THE RUNNER IS WHY.
//
// The version above shipped with one arena because the occupancy note said 99.8%
// of the process was in the top TiB. It ran, and it worked - 16312 placed, 1 held
// at the base, 6 held elsewhere in the window, **0 structurally refused** - and
// the same push failed the coverage test anyway:
//
//   heap page      : 0x2f78000e000                      <- 2.97 TiB
//   nearest above  : [0x7f0000000000, 0x7ffffc000000)   <- gap 0x7c087fff2000
//
// The occupancy note and the coverage test disagree because they are DIFFERENT
// PROCESSES: `rs-env-probe` had its heap in the 1 TiB bucket, `test_probe` had
// its at 2.97 TiB. So on Windows the image and every DLL go to the top TiB while
// an NT heap goes low, and where exactly is redrawn per process. That is the
// Linux shape - a PIE's text four TiB from the kernel's mmap base - not the macOS
// one, and it takes the Linux answer: a second arena.
//
// WHY IT STARTS AT 1 TiB AND NOT AT `lpMinimumApplicationAddress`. Bucket 0 holds
// KUSER_SHARED_DATA at `0x7ffe0000`, mapped into every x64 process - a genuine
// system-wide band. `arena_walk.hpp` treats a covered refusal as held, which is
// sound only while no such band lies inside the arena, so bucket 0 is excluded by
// construction and the ladder probes those addresses individually instead. That
// leaves `[min_map_address, 1 TiB)` uncovered, and it is a stated gap rather than
// an oversight.
//
// WHY 64 GiB WINDOWS AND NOT SAMPLING. 126 TiB in 64 MiB windows is two million
// placements. Linux samples its arenas at a 64 GiB stride and therefore ASSERTS
// THE SPACE BETWEEN SAMPLES, which `probe/arena_walk.hpp` argues against and
// Linux accepts because it has no choice. Windows has a choice:
// `max_single_reservation` measured 70368744177664 (64 TiB) on this host and
// `MEM_RESERVE | PAGE_NOACCESS` never charges the commit limit, so a 64 GiB
// window costs what a 64 MiB one costs. 2016 CONTIGUOUS windows assert only what
// was placed.
//
// THE COST, STATED: resolution. A structural hole smaller than 64 GiB makes the
// whole window unavailable, which over-claims - and over-claiming `unavailable`
// makes the analyzer answer UNSUPPORTED for addresses that are fine. The top
// arena measured ZERO structural refusals, so this has never yet happened, and
// `refused` in the note is the number that says it has. Re-walking a refused
// window at a finer size is the fix when it fires; building it now would be
// fitting code to a case no measurement has produced.
constexpr std::uint64_t kLowArenaBottom = kWinTiB;
constexpr std::uint64_t kLowArenaWindow = 64ull << 30;

std::string region_state_name(DWORD state) {
    switch (state) {
        case MEM_FREE:    return "MEM_FREE";
        case MEM_RESERVE: return "MEM_RESERVE";
        case MEM_COMMIT:  return "MEM_COMMIT";
        default: return "state " + std::to_string(static_cast<unsigned long>(state));
    }
}

// `VirtualQuery` reduced to what `probe/windows_regions.hpp` asks for. Everything
// downstream of this lambda is platform-neutral and tested on every platform this
// project builds on - see that header for why the line is drawn exactly here.
bool query_region(std::uint64_t address, MemRegion& out) {
    MEMORY_BASIC_INFORMATION info{};
    if (::VirtualQuery(reinterpret_cast<LPCVOID>(address), &info,
                       sizeof(info)) == 0) {
        return false;
    }
    out.base = reinterpret_cast<std::uint64_t>(info.BaseAddress);
    out.size = static_cast<std::uint64_t>(info.RegionSize);
    out.free = info.State == MEM_FREE;
    out.state = region_state_name(info.State);
    return true;
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

    if (const std::uint64_t biggest = find_max_single_reservation();
        biggest != 0) {
        profile.vm.max_single_reservation = Fact<std::uint64_t>::known(
            biggest, measured,
            std::string(kSourceProbe) +
                ": largest power-of-two MEM_RESERVE|PAGE_NOACCESS reservation "
                "granted. This is a bound on RESERVATION, not on commit - on "
                "Windows those are separate operations and it is the commit that "
                "charges the pagefile, so a request within this bound can still "
                "fail at commit time (RS-VM-0012 is the finding for that model "
                "difference)");
    } else {
        warnings.emplace_back(
            "no power-of-two reservation from 1 MiB upward was granted, so "
            "max_single_reservation was left unknown rather than recorded as "
            "zero");
    }

    // `max_single_reservation_hinted` IS LEFT UNKNOWN HERE, ON PURPOSE.
    //
    // On Linux and macOS the second measurement asks the same question with an
    // advisory hint, because `nullptr` is not a neutral place to ask: Linux opens
    // the full address space only for a hint above DEFAULT_MAP_WINDOW, so the
    // hintless figure is the narrower claim on an LA57 host.
    //
    // Windows has no such question to ask. A base address passed to
    // `VirtualAlloc` is a REQUIREMENT, not a hint - the call fails when the range
    // is not free rather than relocating, which is the third of the three
    // differences this file opens with. There is no "ask elsewhere and see if you
    // get more"; there is only "ask exactly there and be refused". Measuring
    // something and calling it the analogue would be a false analogy dressed as a
    // fact, so the field stays unknown and this says why.
    warnings.emplace_back(
        "max_single_reservation_hinted is not measured on this platform and is "
        "left unknown: a VirtualAlloc base address is a requirement, not an "
        "advisory hint, so there is no second question to ask. On Linux that "
        "second number is what separates 'the largest the kernel grants' from "
        "'the largest inside the default mmap window'");

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

    // Reserve then commit. This probe establishes the
    // `WindowsReserveCommit` model; the worker-only Job Object CTest
    // independently exercises its synchronous commitment failure.
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

        // THE ARENAS, and there are two because the runner said so. Everything
        // above this point in the scan established no address range at all:
        // `available_ranges` and `unavailable_ranges` were both empty on a real
        // Windows runner, so RS-VM-0001/0002/0003 - this project's flagship
        // rules - answered UNKNOWN for every address on the platform.
        const std::uint64_t top_bottom = arena_floor_for(max_address, kArenaSpan);

        auto scan_one_arena = [&](const char* what, std::uint64_t bottom,
                                  std::uint64_t top, std::uint64_t window,
                                  const char* band_note) {
            if (bottom == 0 || bottom >= top || top - bottom < window) return;

            // `place` and `describe` are two halves of one question and the
            // second needs what the first learned, so the error travels in a
            // local rather than through GetLastError() - which the VirtualQuery
            // inside `place` would have already overwritten.
            std::string refusal = "no error recorded";

            ArenaProbe hooks;
            hooks.place = [&refusal](std::uint64_t base,
                                     std::uint64_t size) -> ArenaPlacement {
                const LPVOID got = ::VirtualAlloc(
                    reinterpret_cast<LPVOID>(base), static_cast<SIZE_T>(size),
                    MEM_RESERVE, PAGE_NOACCESS);
                if (got != nullptr) {
                    // MEM_RESERVE only: this bounds the RESERVATION and never
                    // charges the commit limit, which is what makes a 64 GiB
                    // window cost the same as a 64 MiB one. Released at once -
                    // the arena must leave the address space as it found it,
                    // which `probe_leaves_the_process_address_space_usable`
                    // checks.
                    ::VirtualFree(got, 0, MEM_RELEASE);
                    return ArenaPlacement::Placed;
                }
                refusal = last_error_name(::GetLastError());

                // THE EEXIST ANALOGUE, and Windows has no errno for it.
                //
                // Linux answers EEXIST when MAP_FIXED_NOREPLACE hits one of our
                // own mappings, and `arena_walk` counts that exactly as a
                // success: it proves the kernel hands this space out and says
                // nothing about the host. VirtualAlloc reports
                // ERROR_INVALID_ADDRESS whether the range is ours or the
                // system's, so the distinction has to be asked for separately.
                // Asking at the BASE is the faithful analogue - something of
                // ours is right here - and it keeps `held_no_access` meaning
                // what it means on macOS: the genuinely ambiguous case, where
                // the blocker is elsewhere in the window.
                if (region_at_is_occupied(base, query_region)) {
                    return ArenaPlacement::HeldByProbe;
                }
                return ArenaPlacement::Refused;
            };
            hooks.describe = [&refusal, window](std::uint64_t base) -> ArenaEntry {
                return classify_window(base, window, query_region, refusal);
            };

            const ArenaWalk arena = walk_arena(
                what, bottom, top,
                // The alignment quantum for a BASE ADDRESS here is
                // dwAllocationGranularity, not dwPageSize. Passing the page
                // size would let the walk try bases VirtualAlloc rounds down,
                // which is RSC-0044's subject arriving inside the probe itself.
                granularity, window, hooks);

            for (const auto& r : arena.available) {
                profile.vm.available_ranges.push_back(r);
            }
            for (const auto& r : arena.unavailable) {
                profile.vm.unavailable_ranges.push_back(r);
            }

            // INTO `warnings`, WHERE THE OTHER TWO PLATFORMS PUT IT. The first
            // version of this pushed to `profile.notes`, and the coverage test's
            // diagnosis - which reads `run.warnings` - printed "NO arena was
            // scanned on this platform" in the same failure whose `nearest
            // above` was the arena's own range. One kind of thing, one place.
            warnings.push_back(
                std::string("allocation arena [") + json::to_hex(bottom) + ", " +
                json::to_hex(top) + ") walked in contiguous windows of " +
                json::to_hex(window) + " bytes: " +
                std::to_string(arena.placed) + " placed, " +
                std::to_string(arena.held_by_probe) +
                " already held by this process at the window's own base, " +
                std::to_string(arena.refused) + " structurally refused, " +
                std::to_string(arena.held_no_access) +
                " refused with a region of this process elsewhere in the window "
                "(treated as held; " + band_note +
                ", so if this number rises while unavailable_ranges stays empty "
                "that assumption has broken), and " +
                std::to_string(arena.skipped) +
                " not probed because they lie inside a region already described. "
                "Only the structurally refused count is a host limitation, and a "
                "refusal is recorded at the resolution of one window - which "
                "over-claims for a hole smaller than that, and has measured zero "
                "so far. This split moves with ASLR, which is why it is a warning "
                "and not a fact");
        };

        // Where the image and every DLL are.
        scan_one_arena(
            "allocation arena in the top TiB of the user address space, where "
            "high-entropy ASLR places the image, every loaded DLL and the thread "
            "stacks",
            top_bottom, max_address, kArenaWindow,
            "this arena's floor is 127 TiB above KUSER_SHARED_DATA at "
            "0x7ffe0000, the only system-wide band this platform is known to "
            "place");

        // Where an NT heap goes, which is nowhere near the image. `test_probe`
        // allocated at 0x2f78000e000 - 2.97 TiB - while `rs-env-probe` in the
        // same push had its heap in the 1 TiB bucket.
        scan_one_arena(
            "allocation arena between 1 TiB and the top-TiB arena, where an NT "
            "heap and an unhinted VirtualAlloc are placed",
            kLowArenaBottom, top_bottom, kLowArenaWindow,
            "this arena starts at 1 TiB, above KUSER_SHARED_DATA at 0x7ffe0000, "
            "so the only system-wide band this platform is known to place is "
            "outside it and [min_map_address, 1 TiB) is deliberately left to the "
            "landmark ladder");

        if (top_bottom == 0) {
            warnings.push_back(
                "no allocation arena was walked: max_user_address " +
                json::to_hex(max_address) +
                " leaves no room for one, so every address question on this host "
                "will answer UNKNOWN");
        }

        // Where this process actually sits. Note-only for the same reason, and
        // present because an arena over 128 TiB has to choose its regions from
        // something, and a measurement beats a recollection.
        std::string where =
            "VirtualQuery occupancy (NOT a fact about the host - this "
            "process's own layout, for designing an arena's bounds): occupied "
            "[" + json::to_hex(walk.lowest_occupied) + ", " +
            json::to_hex(walk.highest_occupied_end) + "), " +
            std::to_string(walk.occupied_bytes) + " bytes total, in " +
            std::to_string(walk.occupied_by_tib.size()) + " TiB bucket(s):";
        // Bounded: a hundred buckets is already more than a reader needs, and an
        // unbounded note is a way to put a megabyte in a profile by accident.
        std::size_t shown = 0;
        for (const auto& [bucket, bytes] : walk.occupied_by_tib) {
            if (shown++ == 100) {
                where += " ... and " +
                         std::to_string(walk.occupied_by_tib.size() - 100) +
                         " more";
                break;
            }
            where += " " + json::to_hex(bucket * kWinTiB) + "=" +
                     std::to_string(bytes);
        }
        profile.notes.push_back(where);
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

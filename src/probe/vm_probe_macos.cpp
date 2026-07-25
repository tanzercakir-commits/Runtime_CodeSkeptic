// SPDX-License-Identifier: Apache-2.0
//
// macOS virtual-memory probe, for both native arm64 and x86-64 under
// Rosetta 2.
//
// This platform is where every interesting claim in this project lives, and
// until this file existed none of those claims had been measured. Three
// hand-authored macOS fixtures carried the load, all of them capped at
// PREDICTIVE or OBSERVED_INVARIANT because nobody had run a probe.
//
// Two things differ substantially from the Linux probe and drive the design:
//
//   1. There is no MAP_FIXED_NOREPLACE. mmap's MAP_FIXED is destructive here
//      exactly as it is on Linux, so it is never used. The non-destructive
//      exact-placement primitive on macOS is mach_vm_allocate with
//      VM_FLAGS_FIXED, which returns KERN_NO_SPACE rather than evicting a
//      neighbour. As on Linux, that behaviour is verified before it is
//      trusted.
//
//   2. Translation is detectable. `sysctl.proc_translated` says whether this
//      process is running under Rosetta 2, so translation_mode can be a
//      measured fact instead of the honest `unknown` the Linux probe reports.
//      That matters: the page size, the address-space layout and the W^X
//      policy all differ between a native arm64 process and a translated
//      x86-64 one on the same machine.
#include "runtimeskeptic/probe/vm_probe.hpp"

#if defined(RS_PLATFORM_MACOS)

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include <fcntl.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/vm_statistics.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include "runtimeskeptic/core/sha256.hpp"

// MAP_NORESERVE is advisory where it exists and absent from some macOS SDKs.
// Defining it to zero keeps the call identical in meaning: the flag only ever
// asks the kernel not to pre-commit swap, which macOS does not do anyway.
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif

namespace rs::probe {
namespace {

using rs::Address;
using vm::AddressRange;
using vm::ClassifiedRange;

constexpr const char* kSourceProbe = "rs-env-probe vm (macos)";

std::string kern_error_name(kern_return_t code) {
    switch (code) {
        case KERN_SUCCESS: return "KERN_SUCCESS";
        case KERN_INVALID_ADDRESS: return "KERN_INVALID_ADDRESS";
        case KERN_PROTECTION_FAILURE: return "KERN_PROTECTION_FAILURE";
        case KERN_NO_SPACE: return "KERN_NO_SPACE";
        case KERN_INVALID_ARGUMENT: return "KERN_INVALID_ARGUMENT";
        case KERN_RESOURCE_SHORTAGE: return "KERN_RESOURCE_SHORTAGE";
        default: return "kern_return_t " + std::to_string(code);
    }
}

std::string errno_name(int e) {
    switch (e) {
        case EINVAL: return "EINVAL";
        case ENOMEM: return "ENOMEM";
        case EACCES: return "EACCES";
        case EPERM: return "EPERM";
        case EEXIST: return "EEXIST";
        case EBADF: return "EBADF";
        case EOVERFLOW: return "EOVERFLOW";
        default: return "errno " + std::to_string(e);
    }
}

// sysctlbyname for a small integer. Returns false when the name does not
// exist, which is itself informative: `sysctl.proc_translated` is absent on
// Intel Macs and on older systems.
bool sysctl_int(const char* name, std::int64_t& out) {
    std::int64_t value = 0;
    std::size_t size = sizeof(value);
    if (::sysctlbyname(name, &value, &size, nullptr, 0) != 0) return false;
    out = value;
    return true;
}

std::string sysctl_string(const char* name) {
    std::size_t size = 0;
    if (::sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size == 0) {
        return {};
    }
    std::vector<char> buffer(size + 1, '\0');
    if (::sysctlbyname(name, buffer.data(), &size, nullptr, 0) != 0) return {};
    return std::string(buffer.data());
}

// -------------------------------------------------------------------------
// mmap helpers, identical in spirit to the Linux probe: every mapping this
// creates is unmapped again, and MAP_FIXED is never passed.
// -------------------------------------------------------------------------
struct MapAttempt {
    void* address = MAP_FAILED;
    int error = 0;

    bool ok() const { return address != MAP_FAILED; }
};

MapAttempt try_map(void* hint, std::size_t length, int prot, int flags,
                   int fd = -1, off_t offset = 0) {
    MapAttempt attempt;
    errno = 0;
    attempt.address = ::mmap(hint, length, prot, flags, fd, offset);
    attempt.error = attempt.ok() ? 0 : errno;
    return attempt;
}

void unmap(const MapAttempt& attempt, std::size_t length) {
    if (attempt.ok()) ::munmap(attempt.address, length);
}

// -------------------------------------------------------------------------
// Non-destructive exact placement via mach_vm_allocate.
//
// VM_FLAGS_FIXED tells the kernel to place the allocation at exactly the
// address given. Unlike mmap's MAP_FIXED it does NOT evict an existing
// mapping - it returns KERN_NO_SPACE. That makes it safe to use for probing,
// but only if the kernel really behaves that way, so the probe checks before
// relying on it.
// -------------------------------------------------------------------------
enum class FixedSupport { Yes, No, Undetermined };

bool mach_allocate_fixed(std::uint64_t address, std::size_t length,
                         kern_return_t& result) {
    mach_vm_address_t target = static_cast<mach_vm_address_t>(address);
    result = ::mach_vm_allocate(::mach_task_self(), &target,
                                static_cast<mach_vm_size_t>(length),
                                VM_FLAGS_FIXED);
    if (result != KERN_SUCCESS) return false;
    // VM_FLAGS_FIXED must not relocate. If it did, the answer is worthless.
    if (static_cast<std::uint64_t>(target) != address) {
        ::mach_vm_deallocate(::mach_task_self(), target,
                             static_cast<mach_vm_size_t>(length));
        return false;
    }
    return true;
}

void mach_release(std::uint64_t address, std::size_t length) {
    ::mach_vm_deallocate(::mach_task_self(),
                         static_cast<mach_vm_address_t>(address),
                         static_cast<mach_vm_size_t>(length));
}

FixedSupport detect_fixed_allocation(std::size_t page_size,
                                     std::vector<std::string>& warnings) {
    // Take a page the ordinary way, then ask for exactly that page with
    // VM_FLAGS_FIXED. A conforming kernel refuses with KERN_NO_SPACE.
    MapAttempt occupied = try_map(nullptr, page_size, PROT_READ,
                                  MAP_PRIVATE | MAP_ANON);
    if (!occupied.ok()) {
        warnings.emplace_back(
            "could not create a reference mapping while checking "
            "VM_FLAGS_FIXED: " + errno_name(occupied.error));
        return FixedSupport::Undetermined;
    }
    const auto occupied_address =
        reinterpret_cast<std::uint64_t>(occupied.address);

    kern_return_t result = KERN_SUCCESS;
    const bool granted = mach_allocate_fixed(occupied_address, page_size, result);

    FixedSupport support = FixedSupport::Undetermined;
    if (granted) {
        // It handed us a range that was already ours. Either it silently
        // replaced the mapping or the two overlap; both make exact-placement
        // probing unsafe, so it is disabled rather than trusted.
        warnings.emplace_back(
            "mach_vm_allocate with VM_FLAGS_FIXED succeeded at an address that "
            "was already mapped; exact-placement probing was disabled because "
            "the operation may be destructive on this system");
        mach_release(occupied_address, page_size);
        support = FixedSupport::No;
    } else if (result == KERN_NO_SPACE) {
        support = FixedSupport::Yes;
    } else {
        warnings.emplace_back(
            "the VM_FLAGS_FIXED collision test returned " +
            kern_error_name(result) +
            " instead of KERN_NO_SPACE; exact-placement facts were left "
            "unknown");
        support = FixedSupport::Undetermined;
    }

    unmap(occupied, page_size);
    return support;
}

// Why a placement attempt failed. The distinction is load-bearing, and its
// absence was the first defect the first real macOS measurement exposed.
//
// KERN_NO_SPACE means "this range is already yours" - a fact about ONE
// process's layout, contaminated by wherever the loader put the binary.
// KERN_INVALID_ADDRESS and friends mean "the kernel refuses this part of the
// address space" - a fact about the host. Treating them alike made
// min_map_address report where the probe's own image ended, dressed up as
// platform policy and carrying measured_capability evidence.
enum class Placement { Placed, OccupiedByUs, Refused };

Placement try_place(std::uint64_t address, std::size_t length) {
    kern_return_t result = KERN_SUCCESS;
    if (mach_allocate_fixed(address, length, result)) {
        mach_release(address, length);
        return Placement::Placed;
    }
    return result == KERN_NO_SPACE ? Placement::OccupiedByUs : Placement::Refused;
}

// Steps past pages the probe's own image occupies, so a search converges on a
// kernel boundary rather than on the edge of our own mappings.
Placement try_place_nearby(std::uint64_t address, std::size_t length,
                           std::size_t page_size) {
    for (unsigned i = 0; i < 16; ++i) {
        const std::uint64_t candidate =
            address + static_cast<std::uint64_t>(i) * page_size;
        if (candidate < address) break;  // wrapped
        const Placement p = try_place(candidate, length);
        if (p != Placement::OccupiedByUs) return p;
    }
    return Placement::OccupiedByUs;
}

// -------------------------------------------------------------------------
// Bounds.
//
// macOS puts __PAGEZERO at the bottom of a 64-bit process, so the lowest
// mappable address is far above Linux's mmap_min_addr, and its size is a
// link-time choice that differs between a native arm64 binary and a
// translated x86-64 one. Measured, never assumed.
// -------------------------------------------------------------------------
std::uint64_t find_min_map_address(std::size_t page_size) {
    std::uint64_t first_ok = 0;
    for (unsigned bit = 12; bit < 48; ++bit) {
        const std::uint64_t candidate = std::uint64_t{1} << bit;
        if (try_place_nearby(candidate, page_size, page_size) ==
            Placement::Placed) {
            first_ok = candidate;
            break;
        }
    }
    if (first_ok <= page_size) return first_ok;

    std::uint64_t low = first_ok / 2;
    std::uint64_t high = first_ok;
    while (high - low > page_size) {
        const std::uint64_t mid =
            low + ((high - low) / 2 / page_size) * page_size;
        if (mid == low) break;
        // Only a REFUSAL moves the floor up. "Occupied by us" says nothing
        // about what the kernel would permit another process.
        if (try_place_nearby(mid, page_size, page_size) == Placement::Placed) {
            high = mid;
        } else {
            low = mid;
        }
    }
    return high;
}

// The address space is a SET, not an interval.
//
// The first version walked powers of two and stopped at the first failure,
// then bisected. On Linux that is harmless: user space really is one
// contiguous run. On macOS it is wrong. There are holes, and the search
// halted at the bottom of the first one - reporting the commpage boundary at
// 0xFC0000000 as "the end of the user address space" while 0x7000000000 was
// demonstrably usable. Everything above that false ceiling then went
// untested, including the entire band this project exists to reason about.
struct SpaceSurvey {
    std::uint64_t highest_placed = 0;
    std::vector<std::uint64_t> refused;  // structural refusals, ascending
};

SpaceSurvey survey_address_space(std::size_t page_size) {
    SpaceSurvey survey;
    for (unsigned bit = 20; bit < 63; ++bit) {
        const std::uint64_t candidate = std::uint64_t{1} << bit;
        switch (try_place_nearby(candidate, page_size, page_size)) {
            case Placement::Placed:
                survey.highest_placed = candidate;
                break;
            case Placement::Refused:
                survey.refused.push_back(candidate);
                break;
            case Placement::OccupiedByUs:
                break;  // says nothing about the host
        }
    }
    return survey;
}

// Refines the top of the address space from the highest probe point that
// worked. Returns 0 rather than a guess when the result is inconsistent.
std::uint64_t refine_max_user_address(std::uint64_t highest_placed,
                                      std::size_t page_size) {
    if (highest_placed == 0) return 0;
    if (highest_placed > (UINT64_MAX / 2)) return highest_placed + page_size;
    std::uint64_t low = highest_placed;
    std::uint64_t high = highest_placed * 2;
    if (try_place_nearby(high, page_size, page_size) == Placement::Placed) {
        return 0;  // contradicts the survey; refuse to guess
    }
    while (high - low > page_size) {
        const std::uint64_t mid =
            low + ((high - low) / 2 / page_size) * page_size;
        if (mid == low) break;
        if (try_place_nearby(mid, page_size, page_size) == Placement::Placed) {
            low = mid;
        } else {
            high = mid;
        }
    }
    return low + page_size;
}
// -------------------------------------------------------------------------
// Hint relocation.
// -------------------------------------------------------------------------
enum class RelocationObservation { Relocated, Honoured, Undetermined };

RelocationObservation observe_hint_relocation(std::size_t page_size) {
    MapAttempt occupied = try_map(nullptr, page_size, PROT_READ,
                                  MAP_PRIVATE | MAP_ANON);
    if (!occupied.ok()) return RelocationObservation::Undetermined;

    MapAttempt hinted = try_map(occupied.address, page_size, PROT_READ,
                                MAP_PRIVATE | MAP_ANON);
    RelocationObservation observation = RelocationObservation::Undetermined;
    if (hinted.ok()) {
        observation = hinted.address == occupied.address
                          ? RelocationObservation::Honoured
                          : RelocationObservation::Relocated;
        unmap(hinted, page_size);
    }
    unmap(occupied, page_size);
    return observation;
}

// -------------------------------------------------------------------------
// Protection, including the MAP_JIT path that Apple Silicon requires.
// -------------------------------------------------------------------------
struct ProtectionObservations {
    bool tested_rwx = false;
    bool rwx_allowed = false;
    bool tested_rx = false;
    bool rx_allowed = false;
    bool tested_flip = false;
    bool flip_allowed = false;
    bool tested_jit = false;
    bool jit_allowed = false;
};

ProtectionObservations probe_protection(std::size_t page_size) {
    ProtectionObservations obs;

    MapAttempt rwx = try_map(nullptr, page_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                             MAP_PRIVATE | MAP_ANON);
    obs.tested_rwx = true;
    obs.rwx_allowed = rwx.ok();
    unmap(rwx, page_size);

    MapAttempt rx = try_map(nullptr, page_size, PROT_READ | PROT_EXEC,
                            MAP_PRIVATE | MAP_ANON);
    obs.tested_rx = true;
    obs.rx_allowed = rx.ok();
    unmap(rx, page_size);

    MapAttempt rw = try_map(nullptr, page_size, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANON);
    if (rw.ok()) {
        obs.tested_flip = true;
        obs.flip_allowed =
            ::mprotect(rw.address, page_size, PROT_READ | PROT_EXEC) == 0;
        unmap(rw, page_size);
    }

#if defined(MAP_JIT)
    // MAP_JIT is the sanctioned route to writable-then-executable memory on
    // this platform, and it is granted only to processes carrying the
    // com.apple.security.cs.allow-jit entitlement. Whether it succeeds is
    // therefore a measurement of THIS process's entitlements as much as of
    // the OS - which is exactly the distinction the finding needs to draw.
    MapAttempt jit = try_map(nullptr, page_size,
                             PROT_READ | PROT_WRITE | PROT_EXEC,
                             MAP_PRIVATE | MAP_ANON | MAP_JIT);
    obs.tested_jit = true;
    obs.jit_allowed = jit.ok();
    unmap(jit, page_size);
#endif
    return obs;
}

enum class ReservationObservation { Lazy, Undetermined };

ReservationObservation probe_lazy_reservation() {
    constexpr std::size_t kOneGiB = 1024ull * 1024 * 1024;
    MapAttempt reserved = try_map(nullptr, kOneGiB, PROT_NONE,
                                  MAP_PRIVATE | MAP_ANON | MAP_NORESERVE);
    if (!reserved.ok()) return ReservationObservation::Undetermined;
    unmap(reserved, kOneGiB);
    return ReservationObservation::Lazy;
}

// -------------------------------------------------------------------------
// Beyond end of file, in a forked child so the fault is an observation
// rather than a crash.
// -------------------------------------------------------------------------
enum class EofObservation { Sigbus, ZeroFill, MapRefused, Undetermined };

EofObservation probe_beyond_eof(std::size_t page_size, unsigned timeout_seconds,
                                std::vector<std::string>& warnings) {
    const char* tmpdir = ::getenv("TMPDIR");
    std::string path = (tmpdir != nullptr && tmpdir[0] != '\0') ? tmpdir : "/tmp";
    if (!path.empty() && path.back() != '/') path.push_back('/');
    path += "rs-env-probe-XXXXXX";
    std::vector<char> templ(path.begin(), path.end());
    templ.push_back('\0');

    const int fd = ::mkstemp(templ.data());
    if (fd < 0) {
        warnings.emplace_back(
            "could not create a temporary file for the beyond-EOF test");
        return EofObservation::Undetermined;
    }
    ::unlink(templ.data());

    if (::ftruncate(fd, static_cast<off_t>(page_size)) != 0) {
        ::close(fd);
        return EofObservation::Undetermined;
    }

    const std::size_t map_length = page_size * 2;
    MapAttempt probe_map = try_map(nullptr, map_length, PROT_READ, MAP_SHARED, fd, 0);
    if (!probe_map.ok()) {
        ::close(fd);
        return EofObservation::MapRefused;
    }
    unmap(probe_map, map_length);

    ::fflush(nullptr);
    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(fd);
        warnings.emplace_back("fork failed; the beyond-EOF test was skipped");
        return EofObservation::Undetermined;
    }
    if (pid == 0) {
        ::alarm(timeout_seconds);
        void* p = ::mmap(nullptr, map_length, PROT_READ, MAP_SHARED, fd, 0);
        if (p == MAP_FAILED) ::_exit(20);
        volatile const unsigned char* bytes =
            static_cast<volatile const unsigned char*>(p);
        const unsigned char value = bytes[page_size];
        ::_exit(value == 0 ? 10 : 11);
    }

    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        ::close(fd);
        return EofObservation::Undetermined;
    }
    ::close(fd);

    if (WIFSIGNALED(status)) {
        const int sig = WTERMSIG(status);
        if (sig == SIGBUS) return EofObservation::Sigbus;
        if (sig == SIGALRM) {
            warnings.emplace_back("the beyond-EOF test child timed out");
            return EofObservation::Undetermined;
        }
        warnings.emplace_back("the beyond-EOF test child died on signal " +
                              std::to_string(sig));
        return EofObservation::Undetermined;
    }
    if (WIFEXITED(status)) {
        const int code = WEXITSTATUS(status);
        if (code == 10 || code == 11) return EofObservation::ZeroFill;
        if (code == 20) return EofObservation::MapRefused;
    }
    return EofObservation::Undetermined;
}

// -------------------------------------------------------------------------
// Address-space scan.
//
// The candidate list deliberately includes the band shadPS4 reports as
// GPU-reserved under Rosetta 2 (0x1000000000 - 0x6FFFFFFFFF) and the commpage
// (0xFC0000000 - 0xFFFFFFFFF). Those are the addresses the whole macOS story
// in this project rests on, and until now nothing had measured them.
// -------------------------------------------------------------------------
struct ScanOutcome {
    std::vector<ClassifiedRange> available;
    std::vector<ClassifiedRange> unavailable;
    std::vector<std::string> occupied_notes;
};

ScanOutcome scan_address_space(std::size_t page_size, std::uint64_t probe_length,
                               std::uint64_t min_address) {
    ScanOutcome outcome;

    std::vector<std::uint64_t> candidates;
    for (unsigned bit = 16; bit < 63; ++bit) {
        const std::uint64_t boundary = std::uint64_t{1} << bit;
        candidates.push_back(boundary);
        if (boundary > probe_length) {
            const std::uint64_t below = boundary - probe_length;
            if (below % page_size == 0) candidates.push_back(below);
        }
    }
    // The reported Rosetta 2 reserved bands, sampled at both ends and in the
    // middle, plus the address from the GTA V incident.
    for (std::uint64_t addr : {0xfc0000000ull,    // commpage start
                               0xff0000000ull,    // commpage middle
                               0x1000000000ull,   // GPU carveout start
                               0x1307200000ull,   // shadPS4 issue #4157
                               0x2000000000ull,
                               0x4000000000ull,
                               0x6f00000000ull,
                               0x6fc0000000ull,   // reported carveout end
                               0x7000000000ull}) {  // shadPS4 USER_MIN
        if (addr % page_size == 0) candidates.push_back(addr);
    }

    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()),
                     candidates.end());

    for (std::uint64_t base : candidates) {
        if (base % page_size != 0) continue;
        // Deliberately NOT skipped against max_address. That ceiling is
        // itself a measurement and was wrong once already; letting it prune
        // the candidate list is how the first run silently failed to test the
        // band the whole project is about.
        if (min_address != 0 && base < min_address) continue;
        const auto range = AddressRange::from_base_size(base, probe_length);
        if (!range) continue;

        kern_return_t result = KERN_SUCCESS;
        if (mach_allocate_fixed(base, static_cast<std::size_t>(probe_length),
                                result)) {
            ClassifiedRange cr;
            cr.range = *range;
            cr.evidence = EvidenceClass::MeasuredCapability;
            cr.note = "mach_vm_allocate(VM_FLAGS_FIXED) succeeded at this exact "
                      "address in the probe process";
            outcome.available.push_back(cr);
            mach_release(base, static_cast<std::size_t>(probe_length));
            continue;
        }

        if (result == KERN_NO_SPACE) {
            // Occupied in THIS process. That is a property of one process
            // layout, not of the host, so it must not become a host
            // limitation other programs are judged against.
            outcome.occupied_notes.push_back(
                "range " + range->to_string() +
                " was occupied in the probe process (KERN_NO_SPACE); this is a "
                "property of the probe's own layout and was NOT recorded as a "
                "host limitation");
            continue;
        }

        // KERN_INVALID_ADDRESS and friends are structural: the kernel refuses
        // this part of the address space regardless of what is mapped.
        ClassifiedRange cr;
        cr.range = *range;
        cr.evidence = EvidenceClass::MeasuredCapability;
        cr.note = "exact allocation refused with " + kern_error_name(result);
        outcome.unavailable.push_back(cr);
    }
    return outcome;
}

std::string utc_timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm tm_buf{};
    ::gmtime_r(&now, &tm_buf);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return buffer;
}

}  // namespace

std::string probe_platform_name() { return "macos"; }

Result probe_virtual_memory(const Options& options) {
    Result result;
    result.implemented = true;

    vm::EnvironmentProfile& profile = result.profile;
    std::vector<std::string>& warnings = profile.run.warnings;

    profile.origin = vm::ProfileOrigin::Measured;
    profile.run.probe_version = kProbeVersion;
    profile.platform.os = vm::OperatingSystem::MacOS;

    // -- platform ---------------------------------------------------------
    utsname uts{};
    if (::uname(&uts) == 0) {
        profile.platform.kernel_version = uts.release;
        profile.platform.os_version = std::string(uts.sysname) + " " + uts.release;
    }
    const std::string product = sysctl_string("kern.osproductversion");
    if (!product.empty()) {
        profile.platform.os_version = "macOS " + product;
    }

    // Which architecture is this PROCESS?
#if defined(__aarch64__) || defined(__arm64__)
    profile.platform.process_arch = vm::Architecture::Aarch64;
#elif defined(__x86_64__)
    profile.platform.process_arch = vm::Architecture::X86_64;
#else
    profile.platform.process_arch = vm::Architecture::Unknown;
#endif

    // Which architecture is the MACHINE, and are we translated? These are
    // different questions and the answer to the second is the whole reason
    // this platform is interesting.
    std::int64_t translated = 0;
    const bool has_translated_flag =
        sysctl_int("sysctl.proc_translated", translated);
    std::int64_t arm64_host = 0;
    const bool has_arm64_flag = sysctl_int("hw.optional.arm64", arm64_host);

    if (has_arm64_flag && arm64_host != 0) {
        profile.platform.host_arch = vm::Architecture::Aarch64;
    } else if (has_translated_flag && translated != 0) {
        // Translated implies an arm64 host even if hw.optional.arm64 is not
        // visible to the translated process.
        profile.platform.host_arch = vm::Architecture::Aarch64;
    } else if (!has_arm64_flag && !has_translated_flag) {
        profile.platform.host_arch = vm::Architecture::Unknown;
        warnings.emplace_back(
            "neither hw.optional.arm64 nor sysctl.proc_translated is readable; "
            "the host architecture was left unknown rather than assumed");
    } else {
        profile.platform.host_arch = vm::Architecture::X86_64;
    }

    if (has_translated_flag) {
        profile.platform.translation_mode = translated != 0
                                                ? vm::TranslationMode::Rosetta2
                                                : vm::TranslationMode::None;
    } else {
        // Absent on Intel Macs and older systems. Absence is weak evidence of
        // "not translated", but it is evidence: Rosetta 2 sets the flag.
        profile.platform.translation_mode = vm::TranslationMode::None;
        warnings.emplace_back(
            "sysctl.proc_translated is not available on this system; "
            "translation_mode was recorded as 'none' on the grounds that "
            "Rosetta 2 always publishes the flag");
    }

    // -- page size --------------------------------------------------------
    const long sc_page = ::sysconf(_SC_PAGESIZE);
    if (sc_page <= 0) {
        warnings.emplace_back("sysconf(_SC_PAGESIZE) failed; the probe cannot "
                              "continue meaningfully");
        return result;
    }
    const std::size_t page_size = static_cast<std::size_t>(sc_page);

    profile.vm.page_size = Fact<std::uint64_t>::known(
        static_cast<std::uint64_t>(page_size), EvidenceClass::MeasuredCapability,
        std::string(kSourceProbe) + ": sysconf(_SC_PAGESIZE)");
    profile.vm.allocation_granularity = Fact<std::uint64_t>::known(
        static_cast<std::uint64_t>(page_size), EvidenceClass::MeasuredCapability,
        std::string(kSourceProbe) +
            ": mach_vm_allocate places at page granularity");

    // -- can we map at all? -----------------------------------------------
    {
        MapAttempt basic = try_map(nullptr, page_size, PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANON);
        profile.vm.anonymous_mapping_supported = Fact<bool>::known(
            basic.ok(), EvidenceClass::MeasuredCapability,
            std::string(kSourceProbe) + ": mmap(NULL, page_size, RW, "
            "MAP_PRIVATE|MAP_ANON)" +
                (basic.ok() ? "" : " failed with " + errno_name(basic.error)));
        unmap(basic, page_size);
    }

    // -- exact placement ---------------------------------------------------
    const FixedSupport fixed = detect_fixed_allocation(page_size, warnings);
    switch (fixed) {
        case FixedSupport::Yes:
            profile.vm.fixed_noreplace_available = Fact<bool>::known(
                true, EvidenceClass::MeasuredCapability,
                std::string(kSourceProbe) +
                    ": mach_vm_allocate(VM_FLAGS_FIXED) refused a collision "
                    "with KERN_NO_SPACE rather than evicting the neighbour");
            profile.vm.exact_mapping = Fact<SupportLevel>::known(
                SupportLevel::ConditionallySupported,
                EvidenceClass::MeasuredCapability,
                std::string(kSourceProbe) +
                    ": exact placement succeeds for free, in-bounds, "
                    "page-aligned ranges");
            profile.vm.exact_mapping_failure_codes = {"KERN_NO_SPACE",
                                                      "KERN_INVALID_ADDRESS",
                                                      "ENOMEM"};
            break;
        case FixedSupport::No:
            profile.vm.fixed_noreplace_available = Fact<bool>::known(
                false, EvidenceClass::MeasuredCapability,
                std::string(kSourceProbe) +
                    ": VM_FLAGS_FIXED did not refuse a collision");
            break;
        case FixedSupport::Undetermined:
            break;
    }
    const bool exact_probing_allowed = fixed == FixedSupport::Yes;
    if (!exact_probing_allowed) {
        warnings.emplace_back(
            "exact-placement probing was skipped because VM_FLAGS_FIXED could "
            "not be confirmed non-destructive; mmap's MAP_FIXED is never used "
            "by this probe because it destroys existing mappings");
    }

    // -- bounds ------------------------------------------------------------
    std::uint64_t min_address = 0;
    std::uint64_t max_address = 0;
    (void)max_address;  // retained for readability of the survey block below
    if (exact_probing_allowed) {
        min_address = find_min_map_address(page_size);
        if (min_address != 0) {
            profile.vm.min_map_address = Fact<Address>::known(
                Address(min_address), EvidenceClass::MeasuredCapability,
                std::string(kSourceProbe) +
                    ": binary search for the lowest placeable page (this is "
                    "where __PAGEZERO ends)");
        }
        const SpaceSurvey survey = survey_address_space(page_size);
        max_address = refine_max_user_address(survey.highest_placed, page_size);
        if (max_address != 0) {
            profile.vm.max_user_address = Fact<Address>::known(
                Address(max_address), EvidenceClass::MeasuredCapability,
                std::string(kSourceProbe) +
                    ": highest placeable probe point was " +
                    std::to_string(survey.highest_placed) +
                    ", refined by bisection above it");
        } else if (survey.highest_placed != 0) {
            warnings.emplace_back(
                "the top of the address space could not be pinned down "
                "consistently; max_user_address was left unknown rather than "
                "guessed");
        }
        // Structural refusals BELOW the highest placeable address are holes,
        // not the ceiling. Recording them is the whole point on this platform.
        for (std::uint64_t refused : survey.refused) {
            if (refused >= survey.highest_placed) continue;
            const auto hole = AddressRange::from_base_size(
                refused, static_cast<std::uint64_t>(page_size));
            if (!hole) continue;
            ClassifiedRange cr;
            cr.range = *hole;
            cr.evidence = EvidenceClass::MeasuredCapability;
            cr.note = "the kernel refused an exact placement here while higher "
                      "addresses remained placeable, so this is a hole in the "
                      "address space rather than its end";
            profile.vm.unavailable_ranges.push_back(cr);
        }
    }

    // -- hint relocation ---------------------------------------------------
    switch (observe_hint_relocation(page_size)) {
        case RelocationObservation::Relocated:
            profile.vm.hinted_mapping_may_relocate = Fact<bool>::known(
                true, EvidenceClass::MeasuredCapability,
                std::string(kSourceProbe) +
                    ": a hinted mapping at an occupied address was relocated");
            break;
        case RelocationObservation::Honoured:
            profile.vm.hinted_mapping_may_relocate = Fact<bool>::known(
                true, EvidenceClass::SpecifiedGuarantee,
                "POSIX mmap: addr is a hint and the implementation may place "
                "the mapping elsewhere");
            warnings.emplace_back(
                "the hint was honoured in this run, but mmap permits "
                "relocation; the specified guarantee was recorded rather than "
                "the observation");
            break;
        case RelocationObservation::Undetermined:
            break;
    }

    // -- protection --------------------------------------------------------
    const ProtectionObservations prot = probe_protection(page_size);
    if (prot.tested_rwx) {
        profile.vm.protection.write_execute_simultaneous = Fact<bool>::known(
            prot.rwx_allowed, EvidenceClass::MeasuredCapability,
            std::string(kSourceProbe) +
                ": mmap(PROT_READ|PROT_WRITE|PROT_EXEC) without MAP_JIT");
    }
    if (prot.tested_rx) {
        profile.vm.protection.anonymous_executable_mapping = Fact<bool>::known(
            prot.rx_allowed, EvidenceClass::MeasuredCapability,
            std::string(kSourceProbe) + ": mmap(PROT_READ|PROT_EXEC)");
    }
    if (prot.tested_flip) {
        profile.vm.protection.write_then_execute_transition = Fact<bool>::known(
            prot.flip_allowed, EvidenceClass::MeasuredCapability,
            std::string(kSourceProbe) + ": mprotect(RW -> RX)");
    }
    if (prot.tested_jit) {
        // The entitlement question, answered by measurement rather than by
        // documentation: if plain RWX is refused but MAP_JIT is granted, this
        // process HAS the entitlement and an unentitled one would not.
        if (!prot.rwx_allowed && prot.jit_allowed) {
            profile.vm.protection.jit_entitlement_required = Fact<bool>::known(
                true, EvidenceClass::MeasuredCapability,
                std::string(kSourceProbe) +
                    ": plain RWX was refused while MAP_JIT succeeded, so "
                    "executable memory here is gated on the JIT entitlement");
        } else if (prot.rwx_allowed) {
            profile.vm.protection.jit_entitlement_required = Fact<bool>::known(
                false, EvidenceClass::MeasuredCapability,
                std::string(kSourceProbe) +
                    ": plain RWX succeeded without MAP_JIT or any entitlement");
        } else {
            warnings.emplace_back(
                "neither plain RWX nor MAP_JIT succeeded; whether the "
                "restriction is the entitlement or something else was left "
                "unknown");
        }
    }

    // -- reserve/commit ----------------------------------------------------
    if (probe_lazy_reservation() == ReservationObservation::Lazy) {
        profile.vm.reserve_commit_model = Fact<vm::ReserveCommitModel>::known(
            vm::ReserveCommitModel::PosixLazy, EvidenceClass::MeasuredCapability,
            std::string(kSourceProbe) +
                ": a 1 GiB PROT_NONE reservation succeeded without backing it");
    }

    // -- beyond end of file ------------------------------------------------
    if (options.run_faulting_tests) {
        switch (probe_beyond_eof(page_size, options.child_timeout_seconds,
                                 warnings)) {
            case EofObservation::Sigbus:
                profile.vm.file_map_beyond_eof =
                    Fact<vm::BeyondEofBehavior>::known(
                        vm::BeyondEofBehavior::Sigbus,
                        EvidenceClass::MeasuredCapability,
                        std::string(kSourceProbe) +
                            ": an isolated child took SIGBUS reading a whole "
                            "page past end of file");
                break;
            case EofObservation::ZeroFill:
                profile.vm.file_map_beyond_eof =
                    Fact<vm::BeyondEofBehavior>::known(
                        vm::BeyondEofBehavior::ZeroFill,
                        EvidenceClass::MeasuredCapability,
                        std::string(kSourceProbe) +
                            ": an isolated child read past end of file without "
                            "faulting");
                break;
            case EofObservation::MapRefused:
                profile.vm.file_map_beyond_eof =
                    Fact<vm::BeyondEofBehavior>::known(
                        vm::BeyondEofBehavior::Error,
                        EvidenceClass::MeasuredCapability,
                        std::string(kSourceProbe) +
                            ": the mapping call refused to extend past end of "
                            "file");
                break;
            case EofObservation::Undetermined:
                break;
        }
    } else {
        warnings.emplace_back(
            "faulting tests were disabled; beyond-EOF behavior is unknown");
    }

    // -- address-space scan ------------------------------------------------
    if (options.scan_address_space && exact_probing_allowed) {
        const std::uint64_t probe_length =
            options.max_test_mapping_bytes < page_size
                ? static_cast<std::uint64_t>(page_size)
                : options.max_test_mapping_bytes;
        ScanOutcome scan =
            scan_address_space(page_size, probe_length, min_address);
        profile.vm.available_ranges = std::move(scan.available);
        for (auto& r : scan.unavailable) {
            profile.vm.unavailable_ranges.push_back(std::move(r));
        }
        for (auto& note : scan.occupied_notes) {
            warnings.push_back(std::move(note));
        }
    }

    // -- run metadata ------------------------------------------------------
    profile.run.timestamp_utc = utc_timestamp();
    profile.run.run_id = rs::hash::sha256_hex(profile.run.timestamp_utc + ":" +
                                              std::to_string(::getpid()))
                             .substr(0, 16);
    profile.notes.emplace_back(
        "Ranges the probe found occupied by its own process image are NOT "
        "recorded as host limitations; see probe_run.warnings.");
    profile.notes.emplace_back(
        "MAP_FIXED is never used by this probe. Exact placement is probed "
        "through mach_vm_allocate(VM_FLAGS_FIXED), which refuses a collision "
        "instead of evicting the existing mapping.");

    return result;
}

}  // namespace rs::probe

#endif  // RS_PLATFORM_MACOS

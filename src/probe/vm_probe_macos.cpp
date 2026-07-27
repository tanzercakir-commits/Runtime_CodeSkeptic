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
#include <mach/vm_region.h>
#include <mach/vm_statistics.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include "runtimeskeptic/core/sha256.hpp"
#include "runtimeskeptic/probe/arena_walk.hpp"

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
using vm::collapse_contained_ranges;

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

// What does the task map actually say about this address?
//
// Iteration four on the same boundary, and the previous three were all wrong
// in instructive ways. "Is there a region covering it?" turned out to be true
// for the commpage and the GPU carveout: macOS places those in EVERY task's
// map, so the presence of a region says nothing about who may use it.
//
// vm_region_basic_info carries the field that does: `reserved` means the
// entry is a placeholder the system has taken, not a real mapping. That is
// precisely the distinction between "another part of my program is here" and
// "the platform will never give me this".
struct RegionInfo {
    bool found = false;
    bool covers = false;
    bool reserved = false;
    vm_prot_t protection = 0;
    std::uint64_t start = 0;
    std::uint64_t size = 0;
};

RegionInfo describe_region(std::uint64_t address) {
    RegionInfo out;
    mach_vm_address_t region = static_cast<mach_vm_address_t>(address);
    mach_vm_size_t size = 0;
    vm_region_basic_info_data_64_t info{};
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    const kern_return_t kr = ::mach_vm_region(
        ::mach_task_self(), &region, &size, VM_REGION_BASIC_INFO_64,
        reinterpret_cast<vm_region_info_t>(&info), &count, &object);
    if (kr != KERN_SUCCESS) return out;
    out.found = true;
    out.start = static_cast<std::uint64_t>(region);
    out.size = static_cast<std::uint64_t>(size);
    out.covers = out.start <= address && address < out.start + out.size;
    out.reserved = info.reserved != 0;
    out.protection = info.protection;
    return out;
}

// Human-readable, and carried into the profile so the NEXT measurement
// explains itself instead of needing another round of guessing.
std::string describe_region_text(const RegionInfo& r) {
    if (!r.found) return "mach_vm_region found no region at or above this address";
    std::string s = "region [" + json::to_hex(r.start) + ", " +
                    json::to_hex(r.start + r.size) + ")";
    s += r.covers ? " covers it" : " starts above it, so nothing covers it";
    if (r.covers) {
        s += r.reserved ? ", and is a system RESERVATION (vm_region reserved=1)"
                        : ", and is a real mapping (reserved=0)";
        s += ", protection ";
        s.push_back((r.protection & VM_PROT_READ) ? 'r' : '-');
        s.push_back((r.protection & VM_PROT_WRITE) ? 'w' : '-');
        s.push_back((r.protection & VM_PROT_EXECUTE) ? 'x' : '-');
    }
    return s;
}

Placement try_place(std::uint64_t address, std::size_t length) {
    kern_return_t result = KERN_SUCCESS;
    if (mach_allocate_fixed(address, length, result)) {
        mach_release(address, length);
        return Placement::Placed;
    }
    if (result != KERN_NO_SPACE) return Placement::Refused;

    // KERN_NO_SPACE means "this range is not free". It does NOT say WHO holds
    // it, and reading it as "the probe's own image" was wrong in the most
    // consequential place possible.
    //
    // The first macOS run refused every address from 0xFC0000000 to
    // 0x6FC0000000 with KERN_NO_SPACE - exactly the band shadPS4 documents as
    // the commpage plus the GPU carveout - and the probe filed all of it under
    // "occupied by us, not a host limitation" and recorded nothing. A fresh
    // process does not own 400 GiB at 64 GiB. The kernel does.
    //
    // So ask. If our own task has no mapping covering the address and the
    // allocation was still refused, the reservation belongs to the platform
    // and IS a host limitation.
    const RegionInfo region = describe_region(address);
    if (!region.covers) return Placement::Refused;
    // The `reserved` flag was the fourth wrong guess. macOS does NOT express
    // the commpage and the GPU carveout as reserved placeholders; it expresses
    // them as ordinary map entries that grant NO ACCESS AT ALL:
    //
    //   region [0xfc0000000,  0x1000000000)  reserved=0  protection ---
    //   region [0x1000000000, 0x7000000000)  reserved=0  protection ---
    //
    // Which is the whole point, and it is the right signal. A program asking
    // for that address does not care who nominally owns the entry; it cares
    // that nothing can be mapped there. An entry with no rights is address
    // space the host will not give up.
    if (region.reserved || region.protection == VM_PROT_NONE) {
        return Placement::Refused;
    }
    return Placement::OccupiedByUs;
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
// The largest single reservation the kernel actually grants, as a power of two.
//
// The Linux probe measures this; without it every macOS analysis of a request
// above 4 GiB answers UNKNOWN through `RS-VM-0027` - the correct reading of a
// profile that has not measured it, and a visible piece of missing work.
//
// Why it exists at all: `RS-VM-0021` compared a request against the width of the
// address space and, when it fitted, said nothing more. A 5-level-paging Linux
// runner refused a 4 PiB reservation with ENOMEM while the analyzer said
// SUPPORTED. Fitting is necessary, not sufficient.
//
// A POWER OF TWO on purpose: the exact largest reservation moves between two runs
// as the task's own mappings shift, and a fact that moves is a fact about the
// probe - `check_reproducible.sh` would fail and `profile_id` would stop naming
// the host.
//
// mmap with MAP_NORESERVE rather than mach_vm_allocate, matching
// `tests/groundtruth/cases/oversized_reservation.c` and the Linux probe, so the
// three are answering the same question. MAP_NORESERVE is #defined to 0 above
// where the SDK lacks it, which changes nothing: macOS does not pre-commit swap.
std::uint64_t find_max_single_reservation() {
    std::uint64_t largest = 0;
    for (unsigned bit = 20; bit < 63; ++bit) {   // from 1 MiB
        const std::uint64_t size = std::uint64_t{1} << bit;
        MapAttempt attempt =
            try_map(nullptr, static_cast<std::size_t>(size), PROT_NONE,
                    MAP_PRIVATE | MAP_ANON | MAP_NORESERVE);
        if (attempt.ok()) {
            largest = size;
            unmap(attempt, static_cast<std::size_t>(size));
        }
    }
    return largest;
}

// The same measurement asked high in the space. See the long note on the Linux
// probe's `find_max_single_reservation_hinted()`: `nullptr` is not a neutral
// place to ask on Linux, where `find_start_end()` opens the full address space
// only for a hint above `DEFAULT_MAP_WINDOW`.
//
// macOS documents no such window, so these two are EXPECTED to agree here - and
// measuring rather than assuming that is the entire reason this exists on this
// platform. A number that is expected to match and is never checked is an
// assumption wearing a fact's clothes, and this project has published several.
constexpr std::uint64_t kHighHint = std::uint64_t{1} << 47;

std::uint64_t find_max_single_reservation_hinted() {
    std::uint64_t largest = 0;
    for (unsigned bit = 20; bit < 63; ++bit) {   // from 1 MiB, as above
        const std::uint64_t size = std::uint64_t{1} << bit;
        // Advisory: no MAP_FIXED, so a kernel that dislikes the address
        // relocates and the measurement still happens.
        MapAttempt attempt = try_map(reinterpret_cast<void*>(kHighHint),
                                     static_cast<std::size_t>(size), PROT_NONE,
                                     MAP_PRIVATE | MAP_ANON | MAP_NORESERVE);
        if (attempt.ok()) {
            largest = size;
            unmap(attempt, static_cast<std::size_t>(size));
        }
    }
    return largest;
}

// USABLE BY ANYONE, which is not the same question as "free right now".
//
// `Placed` proves the kernel hands this address out. So does `OccupiedByUs`: a
// mapping of OUR OWN at an address is proof the kernel granted that address to a
// process. Only a refusal is a statement about the host. This is the EEXIST
// argument, `no_access_here_is_ours()`, `ladder_record()` and the arena's held
// windows - the same rule, and this is the fourth and fifth site of it.
//
// BOTH BISECTIONS BELOW HAD IT BACKWARDS, and one of them said so in a comment
// while doing the opposite: `find_min_map_address` reads "Only a REFUSAL moves
// the floor up. 'Occupied by us' says nothing about what the kernel would permit
// another process" directly above an `else` branch that moved the floor up on
// exactly that.
//
// What it cost was measured, on `2d058f7`, by the ground-truth case that had just
// stopped naming its ceiling as a constant:
//
//   exact-mapping-above-user-space   UNSUPPORTED  satisfied  CONTRADICTED
//     mmap(0x600000000000, 131072, MAP_FIXED) placed the mapping exactly there
//     and it accepted reads and writes
//
// `max_user_address` was reported as `0x600000000000` on macOS arm64 and a
// mapping went in AT that address and was written to. So the ceiling was not the
// top of the address space; it was the top of the space that happened to be free
// in one process on one morning - and every analyzer verdict above it was a
// false UNSUPPORTED, in the direction that tells a caller their working program
// will not work.
bool address_is_usable(std::uint64_t address, std::size_t page_size) {
    const Placement p = try_place_nearby(address, page_size, page_size);
    return p == Placement::Placed || p == Placement::OccupiedByUs;
}

// EXISTS, which is a different question from usable, and the ceiling asks this one.
//
// "The address space is a SET, not an interval" is written forty lines below, and
// it is why `survey_address_space` walks powers of two instead of stopping at the
// first failure. `refine_max_user_address` then bisected between two of those
// powers - and a bisection between a usable point and an unusable one IS the
// interval assumption, reintroduced one function later.
//
// IT COST THE SAME MISTAKE TWICE, four days apart, with two different bands. The
// first macOS run reported the bottom of the COMMPAGE as the top of the address
// space. On `7720b4b` the Rosetta lane reported the bottom of a Rosetta band:
//
//   max_user_address 0x7ff800000000, and mach_vm_region at the ceiling says:
//   region [0x7ff800000000, 0x7ff84d600000) covers it, and is a system
//   RESERVATION (vm_region reserved=1), protection r--
//
// while the native lane, with no such band, reached 0x7ffffe000000 - some 34 GiB
// higher. The ground-truth case then placed and WROTE a mapping at exactly the
// reported ceiling, because `mmap(MAP_FIXED)` is destructive (RSC-0051) and will
// replace even a system reservation inside your own task.
//
// A REGION THAT COVERS AN ADDRESS IS PROOF THE ADDRESS IS INSIDE THE SPACE. It is
// mapped; the kernel is describing it. Whether it is FREE is a different question
// and not the one `max_user_address` answers. Only a refusal with nothing covering
// it - the native lane's "mach_vm_region found no region at or above this address"
// - means the search has left the address space.
bool address_exists_in_this_task(std::uint64_t address, std::size_t page_size) {
    if (address_is_usable(address, page_size)) return true;
    return describe_region(address).covers;
}

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
        // about what the kernel would permit another process - and until
        // `address_is_usable()` existed, this line said that and did the
        // opposite.
        if (address_is_usable(mid, page_size)) {
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
            case Placement::OccupiedByUs:
                // Both prove the kernel hands this address out. The second used
                // to fall through to "says nothing about the host", which is
                // true of a LIMITATION and false of a CEILING: a mapping of ours
                // at an address is the strongest possible evidence that the
                // address exists.
                survey.highest_placed = candidate;
                break;
            case Placement::Refused:
                survey.refused.push_back(candidate);
                break;
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
    if (address_exists_in_this_task(high, page_size)) {
        return 0;  // contradicts the survey; refuse to guess
    }
    while (high - low > page_size) {
        const std::uint64_t mid =
            low + ((high - low) / 2 / page_size) * page_size;
        if (mid == low) break;
        // EXISTS, not usable: a system reservation is inside the address space
        // and stopping under it reports a band's floor as the top of the world,
        // which this probe has now done twice with two different bands.
        if (address_exists_in_this_task(mid, page_size)) {
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

// -------------------------------------------------------------------------
// The allocation arena: where a macOS program's own code and heap actually are.
//
// T-013 fixed this for Linux and only for Linux. The conformance case
// `the_scan_covers_where_this_process_is_actually_mapped` asserts coverage on
// EVERY platform, so it had been failing here since the day it was added -
// invisible because ci.yml ran macOS only in `expensive-platforms`, gated off
// pushes.
//
// The runner said exactly where, once the failure was made to carry evidence:
//
//   code page 0x1023a4000   nearest established below [0x100000000, 0x100004000)
//                           - 16 KiB wide, 37 MiB away
//   heap page 0x7be800000   nearest established above [0x7bf400000, 0xabe000000)
//                           - refused, 12 MiB away
//   containing: (none) for both. A scan-window gap, not a query bug.
//
// So the ladder DOES sample 0x1_0000_0000, 0x2_0000_0000, 0x4_0000_0000. The
// defect is not too few landmarks - it is that a landmark is a 4 MiB POINT, and
// what a program needs answered is the space between them.
//
// WHERE THE BOUNDS COME FROM, and why not from this process.
//
//   bottom  0x1_0000_0000  the __PAGEZERO size, and therefore the default
//                          __TEXT base, of a 64-bit Mach-O on x86_64 and
//                          arm64 alike. A per-architecture constant, the same
//                          in every task - which is the property Linux's
//                          ELF_ET_DYN_BASE has and /proc/self/maps does not.
//   top     0xfc0000000    the start of the COMMPAGE, already a named constant
//                          in the ladder above.
//
// The top was the carveout start `0x10_0000_0000` and had to come down, because
// the arena's resolution of an ambiguous refusal depends on it. Inside the arena a
// refusal covered by a no-access region of this task is treated as HELD - it says
// nothing about the host, the same argument EEXIST gets on Linux - and that is
// only sound while no band the PLATFORM puts in every task lies inside the bounds.
// The commpage `[0xfc0000000, 0x1000000000)` is such a band and was inside them.
//
// Both bands stay with the ladder above, which probes them at their documented
// addresses and records them properly. The arena's job is the space between, where
// programs are.
//
// Residual risk, stated rather than discovered later: if a macOS version puts an
// undocumented no-access band inside `[0x1_0000_0000, 0xfc0000000)`, this arena
// will claim it available. `held_no_access` in the note is the number that would
// expose it - it goes up while `unavailable_ranges` stays empty.
//
// `mach_vm_region` was the obvious-looking alternative and it is the same trap
// T-013 refused on Linux wearing a Mach name: it reports THIS task's slide, so
// an arena derived from it would hash this morning's layout into an id that is
// supposed to name the host.
//
// The walk itself is in `probe/arena_walk.hpp`, platform-neutral and driven by
// the two callbacks below, so that `tests/unit/test_arena_walk.cpp` can check it
// on a machine with no Mach headers. That is not tidiness: the first version of
// this arena was wrong, and the check that found it was a throwaway program that
// stubbed these two calls.
constexpr const char* kMachPlacementCall =
    "mach_vm_allocate(VM_FLAGS_FIXED)";

constexpr std::uint64_t kMachOTextBase = 0x100000000ull;
constexpr std::uint64_t kArenaTop = 0xfc0000000ull;   // commpage start
// A floor, so a caller passing a small max_test_mapping_bytes cannot turn a
// 60 GiB walk into four million windows.
constexpr std::uint64_t kMinArenaWindow = 4ull * 1024 * 1024;

// THE SECOND ARENA, and macOS is the third platform to need one.
//
// The arena above was designed from the NATIVE lane, where the runner reported
// code at 4.03 GiB and heap at 31 GiB - both inside [__TEXT base, commpage). One
// arena looked sufficient, and `d6abf18` showed why: the lane that would have
// disproved it could not see its own ceiling.
//
// With an honest `max_user_address`, `rosetta-x86_64` said:
//
//   heap page : 0x7f9ab0028000                  <- 140 TiB
//   arena     : [0x100000000, 0xfc0000000)      <- 15042 placed, 0 refused:
//                                                  working perfectly, 140 TiB away
//
// A TRANSLATED x86-64 process puts its heap at 0x7f..., like Linux, not at
// 0x7be800000 like the native one. Three platforms, three times the same
// discovery: one arena is never enough, because a program's code and its heap do
// not live together.
//
//   Linux    mmap base (top of space)  +  ET_DYN base
//   Windows  top TiB (image, DLLs)     +  1..127 TiB (NT heap)
//   macOS    [__TEXT base, commpage)   +  [ceiling - 4 TiB, ceiling)
//
// FOUR TiB, and 64 GiB WINDOWS. The span matches Linux's mmap arena, which exists
// for the same reason on the same shape of layout. The window is the Windows
// sizing rather than Linux's sampling: 4 TiB in 64 GiB windows is 64 CONTIGUOUS
// placements, and a contiguous walk asserts only what it placed, while a sampled
// one asserts the space between its samples - which `probe/arena_walk.hpp` argues
// against and Linux accepts only because 128 TiB leaves it no choice.
constexpr std::uint64_t kHighArenaSpan = 4ull << 40;
constexpr std::uint64_t kHighArenaWindow = 64ull << 30;

// WHERE A NO-ACCESS REGION IS ASSUMED TO BE OURS. One rule, in one place, because
// three separate producers reached for it and the first two disagreed.
//
// macOS gives no way to ask whose a map entry is. The commpage and the GPU carveout
// are no-access entries and REAL host limitations; a process's own malloc guards,
// dyld reservations and thread stack guards are no-access entries and are not.
// `try_place()` collapses both into `Refused`, and every consumer inherits the
// ambiguity:
//
//   the arena walk          filed ~80 of its own guard pages as limitations, and
//                           profile_id moved between two runs (83 vs 74 entries)
//   the max-address survey  recorded a 16 KiB "hole in the address space" at
//                           0x200000000 that the arena then placed a window across
//   the landmark ladder     widened a 12 GiB no-access region at 0x59e000000 to its
//                           full extent and called it a host limitation
//
// The bounds of the arena ARE the rule: inside them a no-access entry is ours,
// because both documented bands lie outside them (which is why the arena's top came
// down to the commpage start). Outside them it is the host's, and the ladder probes
// those addresses on purpose.
//
// Residual risk, stated once here rather than in three places: an undocumented
// platform band inside these bounds would be treated as ours by every producer. The
// `held_no_access` count in the arena's note is the signal - it rises while
// `unavailable_ranges` stays empty.
bool no_access_here_is_ours(std::uint64_t address) {
    return address >= kMachOTextBase && address < kArenaTop;
}

// One arena, not two, and that is a deliberate stopping point rather than an
// oversight. Linux needs two because a PIE's text and the kernel's mmap base sit
// four TiB apart. On macOS both measured addresses - code at 4.03 GiB, heap at
// 31 GiB - fall inside a single band between two constants, so a second arena
// would be a guess. If a runner puts either page outside this one, the failure
// now prints where, and THAT measurement can size the next arena.
// THE BOTTOM IS THE CONSTANT, AND NOTHING IS ALLOWED TO RAISE IT.
//
// The first version wrote `max(min_address, kMachOTextBase)`, with a comment
// claiming `min_address` is 0x1_0000_0000 on x86_64 because __PAGEZERO is four
// GiB, so "where the two disagree the measurement wins". That was wrong, and the
// runner said so on the next push: the code page moved from 0x1023a4000 to
// 0x102df0000 and was still not established, while the heap page - the other
// failure - now was.
//
// `find_min_map_address()` does not return the top of __PAGEZERO. It returns the
// lowest page THIS PROCESS can place, which is above __PAGEZERO *and above this
// process's whole low image*. So the code page is below it by construction, and
// deferring to it moves the arena's floor above the exact page the arena exists
// to cover.
//
// This file already says so twenty lines from where that call is made:
// min_map_address is "DELIBERATELY NOT RECORDED AS A HOST FACT ... a property of
// how this binary was linked and where the loader put it". Feeding it to the
// arena as a bound is the same value doing the same damage through a different
// door - the trap the paragraph above congratulates itself on avoiding, walked
// into at the bottom of the same function.
//
// Why the existing `min_address` filter on the LADDER is still right, and this is
// not: a ladder candidate below our image is recorded as available or occupied
// according to where our heap and libraries happen to sit, so the candidate set
// is a fact about our morning. The arena's bounds are constants, and
// `OccupiedByUs` is treated as usable rather than as a limitation - so our own
// image can change which windows are `placed` versus `held` (a note, outside
// profile_id) but never which are `refused`, and only a refusal splits a run. The
// recorded ranges are therefore identical in every process. That is the property
// that makes deferring unnecessary, and it is measurable:
// tools/campaign/check_reproducible.sh across two processes.
// ONE ARENA'S WALK, with its own bounds as its own rule.
//
// `probe/arena_walk.hpp` states the principle and the file-scope
// `no_access_here_is_ours()` was an instance of it hard-coded to one arena's
// bounds: "The bounds of the arena ARE the rule: inside them a no-access entry is
// ours, because both documented bands lie outside them." A second arena needs the
// same sentence with different numbers, so the numbers become parameters. For the
// original arena `[kMachOTextBase, kArenaTop)` this is byte-identical - the
// predicate below evaluates exactly what `no_access_here_is_ours()` does, and
// that function stays for the ladder and the survey, which have no arena bounds
// to speak of.
void scan_one_macos_arena(const char* what, std::uint64_t bottom,
                          std::uint64_t top, std::uint64_t page_size,
                          std::uint64_t window, const char* band_note,
                          ScanOutcome& outcome) {
    if (bottom == 0 || bottom >= top || top - bottom < window) return;

    ArenaProbe hooks;
    // OccupiedByUs maps to HeldByProbe, which the walk treats as usable - for
    // the same reason EEXIST does on Linux: it proves the kernel hands this
    // space out and proves nothing about the host. try_place() already separates
    // that from a real refusal, through four wrong iterations documented above,
    // and nothing here second-guesses it.
    hooks.place = [](std::uint64_t base, std::uint64_t size) {
        switch (try_place(base, static_cast<std::size_t>(size))) {
            case Placement::Placed:       return ArenaPlacement::Placed;
            case Placement::OccupiedByUs: return ArenaPlacement::HeldByProbe;
            case Placement::Refused:      break;
        }
        return ArenaPlacement::Refused;
    };
    // Captures the arena's bounds, because the ours-rule below is now THIS
    // arena's bounds rather than a file-scope constant pair.
    hooks.describe = [bottom, top](std::uint64_t base) {
        const RegionInfo region = describe_region(base);
        ArenaEntry entry;
        // `covers` alone is not enough, and that distinction cost four wrong
        // guesses in try_place(): macOS puts the commpage and the GPU carveout
        // in EVERY task's map, so a covering entry says nothing about who may
        // use it. Only an entry that grants no access, or a system reservation,
        // is address space the host will not give up - and only such an entry
        // may have its extent recorded as a limitation.
        const bool denies_everything =
            region.covers &&
            (region.reserved || region.protection == VM_PROT_NONE);
        // INSIDE THE ARENA, NO REFUSAL IS ATTRIBUTABLE - not just the ones with a
        // no-access entry covering them.
        //
        // The previous commit resolved the covered case and left three entries that
        // still moved between runs:
        //
        //   U [0x100000000, 0x100400000)   "exact placement refused inside the
        //   U [0x15b400000, 0x15b800000)    allocation arena"
        //   U [0x16bc00000, 0x16c000000)
        //
        // Those are refusals where `mach_vm_region` reported NOTHING covering the
        // window - and that combination is self-contradictory unless the task map
        // changed between the allocate and the query, which it does constantly,
        // because this probe allocates and releases thousands of windows while
        // walking. So they are races, and their addresses drift: 46 vs 56 entries
        // across two runs.
        //
        // `no_access_here_is_ours()` already says the host is not answerable for
        // what is in the way inside these bounds. This extends that to refusals with
        // no visible cause at all, which is the same claim with less evidence behind
        // it, not more. Everything unplaceable inside the arena is counted in the
        // note and recorded as nothing.
        // The arena's own bounds, not the file-scope constant pair. Same
        // claim, parameterised: inside THIS arena a refusal is not attributable
        // to the host, which is sound exactly while no platform band lies inside
        // it - the caller's responsibility, and why the low arena still stops at
        // the commpage and the high one starts 4 TiB below a measured ceiling.
        entry.covers = denies_everything || (base >= bottom && base < top);
        entry.start = region.start;
        entry.size = region.size;
        entry.text = describe_region_text(region);
        return entry;
    };

    const ArenaWalk walk = walk_arena(what, bottom, top, page_size, window, hooks);

    for (const auto& r : walk.available) outcome.available.push_back(r);
    for (const auto& r : walk.unavailable) outcome.unavailable.push_back(r);

    // The placed/held split is this task's own layout, so it goes to the notes -
    // outside the facts subtree and outside profile_id - rather than into any
    // recorded fact.
    outcome.occupied_notes.push_back(
        "allocation arena [" + json::to_hex(bottom) + ", " +
        json::to_hex(top) + ") walked in contiguous windows of " +
        json::to_hex(window) + " bytes: " + std::to_string(walk.placed) +
        " placed, " + std::to_string(walk.held_by_probe) +
        " already held by the probe process, " + std::to_string(walk.refused) +
        " structurally refused, " + std::to_string(walk.held_no_access) +
        " refused but covered by a no-access region of this task (treated as held, "
        "because nothing here distinguishes one of our own guard reservations from "
        "a platform band - " + band_note +
        "; if this number rises while unavailable_ranges stays empty, that "
        "assumption has broken), and " +
        std::to_string(walk.skipped) +
        " not probed because they lie inside an entry already described. Only the "
        "structurally refused count is a host limitation");
}

// BOTH ARENAS. `max_user_address` is measured before this runs and is 0 when the
// probe could not pin it down, in which case the high arena is skipped rather
// than placed from a guess - the whole point of `d6abf18` was that a ceiling
// nobody measured is not a ceiling.
void scan_allocation_arenas(std::uint64_t page_size, std::uint64_t probe_length,
                            std::uint64_t max_user_address,
                            ScanOutcome& outcome) {
    const std::uint64_t window =
        probe_length > kMinArenaWindow ? probe_length : kMinArenaWindow;

    scan_one_macos_arena(
        "allocation arena between the Mach-O __TEXT base and the Rosetta GPU "
        "carveout, where a NATIVE program's own code and heap are placed",
        kMachOTextBase, kArenaTop, page_size, window,
        "this arena stops below the commpage so that no platform band is inside "
        "it", outcome);

    // See `high_arena_floor()` in probe/arena_walk.hpp - there rather than here
    // for the reason `arena_ceiling_for()` is: arithmetic that decides where a
    // probe looks, on a platform no machine in this project can compile, is
    // arithmetic no test can reach. It returns 0 both when the ceiling was never
    // pinned down and when the arena would overlap the one below it.
    {
        const std::uint64_t high_bottom =
            high_arena_floor(max_user_address, kHighArenaSpan, kArenaTop);
        if (high_bottom != 0) {
            scan_one_macos_arena(
                "allocation arena in the top 4 TiB of the user address space, "
                "where a TRANSLATED x86-64 process places its heap - measured at "
                "0x7f9ab0028000 on the Rosetta lane, 140 TiB above the native "
                "arena",
                high_bottom, max_user_address, page_size, kHighArenaWindow,
                "this arena's floor is derived from the measured max_user_address "
                "and lies far above the commpage and the GPU carveout, the only "
                "platform bands this project has documented", outcome);
        }
    }
}

ScanOutcome scan_address_space(std::size_t page_size, std::uint64_t probe_length,
                               std::uint64_t min_address,
                               std::uint64_t max_user_address) {
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
        //
        // The min_address filter stays, and the argument for removing it was
        // wrong in a way only a measurement could show.
        //
        // The argument: skipping candidates below our own image makes the
        // candidate SET depend on where the loader put us, which under Rosetta
        // moves at every exec. That much is true. It was removed on that
        // reasoning, and the cross-process reproducibility gate failed on BOTH
        // lanes on the next run - including native arm64, which had been
        // byte-identical across two earlier runs.
        //
        // Why: filtering was not the only thing that depended on our layout,
        // it was the thing SUPPRESSING the dependency. Probe a low candidate
        // and the answer is "free" or "occupied" according to where this
        // process's heap and libraries happen to sit, and a free one is
        // recorded in available_ranges - a fact about our morning, hashed into
        // an id that is supposed to name the host. The filter cut those
        // candidates before they could be recorded.
        //
        // The residual risk the removal was aimed at is real and stays: if the
        // image ever slides across a power-of-two sample point, the candidate
        // set changes. It is now a risk the gate can SEE - which is a better
        // position than trading a narrow hazard for a broad one on an argument
        // that sounded right.
        if (min_address != 0 && base < min_address) continue;
        const auto range = AddressRange::from_base_size(base, probe_length);
        if (!range) continue;

        kern_return_t result = KERN_SUCCESS;
        if (mach_allocate_fixed(base, static_cast<std::size_t>(probe_length),
                                result)) {
            // See `ladder_record()` in probe/arena_walk.hpp. This note and the
            // held-path note below are the SAME STRING from the SAME CALL, and
            // that is the entire fix: they used to be two texts, chosen by
            // whether ASLR had put one of our mappings on the landmark, and the
            // note is inside the hashed facts subtree - so `profile_id`
            // alternated between exactly two values across every push for two
            // days while the bounds and the count both matched.
            ClassifiedRange cr;
            cr.range = *range;
            cr.evidence = EvidenceClass::MeasuredCapability;
            cr.note = ladder_record(ArenaPlacement::Placed,
                                    kMachPlacementCall, "").note;
            outcome.available.push_back(cr);
            mach_release(base, static_cast<std::size_t>(probe_length));
            continue;
        }

        const RegionInfo region = describe_region(base);
        // An entry that grants no access is not "ours" in any useful sense: no
        // program can map there, whoever nominally holds it.
        //
        // WITH ONE EXCEPTION, and it is this process's own __PAGEZERO. That is
        // also a VM_PROT_NONE entry, so the rule above would file it as a host
        // limitation - which is exactly the mistake defect 2 was about, walking
        // back in through a different door now that the candidate list is no
        // longer filtered against our load address.
        //
        // __PAGEZERO is the only no-access entry that starts at 0; the commpage
        // begins at 0xfc0000000 and the carveout at 0x1000000000. Its SIZE is a
        // link-time choice (-pagezero_size) and its top moves with ASLR, so
        // nothing about it describes the platform. Addresses inside it are not
        // available and not unavailable - they are unmeasurable from in here,
        // and the warning says so.
        const bool is_our_pagezero =
            region.covers && region.start == 0 &&
            region.protection == VM_PROT_NONE;
        // `!no_access_here_is_ours(base)` is the shared rule declared above, and
        // without it this branch widened a 12 GiB no-access region at 0x59e000000
        // to its full extent and published it as a host limitation - while the
        // arena, obeying the same rule, reported the same span available. Third
        // producer, same ambiguity. The reasoning that built this branch was sound
        // for the carveout, which is 384 GiB at a documented address and OUTSIDE
        // these bounds; it does not carry to an entry the process made itself.
        const bool entry_denies_everything =
            region.covers && !is_our_pagezero &&
            !no_access_here_is_ours(base) &&
            (region.reserved || region.protection == VM_PROT_NONE);
        if (is_our_pagezero) {
            outcome.occupied_notes.push_back(
                "range " + range->to_string() +
                " lies inside this process's own __PAGEZERO (" +
                describe_region_text(region) +
                "); its size is a link-time choice, so this says nothing about "
                "the platform and was NOT recorded as a host limitation");
            continue;
        }
        // INSIDE THE ARENA, NO LADDER REFUSAL IS ATTRIBUTABLE EITHER - and this
        // is the fourth time one rule has had to be carried from the arena to
        // the ladder.
        //
        // The arena already says this about itself, forty lines away: "refusals
        // where mach_vm_region reported NOTHING covering the window - and that
        // combination is self-contradictory unless the task map changed between
        // the allocate and the query, which it does constantly, because this
        // probe allocates and releases thousands of windows while walking. So
        // they are races." The arena treats everything unplaceable inside its
        // bounds as held. The LADDER, sampling the same band, did not.
        //
        // The Rosetta lane caught it on `97f40a6`, in run 3 of 5:
        //
        //   available_ranges:   25 vs 24   only in run 1: [0x200000000, 0x200400000)
        //   unavailable_ranges: 34 vs 35   only in run 2: [0x200000000, 0x200400000)
        //
        // One landmark, 8 GiB, INSIDE [kMachOTextBase, kArenaTop) - the band the
        // arena walks contiguously and establishes as available - flipping to a
        // host limitation on one run in five. The arena is the authority inside
        // its own bounds; a single sample must not contradict a contiguous walk
        // because a query lost a race with our own allocator.
        //
        // Outside the arena the ladder keeps its teeth: the commpage and the GPU
        // carveout are exactly what it exists to probe, and `no_access_here_is_
        // ours()` is false there.
        if (no_access_here_is_ours(base)) {
            ClassifiedRange ours;
            ours.range = *range;
            ours.evidence = EvidenceClass::MeasuredCapability;
            ours.note = ladder_record(ArenaPlacement::HeldByProbe,
                                      kMachPlacementCall, "").note;
            outcome.available.push_back(ours);
            outcome.occupied_notes.push_back(
                "landmark " + range->to_string() + " could not be placed (" +
                kern_error_name(result) + "), but it lies inside the allocation "
                "arena, which walks this band contiguously and establishes it. "
                "Recorded as available for the same reason the arena treats its "
                "own unplaceable windows as held: inside these bounds nothing "
                "distinguishes one of our own reservations, or a race with our "
                "own allocator, from a host limitation");
            continue;
        }

        if (result == KERN_NO_SPACE && region.covers &&
            !entry_denies_everything) {
            // A real mapping of ours: a property of one process layout, not of
            // the host, so it must not become a limitation other programs are
            // judged against.
            //
            // AND IT MUST BE RECORDED AS AVAILABLE, which is the same rule applied
            // in the other direction and the last thing that moved between runs.
            //
            // Recording nothing was still layout-dependent: a landmark that happened
            // to be free produced an available entry, and the same landmark with one
            // of our own mappings on it produced none. `available_ranges: 22 vs 20`,
            // two runs of one binary - the presence of a FACT depended on our
            // morning even though its content did not.
            //
            // The argument is the one EEXIST gets on Linux and
            // `no_access_here_is_ours()` gets above: an entry of ours that grants
            // access proves the kernel hands this address out, and proves nothing
            // about the host. Another program can map here. So the entry belongs in
            // `available_ranges` whether we happened to be sitting on it or not, and
            // the recorded set stops depending on where we were.
            // THE SENTENCE THAT USED TO BE HERE ENDED "Whether it was held or
            // free depends on the probe's own layout and is deliberately not
            // recorded" - and choosing it instead of the placed-path sentence
            // WAS that record. Self-refuting, hashed, and red on roughly every
            // second push for two days. One call, one string, both paths.
            ClassifiedRange ours;
            ours.range = *range;
            ours.evidence = EvidenceClass::MeasuredCapability;
            ours.note = ladder_record(ArenaPlacement::HeldByProbe,
                                      kMachPlacementCall, "").note;
            outcome.available.push_back(ours);
            outcome.occupied_notes.push_back(
                "range " + range->to_string() + " is a real mapping in the "
                "probe process (" + describe_region_text(region) +
                "); recorded as AVAILABLE, never as a host limitation");
            continue;
        }

        // KERN_INVALID_ADDRESS and friends are structural: the kernel refuses
        // this part of the address space regardless of what is mapped.
        ClassifiedRange cr;
        cr.range = *range;
        cr.evidence = EvidenceClass::MeasuredCapability;
        cr.note = ladder_record(ArenaPlacement::Refused, kMachPlacementCall,
                                kern_error_name(result) + "; " +
                                    describe_region_text(region))
                      .note;

        // Record how far the refusal actually reaches.
        //
        // The probe asked about a 4 MiB window; vm_region answered with the
        // bounds of the whole map entry, and on this platform that entry is
        // 384 GiB wide. Filing the window and discarding the entry turns the
        // kernel's own statement into a sampling artefact - and it is not a
        // harmless one. Every address in the carveout that this probe did not
        // happen to list came back UNKNOWN instead of UNSUPPORTED, on the one
        // question this project exists to answer.
        //
        // The widening is not extrapolation from the sample. vm_region reports
        // the entry's extent directly, and an entry with no access rights
        // refuses placement everywhere inside itself by construction. What the
        // note must not do is blur which part was probed and which part the
        // kernel described, so it says both.
        if (entry_denies_everything) {
            const std::uint64_t region_end = region.start + region.size;
            if (region.size > 0 && region_end > region.start) {
                cr.range.start = std::min(cr.range.start, region.start);
                cr.range.end = std::max(cr.range.end, region_end);
                cr.note += "; placement was probed at " + json::to_hex(base) +
                           " and the refusal is recorded across the entry's "
                           "full extent as vm_region reported it, not across "
                           "the probe window alone";
            }
        }
        outcome.unavailable.push_back(cr);
    }
    // After the landmark ladder, not instead of it. The ladder's single points
    // inside the commpage and the GPU carveout are the measurements the whole
    // macOS story in this project rests on; the arena answers the space between
    // them, which is where programs actually are.
    scan_allocation_arenas(page_size, probe_length, max_user_address,
                           outcome);
    collapse_contained_ranges(outcome.unavailable);
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
        // DELIBERATELY NOT RECORDED AS A HOST FACT.
        //
        // The search finds the lowest page this process can place, which is
        // where its own __PAGEZERO and image end. That is a property of how
        // this binary was linked and where the loader put it - not a platform
        // policy. Linux has a real one to read (/proc/sys/vm/mmap_min_addr);
        // macOS does not, and a binary search cannot manufacture it.
        //
        // Two CI runs of the same job on the same runner made this concrete:
        //
        //   run 1 (2d9ea6c)  min = 0x10a8f2000
        //   run 2 (fd649c7)  min = 0x10d841000
        //
        // ~48 MiB apart, because a translated image is slid by ASLR. Recorded
        // as measured_capability it was worse than useless: measured_capability
        // permits a PROVEN conclusion, the analyzer rejects requested addresses
        // below it, and so a contract asking for a low address was judged
        // against where this probe's own binary happened to land that morning.
        // It also made profile_id - which is supposed to identify a HOST -
        // change between two measurements of the same machine.
        //
        // Leaving it unknown makes rs-check answer UNKNOWN for questions that
        // depend on it. That is the true answer: nothing here measured the
        // platform's floor. The native lane is stable at 0x300000000 only
        // because __PAGEZERO has a fixed default size there; link with a
        // different -pagezero_size and it moves too, so the fact is
        // process-scoped in both lanes and stable in neither sense that counts.
        if (min_address != 0) {
            warnings.emplace_back(
                "the lowest placeable page in this process is " +
                json::to_hex(min_address) +
                ", which is where its own __PAGEZERO and image end. macOS has "
                "no equivalent of vm.mmap_min_addr to read, so min_map_address "
                "is left UNKNOWN rather than filled in with a property of the "
                "probe binary; under Rosetta this value moves between runs of "
                "the same job because the translated image is slid by ASLR");
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

        // THE CEILING, CARRYING ITS OWN EVIDENCE, because two explanations fit
        // the failure that provoked this and only a runner can separate them.
        //
        // On `2d058f7` the ground-truth case placed and wrote a mapping at
        // exactly the reported `max_user_address` (0x600000000000), so the value
        // was not the top of anything. Either (a) our own mappings sat there and
        // the bisection read that as the host refusing - now fixed by
        // `address_is_usable()` - or (b) `mach_vm_allocate(VM_FLAGS_FIXED)` is
        // refused there for a reason `mmap(MAP_FIXED)` is not, in which case the
        // two calls answer different questions and the probe is measuring the
        // wrong one.
        //
        // `mach_vm_region` at the ceiling tells them apart in one line: a region
        // of this task covering it is (a); nothing covering it is (b). Printing
        // it costs one call and saves the round trip that would otherwise be
        // spent guessing - the sequence that has worked every time this week.
        if (max_address != 0) {
            const RegionInfo at_ceiling = describe_region(max_address);
            warnings.emplace_back(
                "max_user_address resolved to " + json::to_hex(max_address) +
                " from a highest placed probe point of " +
                json::to_hex(survey.highest_placed) + "; mach_vm_region at the "
                "ceiling says: " + describe_region_text(at_ceiling) +
                ". A region of this task covering that address would mean the "
                "ceiling is our own layout rather than the host's, which is the "
                "defect this line exists to expose");
        }

        if (const std::uint64_t biggest = find_max_single_reservation();
            biggest != 0) {
            profile.vm.max_single_reservation = Fact<std::uint64_t>::known(
                biggest, EvidenceClass::MeasuredCapability,
                std::string(kSourceProbe) +
                    ": largest power-of-two PROT_NONE MAP_NORESERVE reservation "
                    "the kernel granted");
        } else {
            warnings.emplace_back(
                "no power-of-two reservation from 1 MiB upward was granted, so "
                "max_single_reservation was left unknown rather than recorded as "
                "zero");
        }

        // And the same question asked high in the space, which macOS is expected
        // to answer identically because it documents no DEFAULT_MAP_WINDOW.
        // Measured rather than assumed - see the probe's own comment.
        if (const std::uint64_t hinted = find_max_single_reservation_hinted();
            hinted != 0) {
            profile.vm.max_single_reservation_hinted =
                Fact<std::uint64_t>::known(
                    hinted, EvidenceClass::MeasuredCapability,
                    std::string(kSourceProbe) +
                        ": largest power-of-two PROT_NONE MAP_NORESERVE "
                        "reservation granted for a request hinted at " +
                        json::to_hex(kHighHint));
            if (profile.vm.max_single_reservation.is_known() &&
                profile.vm.max_single_reservation.value() != hinted) {
                warnings.emplace_back(
                    "a hint high in the address space changes what this host "
                    "will reserve: " +
                    json::to_hex(profile.vm.max_single_reservation.value()) +
                    " hintless vs " + json::to_hex(hinted) +
                    " hinted. macOS documents no window that would explain "
                    "that, so it is a finding rather than a known platform "
                    "behaviour, and max_single_reservation is the narrower of "
                    "the two claims");
            }
        }
        // Structural refusals BELOW the highest placeable address are holes,
        // not the ceiling. Recording them is the whole point on this platform -
        // but only the ones that are really the host's.
        //
        // THE SAME AMBIGUITY AS THE ARENA'S, INHERITED THROUGH try_place().
        //
        // A survey refusal means `try_place` said Refused at the candidate and at
        // the next fifteen pages. That happens when a no-access region covers them
        // - and a macOS process is full of its own PROT_NONE reservations. The
        // runner showed exactly this:
        //
        //   available   [0x169c00000, 0xfc0000000)   the arena, placing 4 MiB
        //                                            windows across 0x200000000
        //   unavailable [0x200000000, 0x200004000)   "a hole in the address space"
        //
        // Both from one profile. The arena placed a 4 MiB window over the same
        // page the survey had called a host limitation - so the note's inference,
        // "higher addresses remained placeable, so this is a hole", was the
        // unjustified step: a one-page refusal while higher addresses work is
        // equally consistent with our own reservation sitting there.
        //
        // Nothing is lost by declining to record it. The commpage and the GPU
        // carveout are no-access bands AND genuine host limitations, and
        // `scan_address_space` records them at their documented addresses across
        // their real extents; a 16 KiB duplicate inside one of them was already
        // being dropped by collapse_contained_ranges.
        std::size_t holes_recorded = 0, holes_ours = 0, holes_below_min = 0;
        for (std::uint64_t refused : survey.refused) {
            if (refused >= survey.highest_placed) continue;
            // NOTHING BELOW THE LOWEST PLACEABLE PAGE IS A HOST FACT, and the
            // profile published by the previous commit is why this is here:
            //
            //   U 0x100000 0x104000  "a hole in the address space"
            //   U 0x200000 0x204000  ... and 0x400000, 0x800000, 0x1000000 ...
            //
            // Every one of those is inside this process's own __PAGEZERO, which on
            // a 64-bit Mach-O is four GiB and whose size is a LINK-TIME CHOICE.
            // `mach_vm_region` reports no entry covering them - __PAGEZERO is a
            // hole in the map rather than a no-access entry - so the ownership
            // check above cannot see them, and they were recorded as structural.
            //
            // Twenty lines up, this file already refuses to publish
            // `min_map_address` as a host fact for exactly this reason: "a property
            // of how this binary was linked and where the loader put it". Recording
            // holes below it is the same claim through a different door - the
            // fourth time today that a fact about the probe reached for a field
            // meant for the host.
            if (min_address != 0 && refused < min_address) {
                ++holes_below_min;
                continue;
            }
            const auto hole = AddressRange::from_base_size(
                refused, static_cast<std::uint64_t>(page_size));
            if (!hole) continue;
            const RegionInfo region = describe_region(refused);
            if (region.covers &&
                (region.reserved || region.protection == VM_PROT_NONE)) {
                ++holes_ours;
                continue;
            }
            ClassifiedRange cr;
            cr.range = *hole;
            cr.evidence = EvidenceClass::MeasuredCapability;
            cr.note = "the kernel refused an exact placement here while higher "
                      "addresses remained placeable, and no region of this task "
                      "covers it, so this is a hole in the address space rather "
                      "than its end or a reservation of our own";
            profile.vm.unavailable_ranges.push_back(cr);
            ++holes_recorded;
        }
        if (holes_ours != 0 || holes_below_min != 0) {
            warnings.emplace_back(
                "survey: " + std::to_string(holes_recorded) +
                " structural hole(s) recorded; " + std::to_string(holes_ours) +
                " refusal(s) not recorded because a no-access region of this task "
                "covers them, and " + std::to_string(holes_below_min) +
                " not recorded because they lie below the lowest page this process "
                "can place, which is where its own __PAGEZERO and image end - a "
                "link-time choice, not a platform property. Both classes moved "
                "between runs, which is what made profile_id irreproducible");
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
            scan_address_space(page_size, probe_length, min_address,
                               max_address);
        profile.vm.available_ranges = std::move(scan.available);
        for (auto& r : scan.unavailable) {
            profile.vm.unavailable_ranges.push_back(std::move(r));
        }
        for (auto& note : scan.occupied_notes) {
            warnings.push_back(std::move(note));
        }
    }
    // The survey ladder and the scan both file refusals, and the ladder's
    // single-page holes sit inside entries the scan measured at full width.
    // Collapse across both sources, not just within the scan.
    collapse_contained_ranges(profile.vm.unavailable_ranges);

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

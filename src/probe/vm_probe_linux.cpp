// SPDX-License-Identifier: Apache-2.0
//
// Linux x86-64 / aarch64 virtual-memory probe.
#include "runtimeskeptic/probe/vm_probe.hpp"

#if defined(RS_PLATFORM_LINUX)

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include "runtimeskeptic/core/sha256.hpp"
#include "runtimeskeptic/probe/arena_walk.hpp"

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

namespace rs::probe {
namespace {

using rs::Address;
using vm::AddressRange;
using vm::ClassifiedRange;
using vm::collapse_contained_ranges;

constexpr const char* kSourceProbe = "rs-env-probe vm (linux)";

std::string errno_name(int e) {
    switch (e) {
        case EEXIST: return "EEXIST";
        case EINVAL: return "EINVAL";
        case ENOMEM: return "ENOMEM";
        case EACCES: return "EACCES";
        case EPERM: return "EPERM";
        case EAGAIN: return "EAGAIN";
        case EBADF: return "EBADF";
        case ENODEV: return "ENODEV";
        case EOVERFLOW: return "EOVERFLOW";
        default: return "errno " + std::to_string(e);
    }
}

std::string read_small_file(const char* path) {
    int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return {};
    char buffer[256];
    const ssize_t n = ::read(fd, buffer, sizeof(buffer) - 1);
    ::close(fd);
    if (n <= 0) return {};
    buffer[n] = '\0';
    std::string s(buffer);
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s;
}

// A single mmap attempt that always cleans up after itself.
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
// MAP_FIXED_NOREPLACE detection.
//
// Kernels older than 4.17 do not know the flag. They ignore the unknown bit
// and treat the call as an ordinary hinted mapping, which means a request at
// an occupied address SUCCEEDS somewhere else instead of failing. Using the
// flag without this check would make every "exact placement worked" answer
// unreliable, so the probe establishes it first and refuses to use exact
// placement facts at all if the flag is not honoured.
// -------------------------------------------------------------------------
enum class NoReplaceSupport { Yes, No, Undetermined };

NoReplaceSupport detect_fixed_noreplace(std::size_t page_size,
                                        std::vector<std::string>& warnings) {
    // Occupy a page, then ask for exactly that page with MAP_FIXED_NOREPLACE.
    MapAttempt occupied = try_map(nullptr, page_size, PROT_READ,
                                  MAP_PRIVATE | MAP_ANONYMOUS);
    if (!occupied.ok()) {
        warnings.emplace_back(
            "could not create a reference mapping while detecting "
            "MAP_FIXED_NOREPLACE: " + errno_name(occupied.error));
        return NoReplaceSupport::Undetermined;
    }

    MapAttempt collide =
        try_map(occupied.address, page_size, PROT_READ,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE);

    NoReplaceSupport support = NoReplaceSupport::Undetermined;
    if (!collide.ok()) {
        // EEXIST is the documented refusal. Any other refusal is ambiguous, so
        // we do not claim support.
        support = collide.error == EEXIST ? NoReplaceSupport::Yes
                                          : NoReplaceSupport::Undetermined;
        if (collide.error != EEXIST) {
            warnings.emplace_back(
                "MAP_FIXED_NOREPLACE collision test failed with " +
                errno_name(collide.error) + " instead of EEXIST; exact-placement "
                "facts were left unknown");
        }
    } else if (collide.address == occupied.address) {
        // It replaced the mapping. That is the destructive MAP_FIXED behavior
        // and must never happen with NOREPLACE.
        support = NoReplaceSupport::Undetermined;
        warnings.emplace_back(
            "MAP_FIXED_NOREPLACE replaced an existing mapping; the flag is not "
            "behaving as documented and exact-placement probing was disabled");
        // The original mapping is gone; the new one covers the same address.
        unmap(collide, page_size);
        return support;
    } else {
        // Silently relocated: the kernel ignored the flag.
        support = NoReplaceSupport::No;
        unmap(collide, page_size);
    }

    unmap(occupied, page_size);
    return support;
}

// -------------------------------------------------------------------------
// Highest usable user address, by binary search.
//
// Only ever attempts one page, and only with MAP_FIXED_NOREPLACE, so the
// search cannot disturb the process.
// -------------------------------------------------------------------------
bool can_map_exactly_at(std::uint64_t address, std::size_t length) {
    MapAttempt attempt =
        try_map(reinterpret_cast<void*>(address), length, PROT_NONE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE | MAP_NORESERVE);
    if (!attempt.ok()) return false;
    const bool exact = reinterpret_cast<std::uint64_t>(attempt.address) == address;
    unmap(attempt, length);
    return exact;
}

// The largest single reservation the kernel actually grants, as a power of two.
//
// This exists because `RS-VM-0021` had nothing to compare a size against except
// the width of the address space, and asserted SUPPORTED whenever a request fitted
// - its own rejected-fix text saying "the limit is the width of the address space,
// not the amount of free memory in it". A 5-level-paging runner disproved that in
// one line: 4 PiB fits below a 56-bit `max_user_address` and the kernel refused it
// with ENOMEM. Fitting is necessary, not sufficient.
//
// A POWER OF TWO ON PURPOSE. The exact largest reservation moves between two runs
// of one binary as the process's own mappings shift, and a fact that moves is a
// fact about the probe rather than the host - `check_reproducible.sh` would fail
// and `profile_id` would stop naming anything. Powers of two are stable across
// runs, and the only question asked of this fact is whether a request of a given
// order is plausible here at all.
//
// Ascending, not bisecting: the answer is not monotone in the way a bisection
// needs. A reservation can fail at 2^k and succeed at 2^(k+1) if the kernel
// happens to find a differently-shaped hole, so the loop keeps the LARGEST success
// rather than stopping at the first failure - the same mistake `find_max_user_address`
// documents having made once.
//
// PROT_NONE and MAP_NORESERVE, matching `tests/groundtruth/cases/
// oversized_reservation.c`, so this is a question about address space and not
// about swap.
std::uint64_t find_max_single_reservation() {
    std::uint64_t largest = 0;
    for (unsigned bit = 20; bit < 63; ++bit) {   // from 1 MiB
        const std::uint64_t size = std::uint64_t{1} << bit;
        MapAttempt attempt =
            try_map(nullptr, static_cast<std::size_t>(size), PROT_NONE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE);
        if (attempt.ok()) {
            largest = size;
            unmap(attempt, static_cast<std::size_t>(size));
        }
    }
    return largest;
}

// THE SAME MEASUREMENT, ASKED SOMEWHERE ELSE, and the reason is that the one
// above is labelled as more than it measures.
//
// `nullptr` is not a neutral place to ask on x86-64 Linux. `find_start_end()` in
// arch/x86/kernel/sys_x86_64.c widens the search to the full address space only
// when `addr > DEFAULT_MAP_WINDOW`, and
// Documentation/arch/x86/x86_64/5level-paging.rst says so outright:
//
//   "the kernel will not allocate memory above 47-bit by default ... an
//    application has to specify mmap hint address above 47-bit to opt in"
//
// So on a 5-level-paging host the hintless probe stops at 128 TiB while a caller
// passing a high hint can be granted far more, and `max_single_reservation`
// quietly means "the largest grant inside the default window" - while its own
// comment claims "the largest the kernel actually grants".
//
// This asks the second question. On a 4-level host `DEFAULT_MAP_WINDOW` IS the
// top of the space, so the hint is above everything, mmap ignores it as advisory,
// and the two numbers agree. THAT AGREEMENT IS THE EVIDENCE - every ordinary
// runner publishes a matching pair, and the first LA57 host to come along
// publishes the difference with nobody present to ask.
//
// The hint is advisory (no MAP_FIXED), so this can never fail because of where it
// asked: a kernel that dislikes the address relocates and the measurement still
// happens.
constexpr std::uint64_t kAboveDefaultMapWindow = std::uint64_t{1} << 47;

std::uint64_t find_max_single_reservation_hinted() {
    std::uint64_t largest = 0;
    for (unsigned bit = 20; bit < 63; ++bit) {   // from 1 MiB, as above
        const std::uint64_t size = std::uint64_t{1} << bit;
        MapAttempt attempt =
            try_map(reinterpret_cast<void*>(kAboveDefaultMapWindow),
                    static_cast<std::size_t>(size), PROT_NONE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE);
        if (attempt.ok()) {
            largest = size;
            unmap(attempt, static_cast<std::size_t>(size));
        }
    }
    return largest;
}

std::uint64_t find_max_user_address(std::size_t page_size) {
    // Lower bound: an address we know works. Upper bound: one we know fails.
    // Do NOT stop at the first failure. Linux user space happens to be one
    // contiguous run today, so stopping early is harmless here - but the same
    // logic on macOS reported the bottom of the first hole as the top of the
    // address space, and everything above the false ceiling went untested.
    // The address space is a set, not an interval; probe to the top and keep
    // the highest address that actually worked.
    std::uint64_t low = 0;
    for (unsigned bit = 20; bit < 63; ++bit) {
        const std::uint64_t candidate = std::uint64_t{1} << bit;
        if (can_map_exactly_at(candidate, page_size)) low = candidate;
    }
    if (low == 0) return 0;
    // The bisection below is only meaningful if the next power of two really
    // is unavailable; otherwise the search would run off the top.
    if (low > (UINT64_MAX / 2)) return low + page_size;
    if (can_map_exactly_at(low * 2, page_size)) return 0;

    std::uint64_t high = low * 2;
    // Binary search for the first page that cannot be mapped.
    while (high - low > page_size) {
        const std::uint64_t mid = low + ((high - low) / 2 / page_size) * page_size;
        if (mid == low) break;
        if (can_map_exactly_at(mid, page_size)) {
            low = mid;
        } else {
            high = mid;
        }
    }
    // `low` is the last page that mapped successfully; the exclusive end of the
    // usable space is one page above it.
    return low + page_size;
}

// -------------------------------------------------------------------------
// Does a hint relocate?
// -------------------------------------------------------------------------
enum class RelocationObservation { Relocated, Honoured, Undetermined };

RelocationObservation observe_hint_relocation(std::size_t page_size) {
    MapAttempt occupied = try_map(nullptr, page_size, PROT_READ,
                                  MAP_PRIVATE | MAP_ANONYMOUS);
    if (!occupied.ok()) return RelocationObservation::Undetermined;

    // Ask for the occupied address as a plain hint (no MAP_FIXED* flags).
    MapAttempt hinted = try_map(occupied.address, page_size, PROT_READ,
                                MAP_PRIVATE | MAP_ANONYMOUS);
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
// Protection experiments.
// -------------------------------------------------------------------------
struct ProtectionObservations {
    bool tested_rwx = false;
    bool rwx_allowed = false;
    bool tested_rx = false;
    bool rx_allowed = false;
    bool tested_flip = false;
    bool flip_allowed = false;
};

ProtectionObservations probe_protection(std::size_t page_size) {
    ProtectionObservations obs;

    MapAttempt rwx = try_map(nullptr, page_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                             MAP_PRIVATE | MAP_ANONYMOUS);
    obs.tested_rwx = true;
    obs.rwx_allowed = rwx.ok();
    unmap(rwx, page_size);

    MapAttempt rx = try_map(nullptr, page_size, PROT_READ | PROT_EXEC,
                            MAP_PRIVATE | MAP_ANONYMOUS);
    obs.tested_rx = true;
    obs.rx_allowed = rx.ok();
    unmap(rx, page_size);

    MapAttempt rw = try_map(nullptr, page_size, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS);
    if (rw.ok()) {
        obs.tested_flip = true;
        obs.flip_allowed =
            ::mprotect(rw.address, page_size, PROT_READ | PROT_EXEC) == 0;
        unmap(rw, page_size);
    }
    return obs;
}

// -------------------------------------------------------------------------
// Lazy reservation: can a large range be reserved without backing it?
// -------------------------------------------------------------------------
enum class ReservationObservation { Lazy, Eager, Undetermined };

ReservationObservation probe_lazy_reservation() {
    constexpr std::size_t kOneGiB = 1024ull * 1024 * 1024;
    MapAttempt reserved = try_map(nullptr, kOneGiB, PROT_NONE,
                                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE);
    if (!reserved.ok()) return ReservationObservation::Undetermined;
    unmap(reserved, kOneGiB);
    return ReservationObservation::Lazy;
}

// -------------------------------------------------------------------------
// Faulting test: mapping past end of file. Runs in a forked child.
// -------------------------------------------------------------------------
enum class EofObservation { Sigbus, ZeroFill, MapRefused, Undetermined };

EofObservation probe_beyond_eof(std::size_t page_size, unsigned timeout_seconds,
                                std::vector<std::string>& warnings) {
    const char* tmpdir = ::getenv("TMPDIR");
    std::string path = (tmpdir != nullptr && tmpdir[0] != '\0') ? tmpdir : "/tmp";
    path += "/rs-env-probe-XXXXXX";
    std::vector<char> templ(path.begin(), path.end());
    templ.push_back('\0');

    const int fd = ::mkstemp(templ.data());
    if (fd < 0) {
        warnings.emplace_back("could not create a temporary file for the "
                              "beyond-EOF test");
        return EofObservation::Undetermined;
    }
    ::unlink(templ.data());  // the file lives only as long as the descriptor

    // One page of file, two pages of mapping: the second page lies entirely
    // past end of file.
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
    // The parent never touches the second page; only the child does.
    unmap(probe_map, map_length);

    ::fflush(nullptr);
    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(fd);
        warnings.emplace_back("fork failed; the beyond-EOF test was skipped");
        return EofObservation::Undetermined;
    }

    if (pid == 0) {
        // Child. Any fault here is the observation we want; it must not be
        // caught or reported through the parent's machinery.
        ::alarm(timeout_seconds);
        void* p = ::mmap(nullptr, map_length, PROT_READ, MAP_SHARED, fd, 0);
        if (p == MAP_FAILED) ::_exit(20);
        volatile const unsigned char* bytes =
            static_cast<volatile const unsigned char*>(p);
        // Touch a byte on the page that lies entirely past end of file.
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
// -------------------------------------------------------------------------
struct ScanOutcome {
    std::vector<ClassifiedRange> available;
    std::vector<ClassifiedRange> unavailable;
    std::vector<std::string> occupied_notes;
};

// -------------------------------------------------------------------------
// The allocation arena: where the kernel actually puts things.
//
// The ladder below climbs powers of two plus four hand-picked landmarks, every
// one of them a plausible EMULATOR base, because this probe was built for the
// shadPS4 question. Measuring the false-positive rate showed what that cost:
// 13 real programs made 639 MAP_FIXED requests on this host and 637 landed in
// a range the probe had established nothing about, so the analyzer answered
// UNKNOWN 99.7% of the time. Correct, and useless.
//
//   probe established 56 windows x 4 MiB  =  224 MiB of a 128 TiB space
//   observed addresses:  629 in 0x7f...,  7 in 0x7e...,  3 low
//   inside a probe window:  2 of 639
//
// Everything real lands near `mmap_base`, which the kernel places below the
// stack and randomizes within `mmap_rnd_bits` (28 on x86-64: a 1 TiB span).
// This samples that arena.
//
// THE TRAP, AND IT HAS BEEN SPRUNG ON THIS PROJECT BEFORE. `min_map_address`
// was once the probe's own ASLR slide recorded as a host fact, and six
// campaign contracts returned a confident UNSUPPORTED off it. Anything derived
// from where THIS process happens to sit must not reach the profile. So:
//
//   1. The arena's bounds come from `max_user_address` - a kernel constant,
//      measured, identical in every process - and NEVER from /proc/self/maps.
//   2. A sample returning EEXIST counts exactly as a sample that succeeds.
//      EEXIST means the address is already held BY US: proof that the kernel
//      hands this part of the space out, and proof of nothing about the host.
//      Treating it as a distinct outcome would make the recorded set depend on
//      where our own libc landed - the very dependency rule 1 prevents.
//
// A multi-page placement failure is ambiguous: ENOMEM can describe the
// requested size (not the address), and EEXIST proves only that some part of a
// request overlaps a VMA. The adaptive walk below subdivides both outcomes and
// records a limitation only after an exact page placement is refused.
constexpr std::uint64_t kTiB = 1ull << 40;
constexpr std::uint64_t kArenaMaxWindow = 1ull << 30;
constexpr std::size_t kArenaAttemptBudget = 262144;

void scan_one_arena(const char* what, std::uint64_t bottom, std::uint64_t top,
                    std::uint64_t page_size, std::uint64_t max_window_size,
                    ScanOutcome& outcome) {
    std::string refusal_text;
    ArenaProbe platform_probe;
    platform_probe.place = [&](std::uint64_t base, std::uint64_t size) {
        MapAttempt attempt = try_map(
            reinterpret_cast<void*>(base), static_cast<std::size_t>(size),
            PROT_NONE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE | MAP_NORESERVE);
        if (attempt.ok()) {
            const bool exact =
                reinterpret_cast<std::uint64_t>(attempt.address) == base;
            unmap(attempt, static_cast<std::size_t>(size));
            if (exact) return ArenaPlacement::Placed;
            refusal_text = "mmap(MAP_FIXED_NOREPLACE) relocated the request";
            return ArenaPlacement::Refused;
        }
        if (attempt.error == EEXIST) return ArenaPlacement::HeldByProbe;
        refusal_text = "mmap(MAP_FIXED_NOREPLACE) failed with " +
                       errno_name(attempt.error);
        return ArenaPlacement::Refused;
    };
    platform_probe.describe = [&](std::uint64_t) {
        ArenaEntry entry;
        entry.text = refusal_text;
        return entry;
    };

    ArenaWalk walk = walk_arena_adaptive(
        what, bottom, top, page_size, max_window_size, kArenaAttemptBudget,
        platform_probe);
    outcome.available.insert(outcome.available.end(), walk.available.begin(),
                             walk.available.end());
    outcome.unavailable.insert(outcome.unavailable.end(),
                               walk.unavailable.begin(),
                               walk.unavailable.end());

    // Counts may move with this process's ASLR layout, so they stay outside the
    // facts subtree and therefore outside profile_id.
    std::string note =
        std::string(what) + " [" + json::to_hex(bottom) + ", " +
        json::to_hex(top) + ") walked with adaptive windows no larger than " +
        json::to_hex(max_window_size) + " bytes: " +
        std::to_string(walk.attempts) + " placement attempts, " +
        std::to_string(walk.placed) + " exact placements granted, " +
        std::to_string(walk.held_by_probe) +
        " pages already held by this process, " +
        std::to_string(walk.held_no_access) +
        " no-access pages treated as held, " +
        std::to_string(walk.refused) +
        " exact pages structurally refused. Multi-page refusals and collisions "
        "were subdivided and never generalized";
    if (walk.budget_exhausted) {
        note += "; the placement-attempt budget was exhausted, so the unvisited "
                "remainder was deliberately left unknown";
    }
    outcome.occupied_notes.push_back(std::move(note));
}

// The allocation corridor a Linux process is actually made of, derived from
// TASK_SIZE and neither endpoint from this process's own layout.
//
//   lower endpoint   ELF_ET_DYN_BASE = TASK_SIZE / 3 * 2, where the kernel
//                    puts a position-independent executable's text
//   upper endpoint   the default map-window ceiling, below which the loader
//                    puts shared libraries and anonymous mappings
//
// One continuous corridor is required because mmap_rnd_bits is runtime-tunable:
// WSL2 and hardened hosts can place anonymous mappings between the older fixed
// ET_DYN and mmap arenas. Initial tiles are capped at 1 GiB and at the largest
// reservation this probe actually measured. Adaptive subdivision makes a size
// limit or one occupied page local; a hard attempt budget leaves any unvisited
// remainder unknown rather than risking unbounded work.
void scan_allocation_arenas(std::uint64_t page_size,
                            std::uint64_t max_user_address,
                            std::uint64_t max_window_size,
                            ScanOutcome& outcome) {
    if (page_size == 0 || max_window_size == 0 ||
        max_user_address <= max_window_size) {
        return;
    }

    const std::uint64_t ceiling = arena_ceiling_for(max_user_address, kTiB);
    const std::uint64_t raw_top =
        max_user_address < ceiling ? max_user_address : ceiling;
    const std::uint64_t probe_top = (raw_top / page_size) * page_size;
    const std::uint64_t dyn_base = (ceiling / 3) * 2;
    const std::uint64_t dyn_bottom = (dyn_base / kTiB) * kTiB;
    if (dyn_bottom > 0 && dyn_bottom < probe_top) {
        scan_one_arena(
            "Linux default allocation corridor from ET_DYN base through the "
            "kernel's mmap arena",
            dyn_bottom, probe_top, page_size, max_window_size, outcome);
    }
}

ScanOutcome scan_address_space(std::size_t page_size, std::uint64_t probe_length,
                               std::uint64_t max_user_address,
                               std::uint64_t arena_window_size) {
    ScanOutcome outcome;

    std::vector<std::uint64_t> candidates;
    for (unsigned bit = 16; bit < 63; ++bit) {
        const std::uint64_t boundary = std::uint64_t{1} << bit;
        candidates.push_back(boundary);
        // Also sample JUST BELOW each boundary. The ladder used to climb only
        // powers of two, and it systematically missed the addresses that
        // emulators actually use: guard pages, commpages and rollover
        // barriers are placed just under a boundary, never on it. QEMU's ARM
        // commpage at 0xffff0f00 and Box64's 4 GiB rollover guard both sit in
        // that blind spot, so both came back UNKNOWN for no better reason
        // than where the probe happened to look.
        if (boundary > probe_length) {
            const std::uint64_t below = boundary - probe_length;
            if (below % page_size == 0) candidates.push_back(below);
        }
    }
    // The ROADMAP's motivating address, and the band around it.
    candidates.push_back(0x1000000000ull);
    candidates.push_back(0x4000000000ull);
    candidates.push_back(0x6fffff0000ull);
    candidates.push_back(0x7fff00000000ull);

    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()),
                     candidates.end());

    for (std::uint64_t base : candidates) {
        if (base % page_size != 0) continue;
        if (max_user_address != 0 && base >= max_user_address) {
            // Already covered by the max_user_address fact; probing here would
            // only restate it.
            continue;
        }
        // ...and neither may the window's FAR END cross it, which is the half of
        // that rule that was missing.
        //
        // The candidate at 0x7fffffc00000 sits below max_user_address
        // (0x7ffffffff000), so it survived the check above - and a 4 MiB window
        // there ends at 0x800000000000, past the top of the space. The kernel
        // refused it with ENOMEM and the probe recorded
        // [0x7fffffc00000, 0x800000000000) as a host limitation.
        //
        // That refusal is an artefact of where the window was put, not a fact
        // about the address: once the arena began probing a window that ENDS at
        // max_user_address, it placed 0x7ffffbfff000 successfully, and the profile
        // asserted one range both available and unavailable. Caught by
        // `available_and_unavailable_ranges_do_not_overlap`, locally this time.
        //
        // The part genuinely beyond the top is what `max_user_address` already
        // says, so nothing is lost by declining to restate it.
        if (max_user_address != 0 && base + probe_length > max_user_address) {
            continue;
        }
        const auto range = AddressRange::from_base_size(base, probe_length);
        if (!range) continue;

        MapAttempt attempt = try_map(
            reinterpret_cast<void*>(base), static_cast<std::size_t>(probe_length),
            PROT_NONE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE | MAP_NORESERVE);

        // THE LADDER'S DECISION IS NOT MADE HERE. See `ladder_record()` in
        // probe/arena_walk.hpp: a landmark we placed and a landmark we already
        // held record the IDENTICAL entry, because both prove the kernel hands
        // this exact address out and neither says anything about the host.
        //
        // What this code used to do was record the free case and record NOTHING
        // for EEXIST, so the presence of the fact moved with our own ASLR slide.
        // That is the defect the macOS probe had already fixed - its comment
        // cites "the argument EEXIST gets on Linux" - and that this file's own
        // ARENA applies correctly forty lines above. It never went red, which is
        // not the same as being right.
        ArenaPlacement placement = ArenaPlacement::Refused;
        if (attempt.ok()) {
            placement = reinterpret_cast<std::uint64_t>(attempt.address) == base
                            ? ArenaPlacement::Placed
                            : ArenaPlacement::Refused;
            unmap(attempt, static_cast<std::size_t>(probe_length));
            // A relocation is not a refusal of THIS address and not a grant of
            // it either: the kernel answered a different question. Recording
            // nothing here does not depend on our layout, so the rule above is
            // untouched.
            if (placement == ArenaPlacement::Refused) continue;
        } else if (attempt.error == EEXIST) {
            placement = ArenaPlacement::HeldByProbe;
        }

        if (placement != ArenaPlacement::Refused) {
            const LadderRecord rec = ladder_record(
                placement, "mmap(MAP_FIXED_NOREPLACE)", "");
            ClassifiedRange cr;
            cr.range = *range;
            cr.evidence = EvidenceClass::MeasuredCapability;
            cr.note = rec.note;
            outcome.available.push_back(cr);
            continue;
        }

        // EINVAL / ENOMEM / EPERM at a specific address are structural: the
        // kernel refuses this part of the address space regardless of what is
        // already mapped.
        //
        // Unlike the macOS probe, this records the probe window and does not
        // widen it to the extent of a containing entry. That is not an
        // oversight and not a gap waiting to be filled the same way: the macOS
        // widening is sound only because mach_vm_region reports the bounds of
        // an entry that exists and denies access. A structural refusal on
        // Linux is refused precisely because nothing is mapped there, so
        // /proc/self/maps has nothing to say about how far it reaches. There
        // is no measurement to widen to, and inventing one from the sample is
        // the mistake this comment exists to prevent.
        const LadderRecord rec = ladder_record(
            ArenaPlacement::Refused, "mmap(MAP_FIXED_NOREPLACE)",
            errno_name(attempt.error));
        ClassifiedRange cr;
        cr.range = *range;
        cr.evidence = EvidenceClass::MeasuredCapability;
        cr.note = rec.note;
        outcome.unavailable.push_back(cr);
    }

    scan_allocation_arenas(page_size, max_user_address, arena_window_size,
                            outcome);

    collapse_contained_ranges(outcome.unavailable);
    collapse_contained_ranges(outcome.available);
    return outcome;
}

std::string utc_timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &now);
#else
    ::gmtime_r(&now, &tm_buf);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return buffer;
}

vm::Architecture architecture_from_machine(const char* machine) {
    if (machine == nullptr) return vm::Architecture::Unknown;
    const std::string m(machine);
    if (m == "x86_64" || m == "amd64") return vm::Architecture::X86_64;
    if (m == "aarch64" || m == "arm64") return vm::Architecture::Aarch64;
    if (m == "i386" || m == "i686") return vm::Architecture::X86;
    if (m.rfind("arm", 0) == 0) return vm::Architecture::Arm;
    return vm::Architecture::Other;
}

}  // namespace

std::string probe_platform_name() { return "linux"; }

Result probe_virtual_memory(const Options& options) {
    Result result;
    result.implemented = true;

    vm::EnvironmentProfile& profile = result.profile;
    std::vector<std::string>& warnings = profile.run.warnings;

    profile.origin = vm::ProfileOrigin::Measured;
    profile.run.probe_version = kProbeVersion;

    // -- platform ---------------------------------------------------------
    profile.platform.os = vm::OperatingSystem::Linux;
    utsname uts{};
    if (::uname(&uts) == 0) {
        profile.platform.kernel_version = uts.release;
        profile.platform.os_version = std::string(uts.sysname) + " " + uts.release;
        profile.platform.host_arch = architecture_from_machine(uts.machine);
    }
    // The probe is the process being measured, so process width is known
    // exactly and does not need uname.
    profile.platform.process_arch =
        sizeof(void*) == 8
            ? (profile.platform.host_arch == vm::Architecture::Aarch64
                   ? vm::Architecture::Aarch64
                   : vm::Architecture::X86_64)
            : vm::Architecture::X86;
    // Detecting emulated execution (qemu-user and friends) is not attempted in
    // v0.1, so translation mode stays unknown rather than being guessed as
    // "none".
    profile.platform.translation_mode = vm::TranslationMode::Unknown;
    warnings.emplace_back(
        "translation_mode was not probed in v0.1 and is reported as unknown "
        "rather than assumed to be 'none'");

    // -- page size and granularity ----------------------------------------
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

    // On Linux, mmap placement granularity equals the page size. This is a
    // property of the mmap interface rather than a separate measurement, so
    // it is recorded as a specified guarantee with an explicit source.
    profile.vm.allocation_granularity = Fact<std::uint64_t>::known(
        static_cast<std::uint64_t>(page_size), EvidenceClass::SpecifiedGuarantee,
        "Linux mmap(2): addr is rounded to page granularity");

    // -- can we map anything at all? --------------------------------------
    // The most basic capability, and the one a profile must carry before any
    // request may be reported as SUPPORTED. Measured rather than assumed:
    // seccomp filters, restrictive LSM policy and exhausted map counts all
    // make this false on real machines.
    {
        MapAttempt basic = try_map(nullptr, page_size, PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANONYMOUS);
        profile.vm.anonymous_mapping_supported = Fact<bool>::known(
            basic.ok(), EvidenceClass::MeasuredCapability,
            std::string(kSourceProbe) + ": mmap(NULL, page_size, RW, "
            "MAP_PRIVATE|MAP_ANONYMOUS)" +
                (basic.ok() ? "" : " failed with " + errno_name(basic.error)));
        unmap(basic, page_size);
        if (!basic.ok()) {
            warnings.emplace_back(
                "a plain anonymous mapping failed with " +
                errno_name(basic.error) +
                "; the remaining measurements are unlikely to be meaningful");
        }
    }

    // -- minimum mappable address -----------------------------------------
    const std::string mmap_min = read_small_file("/proc/sys/vm/mmap_min_addr");
    if (!mmap_min.empty()) {
        profile.vm.min_map_address = Fact<Address>::known(
            Address(std::strtoull(mmap_min.c_str(), nullptr, 10)),
            EvidenceClass::MeasuredCapability,
            std::string(kSourceProbe) + ": /proc/sys/vm/mmap_min_addr");
    } else {
        warnings.emplace_back(
            "/proc/sys/vm/mmap_min_addr was unreadable; the minimum mappable "
            "address is unknown");
    }

    // -- MAP_FIXED_NOREPLACE ----------------------------------------------
    const NoReplaceSupport noreplace = detect_fixed_noreplace(page_size, warnings);
    switch (noreplace) {
        case NoReplaceSupport::Yes:
            profile.vm.fixed_noreplace_available = Fact<bool>::known(
                true, EvidenceClass::MeasuredCapability,
                std::string(kSourceProbe) +
                    ": MAP_FIXED_NOREPLACE refused a collision with EEXIST");
            break;
        case NoReplaceSupport::No:
            profile.vm.fixed_noreplace_available = Fact<bool>::known(
                false, EvidenceClass::MeasuredCapability,
                std::string(kSourceProbe) +
                    ": MAP_FIXED_NOREPLACE was ignored and the mapping relocated");
            break;
        case NoReplaceSupport::Undetermined:
            break;
    }

    // Exact placement facts depend on a trustworthy MAP_FIXED_NOREPLACE. If we
    // could not establish it, we deliberately learn nothing about exact
    // placement rather than probing destructively.
    const bool exact_probing_allowed = noreplace == NoReplaceSupport::Yes;
    if (!exact_probing_allowed) {
        warnings.emplace_back(
            "exact-placement probing was skipped because MAP_FIXED_NOREPLACE "
            "could not be confirmed; MAP_FIXED is never used by this probe "
            "because it destroys existing mappings");
    }

    if (exact_probing_allowed) {
        profile.vm.exact_mapping = Fact<SupportLevel>::known(
            SupportLevel::ConditionallySupported,
            EvidenceClass::MeasuredCapability,
            std::string(kSourceProbe) +
                ": exact placement succeeds for free, in-bounds, page-aligned "
                "ranges");
        profile.vm.exact_mapping_failure_codes = {"EEXIST", "EINVAL", "ENOMEM",
                                                  "EPERM"};
    }

    // -- maximum user address ---------------------------------------------
    std::uint64_t max_user = 0;
    if (exact_probing_allowed) {
        max_user = find_max_user_address(page_size);
        if (max_user != 0) {
            profile.vm.max_user_address = Fact<Address>::known(
                Address(max_user), EvidenceClass::MeasuredCapability,
                std::string(kSourceProbe) +
                    ": binary search with MAP_FIXED_NOREPLACE");
        }
    }

    // -- largest single reservation ----------------------------------------
    const std::uint64_t biggest = find_max_single_reservation();
    if (biggest != 0) {
        profile.vm.max_single_reservation = Fact<std::uint64_t>::known(
            biggest, EvidenceClass::MeasuredCapability,
            std::string(kSourceProbe) +
                ": largest power-of-two PROT_NONE MAP_NORESERVE reservation the "
                "kernel granted");
    } else {
        warnings.emplace_back(
            "no power-of-two reservation from 1 MiB upward was granted, so "
            "max_single_reservation was left unknown rather than recorded as "
            "zero");
    }

    // And the same question asked above DEFAULT_MAP_WINDOW. See the comment on
    // find_max_single_reservation_hinted(): the field above is probed hintlessly
    // and Linux does not open 5-level paging to a hintless mmap, so on an LA57
    // host these two differ and the hintless one is the narrower claim.
    if (const std::uint64_t hinted = find_max_single_reservation_hinted();
        hinted != 0) {
        profile.vm.max_single_reservation_hinted = Fact<std::uint64_t>::known(
            hinted, EvidenceClass::MeasuredCapability,
            std::string(kSourceProbe) +
                ": largest power-of-two PROT_NONE MAP_NORESERVE reservation the "
                "kernel granted for a request hinted at " +
                json::to_hex(kAboveDefaultMapWindow) +
                ", above DEFAULT_MAP_WINDOW");

        // THE COMPARISON IS THE WHOLE POINT, so it is stated rather than left
        // for a reader to compute. On a 4-level host these agree and the pair is
        // evidence that the hintless number means what it says; where they
        // differ, the hintless one is a fact about the DEFAULT WINDOW and not
        // about the kernel's willingness, and every verdict resting on it
        // inherits that.
        if (profile.vm.max_single_reservation.is_known()) {
            const std::uint64_t plain = profile.vm.max_single_reservation.value();
            if (hinted != plain) {
                warnings.emplace_back(
                    "a hint above DEFAULT_MAP_WINDOW changes what this host will "
                    "reserve: " + json::to_hex(plain) + " hintless vs " +
                    json::to_hex(hinted) + " hinted. max_single_reservation is "
                    "therefore the largest grant INSIDE THE DEFAULT MMAP WINDOW "
                    "on this host, not the largest the kernel will give - which "
                    "is what a 5-level-paging kernel does, and RS-VM-0026's "
                    "verdicts rest on the hintless figure unless the request "
                    "names a high address");
            } else {
                warnings.emplace_back(
                    "a hint above DEFAULT_MAP_WINDOW does not change what this "
                    "host will reserve (" + json::to_hex(plain) +
                    " either way), so max_single_reservation means what it says "
                    "here. On a host with 5-level paging it would not");
            }
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
            // Observing one honoured hint does NOT establish that hints are
            // never relocated. mmap(2) explicitly permits relocation, so the
            // specified guarantee outranks this single observation.
            profile.vm.hinted_mapping_may_relocate = Fact<bool>::known(
                true, EvidenceClass::SpecifiedGuarantee,
                "Linux mmap(2): addr is a hint and the kernel may place the "
                "mapping elsewhere");
            warnings.emplace_back(
                "the hint was honoured in this run, but mmap(2) permits "
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
            std::string(kSourceProbe) + ": mmap(PROT_READ|PROT_WRITE|PROT_EXEC)");
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
    if (prot.tested_rx && prot.rx_allowed) {
        // Executable memory was obtained by an ordinary unprivileged process
        // with no entitlement of any kind.
        profile.vm.protection.jit_entitlement_required = Fact<bool>::known(
            false, EvidenceClass::MeasuredCapability,
            std::string(kSourceProbe) +
                ": executable memory was obtained without any entitlement");
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
                            ": the mapping call itself refused to extend past "
                            "end of file");
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
                ? page_size
                : options.max_test_mapping_bytes;
        const std::uint64_t arena_window =
            biggest == 0 ? page_size
                         : (biggest < kArenaMaxWindow ? biggest
                                                     : kArenaMaxWindow);
        ScanOutcome scan = scan_address_space(page_size, probe_length, max_user,
                                               arena_window);
        profile.vm.available_ranges = std::move(scan.available);
        profile.vm.unavailable_ranges = std::move(scan.unavailable);
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

    return result;
}

}  // namespace rs::probe

#endif  // RS_PLATFORM_LINUX

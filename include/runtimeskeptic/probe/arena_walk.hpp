// SPDX-License-Identifier: Apache-2.0
#ifndef RUNTIMESKEPTIC_PROBE_ARENA_WALK_HPP
#define RUNTIMESKEPTIC_PROBE_ARENA_WALK_HPP

// Walking an allocation arena, with the platform calls injected.
//
// WHY THIS IS A HEADER AND NOT PART OF THE PLATFORM PROBE.
//
// The arena logic was written inside `vm_probe_macos.cpp`, which no machine in
// this project can compile except a macOS runner. Verifying it therefore meant
// pushing and waiting - and the first version was wrong in a way that a test
// would have caught instantly: with a 32 MiB stride, the run correctly stopped
// at the last window it had placed and left the heap page the CI failure was
// about sitting in the untested tail.
//
// It was in fact caught instantly, by a throwaway program that stubbed the Mach
// calls and drove the real code with the layout the runner had reported. That
// program lived in /tmp and would have died with the session, which makes it a
// comment rather than a check. So the logic moved here, behind two injected
// callbacks, and `tests/unit/test_arena_walk.cpp` drives it against measured
// layouts on every platform this project builds on.
//
// This is the same move `check_includes.py` and `check_shell_portability.py`
// make in their own languages: the recurring defect in this project is code that
// is green wherever anyone runs it and fatal where nobody does, and the only
// version of a fix that scales removes the need for the platform rather than
// moving the dependency.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "runtimeskeptic/vm/address_range.hpp"

namespace rs::probe {

// Whether an exact placement at a window succeeded, and if not, who was in the
// way. The distinction between the last two is the whole correctness question:
// `HeldByProbe` proves the kernel hands this space out and says nothing about
// the host, exactly as EEXIST does on Linux; `Refused` is a host limitation.
// Deciding which is a platform question and stays in the platform probe.
enum class ArenaPlacement { Placed, HeldByProbe, Refused };

// What the platform says covers an address, when a placement was refused. An
// entry that grants no access refuses placement everywhere inside itself by
// construction, so its extent is reportable evidence rather than extrapolation.
struct ArenaEntry {
    bool covers = false;
    std::uint64_t start = 0;
    std::uint64_t size = 0;
    std::string text;   // human-readable, carried into the profile note
};

struct ArenaProbe {
    std::function<ArenaPlacement(std::uint64_t base, std::uint64_t size)> place;
    std::function<ArenaEntry(std::uint64_t base)> describe;
};

struct ArenaWalk {
    std::vector<vm::ClassifiedRange> available;
    std::vector<vm::ClassifiedRange> unavailable;

    // These move with the probe's own layout, so a caller must keep them OUT of
    // the facts subtree - they belong in notes, outside profile_id.
    std::size_t placed = 0;
    std::size_t held_by_probe = 0;
    std::size_t refused = 0;
    std::size_t skipped = 0;   // inside an entry already described
};

// Walks `[bottom, top)` in CONTIGUOUS windows of `window_size`, merging adjacent
// usable windows into one range.
//
// Contiguous, not sampled, and that is the design decision worth arguing about.
// A sampled arena asserts the space between its samples; with a 32 MiB stride
// that assertion left a 16 MiB tail unclaimed against a refused band, and the
// page the whole exercise was about was in it. Shrinking the stride would have
// fixed that one layout by fitting a number to one runner's morning. Contiguous
// windows have no tail by construction, and assert only what was placed.
//
// It is affordable because of a platform difference and is not a criticism of a
// sampled arena: 60 GiB in 4 MiB windows is 15,360 probes, while the Linux arena
// spans 128 TiB, where the same approach would be 33 million.
//
// `page_size` only skips misaligned bases; it does not size the window.
ArenaWalk walk_arena(const std::string& what, std::uint64_t bottom,
                     std::uint64_t top, std::uint64_t page_size,
                     std::uint64_t window_size, const ArenaProbe& probe);

// The ceiling an arena at the TOP of the user address space should use, given a
// measured `max_user_address`. `granularity` is what the result is rounded up to.
//
// `TASK_SIZE` IS NOT WHERE PROGRAMS ARE, and that mistake cost two days as a
// suspected flaky test. `linux---gcc` failed the coverage conformance case on some
// pushes and passed on others with byte-identical C++, so it was recorded as
// nondeterminism on the runner and deliberately left alone pending a second
// reading. The second reading was not noise - it was different hardware:
//
//   max_user_address: 0xfffffffffff000          <- 56-bit: 5-level paging
//   arena:            [0xfffc0000000000, 0xfffffffffff000)
//   code page 0x5606b35a0000  heap page 0x7fe8df6ff000   <- both 47-bit
//
// On a host with 5-level paging the kernel gives userspace 56-bit addresses but
// "refuses to allocate above 47 bits by default and requires an explicit high
// hint to opt in" - the compatibility rule that exists precisely because JIT
// compilers pack tags into the high bits. This project already had that written
// down, in `corpus/runtime_failures/RSC-0049-la57-vs-jit-pointer-tagging.md`. The
// arena derived its bounds from TASK_SIZE anyway, landed in the top 4 TiB of a
// 64 PiB space where nothing is ever mapped, and the conformance test was right
// on every push it failed.
//
// arch/x86/include/asm/processor.h: DEFAULT_MAP_WINDOW is ((1UL << 47) -
// PAGE_SIZE) on x86-64, and both `mmap_base` and `ELF_ET_DYN_BASE` derive from
// it, not from TASK_SIZE_MAX. On a 4-level host `max_user_address` is exactly
// (1<<47) - PAGE_SIZE, so the cap changes the answer by nothing at all and cannot
// regress the measured false-positive campaign.
//
// It lives in this header, away from the platform file, for one reason: an LA57
// host is one this project cannot obtain on demand, and the version of the fix
// that needs one to be checked has not removed the dependency - it has moved it.
std::uint64_t arena_ceiling_for(std::uint64_t max_user_address,
                                std::uint64_t granularity);

}  // namespace rs::probe

#endif  // RUNTIMESKEPTIC_PROBE_ARENA_WALK_HPP

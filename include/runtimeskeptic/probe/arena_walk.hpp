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

// What the platform says covers an address, when a placement was refused.
//
// `covers` MEANS: a region of this task's map covers the address and grants no
// access at all. That is not the same thing as a host limitation, and conflating
// them is what made the macOS arena irreproducible:
//
//   available_ranges:    35 vs 32 entries      two runs, one binary, one machine
//   unavailable_ranges:  83 vs 74 entries
//
// A macOS process is full of its own PROT_NONE reservations - malloc guards,
// dyld, thread stack guards - and their addresses move with ASLR. Filing them as
// host limitations put ~80 facts about the probe's own morning into an id that is
// supposed to name the host: the exact defect `min_map_address` was once guilty
// of. Linux never showed it because MAP_FIXED_NOREPLACE answers EEXIST for ANY
// existing mapping, and EEXIST is treated as held.
//
// So a refusal WITH such an entry is ambiguous - ours, or a band the platform
// puts in every task - and the walk resolves it by treating it as held, on the
// same argument EEXIST gets: it says nothing about the host. That is only sound
// while no platform band lies inside the arena's bounds, which is the caller's
// responsibility and is why the macOS arena now stops at the commpage.
//
// A refusal WITHOUT such an entry is structural: a hard error, or KERN_NO_SPACE
// with nothing of ours in the way. Those are recorded.
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
    std::size_t attempts = 0;
    bool budget_exhausted = false;
    std::size_t placed = 0;
    std::size_t held_by_probe = 0;
    std::size_t refused = 0;      // structural: recorded as a limitation
    std::size_t skipped = 0;      // inside an entry already described
    // Refused, but a no-access region of this task covers it. Treated as held.
    // THIS COUNT IS THE SAFETY VALVE for the assumption above: if a platform band
    // ever does lie inside an arena, this is the number that goes up while
    // `unavailable` stays empty, so it must stay readable in the note.
    std::size_t held_no_access = 0;
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

// Walks every page in `[bottom, top)` without treating a large reservation
// failure as evidence about all addresses it spans. Each tile is attempted at
// up to `max_window_size`; held or refused tiles are recursively divided until
// the result is established at page granularity. Adjacent leaves with the same
// verdict are merged, so process-local occupancy changes counts but not facts.
//
// Unlike `walk_arena`, this routine may use very large initial tiles: neither
// EEXIST nor a size-sensitive ENOMEM is generalized to the tile. The hard
// `max_attempts` budget leaves the unvisited remainder unknown instead of
// allowing a global size refusal to expand into billions of page probes. If the
// budget is exhausted, ALL partial facts from this walk are discarded so their
// prefix cannot depend on process-local occupancy or recursion order.
// Bounds and sizes must be page-aligned. Invalid input fails closed.
ArenaWalk walk_arena_adaptive(const std::string& what,
                              std::uint64_t bottom, std::uint64_t top,
                              std::uint64_t page_size,
                              std::uint64_t max_window_size,
                              std::size_t max_attempts,
                              const ArenaProbe& probe);

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
// `default_map_window` is the architecture-specific Linux mmap policy ceiling;
// the default preserves the x86-64 behavior for callers and focused unit tests.
std::uint64_t arena_ceiling_for(
    std::uint64_t max_user_address, std::uint64_t granularity,
    std::uint64_t default_map_window = std::uint64_t{1} << 47);

// The FLOOR of an arena occupying the top `span` of the user address space, or 0
// meaning "there is no room for one". Returns the base of the `span`-sized bucket
// that `max_user_address` falls in, so the arena is `[result, max_user_address)`.
//
// This is the Windows arena's bound, and it is here for the same reason
// `arena_ceiling_for` is: the file that uses it compiles on exactly one machine
// this project cannot obtain on demand, so a version of the derivation that lives
// there has not removed the platform dependency - it has moved it into the one
// place a test cannot reach. That lesson cost two days as a suspected flaky test.
//
// WHY THE TOP BUCKET IS THE RIGHT ONE, MEASURED RATHER THAN RECALLED. A Windows
// Server 2025 runner reported its own occupancy across the 128 TiB user space in
// 1 TiB buckets:
//
//   0x0            =       6299648 bytes   (lowest occupied 0x7ffe0000)
//   0x10000000000  =       2633728 bytes
//   0x7f0000000000 =    4340531200 bytes   <- 99.8%: image, DLLs, stacks, heaps
//
// High-entropy ASLR puts essentially everything in the top TiB. `max_user_address`
// is `lpMaximumApplicationAddress + 1` - a system constant, identical in every
// process on the machine - so a floor derived from it does not move with ours,
// which is the rule `min_map_address` broke once and six campaign contracts
// returned confident UNSUPPORTED off.
//
// The exact-multiple case is why this is not written inline as `(max / span) *
// span`: on a host whose `max_user_address` IS a span multiple, that expression
// returns the maximum itself and the arena is empty - a silent zero-coverage
// probe, which is the failure this whole exercise exists to end.
std::uint64_t arena_floor_for(std::uint64_t max_user_address,
                              std::uint64_t span);

// The floor of a SECOND arena sitting `span` below a measured ceiling, or 0 when
// there is no room for one.
//
// Three platforms now need two arenas, because a program's code and its heap do
// not live together:
//
//   Linux    mmap base (top of space)  +  ET_DYN base
//   Windows  top TiB (image, DLLs)     +  1..127 TiB (NT heap)
//   macOS    [__TEXT base, commpage)   +  [ceiling - 4 TiB, ceiling)
//
// macOS was the last to find out, and only because its ceiling was wrong: with
// `max_user_address` reported 35 TiB low, the Rosetta lane could not see that its
// heap sat at `0x7f9ab0028000`, 140 TiB above the only arena there was.
//
// TWO WAYS THIS RETURNS 0, and both are refusals to guess rather than edge cases:
//
//   max_user_address == 0     the probe could not pin the ceiling down. An arena
//                             placed from a guessed ceiling is the defect that
//                             produced the 35 TiB error in the first place.
//   overlap                   the floor would fall at or below `must_stay_above`,
//                             the top of an arena already walked. Two overlapping
//                             arenas record one address twice, and
//                             `available_and_unavailable_ranges_do_not_overlap`
//                             is a conformance test rather than a hope.
std::uint64_t high_arena_floor(std::uint64_t max_user_address,
                               std::uint64_t span,
                               std::uint64_t must_stay_above);

// ---------------------------------------------------------------------------
// THE LANDMARK LADDER'S ONE DECISION, in one place, because both ladders got it
// wrong and both arenas got it right.
//
// The arenas learned this rule twice and it is written across three files: the
// recorded set must not depend on the probe's own layout. Neither LADDER ever
// got the same fix, and each broke it in a different way:
//
//   macOS   a landmark our own mapping happened to sit on produced an available
//           entry with a DIFFERENT `note` from the same landmark when free. The
//           bounds matched, the count matched, and `profile_id` still moved -
//           because the note is inside the hashed facts subtree. Two runs of one
//           binary on one machine alternated between exactly two ids across
//           every push for two days, and `available_ranges: 22 vs 22 entries`
//           was the whole diagnosis anyone got. The held-path note ENDS with the
//           words "Whether it was held or free ... is deliberately not recorded"
//           while being the record of exactly that.
//
//   Linux   the same landmark produced an available entry when free and NOTHING
//           when EEXIST, so the presence of the fact moved with our ASLR slide.
//           This is the identical defect macOS had already fixed - its comment
//           even cites "the argument EEXIST gets on Linux" - and Linux's own
//           ARENA applies the rule correctly forty lines away. It has never gone
//           red, which is not the same as being right.
//
// So the decision moves here, where `tests/unit/test_arena_walk.cpp` can drive
// it on every platform this project builds on, and where PLACED AND HELD ARE
// LITERALLY THE SAME RETURN VALUE rather than two branches that agree today.
// ---------------------------------------------------------------------------
enum class LadderOutcome { Available, Unavailable };

struct LadderRecord {
    LadderOutcome outcome = LadderOutcome::Available;
    std::string note;
};

// `placement_call` names the call in the platform's own words - "mmap(MAP_FIXED_
// NOREPLACE)" or "mach_vm_allocate(VM_FLAGS_FIXED)" - so the note stays readable
// without the outcome depending on the platform. `refusal_text` is used only for
// `Refused`.
//
// `Placed` and `HeldByProbe` return an IDENTICAL record. That is the whole
// function: both prove the kernel hands this exact address out to this process,
// and neither says anything about the host that the other does not.
LadderRecord ladder_record(ArenaPlacement placement,
                           const std::string& placement_call,
                           const std::string& refusal_text);

}  // namespace rs::probe

#endif  // RUNTIMESKEPTIC_PROBE_ARENA_WALK_HPP

// SPDX-License-Identifier: Apache-2.0
//
// The arena walk, driven against layouts that were MEASURED, on any platform.
//
// This is the durable form of a throwaway program. `scan_allocation_arenas` for
// macOS was written inside `vm_probe_macos.cpp`, which nothing in this project
// can compile except a macOS runner, so the only way to check it was to push and
// wait. The first version was wrong: with a 32 MiB stride the run correctly
// closed at the last window it had placed, and the heap page the whole CI failure
// was about sat in the 16 MiB tail between that window and a refused band.
//
// A stubbed program in /tmp found that in one run. It would have died with the
// session - a check that only runs when someone remembers is a comment - so the
// walk moved behind two injected callbacks and the check moved here.
//
// The numbers below are not invented. They are what
// refs/ci-logs/90dc74b/macos---apple-clang printed:
//
//   code page 0x1023a4000
//   heap page 0x7be800000
//   nearest above the heap page: [0x7bf400000, 0xabe000000) refused, no access
#include "runtimeskeptic/probe/arena_walk.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "test_support.hpp"

using namespace rs;
using namespace rs::probe;
using namespace rs::vm;

namespace {

// --- the measured macOS runner layout -------------------------------------
constexpr std::uint64_t kTextBase = 0x100000000ull;
constexpr std::uint64_t kArenaTop = 0x1000000000ull;
constexpr std::uint64_t kWindow = 4ull << 20;
constexpr std::uint64_t kPage = 16384;

constexpr std::uint64_t kDenyStart = 0x7bf400000ull;
constexpr std::uint64_t kDenyEnd = 0xabe000000ull;
constexpr std::uint64_t kOursStart = 0x100000000ull;
constexpr std::uint64_t kOursEnd = 0x104000000ull;

constexpr std::uint64_t kCodePage = 0x1023a4000ull;
constexpr std::uint64_t kHeapPage = 0x7be800000ull;

ArenaProbe measured_macos_runner() {
    ArenaProbe p;
    p.place = [](std::uint64_t base, std::uint64_t size) {
        const std::uint64_t end = base + size;
        if (base < kDenyEnd && end > kDenyStart) return ArenaPlacement::Refused;
        if (base < kOursEnd && end > kOursStart) {
            return ArenaPlacement::HeldByProbe;
        }
        return ArenaPlacement::Placed;
    };
    p.describe = [](std::uint64_t base) {
        ArenaEntry e;
        if (base >= kDenyStart && base < kDenyEnd) {
            e.covers = true;
            e.start = kDenyStart;
            e.size = kDenyEnd - kDenyStart;
            e.text = "region covers it, and is a real mapping (reserved=0), "
                     "protection ---";
        } else {
            e.text = "mach_vm_region found no region at or above this address";
        }
        return e;
    };
    return p;
}

bool covered(const std::vector<ClassifiedRange>& v, std::uint64_t page) {
    return std::any_of(v.begin(), v.end(), [&](const ClassifiedRange& r) {
        return r.range.start <= page && page < r.range.end;
    });
}

}  // namespace

RS_TEST(the_arena_establishes_the_two_pages_that_failed_on_the_runner) {
    const ArenaWalk walk = walk_arena("test arena", kTextBase, kArenaTop, kPage,
                                      kWindow, measured_macos_runner());
    RS_CHECK_MESSAGE(covered(walk.available, kCodePage),
                     "the code page the macOS runner reported is not "
                     "established; this is the first of the two CI failures");
    RS_CHECK_MESSAGE(covered(walk.available, kHeapPage),
                     "the heap page the macOS runner reported is not "
                     "established. A 32 MiB STRIDE failed exactly here, because "
                     "the page sits 12 MiB below a refused band and inside the "
                     "untested tail. Contiguous windows are what fixes it - do "
                     "not reintroduce a stride");
}

RS_TEST(no_available_range_overlaps_a_band_the_host_refuses) {
    // The direction that matters. An over-broad AVAILABLE range answers
    // SUPPORTED where the host refuses, which is a false negative in the
    // dangerous direction: the caller ships and then faults.
    const ArenaWalk walk = walk_arena("test arena", kTextBase, kArenaTop, kPage,
                                      kWindow, measured_macos_runner());
    for (const auto& r : walk.available) {
        RS_CHECK_MESSAGE(!(r.range.start < kDenyEnd && r.range.end > kDenyStart),
                         "an available range overlaps the refused band: " +
                             r.range.to_string());
    }
    RS_CHECK(!covered(walk.available, 0x800000000ull));
}

RS_TEST(the_available_and_refused_ranges_meet_with_no_gap) {
    // Contiguous windows mean the boundary is exact. A gap would be untested
    // space quietly reported as neither, which is the defect this arena exists
    // to remove; an overlap would be a range asserted both ways at once.
    const ArenaWalk walk = walk_arena("test arena", kTextBase, kArenaTop, kPage,
                                      kWindow, measured_macos_runner());
    RS_CHECK_EQ(walk.available.size(), std::size_t{2});
    if (walk.available.size() != 2) return;
    RS_CHECK_EQ(walk.available[0].range.start, kTextBase);
    RS_CHECK_EQ(walk.available[0].range.end, kDenyStart);
    RS_CHECK_EQ(walk.available[1].range.start, kDenyEnd);
    RS_CHECK_EQ(walk.available[1].range.end, kArenaTop);
}

RS_TEST(a_wide_refused_entry_is_recorded_once_at_the_extent_reported) {
    const ArenaWalk walk = walk_arena("test arena", kTextBase, kArenaTop, kPage,
                                      kWindow, measured_macos_runner());
    // 3067 windows fall inside one twelve-GiB entry. Recording one per window
    // would put 3067 entries in the profile, and collapse_contained_ranges keeps
    // ranges that CONTAIN nothing - adjacent siblings contain nothing.
    RS_CHECK_EQ(walk.unavailable.size(), std::size_t{1});
    if (walk.unavailable.empty()) return;
    RS_CHECK_EQ(walk.unavailable[0].range.start, kDenyStart);
    RS_CHECK_EQ(walk.unavailable[0].range.end, kDenyEnd);
}

RS_TEST(skipped_windows_are_counted_so_refused_keeps_its_meaning) {
    const ArenaWalk walk = walk_arena("test arena", kTextBase, kArenaTop, kPage,
                                      kWindow, measured_macos_runner());
    // Without this the note reads "1 structurally refused" for twelve GiB of
    // refused address space, which is a true number answering a question nobody
    // asked.
    RS_CHECK_EQ(walk.refused, std::size_t{1});
    RS_CHECK_EQ(walk.skipped, (kDenyEnd - kDenyStart) / kWindow - 1);
    RS_CHECK(walk.held_by_probe > 0);
    RS_CHECK(walk.placed > 0);
}

RS_TEST(the_probes_own_image_does_not_raise_the_arena_floor) {
    // The bug the runner found on 71af1ee, and the reason it is worth a case of
    // its own rather than a comment.
    //
    // The arena's bottom was `max(find_min_map_address(), kMachOTextBase)`.
    // find_min_map_address() returns the lowest page THIS PROCESS can place,
    // which is above its own low image - so the code page is below it by
    // construction and the arena's floor ended up above the exact page the arena
    // exists to cover. The heap page was fixed; the code page was not.
    //
    // The floor must be the constant. Our own image reports HeldByProbe, which
    // the walk treats as usable, so it changes the placed/held split (a note,
    // outside profile_id) and never which windows are refused - and only a
    // refusal splits a run.
    ArenaProbe ours_low;
    ours_low.place = [](std::uint64_t base, std::uint64_t) {
        // The first 12 windows are this process's own image.
        return base < kTextBase + 12 * kWindow ? ArenaPlacement::HeldByProbe
                                               : ArenaPlacement::Placed;
    };
    ours_low.describe = [](std::uint64_t) { return ArenaEntry{}; };

    const ArenaWalk walk = walk_arena("test arena", kTextBase,
                                      kTextBase + 24 * kWindow, kPage, kWindow,
                                      ours_low);
    RS_CHECK_EQ(walk.available.size(), std::size_t{1});
    if (walk.available.empty()) return;
    RS_CHECK_MESSAGE(walk.available[0].range.start == kTextBase,
                     "the arena floor moved off the constant. A value derived "
                     "from where the probe's own image sits must never raise it "
                     "- that is what put the floor above the code page on "
                     "71af1ee");
    RS_CHECK(walk.available[0].range.contains(
        AddressRange{kTextBase, kTextBase + kWindow}));
    RS_CHECK_EQ(walk.held_by_probe, std::size_t{12});
}

RS_TEST(a_host_that_refuses_everything_establishes_nothing_as_available) {
    ArenaProbe all_refused;
    all_refused.place = [](std::uint64_t, std::uint64_t) {
        return ArenaPlacement::Refused;
    };
    // No covering entry, so nothing may be widened and nothing may be skipped.
    all_refused.describe = [](std::uint64_t) { return ArenaEntry{}; };
    const ArenaWalk walk = walk_arena("test arena", kTextBase,
                                      kTextBase + 16 * kWindow, kPage, kWindow,
                                      all_refused);
    RS_CHECK(walk.available.empty());
    RS_CHECK_EQ(walk.unavailable.size(), std::size_t{16});
    RS_CHECK_EQ(walk.skipped, std::size_t{0});
}

RS_TEST(a_host_that_grants_everything_yields_exactly_one_range) {
    ArenaProbe all_placed;
    all_placed.place = [](std::uint64_t, std::uint64_t) {
        return ArenaPlacement::Placed;
    };
    all_placed.describe = [](std::uint64_t) { return ArenaEntry{}; };
    const ArenaWalk walk = walk_arena("test arena", kTextBase,
                                      kTextBase + 16 * kWindow, kPage, kWindow,
                                      all_placed);
    RS_CHECK_EQ(walk.available.size(), std::size_t{1});
    if (walk.available.empty()) return;
    RS_CHECK_EQ(walk.available[0].range.start, kTextBase);
    RS_CHECK_EQ(walk.available[0].range.end, kTextBase + 16 * kWindow);
    RS_CHECK(walk.unavailable.empty());
}

RS_TEST(degenerate_bounds_produce_nothing_rather_than_a_bad_range) {
    ArenaProbe never;
    never.place = [](std::uint64_t, std::uint64_t) {
        RS_CHECK_MESSAGE(false, "the walk probed a degenerate arena");
        return ArenaPlacement::Placed;
    };
    never.describe = [](std::uint64_t) { return ArenaEntry{}; };

    for (const auto& c : std::vector<std::pair<std::uint64_t, std::uint64_t>>{
             {kTextBase, kTextBase},              // empty
             {kArenaTop, kTextBase},              // inverted
             {kTextBase, kTextBase + kWindow - 1}}) {  // narrower than a window
        const ArenaWalk walk =
            walk_arena("test arena", c.first, c.second, kPage, kWindow, never);
        RS_CHECK(walk.available.empty());
        RS_CHECK(walk.unavailable.empty());
    }
    // A zero window would divide by zero when counting skips.
    const ArenaWalk zero =
        walk_arena("test arena", kTextBase, kArenaTop, kPage, 0, never);
    RS_CHECK(zero.available.empty());
}

RS_TEST(the_walk_stops_at_the_top_of_the_address_space_without_wrapping) {
    // from_base_size() refuses a range that wraps. If the walk ignored that it
    // would emit a range with end < start, which every rule downstream reads as
    // empty - a silent hole rather than a refusal.
    ArenaProbe placed;
    placed.place = [](std::uint64_t, std::uint64_t) {
        return ArenaPlacement::Placed;
    };
    placed.describe = [](std::uint64_t) { return ArenaEntry{}; };
    const std::uint64_t top = ~std::uint64_t{0};
    const ArenaWalk walk =
        walk_arena("test arena", top - 2 * kWindow + 1, top, kPage, kWindow,
                   placed);
    for (const auto& r : walk.available) RS_CHECK(r.range.end > r.range.start);
    for (const auto& r : walk.unavailable) RS_CHECK(r.range.end > r.range.start);
}

// ---------------------------------------------------------------------------
// arena_ceiling_for: the two-day "flake" that was a 5-level-paging runner.
// ---------------------------------------------------------------------------
RS_TEST(a_five_level_paging_host_does_not_move_the_arena_to_the_top_of_56_bits) {
    constexpr std::uint64_t kTiB = 1ull << 40;
    // What GitHub's LA57 Linux runners report. TASK_SIZE is 2^56, but the kernel
    // refuses to allocate above 47 bits without an explicit high hint, so the
    // code page was at 0x5606b35a0000 and the heap at 0x7fe8df6ff000 while the
    // arena sat in [0xfffc0000000000, 0xfffffffffff000) - 64 PiB up, where
    // nothing is ever mapped. Coverage was zero and the conformance case was
    // right on every push it failed.
    const std::uint64_t la57 = arena_ceiling_for(0xfffffffffff000ull, kTiB);
    RS_CHECK_EQ(la57, 0x800000000000ull);

    // And the 4-level host is unchanged, which is what makes the cap safe: the
    // measured false-positive campaign cannot regress.
    const std::uint64_t normal = arena_ceiling_for(0x7ffffffff000ull, kTiB);
    RS_CHECK_EQ(normal, 0x800000000000ull);
    RS_CHECK_EQ(la57, normal);
}

RS_TEST(the_ceiling_rounds_up_never_down) {
    constexpr std::uint64_t kTiB = 1ull << 40;
    // Rounding DOWN was the original bug: 0x7ffffffff000 became 0x7f0000000000,
    // the exact 1 TiB bucket that 629 of 639 observed addresses sit ABOVE.
    RS_CHECK(arena_ceiling_for(0x7ffffffff000ull, kTiB) > 0x7ffffffff000ull);
    RS_CHECK_EQ(arena_ceiling_for(kTiB, kTiB), kTiB);
    RS_CHECK_EQ(arena_ceiling_for(kTiB + 1, kTiB), 2 * kTiB);
    // A small address space is left alone rather than inflated to a TiB... it is
    // rounded up, which the caller guards against with `max_user_address <=
    // kArenaSpan`. Asserted so the guard's necessity stays visible.
    RS_CHECK_EQ(arena_ceiling_for(4096, kTiB), kTiB);
    // Degenerate granularity must not divide by zero.
    RS_CHECK_EQ(arena_ceiling_for(0x7ffffffff000ull, 0), 0x7ffffffff000ull);
    // And must not wrap at the very top.
    RS_CHECK(arena_ceiling_for(~std::uint64_t{0}, kTiB) >= 0x800000000000ull);
}

RS_TEST(a_refusal_extent_never_overlaps_space_already_placed) {
    // The macOS runner failure on 6533633, seven times over:
    //   "the probe reported [0x2a7224000, 0x2ae224000) as both available and
    //    unavailable"
    // A platform entry does not start on a window boundary. Widening a refusal
    // down to the entry's extent reached back inside a window this walk had just
    // placed. The simulation missed it because the simulated band happened to
    // start window-aligned - so this case makes it deliberately UNaligned.
    constexpr std::uint64_t kUnalignedDeny = 0x2a7224000ull;
    constexpr std::uint64_t kUnalignedEnd = 0x2ae224000ull;

    ArenaProbe unaligned;
    unaligned.place = [](std::uint64_t base, std::uint64_t size) {
        return (base < kUnalignedEnd && base + size > kUnalignedDeny)
                   ? ArenaPlacement::Refused
                   : ArenaPlacement::Placed;
    };
    unaligned.describe = [](std::uint64_t) {
        ArenaEntry e;
        e.covers = true;
        e.start = kUnalignedDeny;
        e.size = kUnalignedEnd - kUnalignedDeny;
        e.text = "region covers it, protection ---";
        return e;
    };

    const ArenaWalk walk = walk_arena("test arena", kTextBase, 0x300000000ull,
                                      kPage, kWindow, unaligned);
    for (const auto& a : walk.available) {
        for (const auto& u : walk.unavailable) {
            RS_CHECK_MESSAGE(
                !(a.range.start < u.range.end && u.range.start < a.range.end),
                "available " + a.range.to_string() + " overlaps unavailable " +
                    u.range.to_string() +
                    ". A refusal widened to a platform extent must be clamped to "
                    "space this walk had not already placed");
        }
    }
    RS_CHECK(!walk.unavailable.empty());
}

RS_TEST(the_walk_terminates_when_an_entry_end_is_not_window_aligned) {
    // The skip advances by WHOLE windows and by a FLOOR count. An earlier version
    // set base = entry_end - window_size, which left the walk aligned to a
    // platform extent instead of its own grid; with page_size checks that can
    // silently `continue` past the rest of the arena. A ceiling count would leave
    // an unexamined sliver. Either way the walk must finish.
    ArenaProbe wide_unaligned;
    wide_unaligned.place = [](std::uint64_t base, std::uint64_t size) {
        return (base < 0x900000000ull && base + size > 0x500000123ull)
                   ? ArenaPlacement::Refused
                   : ArenaPlacement::Placed;
    };
    wide_unaligned.describe = [](std::uint64_t) {
        ArenaEntry e;
        e.covers = true;
        e.start = 0x500000123ull;      // deliberately not page or window aligned
        e.size = 0x900000000ull - 0x500000123ull;
        e.text = "unaligned entry";
        return e;
    };
    const ArenaWalk walk = walk_arena("test arena", kTextBase, 0xa00000000ull,
                                      kPage, kWindow, wide_unaligned);
    // Reaching here at all is the assertion. The rest checks it did not silently
    // abandon the space above the entry.
    RS_CHECK(!walk.available.empty());
    RS_CHECK(covered(walk.available, 0x980000000ull));
}

RS_TEST_MAIN("arena walk")

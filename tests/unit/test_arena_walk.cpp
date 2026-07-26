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
//
// THE PROPERTY THAT MATTERS MOST IS THE LAST TEST IN THIS FILE, and it is here
// because the walk failed it on a real runner: `check_reproducible.sh` reported
// 35 vs 32 available and 83 vs 74 unavailable entries across two runs of one
// binary on one machine. A profile_id that moves with the probe's own layout is
// the defect `min_map_address` was once guilty of, and no amount of coverage
// testing finds it - only asking twice does.
#include "runtimeskeptic/probe/arena_walk.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "test_support.hpp"

using namespace rs;
using namespace rs::probe;
using namespace rs::vm;

namespace {

// --- the measured macOS runner layout -------------------------------------
constexpr std::uint64_t kTextBase = 0x100000000ull;
constexpr std::uint64_t kArenaTop = 0xfc0000000ull;   // the commpage start
constexpr std::uint64_t kWindow = 4ull << 20;
constexpr std::uint64_t kPage = 16384;

// A no-access region 12 GiB wide, at the address the runner reported. Ours, as
// far as anything measurable here can tell - which is the whole point.
constexpr std::uint64_t kDenyStart = 0x7bf400000ull;
constexpr std::uint64_t kDenyEnd = 0xabe000000ull;
constexpr std::uint64_t kOursStart = 0x100000000ull;
constexpr std::uint64_t kOursEnd = 0x104000000ull;

constexpr std::uint64_t kCodePage = 0x1023a4000ull;
constexpr std::uint64_t kHeapPage = 0x7be800000ull;

// A probe whose only no-access region sits at [start, end): the shape of a
// process's own guard reservation, parameterised so a test can MOVE it.
ArenaProbe with_our_no_access_at(std::uint64_t start, std::uint64_t end) {
    ArenaProbe p;
    p.place = [start, end](std::uint64_t base, std::uint64_t size) {
        const std::uint64_t win_end = base + size;
        if (base < end && win_end > start) return ArenaPlacement::Refused;
        if (base < kOursEnd && win_end > kOursStart) {
            return ArenaPlacement::HeldByProbe;
        }
        return ArenaPlacement::Placed;
    };
    p.describe = [start, end](std::uint64_t base) {
        ArenaEntry e;
        if (base >= start && base < end) {
            e.covers = true;   // a region of THIS task, granting no access
            e.start = start;
            e.size = end - start;
            e.text = "region covers it, and is a real mapping (reserved=0), "
                     "protection ---";
        } else {
            e.text = "mach_vm_region found no region at or above this address";
        }
        return e;
    };
    return p;
}

ArenaProbe measured_macos_runner() {
    return with_our_no_access_at(kDenyStart, kDenyEnd);
}

// A refusal with NOTHING of ours covering it: a hard error, or KERN_NO_SPACE with
// no covering entry. That is structural and IS a host limitation.
ArenaProbe with_structural_refusal_at(std::uint64_t start, std::uint64_t end) {
    ArenaProbe p;
    p.place = [start, end](std::uint64_t base, std::uint64_t size) {
        return (base < end && base + size > start) ? ArenaPlacement::Refused
                                                  : ArenaPlacement::Placed;
    };
    p.describe = [](std::uint64_t) {
        ArenaEntry e;   // covers = false
        e.text = "mach_vm_region found no region at or above this address";
        return e;
    };
    return p;
}

bool covered(const std::vector<ClassifiedRange>& v, std::uint64_t page) {
    return std::any_of(v.begin(), v.end(), [&](const ClassifiedRange& r) {
        return r.range.start <= page && page < r.range.end;
    });
}

std::vector<std::pair<std::uint64_t, std::uint64_t>> bounds(
    const std::vector<ClassifiedRange>& v) {
    std::vector<std::pair<std::uint64_t, std::uint64_t>> out;
    for (const auto& r : v) out.emplace_back(r.range.start, r.range.end);
    return out;
}

std::string show(const std::vector<std::pair<std::uint64_t, std::uint64_t>>& v) {
    std::string s;
    for (const auto& r : v) {
        s += "[" + json::to_hex(r.first) + ", " + json::to_hex(r.second) + ") ";
    }
    return s.empty() ? "(none)" : s;
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

// ---------------------------------------------------------------------------
// THE REPRODUCIBILITY PROPERTY. This is the one the runner caught.
// ---------------------------------------------------------------------------
RS_TEST(the_output_does_not_move_when_our_own_reservations_move) {
    // `check_reproducible.sh` on the macOS runner:
    //     available_ranges:    35 vs 32 entries
    //     unavailable_ranges:  83 vs 74 entries
    // Two runs of one binary on one machine. A macOS process is full of its own
    // PROT_NONE reservations and ASLR moves them, so filing them as host
    // limitations put ~80 facts about the probe's morning into an id that is
    // supposed to name the host.
    //
    // Four layouts differing ONLY in where our own no-access regions sit. Every
    // recorded range must be identical, because none of them is about us.
    const auto reference =
        walk_arena("a", kTextBase, kArenaTop, kPage, kWindow,
                   with_our_no_access_at(kDenyStart, kDenyEnd));

    struct Layout { std::uint64_t start, end; const char* what; };
    const Layout layouts[] = {
        {0x200000000ull, 0x240000000ull, "one small region, low"},
        {0xb00000000ull, 0xb40000000ull, "one small region, high"},
        {0x480000000ull, 0x9c0000000ull, "a much wider region, elsewhere"},
        {kTextBase, kTextBase + kWindow, "right at the arena floor"},
    };

    for (const auto& l : layouts) {
        const ArenaWalk moved = walk_arena("a", kTextBase, kArenaTop, kPage,
                                           kWindow,
                                           with_our_no_access_at(l.start, l.end));
        RS_CHECK_MESSAGE(
            bounds(moved.available) == bounds(reference.available),
            std::string("moving our own no-access region (") + l.what +
                ") changed the recorded available ranges, so profile_id would "
                "move between two runs of one binary. reference "
                + show(bounds(reference.available)) + "vs " +
                show(bounds(moved.available)));
        RS_CHECK_MESSAGE(
            moved.unavailable.empty(),
            std::string("a no-access region of our own was recorded as a host "
                        "limitation (") + l.what +
                "). That is the 83-vs-74 defect: it says nothing about the host, "
                "exactly as EEXIST does on Linux");
    }
}

RS_TEST(a_no_access_region_of_our_own_is_held_not_a_limitation) {
    const ArenaWalk walk = walk_arena("test arena", kTextBase, kArenaTop, kPage,
                                      kWindow, measured_macos_runner());
    // One range, spanning the whole arena: nothing our own layout did split it.
    RS_CHECK_EQ(walk.available.size(), std::size_t{1});
    RS_CHECK(walk.unavailable.empty());
    if (!walk.available.empty()) {
        RS_CHECK_EQ(walk.available[0].range.start, kTextBase);
        RS_CHECK_EQ(walk.available[0].range.end, kArenaTop);
    }
    // And the count that is the safety valve for that decision is populated: if a
    // platform band ever lands inside an arena, THIS is what rises while
    // `unavailable` stays empty.
    RS_CHECK_MESSAGE(walk.held_no_access > 0,
                     "held_no_access is zero, so a reader cannot tell that "
                     "anything was resolved in favour of the host at all");
    RS_CHECK_EQ(walk.refused, std::size_t{0});
}

RS_TEST(a_structural_refusal_is_recorded_and_does_split_the_run) {
    // The other side of the same decision. No entry of ours covers it, so it is a
    // host limitation and must survive - otherwise the fix for the 83-vs-74 defect
    // would have thrown away real findings with the false ones.
    constexpr std::uint64_t kBad = 0x400000000ull;
    const ArenaWalk walk =
        walk_arena("test arena", kTextBase, kArenaTop, kPage, kWindow,
                   with_structural_refusal_at(kBad, kBad + 2 * kWindow));

    RS_CHECK_MESSAGE(!walk.unavailable.empty(),
                     "a refusal with nothing of ours covering it was not "
                     "recorded; structural refusals are the findings this arena "
                     "exists to keep");
    RS_CHECK_EQ(walk.refused, std::size_t{2});
    RS_CHECK_EQ(walk.held_no_access, std::size_t{0});
    RS_CHECK_EQ(walk.available.size(), std::size_t{2});
    RS_CHECK(!covered(walk.available, kBad));
    for (const auto& a : walk.available) {
        for (const auto& u : walk.unavailable) {
            RS_CHECK_MESSAGE(
                !(a.range.start < u.range.end && u.range.start < a.range.end),
                "available " + a.range.to_string() + " overlaps unavailable " +
                    u.range.to_string());
        }
    }
}

RS_TEST(a_structural_refusal_at_an_unaligned_address_still_does_not_overlap) {
    // The macOS runner failure on 6533633, seven times over:
    //   "the probe reported [0x2a7224000, 0x2ae224000) as both available and
    //    unavailable"
    // The simulation missed it because the simulated band started window-aligned,
    // so this one is deliberately not.
    constexpr std::uint64_t kBad = 0x2a7224000ull;
    const ArenaWalk walk =
        walk_arena("test arena", kTextBase, kArenaTop, kPage, kWindow,
                   with_structural_refusal_at(kBad, kBad + 0x7000000ull));
    for (const auto& a : walk.available) {
        for (const auto& u : walk.unavailable) {
            RS_CHECK_MESSAGE(
                !(a.range.start < u.range.end && u.range.start < a.range.end),
                "available " + a.range.to_string() + " overlaps unavailable " +
                    u.range.to_string());
        }
    }
    RS_CHECK(!walk.unavailable.empty());
}

RS_TEST(the_probes_own_image_does_not_raise_the_arena_floor) {
    // The bug the runner found on 71af1ee. The arena's bottom was
    // `max(find_min_map_address(), kMachOTextBase)`, and find_min_map_address()
    // returns the lowest page THIS PROCESS can place - above its own low image -
    // so the code page is below it by construction and the floor ended up above
    // the exact page the arena exists to cover.
    ArenaProbe ours_low;
    ours_low.place = [](std::uint64_t base, std::uint64_t) {
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
    RS_CHECK_EQ(walk.held_by_probe, std::size_t{12});
}

RS_TEST(skipped_windows_are_counted_so_the_note_keeps_its_meaning) {
    const ArenaWalk walk = walk_arena("test arena", kTextBase, kArenaTop, kPage,
                                      kWindow, measured_macos_runner());
    // Without the count, the note reports a handful of windows for twelve GiB of
    // address space that was resolved without being probed - a true number
    // answering a question nobody asked.
    RS_CHECK(walk.skipped > 0);
    RS_CHECK_EQ(walk.skipped + walk.held_no_access,
                (kDenyEnd - kDenyStart) / kWindow);
}

RS_TEST(a_host_that_refuses_everything_structurally_establishes_nothing) {
    ArenaProbe all_refused;
    all_refused.place = [](std::uint64_t, std::uint64_t) {
        return ArenaPlacement::Refused;
    };
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
             {kTextBase, kTextBase},
             {kArenaTop, kTextBase},
             {kTextBase, kTextBase + kWindow - 1}}) {
        const ArenaWalk walk =
            walk_arena("test arena", c.first, c.second, kPage, kWindow, never);
        RS_CHECK(walk.available.empty());
        RS_CHECK(walk.unavailable.empty());
    }
    const ArenaWalk zero =
        walk_arena("test arena", kTextBase, kArenaTop, kPage, 0, never);
    RS_CHECK(zero.available.empty());
}

RS_TEST(the_walk_stops_at_the_top_of_the_address_space_without_wrapping) {
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
    const std::uint64_t la57 = arena_ceiling_for(0xfffffffffff000ull, kTiB);
    RS_CHECK_EQ(la57, 0x800000000000ull);
    const std::uint64_t normal = arena_ceiling_for(0x7ffffffff000ull, kTiB);
    RS_CHECK_EQ(normal, 0x800000000000ull);
    RS_CHECK_EQ(la57, normal);
}

RS_TEST(the_ceiling_rounds_up_never_down) {
    constexpr std::uint64_t kTiB = 1ull << 40;
    RS_CHECK(arena_ceiling_for(0x7ffffffff000ull, kTiB) > 0x7ffffffff000ull);
    RS_CHECK_EQ(arena_ceiling_for(kTiB, kTiB), kTiB);
    RS_CHECK_EQ(arena_ceiling_for(kTiB + 1, kTiB), 2 * kTiB);
    RS_CHECK_EQ(arena_ceiling_for(4096, kTiB), kTiB);
    RS_CHECK_EQ(arena_ceiling_for(0x7ffffffff000ull, 0), 0x7ffffffff000ull);
    RS_CHECK(arena_ceiling_for(~std::uint64_t{0}, kTiB) >= 0x800000000000ull);
}

RS_TEST_MAIN("arena walk")

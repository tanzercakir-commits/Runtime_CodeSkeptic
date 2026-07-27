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
#include "runtimeskeptic/probe/windows_regions.hpp"
#include "runtimeskeptic/core/json.hpp"

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

// --- the measured Windows runner layout -----------------------------------
//
// From the note `a8bc15f` published off a Windows Server 2025 runner. The
// arena is the top TiB, where 99.8% of the process's 4.05 GiB actually sits;
// 64 MiB windows put 16,384 placements in it, within a rounding error of the
// macOS arena's 15,360.
constexpr std::uint64_t kWinTiB = 1ull << 40;
constexpr std::uint64_t kWinMax = 0x7fffffff0000ull;   // lpMaximumApplicationAddress + 1
constexpr std::uint64_t kWinFloor = 0x7f0000000000ull; // the bucket holding 99.8%
constexpr std::uint64_t kWinWindow = 64ull << 20;
constexpr std::uint64_t kWinGranularity = 65536;       // NOT dwPageSize, which is 4096

using Occupied = std::vector<std::pair<std::uint64_t, std::uint64_t>>;

// `VirtualQuery` over a synthetic map: the ONE thing a test has to fake, and the
// smallest thing it could be. Everything the Windows arena decides from here on
// is the real `classify_window` / `region_at_is_occupied` from
// `probe/windows_regions.cpp` - the same code the probe calls.
//
// Reports the containing region for an occupied address, and otherwise the free
// gap running up to the next occupied region, which is what VirtualQuery does.
RegionQuery query_over(const Occupied& occupied, std::uint64_t space_end) {
    return [occupied, space_end](std::uint64_t address, MemRegion& out) -> bool {
        if (address >= space_end) return false;
        for (const auto& r : occupied) {
            if (address >= r.first && address < r.second) {
                out.base = r.first;
                out.size = r.second - r.first;
                out.free = false;
                out.state = "MEM_COMMIT";
                return true;
            }
        }
        std::uint64_t gap_end = space_end;
        for (const auto& r : occupied) {
            if (r.first > address) gap_end = std::min(gap_end, r.first);
        }
        out.base = address;
        out.size = gap_end - address;
        out.free = true;
        out.state = "MEM_FREE";
        return true;
    };
}

// WINDOWS SEMANTICS, WITH THE DECISIONS MADE BY THE PROBE'S OWN CODE:
//
//   VirtualAlloc succeeds                        -> Placed
//   fails, and the region AT THE BASE is ours    -> HeldByProbe   (EEXIST's analogue)
//   fails, base free, something else in window   -> Refused + classify_window covers
//   fails, whole window free                     -> Refused + classify_window does not
//
// Only the VirtualAlloc outcome is modelled here; `region_at_is_occupied` and
// `classify_window` are called for real. A test that mirrors an implementation
// proves the mirror.
// `window` is a parameter and not the constant it started as: the low arena
// walks in 64 GiB windows, and a describe hook that scanned a hardcoded 64 MiB
// would miss a blocker 256 MiB into the window - which is exactly what the
// first version of the 64 GiB case caught, in the test rather than on a runner.
ArenaProbe windows_probe(Occupied occupied, Occupied structural = {},
                         std::uint64_t window = kWinWindow) {
    ArenaProbe p;
    const RegionQuery query = query_over(occupied, kWinMax);
    p.place = [occupied, structural, query](std::uint64_t base,
                                            std::uint64_t size) {
        const std::uint64_t end = base + size;
        const bool refused_by_host =
            std::any_of(structural.begin(), structural.end(), [&](const auto& r) {
                return base < r.second && end > r.first;
            });
        const bool overlaps_ours =
            std::any_of(occupied.begin(), occupied.end(), [&](const auto& r) {
                return base < r.second && end > r.first;
            });
        if (!refused_by_host && !overlaps_ours) return ArenaPlacement::Placed;
        if (region_at_is_occupied(base, query)) return ArenaPlacement::HeldByProbe;
        return ArenaPlacement::Refused;
    };
    p.describe = [query, window](std::uint64_t base) {
        return classify_window(base, window, query, "ERROR_INVALID_ADDRESS");
    };
    return p;
}

// One plausible draw of high-entropy ASLR in the top TiB: the image, a few DLL
// clusters, a thread stack and two heaps. Parameterised by a slide, because the
// load-bearing property is that MOVING all of it changes nothing recorded.
Occupied windows_layout(std::uint64_t slide) {
    const std::uint64_t b = kWinFloor + slide;
    return {
        {b + 0x0a000000ull, b + 0x0a800000ull},   // the exe image
        {b + 0x1f000000ull, b + 0x21400000ull},   // a DLL cluster, ~36 MiB
        {b + 0x40000000ull, b + 0x40100000ull},   // a thread stack
        {b + 0x91000000ull, b + 0x95000000ull},   // a heap, 64 MiB: spans windows
        {b + 0xd2000000ull, b + 0xd2040000ull},   // a small private block
    };
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

RS_TEST(a_held_entry_reaching_past_the_top_does_not_push_the_range_past_it) {
    // `available_ranges: 22 vs 22 entries` on the macOS runner - the same COUNT
    // with different CONTENTS, after the counts had already been made stable.
    //
    // The skip past an already-described entry sets the run's end from that
    // entry's extent, which is a platform-reported value that moves with this
    // task's own layout. If the entry reaches beyond the arena's ceiling, the
    // emitted range ended above `top` at a position that differed between two runs
    // of one binary.
    //
    // A walk bounded by [bottom, top) must not emit a range outside it, which is
    // true whether or not anything moves.
    constexpr std::uint64_t kTop = kTextBase + 16 * kWindow;
    // A no-access region of ours that starts inside the arena and runs far past
    // its ceiling.
    ArenaProbe overhanging =
        with_our_no_access_at(kTextBase + 8 * kWindow, kTop + 4096 * kWindow);

    const ArenaWalk walk =
        walk_arena("test arena", kTextBase, kTop, kPage, kWindow, overhanging);
    for (const auto& r : walk.available) {
        RS_CHECK_MESSAGE(r.range.end <= kTop,
                         "an available range ends past the arena ceiling: " +
                             r.range.to_string() + " with top " +
                             json::to_hex(kTop));
        RS_CHECK(r.range.start >= kTextBase);
    }
    for (const auto& r : walk.unavailable) {
        RS_CHECK(r.range.start >= kTextBase);
    }
    RS_CHECK(!walk.available.empty());
}

// ---------------------------------------------------------------------------
// The Windows arena. Same walk, third platform, and the first one whose bounds
// were measured before they were written.
// ---------------------------------------------------------------------------
RS_TEST(the_windows_arena_covers_the_top_tib_where_the_runner_said_things_are) {
    const ArenaWalk walk =
        walk_arena("test arena", kWinFloor, kWinMax, kWinGranularity, kWinWindow,
                   windows_probe(windows_layout(0)));

    // Everything of ours is transparent, so one contiguous range, not six.
    RS_CHECK_MESSAGE(walk.available.size() == 1,
                     "expected ONE merged range, got " + show(bounds(walk.available)));
    RS_CHECK(walk.unavailable.empty());
    RS_CHECK(walk.available.front().range.start == kWinFloor);
    RS_CHECK(walk.available.front().range.end <= kWinMax);

    // And it must actually answer for where a Windows program lives. The runner's
    // own occupied span ended at 0x7ff9fa967000.
    RS_CHECK_MESSAGE(covered(walk.available, 0x7ff9fa967000ull - 4096),
                     "the arena does not cover the top of the runner's own "
                     "occupied span, which is where its image and heaps were");
}

RS_TEST(the_windows_output_does_not_move_when_aslr_redraws) {
    // THE LOAD-BEARING TEST, and the Windows half of the one that took five
    // rounds on macOS. High-entropy ASLR redraws every load address on every
    // run; if any of that reaches the recorded set, `profile_id` names our
    // morning rather than the host - which is what `min_map_address` did once
    // and what six campaign contracts returned confident UNSUPPORTED off.
    std::vector<std::vector<std::pair<std::uint64_t, std::uint64_t>>> seen;
    for (std::uint64_t slide : {0ull, 0x3000000ull, 0x1c800000ull,
                                0x64000000ull, 0xf0000000ull}) {
        const ArenaWalk walk =
            walk_arena("test arena", kWinFloor, kWinMax, kWinGranularity,
                       kWinWindow, windows_probe(windows_layout(slide)));
        RS_CHECK(walk.unavailable.empty());
        seen.push_back(bounds(walk.available));
    }
    for (std::size_t i = 1; i < seen.size(); ++i) {
        RS_CHECK_MESSAGE(seen[i] == seen[0],
                         "the recorded set moved with our own layout: draw 0 gave " +
                             show(seen[0]) + " and draw " + std::to_string(i) +
                             " gave " + show(seen[i]));
    }
}

RS_TEST(a_windows_refusal_with_the_window_wholly_free_is_a_host_limitation) {
    // The other half: something that is NOT ours must still be recorded, or the
    // fix for irreproducibility has bought it by recording nothing at all.
    const std::uint64_t band_start = kWinFloor + 0x50000000ull;
    const std::uint64_t band_end = band_start + kWinWindow;
    const ArenaWalk walk =
        walk_arena("test arena", kWinFloor, kWinMax, kWinGranularity, kWinWindow,
                   windows_probe(windows_layout(0), {{band_start, band_end}}));

    RS_CHECK_MESSAGE(walk.unavailable.size() == 1,
                     "expected the structural band to be recorded, got " +
                         show(bounds(walk.unavailable)));
    RS_CHECK(walk.unavailable.front().range.start == band_start);
    RS_CHECK(walk.refused == 1);
    // And it splits the run, so nothing claims the band is available.
    RS_CHECK(walk.available.size() == 2);
    for (const auto& r : walk.available) {
        RS_CHECK_MESSAGE(r.range.end <= band_start || r.range.start >= band_end,
                         "an available range overlaps the structural band: " +
                             r.range.to_string());
    }
}

RS_TEST(a_dll_in_the_middle_of_a_window_is_not_a_host_limitation) {
    // Asking VirtualQuery at the window's BASE alone would answer MEM_FREE here
    // and file our own loader's choice as a fact about Windows. The probe scans
    // the whole window; this is that requirement, stated as a test.
    const std::uint64_t window_base = kWinFloor + 4 * kWinWindow;
    const std::uint64_t dll = window_base + kWinWindow / 2;
    const ArenaWalk walk =
        walk_arena("test arena", kWinFloor, kWinMax, kWinGranularity, kWinWindow,
                   windows_probe({{dll, dll + 0x100000ull}}));

    RS_CHECK_MESSAGE(walk.unavailable.empty(),
                     "a mapping of ours in the middle of a window was recorded as "
                     "a host limitation: " + show(bounds(walk.unavailable)));
    RS_CHECK(walk.held_no_access == 1);   // ambiguous-but-held, counted for the note
    RS_CHECK(walk.available.size() == 1);
}

RS_TEST(kuser_shared_data_is_below_the_windows_arena_by_measurement) {
    // `arena_walk`'s treat-a-covered-refusal-as-held rule is sound only while no
    // system-wide band lies inside the arena. On Windows there is one obvious
    // candidate - KUSER_SHARED_DATA, mapped into every x64 process - and the
    // runner printed its address as the lowest thing it occupied.
    constexpr std::uint64_t kKuserSharedData = 0x7ffe0000ull;
    RS_CHECK(arena_floor_for(kWinMax, kWinTiB) == kWinFloor);
    RS_CHECK_MESSAGE(kKuserSharedData < arena_floor_for(kWinMax, kWinTiB),
                     "KUSER_SHARED_DATA is inside the arena, so a system band "
                     "would be silently swallowed as one of ours");
}

RS_TEST(the_two_windows_arenas_tile_without_a_gap_or_an_overlap) {
    // The top arena went out alone because the occupancy note said 99.8% of the
    // process was in the top TiB. It was right about the image and wrong about
    // the heap: `test_probe` on the runner allocated at 0x2f78000e000 - 2.97 TiB -
    // and the coverage test failed with `gap 0x7c087fff2000` against an arena
    // that had otherwise worked perfectly (16312 placed, 0 refused).
    constexpr std::uint64_t kLowBottom = kWinTiB;
    const std::uint64_t boundary = arena_floor_for(kWinMax, kWinTiB);

    // Tiling: the low arena ends exactly where the top one begins.
    RS_CHECK(kLowBottom < boundary);
    RS_CHECK(boundary < kWinMax);

    // The two addresses the runner actually reported, each in the right arena.
    constexpr std::uint64_t kMeasuredHeapPage = 0x2f78000e000ull;
    RS_CHECK_MESSAGE(kMeasuredHeapPage >= kLowBottom &&
                         kMeasuredHeapPage < boundary,
                     "the heap page the Windows runner reported is not in the "
                     "low arena: " + json::to_hex(kMeasuredHeapPage));
    // And the top arena still holds the image region it was built for.
    RS_CHECK(0x7ff9fa967000ull >= boundary && 0x7ff9fa967000ull < kWinMax);

    // KUSER_SHARED_DATA stays outside BOTH, which is what makes
    // treat-a-covered-refusal-as-held sound in either of them.
    RS_CHECK_MESSAGE(0x7ffe0000ull < kLowBottom,
                     "KUSER_SHARED_DATA is inside the low arena, so a "
                     "system-wide band would be swallowed as one of ours");
}

RS_TEST(a_sixty_four_gib_window_walk_still_merges_to_one_range) {
    // The low arena spans 126 TiB, so its window is 64 GiB rather than 64 MiB:
    // 2016 contiguous placements instead of two million. Contiguous matters -
    // Linux samples at this stride and therefore asserts the space between its
    // samples, which is what `walk_arena`'s header argues against.
    constexpr std::uint64_t kBig = 64ull << 30;
    constexpr std::uint64_t kBottom = kWinTiB;
    constexpr std::uint64_t kTop = kBottom + 64 * kBig;   // 4 TiB of arena

    const ArenaWalk walk = walk_arena(
        "test arena", kBottom, kTop, kWinGranularity, kBig,
        windows_probe({{kBottom + 3 * kBig + 0x10000000ull,
                        kBottom + 3 * kBig + 0x10800000ull},   // a heap, mid-window
                       {kBottom + 40 * kBig, kBottom + 40 * kBig + 0x40000ull}},
                      {}, kBig));

    RS_CHECK_MESSAGE(walk.available.size() == 1,
                     "expected one merged range across the 64 GiB windows, got " +
                         show(bounds(walk.available)));
    RS_CHECK(walk.unavailable.empty());
    RS_CHECK(walk.available.front().range.start == kBottom);
    RS_CHECK(walk.available.front().range.end == kTop);
    RS_CHECK(walk.placed + walk.held_by_probe + walk.held_no_access == 64);
}

// ---------------------------------------------------------------------------
// The second arena, on the third platform to need one.
// ---------------------------------------------------------------------------
RS_TEST(the_macos_high_arena_sits_below_the_measured_ceiling_and_above_the_low_one) {
    // The numbers a runner printed on `d6abf18`, after the ceiling was fixed.
    constexpr std::uint64_t kNativeCeiling = 0x7ffffe000000ull;
    constexpr std::uint64_t kRosettaCeiling = 0x7ff800000000ull;
    constexpr std::uint64_t kSpan = 4ull << 40;          // 4 TiB
    constexpr std::uint64_t kCommpage = 0xfc0000000ull;  // the low arena's top

    const std::uint64_t native = high_arena_floor(kNativeCeiling, kSpan, kCommpage);
    const std::uint64_t rosetta = high_arena_floor(kRosettaCeiling, kSpan, kCommpage);
    RS_CHECK(native == kNativeCeiling - kSpan);
    RS_CHECK(rosetta == kRosettaCeiling - kSpan);
    RS_CHECK_MESSAGE(native > kCommpage && rosetta > kCommpage,
                     "the high arena reaches back into the low one, so an address "
                     "would be recorded twice");

    // AND IT MUST COVER THE HEAP THAT EXPOSED ALL OF THIS. The Rosetta lane's
    // heap page was 0x7f9ab0028000, 140 TiB above the native arena, and the
    // coverage test failed on exactly that address.
    constexpr std::uint64_t kRosettaHeapPage = 0x7f9ab0028000ull;
    RS_CHECK_MESSAGE(kRosettaHeapPage >= rosetta && kRosettaHeapPage < kRosettaCeiling,
                     "the high arena does not cover the translated heap page it "
                     "was built for: " + json::to_hex(kRosettaHeapPage) +
                         " against [" + json::to_hex(rosetta) + ", " +
                         json::to_hex(kRosettaCeiling) + ")");
}

RS_TEST(a_ceiling_nobody_measured_places_no_arena) {
    constexpr std::uint64_t kSpan = 4ull << 40;
    constexpr std::uint64_t kCommpage = 0xfc0000000ull;
    // 0 means the probe could not pin the ceiling down. Placing an arena from a
    // guessed ceiling is the defect that put macOS 35 TiB out in the first place.
    RS_CHECK(high_arena_floor(0, kSpan, kCommpage) == 0);
    // A ceiling at or below the span leaves nothing to walk.
    RS_CHECK(high_arena_floor(kSpan, kSpan, kCommpage) == 0);
    RS_CHECK(high_arena_floor(kSpan / 2, kSpan, kCommpage) == 0);
    // And the overlap refusal: a ceiling low enough that the high arena would
    // reach back into the low one yields no arena rather than a double record.
    RS_CHECK(high_arena_floor(kSpan + kCommpage, kSpan, kCommpage) == 0);
    RS_CHECK(high_arena_floor(kSpan + kCommpage + 1, kSpan, kCommpage) ==
             kCommpage + 1);
    RS_CHECK(high_arena_floor(0x7ffffe000000ull, 0, kCommpage) == 0);
}

// ---------------------------------------------------------------------------
// The ladder's one decision. Both ladders broke the arenas' rule, differently.
// ---------------------------------------------------------------------------
RS_TEST(a_placed_landmark_and_a_held_one_record_the_identical_entry) {
    // THE LOAD-BEARING CASE, and it is byte equality rather than "both
    // available" on purpose. macOS recorded both as available and still moved
    // `profile_id` between two runs of one binary for two days, because the two
    // branches wrote different `note` text and the note is hashed. Equal
    // outcomes were never the property that was missing.
    const LadderRecord placed =
        ladder_record(ArenaPlacement::Placed, "mmap(MAP_FIXED_NOREPLACE)", "");
    const LadderRecord held = ladder_record(ArenaPlacement::HeldByProbe,
                                            "mmap(MAP_FIXED_NOREPLACE)", "");

    RS_CHECK(placed.outcome == LadderOutcome::Available);
    RS_CHECK(held.outcome == LadderOutcome::Available);
    RS_CHECK_MESSAGE(placed.note == held.note,
                     "the note differs between a landmark we placed and one we "
                     "already held, so profile_id moves with our ASLR slide:\n"
                     "  placed: " + placed.note + "\n  held:   " + held.note);
}

RS_TEST(the_refusal_text_reaches_the_recorded_note_and_only_then) {
    const LadderRecord refused =
        ladder_record(ArenaPlacement::Refused, "mmap(MAP_FIXED_NOREPLACE)",
                      "ENOMEM");
    RS_CHECK(refused.outcome == LadderOutcome::Unavailable);
    RS_CHECK(refused.note.find("ENOMEM") != std::string::npos);

    // A refusal reason must not leak into an available entry. `errno` after a
    // SUCCESSFUL call is whatever the last failure left there, and a note
    // carrying it would move with the libc call history rather than the host.
    const LadderRecord placed = ladder_record(
        ArenaPlacement::Placed, "mmap(MAP_FIXED_NOREPLACE)", "ENOMEM");
    RS_CHECK_MESSAGE(placed.note.find("ENOMEM") == std::string::npos,
                     "a refusal reason reached an available entry: " +
                         placed.note);
}

RS_TEST(the_platforms_differ_in_wording_and_not_in_what_is_recorded) {
    // Both probes call this, and they must disagree about nothing except the
    // name of the call they made.
    const LadderRecord linux_held = ladder_record(
        ArenaPlacement::HeldByProbe, "mmap(MAP_FIXED_NOREPLACE)", "");
    const LadderRecord macos_held = ladder_record(
        ArenaPlacement::HeldByProbe, "mach_vm_allocate(VM_FLAGS_FIXED)", "");

    RS_CHECK(linux_held.outcome == macos_held.outcome);
    RS_CHECK(linux_held.note != macos_held.note);   // the call name, and nothing else
    const std::string tail = "does not name the host";
    RS_CHECK(linux_held.note.size() > tail.size());
    RS_CHECK(linux_held.note.substr(linux_held.note.size() - tail.size()) == tail);
    RS_CHECK(macos_held.note.substr(macos_held.note.size() - tail.size()) == tail);
}

RS_TEST(an_arena_floor_on_an_exact_multiple_is_not_an_empty_arena) {
    // `(max / span) * span` returns the ceiling itself here, and the arena would
    // be empty: a probe that establishes nothing, silently, which is the exact
    // condition this whole exercise exists to end.
    constexpr std::uint64_t kSpan = 1ull << 40;
    RS_CHECK(arena_floor_for(128 * kSpan, kSpan) == 127 * kSpan);
    RS_CHECK(arena_floor_for(128 * kSpan - 4096, kSpan) == 127 * kSpan);
    // A host too small for a distinct top region gets no arena rather than a bad one.
    RS_CHECK(arena_floor_for(kSpan, kSpan) == 0);
    RS_CHECK(arena_floor_for(0, kSpan) == 0);
    RS_CHECK(arena_floor_for(kWinMax, 0) == 0);
}

RS_TEST_MAIN("arena walk")

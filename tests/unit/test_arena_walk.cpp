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

RS_TEST_MAIN("arena walk")

// SPDX-License-Identifier: Apache-2.0
#include "runtimeskeptic/probe/arena_walk.hpp"

#include <algorithm>
#include <string>

#include "runtimeskeptic/core/json.hpp"

namespace rs::probe {

using rs::EvidenceClass;
using vm::AddressRange;
using vm::ClassifiedRange;

std::uint64_t arena_ceiling_for(std::uint64_t max_user_address,
                                std::uint64_t granularity) {
    // The window Linux allocates in without an explicit high hint. See the
    // header for why TASK_SIZE is the wrong input.
    constexpr std::uint64_t kDefaultMapWindow = 1ull << 47;
    if (granularity == 0) return max_user_address;
    const std::uint64_t effective = max_user_address < kDefaultMapWindow
                                        ? max_user_address
                                        : kDefaultMapWindow;
    // Round UP: rounding down put the whole arena 4 TiB below where anything
    // maps, into the exact bucket 629 of 639 observed addresses sit above.
    if (effective > ~std::uint64_t{0} - granularity) return effective;
    return ((effective + granularity - 1) / granularity) * granularity;
}

std::uint64_t arena_floor_for(std::uint64_t max_user_address,
                              std::uint64_t span) {
    // Two spans, not one: an arena that would start at 0 is not an arena, and a
    // host that small has no top region distinct from its bottom one.
    if (span == 0 || max_user_address < 2 * span) return 0;
    // `max - 1` so an exact multiple yields the bucket BELOW it rather than an
    // empty arena starting at the ceiling. See the header.
    return ((max_user_address - 1) / span) * span;
}

std::uint64_t high_arena_floor(std::uint64_t max_user_address,
                               std::uint64_t span,
                               std::uint64_t must_stay_above) {
    if (max_user_address == 0 || span == 0) return 0;
    if (max_user_address <= span) return 0;
    const std::uint64_t floor = max_user_address - span;
    if (floor <= must_stay_above) return 0;
    return floor;
}

LadderRecord ladder_record(ArenaPlacement placement,
                           const std::string& placement_call,
                           const std::string& refusal_text) {
    LadderRecord out;
    if (placement == ArenaPlacement::Refused) {
        out.outcome = LadderOutcome::Unavailable;
        out.note = "exact placement refused: " + refusal_text;
        return out;
    }
    // ONE STRING FOR BOTH, and it is the point of this function rather than a
    // tidying of it. `Placed` and `HeldByProbe` fall through to the same return,
    // so a future edit cannot make them diverge without deleting this comment.
    out.outcome = LadderOutcome::Available;
    out.note = placement_call +
               " establishes that the kernel grants this exact address to this "
               "process: it either placed a mapping here or reported the address "
               "already held by a mapping of this process. WHICH OF THE TWO is a "
               "property of the probe's own ASLR slide and is deliberately not "
               "recorded - not in the count, not in the bounds, and not in this "
               "sentence, because the note is inside the hashed facts subtree and "
               "a profile_id that names our morning does not name the host";
    return out;
}

ArenaWalk walk_arena(const std::string& what, std::uint64_t bottom,
                     std::uint64_t top, std::uint64_t page_size,
                     std::uint64_t window_size, const ArenaProbe& probe) {
    ArenaWalk out;
    if (window_size == 0 || page_size == 0) return out;
    if (bottom >= top || top - bottom < window_size) return out;

    bool in_run = false;
    std::uint64_t run_start = 0;
    std::uint64_t last_window_end = 0;

    auto close_run = [&](std::uint64_t end) {
        if (!in_run) return;
        in_run = false;
        // Never past the arena's own ceiling. The caller's bounds are the claim's
        // bounds; anything beyond them was not walked.
        if (end > top) end = top;
        if (end <= run_start) return;
        ClassifiedRange cr;
        cr.range = AddressRange{run_start, end};
        cr.evidence = EvidenceClass::MeasuredCapability;
        cr.note = "inside the " + what + ", walked in contiguous windows of " +
                  json::to_hex(window_size) +
                  " bytes. Every window in this range was placed at its exact "
                  "address or was already held by the probe process, and none "
                  "was structurally refused. The windows touch, so nothing "
                  "between them is asserted without having been placed. Which "
                  "windows were which depends on the probe's own layout and is "
                  "deliberately not recorded";
        out.available.push_back(cr);
    };

    for (std::uint64_t base = bottom; base + window_size <= top;
         base += window_size) {
        if (base % page_size != 0) continue;
        const auto window = AddressRange::from_base_size(base, window_size);
        if (!window) break;   // would wrap past the end of the address space

        const ArenaPlacement p = probe.place(base, window_size);
        if (p != ArenaPlacement::Refused) {
            if (p == ArenaPlacement::Placed) ++out.placed;
            else ++out.held_by_probe;
            if (!in_run) {
                run_start = base;
                in_run = true;
            }
            last_window_end = base + window_size;
            continue;
        }

        const ArenaEntry entry = probe.describe(base);

        // AMBIGUOUS, AND RESOLVED THE SAME WAY EEXIST IS.
        //
        // A no-access region of this task covers the window. That is either one of
        // ours - a malloc guard, a dyld reservation, a thread stack guard - or a
        // band the platform puts in every task. Nothing distinguishable here says
        // which, and treating it as a host limitation is what made this walk
        // irreproducible: 83 vs 74 unavailable entries across two runs of one
        // binary on one machine, because our own reservations move with ASLR.
        //
        // So it is treated as HELD: it says nothing about the host, exactly as
        // EEXIST does on Linux. That is sound only while no platform band lies
        // inside `[bottom, top)`, which is the caller's job - and `held_no_access`
        // is the number that exposes a violation, so it must reach the note.
        //
        // Crucially the run is NOT split, which is what makes the recorded ranges
        // independent of our layout. A structural refusal below still splits it.
        if (entry.covers) {
            ++out.held_no_access;
            if (!in_run) {
                run_start = base;
                in_run = true;
            }
            last_window_end = base + window_size;

            // Still skip the rest of the entry: probing inside it cannot return
            // anything new, and on the runner one such entry was twelve GiB.
            const std::uint64_t held_end = entry.start + entry.size;
            if (entry.size > 0 && held_end > base + window_size) {
                const std::uint64_t jump =
                    (held_end - (base + window_size)) / window_size;
                out.skipped += static_cast<std::size_t>(jump);
                // The run must reach where the walk resumes, or the skipped
                // windows become a hole in a range that is otherwise continuous.
                //
                // CLAMPED TO `top`, because `jump` is derived from the entry's
                // extent - a platform-reported value that moves with this task's
                // own layout. Unclamped, the last run could end ABOVE the arena's
                // own ceiling at a position that differs between two runs of one
                // binary: `available_ranges: 22 vs 22 entries` on the macOS runner,
                // the same count with different contents, after the counts had
                // already been made stable.
                //
                // A walk bounded by [bottom, top) must not emit a range outside it
                // under any circumstances, which is true independently of this bug.
                const std::uint64_t reached = base + (jump + 1) * window_size;
                last_window_end = reached < top ? reached : top;
                base += jump * window_size;
            }
            continue;
        }

        ++out.refused;
        // Close at the last PLACED window's end. With contiguous windows that is
        // exactly this window's base, so the available range and the refused
        // range meet with no gap and no overlap - but writing it as the last
        // window's end stays correct if a `page_size` skip ever makes the walk
        // non-contiguous. Closing further than the evidence reaches is how the
        // Linux arena once produced a range asserted both available and not.
        close_run(last_window_end);

        ClassifiedRange cr;
        cr.range = *window;
        cr.evidence = EvidenceClass::MeasuredCapability;
        cr.note = "exact placement refused inside the " + what + "; " +
                  entry.text;

        // NOTHING TO WIDEN TO, and the code that did it is gone rather than
        // guarded.
        //
        // Until the ambiguous case above existed, a refusal was widened to the
        // extent `describe` reported, clamped so it could not reach back into space
        // already placed - which was itself the fix for the probe reporting
        // [0x2a7224000, 0x2ae224000) as both available and unavailable on the macOS
        // runner. That widening applied exactly when a no-access entry covered the
        // window, and that is now the condition for treating the window as HELD, so
        // the widening branch became unreachable. A structural refusal has no
        // covering entry by definition - that is what makes it structural.
        //
        // Kept as a comment because the clamp cost a runner round trip to find, and
        // a future `describe` that reports an extent for a structural refusal will
        // need it back along with the reason.
        out.unavailable.push_back(cr);
    }
    close_run(last_window_end);
    return out;
}

ArenaWalk walk_arena_adaptive(const std::string& what,
                              std::uint64_t bottom, std::uint64_t top,
                              std::uint64_t page_size,
                              std::uint64_t max_window_size,
                              std::size_t max_attempts,
                              const ArenaProbe& probe) {
    ArenaWalk out;
    if (page_size == 0 || max_window_size == 0 || bottom >= top) return out;
    if (bottom % page_size != 0 || top % page_size != 0 ||
        max_window_size % page_size != 0 || !probe.place || !probe.describe) {
        return out;
    }
    if (max_attempts == 0) {
        out.budget_exhausted = true;
        return out;
    }

    const std::string available_note =
        "inside the " + what +
        ", established by exact placements with adaptive subdivision down to "
        "page granularity. Every byte in this range was covered by a placement "
        "that succeeded or by a page already mapped in this process. Large "
        "held or refused attempts were subdivided and never generalized";

    auto append = [](std::vector<ClassifiedRange>& ranges,
                     std::uint64_t start, std::uint64_t end,
                     const std::string& note) {
        if (start >= end) return;
        if (!ranges.empty() && ranges.back().range.end == start &&
            ranges.back().note == note) {
            ranges.back().range.end = end;
            return;
        }
        ClassifiedRange range;
        range.range = AddressRange{start, end};
        range.evidence = EvidenceClass::MeasuredCapability;
        range.note = note;
        ranges.push_back(range);
    };

    std::function<void(std::uint64_t, std::uint64_t)> visit;
    visit = [&](std::uint64_t base, std::uint64_t size) {
        if (out.attempts >= max_attempts) {
            out.budget_exhausted = true;
            return;
        }
        ++out.attempts;
        const ArenaPlacement placement = probe.place(base, size);
        if (placement == ArenaPlacement::Placed) {
            ++out.placed;
            append(out.available, base, base + size, available_note);
            return;
        }

        // EEXIST proves only that SOME page in a multi-page request overlaps an
        // existing VMA. Likewise ENOMEM may describe the requested SIZE (for
        // example RLIMIT_AS), not every address in it. Neither result can be
        // promoted to a fact about the whole tile.
        if (size > page_size) {
            const std::uint64_t page_count = size / page_size;
            const std::uint64_t left_size = (page_count / 2) * page_size;
            visit(base, left_size);
            if (!out.budget_exhausted) {
                visit(base + left_size, size - left_size);
            }
            return;
        }

        if (placement == ArenaPlacement::HeldByProbe) {
            ++out.held_by_probe;
            append(out.available, base, base + page_size, available_note);
            return;
        }

        const ArenaEntry entry = probe.describe(base);
        if (entry.covers) {
            ++out.held_no_access;
            append(out.available, base, base + page_size, available_note);
            return;
        }

        ++out.refused;
        const std::string detail = entry.text.empty()
                                       ? "the platform reported a structural refusal"
                                       : entry.text;
        append(out.unavailable, base, base + page_size,
               "exact page placement refused inside the " + what + "; " +
                   detail);
    };

    for (std::uint64_t base = bottom;
         base < top && !out.budget_exhausted;) {
        const std::uint64_t remaining = top - base;
        const std::uint64_t size =
            remaining < max_window_size ? remaining : max_window_size;
        visit(base, size);
        base += size;
    }
    if (out.budget_exhausted) {
        // A partial prefix would depend on how this process's ASLR layout spent
        // the recursion budget. Keep the counters for diagnostics, but keep all
        // partially measured ranges out of host facts and profile_id.
        out.available.clear();
        out.unavailable.clear();
    }
    return out;
}

}  // namespace rs::probe

// SPDX-License-Identifier: Apache-2.0
#include "runtimeskeptic/probe/arena_walk.hpp"

#include <algorithm>
#include <string>

#include "runtimeskeptic/core/json.hpp"

namespace rs::probe {

using rs::EvidenceClass;
using vm::AddressRange;
using vm::ClassifiedRange;

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
        const ArenaEntry entry = probe.describe(base);
        cr.note = "exact placement refused inside the " + what + "; " +
                  entry.text;

        const std::uint64_t entry_end = entry.start + entry.size;
        const bool widened =
            entry.covers && entry.size > 0 && entry_end > entry.start;
        if (widened) {
            cr.range.start = std::min(cr.range.start, entry.start);
            cr.range.end = std::max(cr.range.end, entry_end);
            cr.note += "; placement was probed at " + json::to_hex(base) +
                       " and the refusal is recorded across the entry's full "
                       "extent as the platform reported it, rather than across "
                       "the window that happened to be probed";
        }

        // Do not record one entry once per window. On the runner that exposed
        // this gap a single no-access entry is twelve GiB wide - 3067 windows,
        // every one widening to the identical range. The caller's
        // collapse_contained_ranges() reduces them to one regardless, so this
        // changes no fact; it stops the probe building 3067 identical note
        // strings in order to discard 3066 of them.
        if (out.unavailable.empty() ||
            !(out.unavailable.back().range == cr.range)) {
            out.unavailable.push_back(cr);
        }

        // And do not keep probing inside an entry the platform has already
        // described. The widening above rests on the argument that an entry
        // granting no access refuses placement everywhere inside itself by
        // construction, so a window inside the same entry cannot return anything
        // new. This is that argument applied to the walk rather than to the
        // record: if skipping were unsound, the widening would already be.
        //
        // Counted, because `refused` otherwise silently changes meaning - on the
        // runner it would have read "1 structurally refused" for twelve GiB.
        if (widened && entry_end > base + window_size) {
            out.skipped += static_cast<std::size_t>(
                (entry_end - (base + window_size)) / window_size);
            base = entry_end - window_size;
        }
    }
    close_run(last_window_end);
    return out;
}

}  // namespace rs::probe

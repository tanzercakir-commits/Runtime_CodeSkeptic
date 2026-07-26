// SPDX-License-Identifier: Apache-2.0
#include "runtimeskeptic/probe/windows_regions.hpp"

#include "runtimeskeptic/core/json.hpp"

namespace rs::probe {
namespace {

// Bounds on both loops, so that a pathological or hostile layout turns into a
// wrong-but-terminating answer rather than a hung probe. A 64 MiB window holding
// more than 4096 distinct regions is already beyond anything a real process
// produces; the runner's whole 128 TiB space had 137.
constexpr unsigned kMaxRegionsPerWindow = 4096;

}  // namespace

bool region_at_is_occupied(std::uint64_t address, const RegionQuery& query) {
    MemRegion region;
    if (!query(address, region)) return false;
    return !region.free;
}

ArenaEntry classify_window(std::uint64_t base, std::uint64_t window,
                           const RegionQuery& query,
                           const std::string& refusal) {
    ArenaEntry entry;
    if (window == 0) {
        entry.text = "a zero-width window describes nothing";
        return entry;
    }
    const std::uint64_t window_end = base + window;
    if (window_end < base) {   // wrapped past the end of the address space
        entry.text = "a window that wraps the address space describes nothing";
        return entry;
    }

    std::uint64_t at = base;
    for (unsigned steps = 0; steps < kMaxRegionsPerWindow && at < window_end;
         ++steps) {
        MemRegion region;
        if (!query(at, region)) break;
        const std::uint64_t region_end = region.base + region.size;
        // No forward progress: stop rather than spin. A query that reports a
        // region ending at or below where we asked is either lying or has
        // wrapped, and neither is a reason to loop forever.
        if (region.size == 0 || region_end <= at) break;

        if (!region.free) {
            std::uint64_t end = region_end;
            for (unsigned more = 0; more < kMaxRegionsPerWindow; ++more) {
                MemRegion next;
                if (!query(end, next)) break;
                if (next.free) break;
                const std::uint64_t next_end = next.base + next.size;
                if (next.size == 0 || next_end <= end) break;
                end = next_end;
            }
            entry.covers = true;
            entry.start = region.base;
            entry.size = end - region.base;
            entry.text = "a region of THIS PROCESS covers it - VirtualQuery "
                         "reports " +
                         (region.state.empty() ? std::string("a non-free state")
                                               : region.state) +
                         " over [" + json::to_hex(region.base) + ", " +
                         json::to_hex(end) + "), so the refusal (" + refusal +
                         ") is our own image, heap or stack and says nothing "
                         "about the host";
            return entry;
        }
        at = region_end;
    }

    entry.text = "VirtualQuery reports every region of this window MEM_FREE, so "
                 "nothing of this process is in the way and the refusal (" +
                 refusal + ") is the system's";
    return entry;
}

}  // namespace rs::probe

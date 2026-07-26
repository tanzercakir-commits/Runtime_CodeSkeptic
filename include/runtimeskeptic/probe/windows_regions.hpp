// SPDX-License-Identifier: Apache-2.0
#ifndef RUNTIMESKEPTIC_PROBE_WINDOWS_REGIONS_HPP
#define RUNTIMESKEPTIC_PROBE_WINDOWS_REGIONS_HPP

// Deciding whose a Windows placement refusal is, with `VirtualQuery` injected.
//
// WHY THIS IS NOT IN `vm_probe_windows.cpp`.
//
// `arena_walk.hpp` exists because the arena logic lived in `vm_probe_macos.cpp`,
// which no machine in this project can compile except a macOS runner - so
// verifying it meant pushing and waiting, and the first version was wrong in a
// way a test would have caught instantly. This is the same argument one level
// down, and the Windows evidence is stronger than the macOS evidence was:
//
//   - the Windows probe was in the tree for a day before anyone noticed it had
//     never once executed, on any machine, including Windows
//   - `test_probe` was 14/14 on a real Windows runner while the probe
//     established zero address ranges
//   - the diagnostics channel published "Missing -C <config>?" in place of every
//     Windows failure for the life of the repository
//
// Three chances to notice, none taken, because everything about Windows here is
// reachable only from Windows. So the decision below - which is the only genuinely
// new reasoning in the Windows arena - is a pure function over what `VirtualQuery`
// reports, and `tests/unit/test_arena_walk.cpp` drives THE REAL CODE with the
// layout a runner measured, on every platform this project builds on.
//
// A test that mirrors an implementation proves the mirror. This is the
// implementation.

#include <cstdint>
#include <functional>
#include <string>

#include "runtimeskeptic/probe/arena_walk.hpp"

namespace rs::probe {

// One `MEMORY_BASIC_INFORMATION`, reduced to what the decision needs. `free` is
// `State == MEM_FREE`; nothing else about the state changes the answer, because
// on Windows ANY non-free region refuses a placement regardless of its
// protection - which is why the macOS refinement (no-access entries only) has no
// analogue here and must not be imitated.
struct MemRegion {
    std::uint64_t base = 0;
    std::uint64_t size = 0;
    bool free = true;
    std::string state;   // "MEM_FREE" / "MEM_RESERVE" / "MEM_COMMIT", for the note
};

// `VirtualQuery`, injected. False means the query failed.
using RegionQuery = std::function<bool(std::uint64_t address, MemRegion& out)>;

// Is the region AT `address` occupied by this process?
//
// THE EEXIST ANALOGUE. Linux answers EEXIST when MAP_FIXED_NOREPLACE hits one of
// our own mappings, and the arena counts that exactly as a success: it proves the
// kernel hands this space out and says nothing about the host. `VirtualAlloc`
// reports ERROR_INVALID_ADDRESS whether the range is ours or the system's, so on
// Windows the distinction has to be asked for separately. Asking at the base is
// the faithful analogue - something of ours is right here - and keeping it
// separate from `classify_window` is what leaves `held_no_access` meaning what it
// means on macOS: the genuinely ambiguous case, where the blocker is elsewhere.
bool region_at_is_occupied(std::uint64_t address, const RegionQuery& query);

// Whose is a refusal of `[base, base + window)`?
//
// Answers the ArenaEntry contract: `covers` true means a region of THIS PROCESS
// lies in the window, so the refusal says nothing about the host and the walk
// treats it as held. False means every region of the window is free and the
// refusal is the system's - a real limitation, recorded.
//
// SCANS THE WHOLE WINDOW, NOT ITS BASE. `VirtualQuery(base)` describes the region
// containing `base` only. A 64 MiB window whose first byte is free but whose
// middle holds one of our DLLs is refused by `VirtualAlloc` while `VirtualQuery`
// at the base says MEM_FREE - so answering from the base alone would file our own
// loader's choice as a host limitation, at an address that moves with every ASLR
// draw. That is the defect that made the macOS arena irreproducible (83 vs 74
// unavailable entries across two runs of one binary on one machine), arriving
// here through a different door.
//
// `entry.start`/`entry.size` report the CONTIGUOUS occupied run, not a union
// across free gaps: `walk_arena` skips a described extent without probing it, and
// a skip across free space would assert ground that was never placed.
ArenaEntry classify_window(std::uint64_t base, std::uint64_t window,
                           const RegionQuery& query, const std::string& refusal);

}  // namespace rs::probe

#endif  // RUNTIMESKEPTIC_PROBE_WINDOWS_REGIONS_HPP

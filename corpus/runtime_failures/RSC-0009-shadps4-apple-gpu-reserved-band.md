---
id: RSC-0009
title: Emulator's fixed guest user-area base collides with a host band the emulator's own source describes as GPU-reserved
category: unsupported exact capability
layers_involved:
  - application
  - compatibility_layer
  - operating_system
  - kernel
evidence_available:
  - statically_inferred
  - heuristic_risk
  - unknown
finding_ids:
  - RS-VM-0001
  - RS-VM-0017
status: draft
provenance: source_citation
source: shadps4-emu/shadPS4@d392abe — src/core/address_space.cpp:36-44
---

# RSC-0009 — Emulator's fixed guest user-area base collides with a host band the emulator's own source describes as GPU-reserved

## Provenance notice

This entry uses `provenance: source_citation`, a value **not yet listed in `README.md` section 3.6**. It means: every claim about program behavior in this entry is a claim about the contents of a specific, publicly readable source file at a specific commit, and can be checked by reading that file.

What that does **and does not** license:

- **Verified:** what `shadps4-emu/shadPS4@d392abe` says and does. Every code claim below carries `file:line`.
- **NOT verified:** that any bug report, GitHub issue, CVE, forum post, user report, or affected game title exists. **None is cited anywhere in this entry**, because none was confirmed. RuntimeSkeptic made no network query about this project.
- **NOT verified:** that macOS actually reserves `0x1000000000-0x6FFFFFFFFF` for the GPU. That statement is **shadPS4's own source comment about the platform**, not an Apple statement, not a documented platform contract, and not something RuntimeSkeptic measured.

`status` remains `draft`, not `sourced`, because section 3.5 defines `sourced` as *"a verifiable public incident"*. What is verifiable here is source code, not an incident.

## Summary

shadPS4 is a PlayStation 4 emulator. It reserves a large guest "user area" at a compile-time-fixed host virtual address and dereferences guest pointers directly as host pointers, so the base address is load-bearing rather than convenient.

On every target except Apple/x86-64 that base is `0x1000000000`. On Apple/x86-64 — that is, an x86-64 build running under Rosetta 2 on Apple Silicon — the base is raised to `0x7000000000`, and the source states why:

```cpp
// src/core/address_space.cpp:36-44
#if defined(__APPLE__) && defined(ARCH_X86_64)
// Commpage ranges from 0xFC0000000 - 0xFFFFFFFFF, so decrease the system reserved maximum.
constexpr VAddr SYSTEM_RESERVED_MAX = 0xFBFFFFFFFULL;
// GPU-reserved memory ranges from 0x1000000000 - 0x6FFFFFFFFF, so increase the user minimum.
constexpr VAddr USER_MIN = 0x7000000000ULL;
#else
constexpr VAddr SYSTEM_RESERVED_MAX = 0xFFFFFFFFFULL;
constexpr VAddr USER_MIN = 0x1000000000ULL;
#endif
```

The comment on line 39 and the constant on line 40 are the entire basis for this entry. **The claim that macOS reserves `0x1000000000-0x6FFFFFFFFF` for the GPU is shadPS4's comment, not a verified platform fact.**

This is the ROADMAP's motivating example — a fixed guest mapping at `0x1000000000` that a translated x86-64 process cannot obtain — appearing verbatim in shipped production code, with the workaround already applied.

## Failure chain

This describes the pre-workaround configuration, i.e. what the `#else` branch (line 43) would do if compiled for Apple/x86-64.

| Layer | What happens |
| --- | --- |
| `application` | shadPS4 requires the guest user area at exactly `USER_MIN`; guest pointers are host pointers |
| `compatibility_layer` | no guest-to-host address translation exists on the x86-64 path, so identity is load-bearing (`address_space.cpp:664`) |
| `operating_system` | per shadPS4's own comment (line 39), the band `0x1000000000-0x6FFFFFFFFF` is not available to this process |
| `kernel` | the band is occupied or excluded by the process address-space layout |
| `application` | `mmap` returns `MAP_FAILED`; the constructor logs `LOG_CRITICAL` and executes `throw std::bad_alloc{}` (`address_space.cpp:679-680`) |

## What the program requires

The reservation is three `mmap` calls on the Apple path (`address_space.cpp:648-655`), guarded by a comment that names both holes:

```cpp
// src/core/address_space.cpp:645-647
// On ARM64 Macs, we run into limitations due to the commpage from 0xFC0000000 - 0xFFFFFFFFF
// and the GPU carveout region from 0x1000000000 - 0x6FFFFFFFFF. Because this creates gaps
// in the available virtual memory region, we map memory space using three distinct parts.
```

The flags are fixed at `address_space.cpp:639`:

```cpp
int map_flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED; // compiler knows its constexpr
```

Two properties of that line matter:

1. **`MAP_FIXED` is destructive.** A tree-wide search of `shadps4-emu/shadPS4@d392abe` finds **no** occurrence of `MAP_FIXED_NOREPLACE`, `MAP_EXCL`, or any `NOREPLACE` token. Only the destructive form is used (`address_space.cpp:639`, `:751`, `:774`). Where the range is merely *occupied* rather than *excluded*, this succeeds by clobbering.
2. **The returned address is never compared to the requested one.** The only check is `== MAP_FAILED`:

```cpp
// src/core/address_space.cpp:677-681
if (system_managed_base == MAP_FAILED || system_reserved_base == MAP_FAILED ||
    user_base == MAP_FAILED) {
    LOG_CRITICAL(Kernel_Vmm, "mmap failed: {}", strerror(errno));
    throw std::bad_alloc{};
}
```

With `MAP_FIXED` that is technically sufficient — POSIX guarantees exact placement on success — but it means identity is assumed by construction and never verified. `user_base` is simply assigned the constant (`address_space.cpp:664`).

## The failure sinks, precisely

Worth stating exactly, because the sink decides severity and the two paths differ:

| Path | Location | Sink kind |
| --- | --- | --- |
| Startup reservation (POSIX) | `address_space.cpp:680` — `throw std::bad_alloc{}` | `process_exit` — **not** an assert |
| Startup reservation (Windows) | `address_space.cpp:179` — `ASSERT_MSG(addr, "Unable to reserve virtual address space: {}", ...)` | `fatal_assert` |
| Per-mapping (POSIX) | `address_space.cpp:753` — `ASSERT_MSG(ret != MAP_FAILED, "mmap failed: {}", strerror(errno))` | `fatal_assert` |
| Unmap / protect (POSIX) | `address_space.cpp:775`, `:792` | `fatal_assert` |

There is **no retry and no fallback** on the POSIX reservation path. The one relocation fallback in the file is architectural, not recovery — see RSC-0010.

## What the workaround costs (arithmetic, not inference)

Raising `USER_MIN` is not free, and the cost is computable from two constants without any platform knowledge.

`MemoryManager::IsValidGpuMapping` bounds GPU-visible addresses at 40 bits (`memory.h:194-198`):

```cpp
bool IsValidGpuMapping(VAddr virtual_addr, u64 size) {
    // The PS4's GPU can only handle 40 bit addresses.
    const VAddr max_gpu_address{0x10000000000};
    return virtual_addr + size < max_gpu_address;
}
```

The GPU-addressable slice of the user area is therefore `max_gpu_address - USER_MIN`:

| Configuration | `USER_MIN` | GPU-addressable user window | Size |
| --- | --- | --- | --- |
| non-Apple | `0x1000000000` | `0x10000000000 - 0x1000000000` | `0xF000000000` = 960 GiB |
| Apple/x86-64 | `0x7000000000` | `0x10000000000 - 0x7000000000` | `0x9000000000` = 576 GiB |

The workaround removes `0x6000000000` = **384 GiB, 40% of the GPU-addressable window**. This is arithmetic over `address_space.cpp:40`, `:43` and `memory.h:196` — it is not an inference about anything.

## Inference, explicitly labelled as inference

**The following is inference, not fact.** A maintainer wrote a platform-conditional constant, an explaining comment, and a three-part mapping strategy that exists only on this platform (`address_space.cpp:23-28`, `:36-44`, `:644-655`). Code of that specificity is not normally written speculatively, so it is *plausible* that field failures on Apple Silicon preceded it.

**No such report was confirmed.** No issue number, no CVE, no user report, no game title is claimed here, and none should be added to this entry without a verified link.

## What RuntimeSkeptic says

Against `profiles/fixtures/macos-arm64-rosetta-x86_64.shadps4-derived.fixture.json`, whose unavailable band cites `src/core/address_space.cpp:39-40`:

| Contract | Verdict | Exit | Finding | Confidence |
| --- | --- | --- | --- | --- |
| `shadps4-user-area-base.json` (`USER_MIN=0x1000000000`) | `UNSUPPORTED` | 1 | RS-VM-0001 | `PREDICTIVE` |
| `shadps4-user-area-base-apple.json` (`USER_MIN=0x7000000000`) | `UNKNOWN` | 3 | RS-VM-0017 | `HYPOTHESIS` |

The pre-workaround base is refuted; the post-workaround base is not. Note carefully that the workaround variant is **`UNKNOWN`, not `SUPPORTED`** — the fixture positively establishes only that one band is unavailable, and never establishes that anything is available (`available_ranges` is empty). Absence of a contradiction is not evidence of availability.

The confidence ceiling is `PREDICTIVE` and can never be higher while the band's evidence class is `heuristic_risk`. Reaching `PROVEN` requires an `rs-env-probe` run on real Apple Silicon under Rosetta 2, which would replace the transcribed comment with a `measured_capability` fact.

## Why this is not an ordinary memory bug

shadPS4 executes no invalid operation. It issues a legal `mmap` that this host configuration refuses, and the same binary is correct on a host whose address space is laid out differently. The remediation was architectural — move the base — and it cost 40% of the GPU-addressable window. There was no line to fix.

## What would upgrade this entry

1. An `rs-env-probe` run on Apple Silicon under Rosetta 2 measuring the band → `measured_capability`, ceiling rises to `PROVEN`, `status: reproduced`.
2. A verified public report of the pre-workaround failure → `provenance: public_report`, `status: sourced`.
3. An Apple statement documenting the reservation → the band becomes `specified_guarantee` and stops being shadPS4's claim.

Until (1) or (3), every statement about the band in this entry must remain phrased as *shadPS4's comment says*, never as *macOS reserves*.

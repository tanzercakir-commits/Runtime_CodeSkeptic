---
id: RSC-0011
title: Fixed GPU-buffer mapping lands in the Rosetta 2 GPU carveout and aborts the emulator
category: unsupported exact capability
secondary_category: semantic success violation
layers_involved: [application, compatibility_layer, operating_system, kernel]
evidence_available: [reported_measurement, source_citation, log_excerpt]
finding_ids: [RS-VM-0001]
provenance: reported_incident
status: sourced
source: shadps4-emu/shadPS4 issue #4157
---

# RSC-0011 — Fixed GPU-buffer mapping in the Rosetta 2 GPU carveout

**This entry cites a real, publicly reported incident.** It is the first in
this corpus to do so; RSC-0001 through RSC-0008 are reconstructed patterns and
say so in their own front matter.

Source: [shadps4-emu/shadPS4 issue #4157](https://github.com/shadps4-emu/shadPS4/issues/4157),
"GTA V (CUSA00419) crashes with 'Mapping cannot fit inside free region' on
macOS Apple Silicon — GPU carveout gap in x86_64 Rosetta 2 address space".
Related: [#825](https://github.com/shadps4-emu/shadPS4/issues/825) (aggregate),
[#1265](https://github.com/shadps4-emu/shadPS4/issues/1265).

## What happened

Grand Theft Auto V (CUSA00419, v1.47) crashes during GPU initialisation under
shadPS4 on an Apple M4 Max, macOS 15.x (Darwin 25.3.0), with the emulator
binary running as x86-64 under Rosetta 2. Reported identically on v0.15.1 WIP
(`2bb20e4`) and v0.15.0.

```text
[Debug] <Critical> ([RAGE] RenderThread) memory.cpp:1467 operator(): Assertion Failed!
Mapping cannot fit inside free region
```

The title's RAGE engine asks for a fixed direct-memory mapping:

```text
sceKernelMapNamedDirectMemory:
  in_addr   = 0x1307200000
  len       = 0x20000
  prot      = 0x33  (RW + GPU RW)
  flags     = 0x10  (MAP_FIXED)
  phys_addr = 0xc2ec0000
  name      = 'anon'
```

`0x1307200000` is roughly 81 GiB, inside the address band macOS reserves.

## The layer chain

| Layer | What it does |
|---|---|
| application | GTA V requires its GPU buffer at exactly `0x1307200000` |
| compatibility_layer | shadPS4 has no guest-to-host translation on this path; the guest address must be the host address |
| operating_system | macOS refuses the whole band to a translated x86-64 process |
| kernel | Rosetta 2 commpage `0xFC0000000`–`0xFFFFFFFFF`, Apple unified-memory GPU carveout `0x1000000000`–`~0x6FC0000000` |

## Why it aborts where it does

The reporter traced the path: `sceKernelMapNamedDirectMemory` →
`MemoryManager::MapMemory()` with the `Fixed` flag. `Fixed` without
`NoOverwrite` hits neither validation branch, so the address passes through
unchecked into `CreateArea()` → `CarveVMA(0x1307200000, 0x20000)`.
`FindVMA` returns the last VMA *before* the gap, `end_in_vma` far exceeds
`vma.size`, and the assertion fires.

The interesting part is that **the emulator already knows about this gap**.
`address_space.cpp` raises `USER_MIN` from `0x1000000000` to `0x7000000000`
under `#if defined(__APPLE__) && defined(ARCH_X86_64)`, with the comment
"GPU-reserved memory ranges from 0x1000000000 - 0x6FFFFFFFFF". The reservation
side was fixed; the per-title mapping path was not. A guest address that
cannot exist on this host still reaches `CarveVMA`.

## The measurement

This is what raises the entry above a plausible story. The reporter compiled
and ran a test program as x86-64 under Rosetta 2:

```text
FAIL: 0x1000000000 - Cannot allocate memory
FAIL: 0x1307200000 - Cannot allocate memory   <- GTA V's target address
FAIL: 0x2000000000 - Cannot allocate memory
...
OK:   0x7000000000                            <- user region
```

Every `MAP_FIXED` in the band returns `ENOMEM`, and — the detail that makes
this a silent-contract-degradation case rather than a plain error — **without
`MAP_FIXED`, hints in the gap redirect to `0x7000000000` and succeed there.**
A hinted mapping reports success at an address the guest cannot use.

We did not reproduce this. It is a third-party observation with a described
method, recorded as `observed_invariant`, which caps every derived finding at
`OBSERVED_INVARIANT`. Upgrading it to `measured_capability` requires running
`rs-env-probe vm` on a real Apple Silicon host.

## What RuntimeSkeptic says

`contracts/gtav-rage-direct-memory-mapping.json` against
`profiles/fixtures/macos-arm64-rosetta-x86_64.reported.fixture.json`:

```text
UNSUPPORTED

RS-VM-0001  Exact virtual-memory mapping cannot be satisfied
  severity critical | confidence OBSERVED_INVARIANT | impact UNSUPPORTED
```

with the failure sink naming `memory.cpp:1467` and the evidence chain crossing
application → compatibility_layer → operating_system → kernel.

The same requirement against the weaker macOS fixtures yields `PREDICTIVE`,
and against a measured Linux profile yields `UNKNOWN` — the probe never
sampled `0x1307200000`, and absence of observation is not evidence of
availability in either direction.

## Discrepancy worth recording

The issue says the carveout ends at `~0x6FC0000000`. shadPS4's own source
treats it as ending at `0x6FFFFFFFFF` (hence `USER_MIN = 0x7000000000`). The
two disagree about roughly the last GiB. Neither reading changes the verdict
for `0x1307200000`, and the profile records the issue's figure with the
disagreement noted rather than silently picking one.

## What would have prevented it

- `add_address_translation_layer` — the only remedy that preserves the title's
  semantics. The guest wants an address this host will never provide.
- `choose_different_base_address` — not available: the address is chosen by
  the title, not by the emulator.
- **Will not work:** retrying. The refusal is structural. Also not: checking
  the return value more carefully on the hinted path — that call *succeeds*,
  at the wrong address.

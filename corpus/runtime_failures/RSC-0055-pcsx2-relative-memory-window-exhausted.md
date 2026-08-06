---
id: RSC-0055
title: Emulator startup exhausts every acceptable memory window relative to its own executable
category: resource-topology contradiction
secondary_category: unsupported exact capability
layers_involved: [application, compatibility_layer, operating_system, kernel]
evidence_available: [observed_invariant, statically_inferred]
finding_ids: [RS-VM-0001, RS-VM-0024]
status: sourced
provenance: public_report
source: https://github.com/PCSX2/pcsx2/issues/11728
verified: 2026-08-06
---

# RSC-0055 - Emulator startup exhausts every acceptable memory window relative to its own executable

**Sources:** [PCSX2 issue #11728](https://github.com/PCSX2/pcsx2/issues/11728),
the fixing [PR #11734](https://github.com/PCSX2/pcsx2/pull/11734), and the
affected [`v1.7.5849` memory map](https://github.com/PCSX2/pcsx2/blob/v1.7.5849/pcsx2/Memory.cpp).
Fetched and read on 2026-08-06.

## Summary

PCSX2 v1.7.5849 and later crashed at launch on several older Intel Macs with
`Failed to map data memory at an acceptable location`. The old x86-64 path
tried eleven exact candidate bases tied to the emulator's own text address.
PR #11734 removed that placement dependency, reserved the data and JIT regions
together wherever the host could provide them, and was confirmed by reporters
to fix the affected machines.

## Failure chain

| Layer | Observed requirement or behavior |
|---|---|
| application | PCSX2 wanted its data and recompiler windows within the reach assumed by generated x86-64 code |
| compatibility layer | the emulator converted this into eleven exact placements around its own executable |
| operating system | `mach_vm_map` refused every acceptable candidate on the affected Intel macOS configurations |
| application | startup reported that no acceptable data-memory location existed and released the VM state |

## What the program required

In `v1.7.5849`, `AllocateVirtualMemory` rounds its own function address down to
a 256 MiB boundary and tries offsets `+4` through `-6`, also in 256 MiB steps.
The returned address must equal the candidate. For that build the constants are
155 MiB of data memory and 305 MiB of recompiler memory. The two allocations
therefore have a 460 MiB combined footprint, but they were attempted separately
inside the bounded, executable-relative search.

## What the environment provided

The issue records immediate startup failure on 2012, 2015 and 2016 Intel Macs
running Sonoma or Sequoia through OpenCore Legacy Patcher. One reporter captured
repeated `mach_vm_map() failed` results followed by the same fatal message. The
issue does not publish the process text address or the eleven candidate ranges,
so it establishes exhaustion of the program's search, not a universal macOS
unavailable range.

## Why the mismatch is not detected at the call site

Each refusal is detected. What the old program could not distinguish was
"this candidate failed" from "the bounded placement policy has no remaining
solution on this host" until all eleven calls had already failed.

## Manifestation

The application exits before a game starts. Users see a memory-map error that
looks like ordinary memory pressure even though the fix was to remove the
relative-placement restriction, not to add RAM or reduce the requested size.

## Classification

Primary: **resource-topology contradiction**. Secondary: *unsupported exact
capability*. With the runtime text address and candidate availability,
`RS-VM-0001` can reject each exact placement. Without that process-specific
reference, `RS-VM-0024` is the honest current result: the relative-displacement
constraint is carried but not evaluated.

## Why this is not an ordinary memory bug

The same builds started on other Macs, and the fixing PR changed placement
strategy rather than repairing an invalid access. Reporters confirmed the PR
build on affected hardware. Host layout is an input to the failure.

## What RuntimeSkeptic would need

The requirement is recorded in
`contracts/campaign/pcsx2-v175849-data-window.json`. A decisive preflight needs
the executable's actual placement plus availability evidence for the eleven
derived candidate ranges. Static profiles intentionally contain neither a
future process address nor an invented range, so current measured profiles
return `UNKNOWN` through `RS-VM-0024`.

## Remediation classes

- `choose_different_base_address` - the accepted fix reserves one combined
  mapping area wherever the host provides it.
- `add_address_translation_layer` - generated code can carry an explicit base
  rather than making host placement part of correctness.
- **Rejected:** add physical RAM. The public evidence is address-space
  placement failure, not committed-memory exhaustion.

## Open questions

The issue correlates the failure with Intel Macs and, for one reporter, a
specific OpenCore/SIP configuration. It does not establish why that setting
changes `mach_vm_map` placement. No RuntimeSkeptic profile from an affected
machine exists, so this entry remains `sourced`, not `reproduced`.

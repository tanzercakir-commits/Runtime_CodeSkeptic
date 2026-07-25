---
id: RSC-0015
title: Guest heap cannot grow because the reservation consumed the space above the data segment
category: resource-topology contradiction
secondary_category: invalid fallback
layers_involved: [application, compatibility_layer, operating_system]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0003]
status: sourced
provenance: public_report
source: https://bugs.launchpad.net/qemu/+bug/1749393
verified: 2026-07-25
---

# RSC-0015 — Guest heap cannot grow because the reservation consumed the space above the data segment

**Source:** [sbrk() not working under qemu-user with a PIE-compiled binary?](https://bugs.launchpad.net/qemu/+bug/1749393)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> bash: xmalloc: .././shell.c:1709: cannot allocate 10 bytes (0 bytes allocated)

## Summary

bash ships its own allocator built on `sbrk()`, which requires the data
segment to be able to grow upward contiguously. qemu-user sets
`mmap_next_start = reserved_va` when a large reserved region exists, so a PIE
executable is loaded at the top of the guest address space with nothing above
its data section. Debian rebuilt bash as PIE and qemu-user broke on every
target architecture at once.

## What the program required

bash ships its own allocator built on `sbrk()`, which requires the data
segment to be able to grow upward contiguously. Real Linux guarantees this by
leaving a gap between the data segment and the stack.

## What the environment provided

qemu-user sets `mmap_next_start = reserved_va` when a large reserved region
exists, so a PIE executable is loaded at the top of the guest address space
with nothing above its data section. Without a reserved region, the dynamic
interpreter is mapped immediately adjacent, closing the gap anyway.

## Why the mismatch is not detected at the call site

No mapping call fails. `sbrk()` returns failure, and bash's allocator reports
it as being unable to allocate ten bytes - a number that makes the real cause
impossible to guess.

## Manifestation

Debian rebuilt bash as PIE and qemu-user broke on every target architecture at
once. The visible error is an out-of-memory message on a machine with
gigabytes free.

## Classification

Primary category: **resource-topology contradiction**. Secondary: *invalid fallback*.
Layers: application → compatibility_layer → operating_system.
Finding ids that would diagnose it: `RS-VM-0003`.

## Why this is not an ordinary memory bug

bash's allocator is correct against the platform contract it was written for.
The emulated address space silently lacked a layout property that the real one
guarantees.

## What RuntimeSkeptic would need

A profile fact for the guaranteed gap between the data segment and the next
mapping, which the model does not carry today. `RS-VM-0003` covers the end-of-
space case but not the layout gap.

## Remediation classes

- `ReserveAddressSpaceEarlier` — reserve the range before anything else can take it
- `ChooseDifferentBaseAddress` — choose a base the host can satisfy

## Open questions

Richard Henderson's fix enforces a 16 MB gap for i686. Whether the guaranteed
value should be per-architecture, and what a profile should record, is
unresolved here.

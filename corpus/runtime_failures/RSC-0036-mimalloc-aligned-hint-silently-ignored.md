---
id: RSC-0036
title: An alignment hint outside the usable range is silently substituted, defeating every aligned reservation
category: semantic success violation
secondary_category: invalid fallback
layers_involved: [library, syscall_interface, kernel]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0007, RS-VM-0022]
status: sourced
provenance: public_report
source: https://github.com/microsoft/mimalloc/issues/640
verified: 2026-07-25
---

# RSC-0036 — An alignment hint outside the usable range is silently substituted, defeating every aligned reservation

**Source:** [Lots of warnings due to failing to allocate aligned OS memory](https://github.com/microsoft/mimalloc/issues/640)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> I think the hint area that mimalloc wants to use is unusable, and that's why mmap consistently ignores the (aligned) hint and gives me back something unaligned.

## Summary

mimalloc requests large naturally-aligned regions - 64 MiB aligned to 64 MiB -
by passing a pre-computed aligned address as an `mmap` hint. On a RISC-V SV39
CPU, mimalloc's `MI_HINT_BASE` lies beyond the 39-bit user range. A flood of
warnings while running the `mold` linker, and a permanent performance penalty.

## What the program required

mimalloc requests large naturally-aligned regions - 64 MiB aligned to 64 MiB -
by passing a pre-computed aligned address as an `mmap` hint.

## What the environment provided

On a RISC-V SV39 CPU, mimalloc's `MI_HINT_BASE` lies beyond the 39-bit user
range. A hint is advisory, so the kernel silently substitutes an arbitrary,
unaligned address.

## Why the mismatch is not detected at the call site

No error is ever returned. mimalloc falls into its over-allocate-and-trim slow
path on every single request, continuously and silently.

## Manifestation

A flood of warnings while running the `mold` linker, and a permanent
performance penalty. Diagnosed by cross-referencing the RISC-V virtual-memory-
model chart, not by any message from the platform.

## Classification

Primary category: **semantic success violation**. Secondary: *invalid fallback*.
Layers: library → syscall_interface → kernel.
Finding ids that would diagnose it: `RS-VM-0007`, `RS-VM-0022`.

## Why this is not an ordinary memory bug

The requested alignment is a legitimate property. The interface has no way to
say 'this alignment is required' short of `MAP_FIXED`, which is destructive.

## What RuntimeSkeptic would need

`request.required_alignment` without an address - the case `RS-VM-0022` exists
for, because `required_alignment` used to be consulted only when an address
was also given.

## Remediation classes

- `ChooseDifferentBaseAddress` — choose a base the host can satisfy
- `ReserveAddressSpaceEarlier` — reserve the range before anything else can take it

## Open questions

Whether any platform offers a non-destructive aligned-reservation primitive.
`MAP_ALIGNED` exists on some BSDs; Linux has no equivalent.

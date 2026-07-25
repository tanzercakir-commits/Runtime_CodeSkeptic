---
id: RSC-0017
title: Address-space region reserved for guest/host identity is handed out to the host's own allocator
category: temporal contract violation
secondary_category: semantic success violation
layers_involved: [application, compatibility_layer, operating_system]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0008, RS-VM-0016]
status: sourced
provenance: public_report
source: https://github.com/dolphin-emu/dolphin/pull/9544
verified: 2026-07-25
---

# RSC-0017 — Address-space region reserved for guest/host identity is handed out to the host's own allocator

**Source:** [MemArena: Use memory placeholders for fastmem on Windows to ensure nothing allocates within the fastmem space.](https://github.com/dolphin-emu/dolphin/pull/9544)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> This avoids the possibility that something else allocates memory within the fastmem address region after it has been set up.

## Summary

Dolphin's fastmem translates guest effective addresses by adding a fixed base,
so the whole multi-gigabyte window must stay exclusively Dolphin's for the
lifetime of emulation. The old code mapped views and left the intervening
address space nominally free. Users saw hangs when using large texture packs.

## What the program required

Dolphin's fastmem translates guest effective addresses by adding a fixed base,
so the whole multi-gigabyte window must stay exclusively Dolphin's for the
lifetime of emulation.

## What the environment provided

The old code mapped views and left the intervening address space nominally
free. The host allocator, provoked by the large allocations that big texture
packs cause, handed out memory inside the window.

## Why the mismatch is not detected at the call site

Nothing fails at the moment of the theft. The JIT's identity assumption simply
stops holding, and the next guest access lands on memory belonging to
something else.

## Manifestation

Users saw hangs when using large texture packs. The connection between 'more
textures' and 'the JIT's address arithmetic is now wrong' is not one an
engineer reaches quickly.

## Classification

Primary category: **temporal contract violation**. Secondary: *semantic success violation*.
Layers: application → compatibility_layer → operating_system.
Finding ids that would diagnose it: `RS-VM-0008`, `RS-VM-0016`.

## Why this is not an ordinary memory bug

The reservation was never expressed to the platform as a reservation - only as
a set of mapped views with gaps. The platform behaved correctly on every call.

## What RuntimeSkeptic would need

A distinction between 'mapped' and 'reserved so that nothing else may take
it', which is `reserve_then_commit` plus a non-destructive exact-mapping
primitive (`fixed_noreplace_available`). `RS-VM-0016` names the missing
primitive.

## Remediation classes

- `ReserveAddressSpaceEarlier` — reserve the range before anything else can take it
- `UseSeparateWriteAndExecuteMappings` — separate the writable and executable views

## Open questions

The Linux counterpart (PR #9834) uses `shm_open` with `O_EXCL`. Whether that
is equivalent in strength to Windows placeholders is not established in either
thread.

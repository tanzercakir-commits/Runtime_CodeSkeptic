---
id: RSC-0043
title: A host offers no reserve-without-commit primitive, so the emulator hand-rolls the loader
category: temporal contract violation
secondary_category: capability present but required property absent
layers_involved: [compatibility_layer, operating_system, kernel]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0012, RS-VM-0016]
status: sourced
provenance: public_report
source: https://lkml.iu.edu/hypermail/linux/kernel/0406.1/0047.html
verified: 2026-07-25
---

# RSC-0043 — A host offers no reserve-without-commit primitive, so the emulator hand-rolls the loader

**Source:** [Re: WINE + NX (No eXecute) support for x86, 2.6.7-rc2-bk2](https://lkml.iu.edu/hypermail/linux/kernel/0406.1/0047.html)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> it is not possible to create a segment that is reserve only so that does not unnecessarily consume virtual memory. Apparently ELF allows it, but Linux doesn't.

## Summary

Wine must hold specific address ranges empty so PE images can later map where
they demand. Linux in 2004 offered no ELF program-header equivalent. The
`wine-preloader` that ships to this day: a statically linked binary at a fixed
load address that grabs the ranges and then hand-rolls what the kernel's ELF
loader would otherwise do.

## What the program required

Wine must hold specific address ranges empty so PE images can later map where
they demand. On Windows that is one `VirtualAlloc(MEM_RESERVE)` call: address
space claimed, no commit charge.

## What the environment provided

Linux in 2004 offered no ELF program-header equivalent. Any segment large
enough to reserve the range would also consume virtual memory.

## Why the mismatch is not detected at the call site

There is no failing call. The capability simply does not exist, so the program
must build it.

## Manifestation

The `wine-preloader` that ships to this day: a statically linked binary at a
fixed load address that grabs the ranges and then hand-rolls what the kernel's
ELF loader would otherwise do.

## Classification

Primary category: **temporal contract violation**. Secondary: *capability present but required property absent*.
Layers: compatibility_layer → operating_system → kernel.
Finding ids that would diagnose it: `RS-VM-0012`, `RS-VM-0016`.

## Why this is not an ordinary memory bug

The clearest statement of the reserve-versus-commit gap in this corpus, from a
Wine developer to LKML. The two platforms have genuinely different memory
models and one of them lacks a primitive the other's programs assume.

## What RuntimeSkeptic would need

`reserve_then_commit` on the request against
`virtual_memory.reserve_commit_model`. `RS-VM-0012` is the rule; it is also
the rule this project has never confirmed by execution.

## Remediation classes

- `ReserveAddressSpaceEarlier` — reserve the range before anything else can take it
- `NoKnownRemediationOnThisHost` — no remediation exists on this host

## Open questions

The developer also notes the amount to reserve depends on the PE being loaded,
so a static reservation is not enough. A model that only asks 'does
reserve/commit exist' misses that.

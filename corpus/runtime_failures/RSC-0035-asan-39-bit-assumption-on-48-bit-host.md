---
id: RSC-0035
title: An allocator's region table is sized from a compiled-in address-space width and overflows on a wider host
category: resource-topology contradiction
secondary_category: loader/ABI mismatch
layers_involved: [library, operating_system, hardware]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0013, RS-VM-0003]
status: sourced
provenance: public_report
source: https://github.com/google/sanitizers/issues/1707
verified: 2026-07-25
---

# RSC-0035 — An allocator's region table is sized from a compiled-in address-space width and overflows on a wider host

**Source:** ["CHECK failed: sanitizer_allocator_primary32.h:292" when running on RISC-V 64 systems using SV48](https://github.com/google/sanitizers/issues/1707)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> AddressSanitizer: CHECK failed: sanitizer_allocator_primary32.h:292 "((res)) < ((kNumPossibleRegions))" (0x7fff842, 0x40000)

## Summary

ASan's primary allocator sizes a region table from a compile-time assumption
about how many bits of virtual address exist - 39 on RISC-V. An SV48 kernel
legitimately returns addresses with nine more significant bits. Every C
program built with ASan fails - a total loss rather than a workload-dependent
one.

## What the program required

ASan's primary allocator sizes a region table from a compile-time assumption
about how many bits of virtual address exist - 39 on RISC-V.

## What the environment provided

An SV48 kernel legitimately returns addresses with nine more significant bits.
The computed region index 0x7fff842 exceeds the table bound 0x40000 by roughly
512x.

## Why the mismatch is not detected at the call site

The overflow is caught by an internal assertion, not by any platform call.
Nothing in the mapping interface communicates address-space width.

## Manifestation

Every C program built with ASan fails - a total loss rather than a workload-
dependent one. The thread notes the perverse consequence: fixing the kernel to
default to SV48, as its own commit message claimed, would have broken ASan for
everyone.

## Classification

Primary category: **resource-topology contradiction**. Secondary: *loader/ABI mismatch*.
Layers: library → operating_system → hardware.
Finding ids that would diagnose it: `RS-VM-0013`, `RS-VM-0003`.

## Why this is not an ordinary memory bug

The assumption was true for the platform the code targeted. Widening the
address space is the platform changing under it.

## What RuntimeSkeptic would need

`virtual_memory.max_user_address` measured, and `pointer_storage_width_bits`
on the requirement. `RS-VM-0013` covers truncation; this is the table-index
variant of the same contradiction.

## Remediation classes

- `WidenPointerStorage` — widen the storage the address is kept in
- `SelectDifferentHostConfiguration` — run on a host configured differently

## Open questions

Whether the same table sizing is wrong on x86-64 under LA57, where the width
change is identical in kind.

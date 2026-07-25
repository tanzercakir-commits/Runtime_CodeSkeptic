---
id: RSC-0039
title: Two runtimes in one process each require a fixed slice of the address space and one loses
category: invalid fallback
secondary_category: resource-topology contradiction
layers_involved: [language_runtime, library, operating_system]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0001, RS-VM-0014]
status: sourced
provenance: public_report
source: https://github.com/golang/go/issues/5641
verified: 2026-07-25
---

# RSC-0039 — Two runtimes in one process each require a fixed slice of the address space and one loses

**Source:** [linux/amd64 does not work when 0x00c0 address is unavailable](https://github.com/golang/go/issues/5641)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> AddressSanitizer runtime occupies 0x00c0 address very early during initialization, so Go runtime fallbacks to 32-bit heap allocation.

## Summary

Go/amd64 reserves its heap arena at the hinted base 0x00c0'0000'0000 and
requires the heap to be contiguous. ASan claims overlapping space during its
own early init, before Go's runtime starts. `address space conflict:
map(0x7f25ff1e9000) = 0x7f25f70c9000` later, once the interleaved allocations
make contiguous growth impossible.

## What the program required

Go/amd64 reserves its heap arena at the hinted base 0x00c0'0000'0000 and
requires the heap to be contiguous.

## What the environment provided

ASan claims overlapping space during its own early init, before Go's runtime
starts. Go's reservation fails.

## Why the mismatch is not detected at the call site

Go does not fail - it degrades to its 32-bit heap path, placing the heap in
the ordinary mmap region alongside the C++ side's allocations. The fallback is
the defect: a failed reservation became a working-but-doomed configuration.

## Manifestation

`address space conflict: map(0x7f25ff1e9000) = 0x7f25f70c9000` later, once the
interleaved allocations make contiguous growth impossible. Neither component
is buggy alone; the failure exists only in their composition.

## Classification

Primary category: **invalid fallback**. Secondary: *resource-topology contradiction*.
Layers: language_runtime → library → operating_system.
Finding ids that would diagnose it: `RS-VM-0001`, `RS-VM-0014`.

## Why this is not an ordinary memory bug

This is the cleanest instance of `RS-VM-0014` in the corpus: a permitted
fallback that contradicts a required postcondition. Contiguity was required;
the fallback path cannot provide it.

## What RuntimeSkeptic would need

`permitted_fallbacks` versus `required_postconditions` on the requirement -
decidable without consulting the profile at all, which is why `RS-VM-0014`
never reads a host fact.

## Remediation classes

- `ReserveAddressSpaceEarlier` — reserve the range before anything else can take it
- `ChooseDifferentBaseAddress` — choose a base the host can satisfy

## Open questions

Whether any allocator publishes the address ranges it claims, so that a second
runtime could check before claiming.

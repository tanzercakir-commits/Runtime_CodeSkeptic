---
id: RSC-0033
title: A fixed shadow reservation is refused with ENOMEM while gigabytes are free
category: error-code laundering
secondary_category: unsupported exact capability
layers_involved: [library, syscall_interface, kernel]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0001, RS-VM-0017]
status: sourced
provenance: public_report
source: https://github.com/google/sanitizers/issues/107
verified: 2026-07-25
---

# RSC-0033 — A fixed shadow reservation is refused with ENOMEM while gigabytes are free

**Source:** [ReserveShadowMemoryRange failed](https://github.com/google/sanitizers/issues/107)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> mmap(0xffffffff000, 2199023259648, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0) = -1 ENOMEM (Cannot allocate memory)

## Summary

AddressSanitizer computes its shadow mapping from the address, then demands
the kernel place a ~2 TiB `MAP_NORESERVE` region at exactly that base with
`MAP_FIXED`. With `vm.overcommit_memory = 0` the kernel applies a heuristic to
a reservation that has no commit charge and refuses it, on a machine reporting
roughly 4 GB free. `AddressSanitizer CHECK failed: ...

## What the program required

AddressSanitizer computes its shadow mapping from the address, then demands
the kernel place a ~2 TiB `MAP_NORESERVE` region at exactly that base with
`MAP_FIXED`. It asserts the returned pointer equals the requested one.

## What the environment provided

With `vm.overcommit_memory = 0` the kernel applies a heuristic to a
reservation that has no commit charge and refuses it, on a machine reporting
roughly 4 GB free.

## Why the mismatch is not detected at the call site

ENOMEM is the errno for exhaustion. Nothing distinguishes 'the kernel declines
this placement' from 'there is no memory', and the reported free memory makes
the message look absurd rather than informative.

## Manifestation

`AddressSanitizer CHECK failed: ... "ReserveShadowMemoryRange failed"`. Fatal
at startup, before any user code runs.

## Classification

Primary category: **error-code laundering**. Secondary: *unsupported exact capability*.
Layers: library → syscall_interface → kernel.
Finding ids that would diagnose it: `RS-VM-0001`, `RS-VM-0017`.

## Why this is not an ordinary memory bug

The reservation is not a memory request; it is an address-space request with
`MAP_NORESERVE`. The platform answered a question it was not asked.

## What RuntimeSkeptic would need

`request.exact_address_required` with `request.address` and a very large
`request.size`, against the host's available ranges. `RS-VM-0001` when the
region is known unavailable, `RS-VM-0017` when it was never established -
which is the honest answer for a 2 TiB span.

## Remediation classes

- `SelectDifferentHostConfiguration` — run on a host configured differently
- `ChooseDifferentBaseAddress` — choose a base the host can satisfy

## Open questions

Whether an overcommit heuristic's decision is reproducible enough for a probe
to record. If it is not, the correct profile answer is `unknown` and the
correct verdict is `UNKNOWN`.

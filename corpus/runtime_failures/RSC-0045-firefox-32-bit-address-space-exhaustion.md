---
id: RSC-0045
title: Address space, not memory, is the exhausted resource, and the error says memory
category: error-code laundering
secondary_category: resource-topology contradiction
layers_involved: [application, language_runtime, operating_system]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0003, RS-VM-0017]
status: sourced
provenance: public_report
source: https://bugzilla.mozilla.org/show_bug.cgi?id=1266389
verified: 2026-07-25
---

# RSC-0045 — Address space, not memory, is the exhausted resource, and the error says memory

**Source:** [32-bit Firefox fragments its address space and OOMs in emunittest suite.](https://bugzilla.mozilla.org/show_bug.cgi?id=1266389)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> For users running a 32-bit Firefox on 32-bit Windows ... there's only 2GB of usable address space available.

## Summary

A large contiguous `ArrayBuffer` reservation in a 32-bit process. 2 GB of
usable address space on 32-bit Windows, 4 GB for a 32-bit process on 64-bit
Windows, fragmented by the granularity effect in RSC-0044. A single large
allocation fails while the same byte count succeeds split up - the signature
of address-space rather than memory shortage.

## What the program required

A large contiguous `ArrayBuffer` reservation in a 32-bit process.

## What the environment provided

2 GB of usable address space on 32-bit Windows, 4 GB for a 32-bit process on
64-bit Windows, fragmented by the granularity effect in RSC-0044.

## Why the mismatch is not detected at the call site

The failure is reported as out-of-memory. The diagnostic that distinguishes it
is stated in the bug: allocating the same total in smaller slices succeeds.

## Manifestation

A single large allocation fails while the same byte count succeeds split up -
the signature of address-space rather than memory shortage.

## Classification

Primary category: **error-code laundering**. Secondary: *resource-topology contradiction*.
Layers: application → language_runtime → operating_system.
Finding ids that would diagnose it: `RS-VM-0003`, `RS-VM-0017`.

## Why this is not an ordinary memory bug

The bytes exist. The contiguous span does not.

## What RuntimeSkeptic would need

`virtual_memory.max_user_address` and `available_ranges` against a large
`request.size` with no address. `RS-VM-0003` when the size exceeds the space;
`RS-VM-0017` when fragmentation is the cause, since a profile cannot know a
future process's fragmentation.

## Remediation classes

- `WidenPointerStorage` — widen the storage the address is kept in
- `SelectDifferentHostConfiguration` — run on a host configured differently

## Open questions

Fragmentation is a property of a running process, not of a host. This is the
boundary where a static profile stops being able to answer, and Phase 4
begins.

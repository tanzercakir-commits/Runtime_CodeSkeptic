---
id: RSC-0037
title: An allocator built against 4 KiB pages refuses to initialise on a 16 KiB-page kernel
category: resource-topology contradiction
secondary_category: loader/ABI mismatch
layers_involved: [library, operating_system]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0006]
status: sourced
provenance: public_report
source: https://github.com/fluent/fluent-bit/issues/9730
verified: 2026-07-25
---

# RSC-0037 — An allocator built against 4 KiB pages refuses to initialise on a 16 KiB-page kernel

**Source:** [Docker Raspberry -> jemalloc - Unsupported system page size](https://github.com/fluent/fluent-bit/issues/9730)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> <jemalloc>: Unsupported system page size

## Summary

jemalloc bakes `LG_PAGE` in at configure time; every extent reservation,
alignment and slab size derives from it. A container image built against a 4
KiB host, run on a Raspberry Pi 5 whose default kernel uses 16 KiB pages. A
container that works on one machine and not another, with an error message
pointing at the wrong subsystem.

## What the program required

jemalloc bakes `LG_PAGE` in at configure time; every extent reservation,
alignment and slab size derives from it.

## What the environment provided

A container image built against a 4 KiB host, run on a Raspberry Pi 5 whose
default kernel uses 16 KiB pages. `getconf PAGESIZE` returns 16384.

## Why the mismatch is not detected at the call site

jemalloc does detect it and refuses to initialise, which is the right
behaviour. What is undetected is *downstream*: the visible error is `Cannot
allocate memory` from an unrelated config parser, plus collateral TLS
failures.

## Manifestation

A container that works on one machine and not another, with an error message
pointing at the wrong subsystem. Resolved by changing the kernel, not the
allocator.

## Classification

Primary category: **resource-topology contradiction**. Secondary: *loader/ABI mismatch*.
Layers: library → operating_system.
Finding ids that would diagnose it: `RS-VM-0006`.

## Why this is not an ordinary memory bug

A container image is a promise that the same bytes run the same way. Page size
is a property the image cannot carry.

## What RuntimeSkeptic would need

`request.required_page_size` with relation `Equal` against
`virtual_memory.page_size`, both known. `RS-VM-0006` with `PROVEN`.

## Remediation classes

- `MatchHostPageSize` — derive the page size at runtime instead of baking it in
- `SelectDifferentHostConfiguration` — run on a host configured differently

## Open questions

Whether any container runtime surfaces host page size as a compatibility
constraint. None known.

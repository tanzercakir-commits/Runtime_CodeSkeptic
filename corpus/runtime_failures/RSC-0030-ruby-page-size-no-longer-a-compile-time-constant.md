---
id: RSC-0030
title: PAGE_SIZE ceased to be a compile-time constant when one OS spanned two page sizes
category: resource-topology contradiction
secondary_category: loader/ABI mismatch
layers_involved: [language_runtime, operating_system]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0006]
status: sourced
provenance: public_report
source: https://bugs.ruby-lang.org/issues/17850
verified: 2026-07-25
---

# RSC-0030 — PAGE_SIZE ceased to be a compile-time constant when one OS spanned two page sizes

**Source:** [`PAGE_SIZE` is no longer a constant for macOS](https://bugs.ruby-lang.org/issues/17850)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> Native page size on Intel is 4 kB, on Apple Silicon it's 16 kB: therefore the PAGE_SIZE macro is no longer a constant.

## Summary

Ruby baked the page size into allocator arithmetic as a compile-time constant
- a property every prior macOS target provided. One operating system now spans
two architectures with different page sizes, so the value is only knowable at
runtime via `vm_page_size`. A build break on macOS Big Sur ARM.

## What the program required

Ruby baked the page size into allocator arithmetic as a compile-time constant
- a property every prior macOS target provided.

## What the environment provided

One operating system now spans two architectures with different page sizes, so
the value is only knowable at runtime via `vm_page_size`.

## Why the mismatch is not detected at the call site

It surfaced as a build failure under `-Wundef` rather than a runtime fault,
which is the lucky case.

## Manifestation

A build break on macOS Big Sur ARM. Worth recording precisely because the
failure was early and cheap; the same assumption elsewhere in the corpus fails
at runtime.

## Classification

Primary category: **resource-topology contradiction**. Secondary: *loader/ABI mismatch*.
Layers: language_runtime → operating_system.
Finding ids that would diagnose it: `RS-VM-0006`.

## Why this is not an ordinary memory bug

The constant was correct for every platform that existed when it was written.

## What RuntimeSkeptic would need

`virtual_memory.page_size` as a measured fact, and a requirement field that
says the program treats it as a constant. `RS-VM-0006`.

## Remediation classes

- `MatchHostPageSize` — derive the page size at runtime instead of baking it in

## Open questions

How many programs still use `PAGE_MAX_SIZE` as if it were the actual page
size.

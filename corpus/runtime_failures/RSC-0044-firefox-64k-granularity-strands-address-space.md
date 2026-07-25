---
id: RSC-0044
title: A 4 KiB-page allocator on a 64 KiB-granularity platform strands most of the address space it touches
category: resource-topology contradiction
secondary_category: invalid fallback
layers_involved: [application, library, operating_system]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0004, RS-VM-0005]
status: sourced
provenance: public_report
source: https://bugzilla.mozilla.org/show_bug.cgi?id=965936
verified: 2026-07-25
---

# RSC-0044 — A 4 KiB-page allocator on a 64 KiB-granularity platform strands most of the address space it touches

**Source:** [(defrag) [meta] Virtual address space fragmentation](https://bugzilla.mozilla.org/show_bug.cgi?id=965936)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> On Windows, if we call VirtualAlloc() for less than 64kb, it will create unusable memory region. For instance by allocating 4kb, we will have 60kb of unusable memory because the address has to align to 64kb.

## Summary

Firefox's allocator reasons in 4 KiB pages, as it does on every POSIX host.
Windows rounds every `VirtualAlloc` base address to 64 KiB while rounding size
to 4 KiB, so each sub-64K allocation strands up to 60 KiB of address space
that is neither committed nor reusable. OOM crashes in a 32-bit process with
plenty of memory free.

## What the program required

Firefox's allocator reasons in 4 KiB pages, as it does on every POSIX host.

## What the environment provided

Windows rounds every `VirtualAlloc` base address to 64 KiB while rounding size
to 4 KiB, so each sub-64K allocation strands up to 60 KiB of address space
that is neither committed nor reusable.

## Why the mismatch is not detected at the call site

Every individual `VirtualAlloc` returns success. The loss is per-call and
invisible; only the accumulation is fatal.

## Manifestation

OOM crashes in a 32-bit process with plenty of memory free. One crash dump
shows 113 MB free and all of it in sub-1MB fragments; roughly 60% of the tiny
blocks trace to five 64K-misalignment patterns.

## Classification

Primary category: **resource-topology contradiction**. Secondary: *invalid fallback*.
Layers: application → library → operating_system.
Finding ids that would diagnose it: `RS-VM-0004`, `RS-VM-0005`.

## Why this is not an ordinary memory bug

No allocation is wrong. The granularity mismatch converts a correct allocator
into a fragmenting one.

## What RuntimeSkeptic would need

`request.size` and `request.address` against
`virtual_memory.allocation_granularity`. `RS-VM-0004` and `RS-VM-0005` both
apply, and this entry is why the granularity rule matters even when it never
refuses anything.

## Remediation classes

- `MatchHostPageSize` — derive the page size at runtime instead of baking it in
- `ReserveAddressSpaceEarlier` — reserve the range before anything else can take it

## Open questions

The bug ran 2012-2018 and closed WORKSFORME once 64-bit became the norm -
which is not a fix, and would recur on any address-space-constrained target.

---
id: RSC-0023
title: Decommit requests sized to an internal page granularity are silently not honoured on a 16 KiB host
category: semantic success violation
secondary_category: resource-topology contradiction
layers_involved: [application, language_runtime, operating_system]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0006, RS-VM-0005]
status: sourced
provenance: public_report
source: https://www.mail-archive.com/debian-bugs-dist@lists.debian.org/msg2006001.html
verified: 2026-07-25
---

# RSC-0023 — Decommit requests sized to an internal page granularity are silently not honoured on a 16 KiB host

**Source:** [Bug#1089647: chromium: all tabs crashes after ~30 seconds on 16k page size systems](https://www.mail-archive.com/debian-bugs-dist@lists.debian.org/msg2006001.html)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> Chromium does not work at all on systems with 16k pages.

## Summary

V8 decommits pooled pages using its own internal page granularity, and
requires that a decommit of that size and alignment actually returns the
memory. On an arm64 kernel with 16 KiB pages, the kernel cannot honour a
decommit that is not page-aligned and page-sized. Every tab dies after roughly
thirty seconds.

## What the program required

V8 decommits pooled pages using its own internal page granularity, and
requires that a decommit of that size and alignment actually returns the
memory.

## What the environment provided

On an arm64 kernel with 16 KiB pages, the kernel cannot honour a decommit that
is not page-aligned and page-sized. The call does not fail; the memory is
simply not returned.

## Why the mismatch is not detected at the call site

The heap bookkeeping records the pages as decommitted while the OS still holds
them, so the two views diverge silently until the heap reaches a state V8 does
not expect.

## Manifestation

Every tab dies after roughly thirty seconds. The workaround adopted was to
query the page size at launch and disable the feature: `if [ $(getconf
PAGESIZE) -gt 4096 ]`.

## Classification

Primary category: **semantic success violation**. Secondary: *resource-topology contradiction*.
Layers: application → language_runtime → operating_system.
Finding ids that would diagnose it: `RS-VM-0006`, `RS-VM-0005`.

## Why this is not an ordinary memory bug

The same code is correct on a 4 KiB host. The assumption is about granularity,
not about ownership or lifetime.

## What RuntimeSkeptic would need

`request.required_page_size` against `virtual_memory.page_size`, and
`request.size` against `allocation_granularity`. Both measurable; `RS-VM-0006`
and `RS-VM-0005` apply.

## Remediation classes

- `MatchHostPageSize` — derive the page size at runtime instead of baking it in
- `SelectDifferentHostConfiguration` — run on a host configured differently

## Open questions

A carried allocator patch already existed for 64 KiB pages. Whether the two
share a root cause is not established in the thread.

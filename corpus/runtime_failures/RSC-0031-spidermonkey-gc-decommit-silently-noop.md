---
id: RSC-0031
title: A GC records arenas as decommitted while the OS still holds them, because arena size is below page size
category: semantic success violation
secondary_category: resource-topology contradiction
layers_involved: [language_runtime, operating_system]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0005, RS-VM-0006]
status: sourced
provenance: public_report
source: https://bugzilla.mozilla.org/show_bug.cgi?id=1660006
verified: 2026-07-25
---

# RSC-0031 — A GC records arenas as decommitted while the OS still holds them, because arena size is below page size

**Source:** [Check GC works with 16KB page sizes on Apple arm64 hardware](https://bugzilla.mozilla.org/show_bug.cgi?id=1660006)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> When the page size is 16KB, we didn't decommit, but we still set decommittedArenas to true.

## Summary

SpiderMonkey's GC requires its 4 KB arena granularity to match the OS page
granularity, so that returning an arena actually decommits memory. On Apple
arm64 the page is 16 KB. Memory not returned to the OS, with correct-looking
internal accounting.

## What the program required

SpiderMonkey's GC requires its 4 KB arena granularity to match the OS page
granularity, so that returning an arena actually decommits memory.

## What the environment provided

On Apple arm64 the page is 16 KB. The decommit becomes a no-op.

## Why the mismatch is not detected at the call site

No call fails. The bookkeeping flag is set regardless, so the GC's model of
what it owns and the OS's model diverge with no error anywhere.

## Manifestation

Memory not returned to the OS, with correct-looking internal accounting. Found
by a deliberate pre-launch audit before Apple arm64 hardware shipped - the
only entry in this corpus found *before* users hit it.

## Classification

Primary category: **semantic success violation**. Secondary: *resource-topology contradiction*.
Layers: language_runtime → operating_system.
Finding ids that would diagnose it: `RS-VM-0005`, `RS-VM-0006`.

## Why this is not an ordinary memory bug

No object is freed twice and no pointer dangles. A granularity assumption
became false.

## What RuntimeSkeptic would need

`request.size` against `allocation_granularity`, and the arena-size-versus-
page-size relation. `RS-VM-0005` is the closest rule.

## Remediation classes

- `MatchHostPageSize` — derive the page size at runtime instead of baking it in

## Open questions

The fix introduced explicit `PageSize`, `ArenasPerPage` and `PagesPerChunk`
constants and yielded a 3-23% improvement, which suggests the no-op had a
measurable cost before anyone noticed it.

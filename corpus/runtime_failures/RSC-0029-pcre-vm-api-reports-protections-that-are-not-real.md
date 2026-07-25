---
id: RSC-0029
title: The virtual-memory API reports protections that the hardware mask does not honour
category: platform-observed behavior treated as guaranteed behavior
secondary_category: semantic success violation
layers_involved: [library, operating_system, hardware]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0009, RS-VM-0011]
status: sourced
provenance: public_report
source: https://www.mail-archive.com/pcre-dev@exim.org/msg06582.html
verified: 2026-07-25
---

# RSC-0029 — The virtual-memory API reports protections that the hardware mask does not honour

**Source:** [[pcre-dev] [Bug 2618] JIT crash in macOS 11 with ARM64 hardware](https://www.mail-archive.com/pcre-dev@exim.org/msg06582.html)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> the effective page permissions diverge from the "underlying" page permissions as known by the virtual memory APIs due to the new mask being applied.

## Summary

PCRE's JIT allocator requires that a successful `mmap` plus a query of page
protections tells the truth about what the page can do. The APRR hardware mask
sits on top of the VM protections. A SIGBUS, or a code-signing process kill,
at a point unrelated to the allocation.

## What the program required

PCRE's JIT allocator requires that a successful `mmap` plus a query of page
protections tells the truth about what the page can do.

## What the environment provided

The APRR hardware mask sits on top of the VM protections. The VM API reports
RWX while the hardware denies the write, and pages start out execute-only
rather than writable - inverting the usual write-then-flip order.

## Why the mismatch is not detected at the call site

The allocator's own success check passes. The thread's advice is that
detecting a broken allocator requires catching SIGBUS, because - in the
reporter's words - the virtual memory subsystem will 'lie' to you about it.

## Manifestation

A SIGBUS, or a code-signing process kill, at a point unrelated to the
allocation. Homebrew and MacPorts both shipped PCRE with JIT disabled as the
interim fix.

## Classification

Primary category: **platform-observed behavior treated as guaranteed behavior**. Secondary: *semantic success violation*.
Layers: library → operating_system → hardware.
Finding ids that would diagnose it: `RS-VM-0009`, `RS-VM-0011`.

## Why this is not an ordinary memory bug

Every API contract was honoured as documented. A second protection mechanism,
invisible to the first, decides the outcome.

## What RuntimeSkeptic would need

This is the clearest case in the corpus for `measured_capability` over
`specified_guarantee`: the only trustworthy source is a probe that *attempts
the operation*, which is what `rs-env-probe` does. `RS-VM-0011` covers the
entitlement half.

## Remediation classes

- `RequestPlatformEntitlement` — obtain the platform entitlement the operation needs
- `UseSeparateWriteAndExecuteMappings` — separate the writable and executable views

## Open questions

Whether any API reports the effective rather than the nominal protection.

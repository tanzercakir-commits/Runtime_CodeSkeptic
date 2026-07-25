---
id: RSC-0026
title: A protection transition on a MAP_JIT region is refused because a thread-level mask supersedes it
category: capability present but required property absent
secondary_category: error-code laundering
layers_involved: [application, operating_system, hardware]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0009, RS-VM-0011]
status: sourced
provenance: public_report
source: https://developer.apple.com/forums/thread/672804
verified: 2026-07-25
---

# RSC-0026 — A protection transition on a MAP_JIT region is refused because a thread-level mask supersedes it

**Source:** [mprotect fails after MacOS 11.2 for PROT_EXEC pages on Apple Silicon](https://developer.apple.com/forums/thread/672804)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> The problematic transition that doesn't work anymore is RWX->NONE for some pages in the allocated MAP_JIT region.

## Summary

A page mapped RWX must be re-protectable, including down to `PROT_NONE`.
Apple's per-thread JIT write-protect mask sits above the VM protection state,
so the kernel-visible protection is no longer authoritative and the transition
is denied. Third-party JITs broke immediately after the 11.2 point release,
with no API deprecation and no changed signature.

## What the program required

A page mapped RWX must be re-protectable, including down to `PROT_NONE`. That
is the POSIX model every prior port relied on.

## What the environment provided

Apple's per-thread JIT write-protect mask sits above the VM protection state,
so the kernel-visible protection is no longer authoritative and the transition
is denied.

## Why the mismatch is not detected at the call site

An EACCES that names neither W^X nor the mask. Apple's own DTS engineer
explains in the thread that the API 'never tried to account for the original
protection level'.

## Manifestation

Third-party JITs broke immediately after the 11.2 point release, with no API
deprecation and no changed signature.

## Classification

Primary category: **capability present but required property absent**. Secondary: *error-code laundering*.
Layers: application → operating_system → hardware.
Finding ids that would diagnose it: `RS-VM-0009`, `RS-VM-0011`.

## Why this is not an ordinary memory bug

The program's calls are individually valid. A second, invisible protection
layer was introduced under the one the API exposes.

## What RuntimeSkeptic would need

`protection.write_execute_simultaneous` and
`protection.jit_entitlement_required`, both measurable by probe. `RS-VM-0009`
applies with `PROVEN`.

## Remediation classes

- `UseSeparateWriteAndExecuteMappings` — separate the writable and executable views
- `RequestPlatformEntitlement` — obtain the platform entitlement the operation needs

## Open questions

Whether any documented list exists of which protection transitions remain
legal on a MAP_JIT region.

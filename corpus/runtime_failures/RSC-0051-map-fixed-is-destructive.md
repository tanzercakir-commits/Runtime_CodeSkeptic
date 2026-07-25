---
id: RSC-0051
title: The only way to demand an address silently destroys whatever is already there
category: semantic success violation
secondary_category: unsupported exact capability
layers_involved: [application, syscall_interface, kernel]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0016, RS-VM-0001]
status: sourced
provenance: public_report
source: https://lwn.net/Articles/741335/
verified: 2026-07-25
---

# RSC-0051 — The only way to demand an address silently destroys whatever is already there

**Source:** [mm: introduce MAP_FIXED_SAFE](https://lwn.net/Articles/741335/)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> MAP_FIXED is inherently dangerous as it might silently clobber an existing underlying mapping (e.g. stack).

## Summary

A loader or allocator needs to place a mapping at a specific address and to be
told if that address is taken. For most of Linux's history there were two
options and neither was that: a hint the kernel may ignore, or `MAP_FIXED`,
which unmaps whatever is there without warning. The immediate trigger was a
hardening attempt: removing `MAP_FIXED` from the ELF loader produced runtime
failures, because the plain-hint path applies alignment constraints the loader
cannot tolerate.

## What the program required

A loader or allocator needs to place a mapping at a specific address and to be
told if that address is taken.

## What the environment provided

For most of Linux's history there were two options and neither was that: a
hint the kernel may ignore, or `MAP_FIXED`, which unmaps whatever is there
without warning.

## Why the mismatch is not detected at the call site

By construction. `MAP_FIXED` succeeds. The destroyed mapping - possibly the
stack - fails later, somewhere unrelated.

## Manifestation

The immediate trigger was a hardening attempt: removing `MAP_FIXED` from the
ELF loader produced runtime failures, because the plain-hint path applies
alignment constraints the loader cannot tolerate. There was no correct option
between 'unsafe' and 'will not place where I asked'.

## Classification

Primary category: **semantic success violation**. Secondary: *unsupported exact capability*.
Layers: application → syscall_interface → kernel.
Finding ids that would diagnose it: `RS-VM-0016`, `RS-VM-0001`.

## Why this is not an ordinary memory bug

The API had no safe mode. This is the reference entry for why
`fixed_noreplace_available` is a fact worth measuring rather than assuming.

## What RuntimeSkeptic would need

`virtual_memory.fixed_noreplace_available`, which the probe measures by
attempting a collision and requiring EEXIST. `RS-VM-0016` fires when the
primitive is absent.

## Remediation classes

- `ReserveAddressSpaceEarlier` — reserve the range before anything else can take it
- `NoKnownRemediationOnThisHost` — no remediation exists on this host

## Open questions

The flag shipped as `MAP_FIXED_NOREPLACE` with `EEXIST`, not the RFC's
`ENOMEM`. Any entry citing the RFC should cite it as the RFC.

---
id: RSC-0027
title: The write-protect toggle is per thread, not per region, so unprotecting one region unprotects the running code
category: capability present but required property absent
secondary_category: semantic success violation
layers_involved: [library, language_runtime, operating_system, hardware]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0009]
status: sourced
provenance: public_report
source: https://github.com/MonoMod/MonoMod/issues/90
verified: 2026-07-25
---

# RSC-0027 — The write-protect toggle is per thread, not per region, so unprotecting one region unprotects the running code

**Source:** [Supporting Apple Silicon](https://github.com/MonoMod/MonoMod/issues/90)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> calling `pthread_jit_write_protect_np(false)` from managed code causes a bus error -- this is because `pthread_jit_write_protect_np` controls _every_ page!

## Summary

MonoMod needs per-region control: patch one JIT region while continuing to
execute code from others. Toggling write access disables execute for *every*
`MAP_JIT` page on the thread - including the managed code currently running. A
bus error the instant managed code makes the call.

## What the program required

MonoMod needs per-region control: patch one JIT region while continuing to
execute code from others.

## What the environment provided

Toggling write access disables execute for *every* `MAP_JIT` page on the
thread - including the managed code currently running.

## Why the mismatch is not detected at the call site

The call succeeds. The failure is that the caller's own next instruction fetch
is no longer executable.

## Manifestation

A bus error the instant managed code makes the call. The workaround is to move
the entire write window into a native helper, which is a structural change
rather than a fix.

## Classification

Primary category: **capability present but required property absent**. Secondary: *semantic success violation*.
Layers: library → language_runtime → operating_system → hardware.
Finding ids that would diagnose it: `RS-VM-0009`.

## Why this is not an ordinary memory bug

The API does exactly what it documents. The program required a granularity the
platform does not offer, and there is no call that reports that.

## What RuntimeSkeptic would need

A profile fact for the *granularity* of the W^X toggle - per page, per region,
per thread - which the model does not carry. `RS-VM-0009` records that W and X
are not simultaneous but not that the switch is global.

## Remediation classes

- `UseSeparateWriteAndExecuteMappings` — separate the writable and executable views
- `NoKnownRemediationOnThisHost` — no remediation exists on this host

## Open questions

This is the strongest argument in the corpus for a granularity field on the
protection facts.

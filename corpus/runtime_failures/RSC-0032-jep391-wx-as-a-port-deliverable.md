---
id: RSC-0032
title: A platform forbids write-and-execute, so supporting W^X became a named deliverable of the port
category: capability present but required property absent
layers_involved: [language_runtime, operating_system, hardware]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0009]
status: sourced
provenance: vendor_documentation
source: https://openjdk.org/jeps/391
verified: 2026-07-25
---

# RSC-0032 — A platform forbids write-and-execute, so supporting W^X became a named deliverable of the port

**Source:** [JEP 391: macOS/AArch64 Port](https://openjdk.org/jeps/391)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> macOS/AArch64 forbids memory segments from being executable and writeable at the same time, a policy known as write-xor-execute (W^X).

## Summary

HotSpot routinely creates and modifies executable code and had assumed
writable-and-executable code-cache pages in every prior port. macOS/AArch64
refuses that mapping outright. No incident.

## What the program required

HotSpot routinely creates and modifies executable code and had assumed
writable-and-executable code-cache pages in every prior port.

## What the environment provided

macOS/AArch64 refuses that mapping outright.

## Why the mismatch is not detected at the call site

This entry is the case where it *was* detected in advance - at specification
level, before the port was written, with the cost written into the JEP:
'correctness with a possible performance penalty in uncommon cases, such as
deoptimizations'.

## Manifestation

No incident. A design document. It is in the corpus as the vendor-
documentation counterpart to the bug reports around it, and as evidence that
the contradiction is stable and known rather than a transient bug.

## Classification

Primary category: **capability present but required property absent**.
Layers: language_runtime → operating_system → hardware.
Finding ids that would diagnose it: `RS-VM-0009`.

## Why this is not an ordinary memory bug

It is not a bug at all. It is the platform contract that makes the other Apple
entries in this corpus inevitable.

## What RuntimeSkeptic would need

`protection.write_execute_simultaneous` as `specified_guarantee`, which this
document supplies. That is the only evidence class in the model that a JEP can
establish.

## Remediation classes

- `UseSeparateWriteAndExecuteMappings` — separate the writable and executable views

## Open questions

None. Included as a specification-level source, not as a failure.

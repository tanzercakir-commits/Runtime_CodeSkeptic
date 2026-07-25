---
id: RSC-0028
title: A runtime configured for RWX code pages faults on the first write after the region became executable
category: capability present but required property absent
layers_involved: [language_runtime, operating_system, hardware]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0009, RS-VM-0010]
status: sourced
provenance: public_report
source: https://github.com/dart-lang/sdk/issues/45793
verified: 2026-07-25
---

# RSC-0028 — A runtime configured for RWX code pages faults on the first write after the region became executable

**Source:** [Dart on Apple M1 (arm64) doesn't call pthread_jit_write_protect_np when writing to executable memory](https://github.com/dart-lang/sdk/issues/45793)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> This results in immediate segfault when running on Apple M1.

## Summary

The Dart VM as configured by the Flutter Engine - which disables the VM's own
code write-protection for performance - requires executable pages to stay
simultaneously writable. On Apple Silicon a `MAP_JIT` page is never both at
once. An immediate segfault on launch on M1 hardware.

## What the program required

The Dart VM as configured by the Flutter Engine - which disables the VM's own
code write-protection for performance - requires executable pages to stay
simultaneously writable.

## What the environment provided

On Apple Silicon a `MAP_JIT` page is never both at once.

## Why the mismatch is not detected at the call site

There is no failing call. The write faults.

## Manifestation

An immediate segfault on launch on M1 hardware. The standalone Dart VM was
unaffected, because it already alternated RW and RX - so the same runtime
failed only in one embedding.

## Classification

Primary category: **capability present but required property absent**.
Layers: language_runtime → operating_system → hardware.
Finding ids that would diagnose it: `RS-VM-0009`, `RS-VM-0010`.

## Why this is not an ordinary memory bug

The RWX assumption is correct on Intel macOS and on Linux. It became false
when the hardware enforced W^X.

## What RuntimeSkeptic would need

`request.simultaneous_write_execute` against
`protection.write_execute_simultaneous`. `RS-VM-0009` with `PROVEN`.

## Remediation classes

- `UseSeparateWriteAndExecuteMappings` — separate the writable and executable views

## Open questions

Whether the performance reason for disabling the VM's own protection survives
on a platform where the toggle is mandatory.

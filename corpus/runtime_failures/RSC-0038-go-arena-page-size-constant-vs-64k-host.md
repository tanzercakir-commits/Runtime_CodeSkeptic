---
id: RSC-0038
title: A runtime's compile-time page-size constant makes it predict the wrong address for its own arena
category: resource-topology contradiction
layers_involved: [language_runtime, operating_system]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0006, RS-VM-0005]
status: sourced
provenance: public_report
source: https://github.com/golang/go/issues/11886
verified: 2026-07-25
---

# RSC-0038 — A runtime's compile-time page-size constant makes it predict the wrong address for its own arena

**Source:** [runtime: address space conflict on Linux/Arm64](https://github.com/golang/go/issues/11886)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> The current runtime assumes a 8192 byte page size.  Your arm64 system seems to be using a 65,536 byte page size.

## Summary

Go's heap arena reservation checks that the address the kernel returns matches
what it asked for, rounded by its own compile-time `_PhysPageSize`. Go
1.5beta2 cross-compiled for arm64 carried an 8192-byte assumption; the CentOS
7 arm64 kernel used 64 KiB pages. A startup abort visible only when the binary
is cross-compiled on x86-64 and run on real arm64 hardware.

## What the program required

Go's heap arena reservation checks that the address the kernel returns matches
what it asked for, rounded by its own compile-time `_PhysPageSize`.

## What the environment provided

Go 1.5beta2 cross-compiled for arm64 carried an 8192-byte assumption; the
CentOS 7 arm64 kernel used 64 KiB pages. The kernel rounded the mapping down
to a 64 KiB boundary.

## Why the mismatch is not detected at the call site

The check is present and fires, which is good. But the reported symptom -
`address space conflict: map(0x481fff8000) = 0x481fff0000` - names two
addresses 0x8000 apart and does not name the page size that explains the
difference.

## Manifestation

A startup abort visible only when the binary is cross-compiled on x86-64 and
run on real arm64 hardware.

## Classification

Primary category: **resource-topology contradiction**.
Layers: language_runtime → operating_system.
Finding ids that would diagnose it: `RS-VM-0006`, `RS-VM-0005`.

## Why this is not an ordinary memory bug

Rounding to page granularity is correct kernel behaviour. The runtime's model
of the granularity was wrong.

## What RuntimeSkeptic would need

`virtual_memory.page_size` measured, against a requirement that states the
assumed page size. `RS-VM-0006`.

## Remediation classes

- `MatchHostPageSize` — derive the page size at runtime instead of baking it in

## Open questions

The discussion notes the value 'can be recovered by a syscall, or from the
auxv variable' but 'needs to be a constant'. That tension - a runtime constant
that is a runtime property - recurs across this corpus.

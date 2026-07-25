---
id: RSC-0021
title: MAP_32BIT reservations began failing with ENOMEM while the low address space was free
category: error-code laundering
secondary_category: resource-topology contradiction
layers_involved: [language_runtime, syscall_interface, kernel]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0017]
status: sourced
provenance: public_report
source: https://lkml.iu.edu/hypermail/linux/kernel/2303.0/01823.html
verified: 2026-07-25
---

# RSC-0021 — MAP_32BIT reservations began failing with ENOMEM while the low address space was free

**Source:** [[Regression] mmap with MAP_32BIT randomly fails since 6.1](https://lkml.iu.edu/hypermail/linux/kernel/2303.0/01823.html)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> Failed anonymous mmap((nil), 131072, 0x3, 0x22, -1, 0): Cannot allocate memory

## Summary

Android's `dex2oat` requests low-address regions with `MAP_32BIT`, as ART's
compiler needs 32-bit-representable code addresses. After the VMA maple-tree
conversion, 6.1.12 began refusing them with ENOMEM after as little as 2 MiB,
with no correlation to actual free space in the low 4 GiB. Android builds
became nondeterministically broken - the same tree building sometimes and not
others.

## What the program required

Android's `dex2oat` requests low-address regions with `MAP_32BIT`, as ART's
compiler needs 32-bit-representable code addresses. On 5.17.4 these succeeded
until roughly 1 GiB of cumulative allocation.

## What the environment provided

After the VMA maple-tree conversion, 6.1.12 began refusing them with ENOMEM
after as little as 2 MiB, with no correlation to actual free space in the low
4 GiB.

## Why the mismatch is not detected at the call site

ENOMEM is indistinguishable from genuine exhaustion. The reporter could only
establish the difference by bisecting the kernel.

## Manifestation

Android builds became nondeterministically broken - the same tree building
sometimes and not others.

## Classification

Primary category: **error-code laundering**. Secondary: *resource-topology contradiction*.
Layers: language_runtime → syscall_interface → kernel.
Finding ids that would diagnose it: `RS-VM-0017`.

## Why this is not an ordinary memory bug

Nothing about the request changed. A placement policy inside the kernel
changed how much of a documented region it was willing to hand out, and
reported the result as an out-of-memory condition.

## What RuntimeSkeptic would need

A profile fact for how much of a constrained region is actually obtainable,
which the model does not carry. `RS-VM-0017` is the honest answer today:
availability was never established.

## Remediation classes

- `SelectDifferentHostConfiguration` — run on a host configured differently
- `NoKnownRemediationOnThisHost` — no remediation exists on this host

## Open questions

Whether the regression was fixed upstream, and whether a probe could measure
obtainable low-region capacity reproducibly given that it is allocation-order
dependent.

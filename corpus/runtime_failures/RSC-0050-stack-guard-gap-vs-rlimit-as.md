---
id: RSC-0050
title: A security mitigation enlarged an unmapped region that counts against the process's address-space limit
category: resource-topology contradiction
layers_involved: [application, kernel]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0003]
status: sourced
provenance: public_report
source: https://github.com/torvalds/linux/commit/1be7107fbe18eed3e319a6c3e83c78254b693acb
verified: 2026-07-25
---

# RSC-0050 — A security mitigation enlarged an unmapped region that counts against the process's address-space limit

**Source:** [mm: larger stack guard gap, between vmas](https://github.com/torvalds/linux/commit/1be7107fbe18eed3e319a6c3e83c78254b693acb)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> accounting a larger gap can break userspace - case in point, a program run with 'ulimit -S -v 20000' failed when the 1MB gap was counted for RLIMIT_AS

## Summary

A program running under a tight `RLIMIT_AS` requires the address space it was
configured for to remain available. The Stack Clash mitigation widened the
unmapped guard region below the stack from one page to 1 MiB. Recorded in the
commit message itself, because the regression class was found during review -
one of two entries in this corpus caught before release.

## What the program required

A program running under a tight `RLIMIT_AS` requires the address space it was
configured for to remain available.

## What the environment provided

The Stack Clash mitigation widened the unmapped guard region below the stack
from one page to 1 MiB. That region is charged against address-space
accounting.

## Why the mismatch is not detected at the call site

The program fails to start with an allocation error, on a machine that has not
changed and with a limit that has not changed.

## Manifestation

Recorded in the commit message itself, because the regression class was found
during review - one of two entries in this corpus caught before release.

## Classification

Primary category: **resource-topology contradiction**.
Layers: application → kernel.
Finding ids that would diagnose it: `RS-VM-0003`.

## Why this is not an ordinary memory bug

No allocation is wrong. A kernel-internal accounting change moved a limit the
program was sized against.

## What RuntimeSkeptic would need

A profile fact for the guard gap and for how address-space limits are charged.
Neither exists. `RS-VM-0003` covers the end of the address space, not the
accounting of unmapped regions inside it.

## Remediation classes

- `SelectDifferentHostConfiguration` — run on a host configured differently

## Open questions

The kernel added a `stack_guard_gap` command-line tunable, so the value is
host configuration rather than a constant - which means a profile would have
to measure it, and no probe does.

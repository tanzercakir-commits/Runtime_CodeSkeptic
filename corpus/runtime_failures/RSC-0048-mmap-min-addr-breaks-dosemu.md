---
id: RSC-0048
title: A security floor on the lowest mappable address broke programs whose ABI requires low memory
category: unsupported exact capability
secondary_category: resource-topology contradiction
layers_involved: [application, operating_system, kernel]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0002]
status: sourced
provenance: public_report
source: https://bugs.launchpad.net/ubuntu/+source/dosemu/+bug/216398
verified: 2026-07-25
---

# RSC-0048 — A security floor on the lowest mappable address broke programs whose ABI requires low memory

**Source:** [default mmap_min_addr breaks dosemu](https://bugs.launchpad.net/ubuntu/+source/dosemu/+bug/216398)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> Default value of mmap_min_addr is set to 64kb, which breaks many different userland applications, including dosemu, wine and hardware virtualisation.

## Summary

dosemu emulates real-mode DOS memory, which lives at virtual zero. The kernel
gained a floor on the lowest mappable address to stop NULL-dereference kernel
bugs becoming arbitrary code execution. `LOWRAM mmap: Invalid argument`
followed by a core dump.

## What the program required

dosemu emulates real-mode DOS memory, which lives at virtual zero. The low
range is not a preference.

## What the environment provided

The kernel gained a floor on the lowest mappable address to stop NULL-
dereference kernel bugs becoming arbitrary code execution.

## Why the mismatch is not detected at the call site

`mmap` fails with EINVAL and dosemu then segfaults. Neither message names the
sysctl.

## Manifestation

`LOWRAM mmap: Invalid argument` followed by a core dump. The reporter bisected
by date - 'The last time I'm certain it worked was 2008-03-26' - and initially
blamed a dependency.

## Classification

Primary category: **unsupported exact capability**. Secondary: *resource-topology contradiction*.
Layers: application → operating_system → kernel.
Finding ids that would diagnose it: `RS-VM-0002`.

## Why this is not an ordinary memory bug

The program's requirement is fixed by the ABI it emulates. The platform
withdrew a capability deliberately, for good reason, and there is no
reconciliation.

## What RuntimeSkeptic would need

`request.address` against `virtual_memory.min_map_address`, measured. `RS-
VM-0002` with `PROVEN` - and this is the cleanest possible example of it.

## Remediation classes

- `SelectDifferentHostConfiguration` — run on a host configured differently
- `NoKnownRemediationOnThisHost` — no remediation exists on this host

## Open questions

None. This is the canonical `min_map_address` incident and is recorded as the
reference case for `RS-VM-0002`.

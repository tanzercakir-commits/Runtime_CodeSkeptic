---
id: RSC-0014
title: Guest ABI requires a page at a fixed low address that host policy forbids
category: unsupported exact capability
secondary_category: resource-topology contradiction
layers_involved: [application, compatibility_layer, operating_system, kernel]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0002, RS-VM-0001]
status: sourced
provenance: public_report
source: https://gitlab.com/qemu-project/qemu/-/issues/690
verified: 2026-07-25
---

# RSC-0014 — Guest ABI requires a page at a fixed low address that host policy forbids

**Source:** [32bit qemu-arm can't run GCC due to failure to allocate memory range for guest (Allocating guest commpage error)](https://gitlab.com/qemu-project/qemu/-/issues/690)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> Allocating guest commpage: Operation not permitted

## Summary

The 32-bit ARM ABI places the kernel user-helper commpage at a fixed address
baked into guest code. The host's `vm.mmap_min_addr` policy refuses the
mapping. `qemu-arm-static gcc` fails to start.

## What the program required

The 32-bit ARM ABI places the kernel user-helper commpage at a fixed address
baked into guest code. The guest cannot relocate it, so qemu-arm must create
that page at that exact host address.

## What the environment provided

The host's `vm.mmap_min_addr` policy refuses the mapping. The reporter's own
table shows the errno tracking the sysctl value: success below 53248, ENOMEM
from 53249 to 61440, EPERM at 61441 and above.

## Why the mismatch is not detected at the call site

It is detected - loudly, at startup. What is not detected is that the *cause*
is a host policy rather than a resource shortage, because the kernel reports
it as EPERM or ENOMEM depending on where the boundary falls.

## Manifestation

`qemu-arm-static gcc` fails to start. A regression: QEMU 5.0 works, 5.1
through 6.1 do not, following the `init_guest_space` rework.

## Classification

Primary category: **unsupported exact capability**. Secondary: *resource-topology contradiction*.
Layers: application → compatibility_layer → operating_system → kernel.
Finding ids that would diagnose it: `RS-VM-0002`, `RS-VM-0001`.

## Why this is not an ordinary memory bug

The address is a property of the guest ABI, not a choice. Two platform
policies - a fixed ABI address and a security floor - are simply incompatible
on this host.

## What RuntimeSkeptic would need

`request.address` with `exact_address_required`, against
`virtual_memory.min_map_address` measured on the host. `RS-VM-0002` fires,
with `PROVEN` available since both facts are strong.

## Remediation classes

- `SelectDifferentHostConfiguration` — run on a host configured differently
- `NoKnownRemediationOnThisHost` — no remediation exists on this host

## Open questions

Whether the errno varying with the sysctl value (EPERM vs ENOMEM) is
deliberate kernel behaviour or an artifact of where the check sits.

---
id: RSC-0016
title: Guest requires a 47-bit address-space layout on a host kernel configured for 39 bits
category: resource-topology contradiction
secondary_category: unsupported exact capability
layers_involved: [application, compatibility_layer, operating_system, kernel]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0003, RS-VM-0001]
status: sourced
provenance: public_report
source: https://github.com/ptitSeb/box64/issues/1188
verified: 2026-07-25
---

# RSC-0016 — Guest requires a 47-bit address-space layout on a host kernel configured for 39 bits

**Source:** [Wine GUI applications unable to run (wine 8.21+)](https://github.com/ptitSeb/box64/issues/1188)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> err:virtual:map_fixed_area out of memory for 0x6fffffcd0000-0x6ffffffea000

## Summary

Wine 8.21 changed its address-space layout to require a fixed reservation near
0x6fffffff0000, an address chosen because x86-64 Linux gives userspace 47
bits. box64 runs that x86-64 Wine on ARM64 hosts whose kernels expose a 39-bit
user address space. GUI applications die with 'Application tried to create a
window, but no driver could be loaded'.

## What the program required

Wine 8.21 changed its address-space layout to require a fixed reservation near
0x6fffffff0000, an address chosen because x86-64 Linux gives userspace 47
bits.

## What the environment provided

box64 runs that x86-64 Wine on ARM64 hosts whose kernels expose a 39-bit user
address space. 0x6fffffcd0000 is roughly 123 TiB and simply does not exist
there. box64 announces the narrower space - `Didn't detect 48bits of address
space, considering it's 39bits` - but cannot manufacture the range.

## Why the mismatch is not detected at the call site

Wine's `map_fixed_area` does fail, and reports it. What no layer reports is
that the requirement was formed against a different address-space width, so
the failure looks like memory exhaustion.

## Manifestation

GUI applications die with 'Application tried to create a window, but no driver
could be loaded'. Wine 8.19 worked; 8.21 did not. The same box64 build works
on x86-64 hosts.

## Classification

Primary category: **resource-topology contradiction**. Secondary: *unsupported exact capability*.
Layers: application → compatibility_layer → operating_system → kernel.
Finding ids that would diagnose it: `RS-VM-0003`, `RS-VM-0001`.

## Why this is not an ordinary memory bug

Nothing overflowed and nothing leaked. A layout constant valid on one host
width was carried to a host with a different width.

## What RuntimeSkeptic would need

`request.address` plus `request.size`, against
`virtual_memory.max_user_address`. `RS-VM-0003` fires with `PROVEN` when the
host bound is measured.

## Remediation classes

- `ChooseDifferentBaseAddress` — choose a base the host can satisfy
- `SelectDifferentHostConfiguration` — run on a host configured differently
- `NoKnownRemediationOnThisHost` — no remediation exists on this host

## Open questions

Whether Wine's own guard - which the maintainer notes checks the address-space
size only on arm64 builds, not on x86-64 - has since been extended.

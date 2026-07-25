---
id: RSC-0012
title: x86-64 guest with 4 KiB ELF segment alignment cannot be mapped on a 16 KiB-page host
category: resource-topology contradiction
secondary_category: loader/ABI mismatch
layers_involved: [application, compatibility_layer, operating_system, kernel]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0006]
status: sourced
provenance: public_report
source: https://github.com/FEX-Emu/FEX/issues/1221
verified: 2026-07-25
---

# RSC-0012 — x86-64 guest with 4 KiB ELF segment alignment cannot be mapped on a 16 KiB-page host

**Source:** [Throw an error when compiling and running on a non-4k page host](https://github.com/FEX-Emu/FEX/issues/1221)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> It is impossible to emulate 4k paging in a 16k host with any semblance of speed.

## Summary

Every x86-64 ELF the emulator runs carries `LOAD` segments aligned to 0x1000
and placed at file offsets that are multiples of 4 KiB but not of 16 KiB. On
an Apple M1 under Asahi Linux the host page size is 16 KiB. The maintainers'
resolution was to refuse to start rather than mis-map: a guest that has its
protections silently merged fails later, somewhere unrelated, with a write
succeeding where it should have faulted.

## What the program required

Every x86-64 ELF the emulator runs carries `LOAD` segments aligned to 0x1000
and placed at file offsets that are multiples of 4 KiB but not of 16 KiB.
Adjacent segments carry different protections (R, RX, R, RW) and the guest
requires each to be enforced separately.

## What the environment provided

On an Apple M1 under Asahi Linux the host page size is 16 KiB. Two guest
segments with different protections fall inside one host page, and a page is
the smallest unit the hardware can protect.

## Why the mismatch is not detected at the call site

There is no call that fails. The emulator can map the file; it simply cannot
give the two halves of a host page different permissions, so it must choose
one and be wrong about the other.

## Manifestation

The maintainers' resolution was to refuse to start rather than mis-map: a
guest that has its protections silently merged fails later, somewhere
unrelated, with a write succeeding where it should have faulted.

## Classification

Primary category: **resource-topology contradiction**. Secondary: *loader/ABI mismatch*.
Layers: application → compatibility_layer → operating_system → kernel.
Finding ids that would diagnose it: `RS-VM-0006`.

## Why this is not an ordinary memory bug

Nothing in the guest binary is malformed. It is correct for a 4 KiB platform,
which is the platform its ABI names. The contradiction is between two page-
size regimes, and no amount of care inside the guest removes it.

## What RuntimeSkeptic would need

`request.required_page_size` with relation `AtMost`, against
`virtual_memory.page_size`. The host fact is `measured_capability` and the
program's is `specified_guarantee` (the ELF ABI), so `PROVEN` is permitted.

## Remediation classes

- `MatchHostPageSize` — derive the page size at runtime instead of baking it in
- `SelectDifferentHostConfiguration` — run on a host configured differently
- `NoKnownRemediationOnThisHost` — no remediation exists on this host

## Open questions

Whether the union-of-permissions workaround marcan proposed is safe for any
real workload. skmp's objection - that ELF vaddrs are often fixed - is not
answered in the thread.

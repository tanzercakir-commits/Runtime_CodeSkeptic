---
id: RSC-0013
title: Guest address-space reservation relocated by the kernel and reported as success, with errno 0
category: semantic success violation
secondary_category: error-code laundering
layers_involved: [compatibility_layer, syscall_interface, operating_system, kernel]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0007, RS-VM-0008]
status: sourced
provenance: public_report
source: https://gitlab.com/qemu-project/qemu/-/issues/447
verified: 2026-07-25
---

# RSC-0013 — Guest address-space reservation relocated by the kernel and reported as success, with errno 0

**Source:** [qemu-arm: Unable to reserve 0xffff0000 bytes of virtual address space at 0x1000 (Success) for use as guest address space](https://gitlab.com/qemu-project/qemu/-/issues/447)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> I see the mmap syscall succeeds but always returns the address 0x10000=65536 regardless of the vm.mmap_min_addr (requested address) setting.

## Summary

qemu-user needs a contiguous 0xffff0000-byte reservation based at 0x1000 to
host a 32-bit ARM guest address space. `vm.mmap_min_addr` forbids mapping that
low. A startup failure whose message contradicts itself.

## What the program required

qemu-user needs a contiguous 0xffff0000-byte reservation based at 0x1000 to
host a 32-bit ARM guest address space. Guest pointers are host pointers plus a
fixed base, so the base is not negotiable.

## What the environment provided

`vm.mmap_min_addr` forbids mapping that low. The kernel treats the address as
a hint rather than a requirement, so it maps successfully at 0x10000 instead.

## Why the mismatch is not detected at the call site

`mmap` returned a valid pointer. QEMU's own check is `addr == MAP_FAILED ||
addr != test`, and only the second disjunct is true - so `errno` was never
set, and the diagnostic printed `strerror(0)`. The error message ends in the
word `(Success)`.

## Manifestation

A startup failure whose message contradicts itself. Setting `vm.mmap_min_addr`
to exactly 65536 makes it work by accident, because then the hint and the
requirement coincide.

## Classification

Primary category: **semantic success violation**. Secondary: *error-code laundering*.
Layers: compatibility_layer → syscall_interface → operating_system → kernel.
Finding ids that would diagnose it: `RS-VM-0007`, `RS-VM-0008`.

## Why this is not an ordinary memory bug

The program asked for a specific base because its correctness depends on that
base. The kernel's answer was legal, successful, and useless. No memory was
corrupted and nothing was misused; the two parties disagreed about whether an
address was a request or a suggestion.

## What RuntimeSkeptic would need

`request.exact_address_required` true with `request.address`, against
`virtual_memory.min_map_address` and `hinted_mapping_may_relocate`. This is
exactly `RS-VM-0007`, and the profile fact is measurable.

## Remediation classes

- `RelaxExactAddressRequirement` — stop requiring the exact address
- `ChooseDifferentBaseAddress` — choose a base the host can satisfy
- `SelectDifferentHostConfiguration` — run on a host configured differently

## Open questions

Whether any qemu-user version distinguishes 'relocated' from 'failed' in its
own diagnostics today.

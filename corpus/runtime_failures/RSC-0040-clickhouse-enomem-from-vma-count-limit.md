---
id: RSC-0040
title: A 4 KiB mapping fails with ENOMEM because the process ran out of VMA slots, not memory
category: error-code laundering
layers_involved: [application, syscall_interface, kernel]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0017]
status: sourced
provenance: public_report
source: https://github.com/ClickHouse/ClickHouse/issues/11027
verified: 2026-07-25
---

# RSC-0040 — A 4 KiB mapping fails with ENOMEM because the process ran out of VMA slots, not memory

**Source:** [Better exception message when cannot allocate memory with mmap.](https://github.com/ClickHouse/ClickHouse/issues/11027)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> The exception Allocator: Cannot mmap 4.00 KiB., errno: 12, strerror: Cannot allocate memory may be thrown even in the case when system has enough free memory.

## Summary

An ordinary 4 KiB mapping on a healthy server. ENOMEM, because
`vm.max_map_count` was reached - the kernel is out of VMA slots for the
process, not out of memory. An out-of-memory exception on a machine with free
memory.

## What the program required

An ordinary 4 KiB mapping on a healthy server.

## What the environment provided

ENOMEM, because `vm.max_map_count` was reached - the kernel is out of VMA
slots for the process, not out of memory.

## Why the mismatch is not detected at the call site

`strerror(ENOMEM)` is 'Cannot allocate memory', which is actively false here.
The size in the report makes it unmistakable: no plausible memory-exhaustion
story explains a 4 KiB mapping failing.

## Manifestation

An out-of-memory exception on a machine with free memory. Distinguishable only
by comparing `/proc/sys/vm/max_map_count` against the live mapping count.

## Classification

Primary category: **error-code laundering**.
Layers: application → syscall_interface → kernel.
Finding ids that would diagnose it: `RS-VM-0017`.

## Why this is not an ordinary memory bug

Nothing leaked. A per-process resource unrelated to memory was exhausted, and
the interface has one errno for both.

## What RuntimeSkeptic would need

A profile fact for the VMA limit and a requirement field for how many distinct
mappings a program creates. Neither exists; `RS-VM-0017` is the honest
placeholder.

## Remediation classes

- `SelectDifferentHostConfiguration` — run on a host configured differently

## Open questions

Whether a VMA-count fact belongs in the virtual-memory domain at all, or is a
separate resource domain. It is a topology constraint that is not about
addresses.

---
id: RSC-0022
title: A non-binding address hint caused the kernel to refuse a reservation it had room for
category: semantic success violation
secondary_category: error-code laundering
layers_involved: [language_runtime, syscall_interface, operating_system]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0007, RS-VM-0017]
status: sourced
provenance: public_report
source: https://gnats.netbsd.org/55533
verified: 2026-07-25
---

# RSC-0022 — A non-binding address hint caused the kernel to refuse a reservation it had room for

**Source:** [nodejs fails with "out of memory" on aarch64 (with reproducer)](https://gnats.netbsd.org/55533)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> If mmap is called with a non-zero hint (and not MAP_FIXED) and collides with an existing allocation, it only succeeds if the mapping fits into the first gap.

## Summary

V8 reserves large aligned regions - the Isolate heap, later the CodeRange - by
passing an address *hint* without `MAP_FIXED`, expecting the kernel to treat
it as advisory and fall back to any suitable gap. NetBSD's `uvm_mmap` began
its gap search at the region containing the hint and never wrapped, so a hint
outside the heap meant only a fraction of the address space was searched.
`Fatal process OOM in insufficient memory to create an Isolate`, later `Failed
to reserve virtual memory for CodeRange`.

## What the program required

V8 reserves large aligned regions - the Isolate heap, later the CodeRange - by
passing an address *hint* without `MAP_FIXED`, expecting the kernel to treat
it as advisory and fall back to any suitable gap.

## What the environment provided

NetBSD's `uvm_mmap` began its gap search at the region containing the hint and
never wrapped, so a hint outside the heap meant only a fraction of the address
space was searched. It returned ENOMEM with ample free VA.

## Why the mismatch is not detected at the call site

ENOMEM from `mmap` is reported by V8 as an out-of-memory condition, because
that is what the errno says. The real cause is a search-order property of the
host's VM layer.

## Manifestation

`Fatal process OOM in insufficient memory to create an Isolate`, later `Failed
to reserve virtual memory for CodeRange`. A one-line script calling
`global.gc()` was enough to trigger it.

## Classification

Primary category: **semantic success violation**. Secondary: *error-code laundering*.
Layers: language_runtime → syscall_interface → operating_system.
Finding ids that would diagnose it: `RS-VM-0007`, `RS-VM-0017`.

## Why this is not an ordinary memory bug

A hint is defined as advisory. The program supplied one and the platform
treated it as a constraint on where to look.

## What RuntimeSkeptic would need

`hinted_mapping_may_relocate` is the relevant fact, but this is its inverse: a
host that will not relocate *enough*. The model has no fact for gap-search
scope.

## Remediation classes

- `RelaxExactAddressRequirement` — stop requiring the exact address
- `ChooseDifferentBaseAddress` — choose a base the host can satisfy

## Open questions

The fix - 'If we fail with a hint, try again without it' - is a workaround in
the kernel. Whether V8 should ever pass a hint it does not require is not
settled in the thread.

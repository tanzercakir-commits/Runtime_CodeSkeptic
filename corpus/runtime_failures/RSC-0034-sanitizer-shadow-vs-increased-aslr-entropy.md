---
id: RSC-0034
title: Increased ASLR entropy scattered loaded objects into address ranges the runtime had statically claimed
category: resource-topology contradiction
secondary_category: error-code laundering
layers_involved: [library, operating_system, kernel]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0001, RS-VM-0017]
status: sourced
provenance: public_report
source: https://github.com/google/sanitizers/issues/1614
verified: 2026-07-25
---

# RSC-0034 — Increased ASLR entropy scattered loaded objects into address ranges the runtime had statically claimed

**Source:** [vm.mmap_rnd_bits=32 cause issues for MSAN, LSAN and ASAN](https://github.com/google/sanitizers/issues/1614)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> FATAL: MemorySanitizer can not mmap the shadow memory.

## Summary

The sanitizer runtimes need multi-terabyte slabs of the address space to be
empty at fixed locations, claimed statically at build time. Distributions
raised `vm.mmap_rnd_bits` from 28 to 32. Nondeterministic, roughly four runs
in ten, and only at `vm.mmap_rnd_bits=32`.

## What the program required

The sanitizer runtimes need multi-terabyte slabs of the address space to be
empty at fixed locations, claimed statically at build time.

## What the environment provided

Distributions raised `vm.mmap_rnd_bits` from 28 to 32. The kernel began
placing loaded objects inside regions the runtimes had claimed.

## Why the mismatch is not detected at the call site

Every individual `mmap` succeeded. The resulting *layout* made the fixed
reservation impossible. Worse, the runtime's own diagnostic advises the
opposite condition: 'Disabling ASLR is known to cause this error' - when the
cause was too much ASLR.

## Manifestation

Nondeterministic, roughly four runs in ten, and only at `vm.mmap_rnd_bits=32`.
On GitHub-hosted runners the filesystem is read-only, so the documented
`sysctl` workaround was unavailable and ASan jobs hung indefinitely.

## Classification

Primary category: **resource-topology contradiction**. Secondary: *error-code laundering*.
Layers: library → operating_system → kernel.
Finding ids that would diagnose it: `RS-VM-0001`, `RS-VM-0017`.

## Why this is not an ordinary memory bug

Both parties behaved correctly. Two policies about where things go in the
address space were set independently and became incompatible.

## What RuntimeSkeptic would need

`available_ranges` measured on the host, against a very large fixed `request`.
This entry is a strong argument that the probe must measure the region a
program will actually claim - the same lesson `T-013` produced.

## Remediation classes

- `SelectDifferentHostConfiguration` — run on a host configured differently
- `ChooseDifferentBaseAddress` — choose a base the host can satisfy
- `NoKnownRemediationOnThisHost` — no remediation exists on this host

## Open questions

Whether a probe run in one process can say anything trustworthy about a layout
that ASLR re-rolls per process. Probably not, and that limit belongs in the
model.

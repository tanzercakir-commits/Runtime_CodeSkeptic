---
id: RSC-0025
title: An archive containing absolute pointers must map at a fixed address that ASLR may already have taken
category: unsupported exact capability
secondary_category: platform-observed behavior treated as guaranteed behavior
layers_involved: [language_runtime, operating_system, kernel]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0001, RS-VM-0016]
status: sourced
provenance: public_report
source: https://bugs.openjdk.org/browse/JDK-8188109
verified: 2026-07-25
---

# RSC-0025 — An archive containing absolute pointers must map at a fixed address that ASLR may already have taken

**Source:** [Xshare:on may cause VM to abort start-up](https://bugs.openjdk.org/browse/JDK-8188109)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> On some platforms, especially Windows and 32-bit Linux, there's a chance that, with address space layout randomization, the CDS address may have been occupied by a system DLL before the JVM starts.

## Summary

Class Data Sharing archives contain absolute pointers, so historically the
archive had to be mapped at exactly the address it was dumped at. ASLR chooses
system library placement at load time, independently. Under `-Xshare:on` the
JVM aborts start-up.

## What the program required

Class Data Sharing archives contain absolute pointers, so historically the
archive had to be mapped at exactly the address it was dumped at. That address
is fixed at dump time.

## What the environment provided

ASLR chooses system library placement at load time, independently. On a run
where the two collide, the mapping at the required address fails.

## Why the mismatch is not detected at the call site

It is detected at the call, but only probabilistically - the same JVM, archive
and machine start successfully most of the time.

## Manifestation

Under `-Xshare:on` the JVM aborts start-up. The documented advice is to use
`-Xshare:auto` in production, which is an instruction to accept a silent
performance loss rather than a startup failure.

## Classification

Primary category: **unsupported exact capability**. Secondary: *platform-observed behavior treated as guaranteed behavior*.
Layers: language_runtime → operating_system → kernel.
Finding ids that would diagnose it: `RS-VM-0001`, `RS-VM-0016`.

## Why this is not an ordinary memory bug

Nothing is corrupted. Two independent choices about address-space layout are
allowed to collide, and one of them was made in a previous process.

## What RuntimeSkeptic would need

`request.exact_address_required` with `request.address` against the host's
available ranges - but the answer is not stable across runs, which is the
interesting part. A profile can say the region is generally free; it cannot
say it will be free this time.

## Remediation classes

- `RelaxExactAddressRequirement` — stop requiring the exact address
- `HandleRelocationInCaller` — accept the returned address and adjust

## Open questions

The eventual fix relocates the archive and patches its pointers, reported as
'mapping failures (below 1%)' in testing. A sub-1% failure rate is exactly the
regime where a static profile answer is most misleading.

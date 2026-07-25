---
id: RSC-0041
title: A shared segment containing self-referential pointers must map at one address in every process
category: unsupported exact capability
secondary_category: platform-observed behavior treated as guaranteed behavior
layers_involved: [application, operating_system, kernel]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0001, RS-VM-0008]
status: sourced
provenance: public_report
source: https://www.postgresql.org/message-id/5046CAEB.4010600@grammatech.com
verified: 2026-07-25
---

# RSC-0041 — A shared segment containing self-referential pointers must map at one address in every process

**Source:** [windows 8 RTM compatibility issue (could not reserve shared memory region for child)](https://www.postgresql.org/message-id/5046CAEB.4010600@grammatech.com)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> This shared memory block presumably stores pointers to itself, and so must be allocated at the same address inside every postgres process.

## Summary

PostgreSQL has no `fork()` on Windows, so it re-reserves the shared-memory
region in each suspended child before any code runs. Windows 8 RTM tightened
ASLR, and images began appearing at addresses in a newborn process that the
postmaster's `MapViewOfFileEx(..., NULL)` had been willing to use. `LOG: could
not reserve shared memory region (addr=0000000001410000) for child ...: 487`.

## What the program required

PostgreSQL has no `fork()` on Windows, so it re-reserves the shared-memory
region in each suspended child before any code runs. The segment holds self-
referential pointers, so the address must be identical in every process.

## What the environment provided

Windows 8 RTM tightened ASLR, and images began appearing at addresses in a
newborn process that the postmaster's `MapViewOfFileEx(..., NULL)` had been
willing to use. Those sets had been disjoint in earlier versions.

## Why the mismatch is not detected at the call site

The *parent's* mapping succeeds and returns a plausible address. The failure
surfaces later, in a different process, as `VirtualAllocEx` returning error
487.

## Manifestation

`LOG: could not reserve shared memory region (addr=0000000001410000) for child
...: 487`. The reporter measured it: never blocked on Windows 7, blocked 2% of
the time on Windows 8.

## Classification

Primary category: **unsupported exact capability**. Secondary: *platform-observed behavior treated as guaranteed behavior*.
Layers: application → operating_system → kernel.
Finding ids that would diagnose it: `RS-VM-0001`, `RS-VM-0008`.

## Why this is not an ordinary memory bug

The dependency was on a property nobody had promised - that two independently
randomised address sets stay disjoint. It held for years.

## What RuntimeSkeptic would need

`request.exact_address_required` where the address is chosen by an earlier
process. The model has no way to express 'the same address in a future
process', which is what this requires.

## Remediation classes

- `RelaxExactAddressRequirement` — stop requiring the exact address
- `ChooseDifferentBaseAddress` — choose a base the host can satisfy
- `SelectDifferentHostConfiguration` — run on a host configured differently

## Open questions

The eventual fix was to disable ASLR for the binary - trading a security
property for a layout guarantee. Whether that trade is recordable as a
remediation class is an open modelling question.

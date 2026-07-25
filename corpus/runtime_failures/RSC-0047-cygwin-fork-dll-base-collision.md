---
id: RSC-0047
title: fork() emulation requires identical library placement in two processes and a filename change broke it
category: unsupported exact capability
secondary_category: loader/ABI mismatch
layers_involved: [application, compatibility_layer, operating_system]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0001]
status: sourced
provenance: public_report
source: https://github.com/Perl/perl5/issues/22104
verified: 2026-07-25
---

# RSC-0047 — fork() emulation requires identical library placement in two processes and a filename change broke it

**Source:** [cygwin: as of the 5.39.10 version bump perl fails to fork with mro loaded](https://github.com/Perl/perl5/issues/22104)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> address space needed by 'mro.dll' (0x190000) is already occupied

## Summary

Cygwin emulates `fork()` by replaying the parent's address-space layout into a
new child. Windows offers no way to demand that, so Cygwin pre-assigns each
DLL a base derived from a hash of its filename and hopes for disjointness -
about a 1-in-89 chance of collision on 64-bit. Perl's Cygwin CI went red after
a routine version bump: the DLL filename changed, the hash changed, and
`cygperl5_39_10.dll` and `mro.dll` landed on top of each other.

## What the program required

Cygwin emulates `fork()` by replaying the parent's address-space layout into a
new child. Every DLL must land at the same base in the child, because a forked
child cannot be relocated.

## What the environment provided

Windows offers no way to demand that, so Cygwin pre-assigns each DLL a base
derived from a hash of its filename and hopes for disjointness - about a
1-in-89 chance of collision on 64-bit.

## Why the mismatch is not detected at the call site

Nothing about the allocation is wrong. The collision is invisible until fork
time, in a different process.

## Manifestation

Perl's Cygwin CI went red after a routine version bump: the DLL filename
changed, the hash changed, and `cygperl5_39_10.dll` and `mro.dll` landed on
top of each other. It un-broke itself at 5.39.11 when the version string
changed again.

## Classification

Primary category: **unsupported exact capability**. Secondary: *loader/ABI mismatch*.
Layers: application → compatibility_layer → operating_system.
Finding ids that would diagnose it: `RS-VM-0001`.

## Why this is not an ordinary memory bug

A version bump is not a memory operation. The dependency is on a hash of a
filename determining an address, which no layer documents as a contract.

## What RuntimeSkeptic would need

`request.exact_address_required` where the address is derived from a name. As
with RSC-0041, the model cannot express 'the same address in another process'.

## Remediation classes

- `ChooseDifferentBaseAddress` — choose a base the host can satisfy
- `RelaxExactAddressRequirement` — stop requiring the exact address

## Open questions

It appeared only in `-DDEBUGGING` builds, where the larger DLL spans far
enough to reach its neighbour. A contradiction that depends on build flags is
one a static profile cannot see.

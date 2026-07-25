---
id: RSC-0052
title: The safe placement flag was reverted from the ELF loader because real binaries have overlapping segments
category: loader/ABI mismatch
secondary_category: unsupported exact capability
layers_involved: [application, kernel]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0016]
status: sourced
provenance: public_report
source: https://github.com/torvalds/linux/commit/b212921b13bda088a004328457c5c21458262fe2
verified: 2026-07-25
---

# RSC-0052 — The safe placement flag was reverted from the ELF loader because real binaries have overlapping segments

**Source:** [elf: don't use MAP_FIXED_NOREPLACE for elf executable mappings](https://github.com/torvalds/linux/commit/b212921b13bda088a004328457c5c21458262fe2)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> Now Russell King reports another overlapping case with old 32-bit x86 binaries, which doesn't trigger that limited case. End result: we had better just drop MAP_FIXED_NOREPLACE entirely, and go back to MAP_FIXED.

## Summary

Existing ELF binaries whose `LOAD` segments share a page - the same file
remapped with different protections, typically a RELRO artifact - require the
loader to be able to map overlapping regions. Once the loader switched to
`MAP_FIXED_NOREPLACE`, the second mapping legitimately overlapped the first
and was refused, so those binaries could not be loaded at all. Users reported
that ordinary programs stopped executing.

## What the program required

Existing ELF binaries whose `LOAD` segments share a page - the same file
remapped with different protections, typically a RELRO artifact - require the
loader to be able to map overlapping regions.

## What the environment provided

Once the loader switched to `MAP_FIXED_NOREPLACE`, the second mapping
legitimately overlapped the first and was refused, so those binaries could not
be loaded at all.

## Why the mismatch is not detected at the call site

It is detected immediately and totally: the program does not start.

## Manifestation

Users reported that ordinary programs stopped executing. It took three commits
to settle - introduce, partially patch, then fully revert for executables. The
flag was retained for the interpreter and library paths, in Linus's words,
'because nobody has reported breakage for those. Yet.'

## Classification

Primary category: **loader/ABI mismatch**. Secondary: *unsupported exact capability*.
Layers: application → kernel.
Finding ids that would diagnose it: `RS-VM-0016`.

## Why this is not an ordinary memory bug

The perfect sequel to RSC-0051. The safe primitive was added to fix a real
hazard, and adopting it broke software that depended on the hazardous
behaviour. Both directions are in this corpus on purpose.

## What RuntimeSkeptic would need

`fixed_noreplace_available` is necessary but not sufficient: a program may
*require* the destructive behaviour. The model has no field for that, and this
entry is the argument that it should.

## Remediation classes

- `RelaxExactAddressRequirement` — stop requiring the exact address
- `NoKnownRemediationOnThisHost` — no remediation exists on this host

## Open questions

Whether any profile should record that a host's loader uses the destructive
form. It is observable and it changes what a program may assume.

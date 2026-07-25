---
id: RSC-0046
title: A reservation with no commit charge is refused because the host kernel has no lazy commit
category: temporal contract violation
secondary_category: capability present but required property absent
layers_involved: [library, compatibility_layer, kernel]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0012]
status: sourced
provenance: public_report
source: https://github.com/microsoft/WSL/issues/2784
verified: 2026-07-25
---

# RSC-0046 — A reservation with no commit charge is refused because the host kernel has no lazy commit

**Source:** [large mmap() with MAP_NORESERVE fails unless overcommit_memory is 1](https://github.com/microsoft/WSL/issues/2784)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> Prints a valid pointer on a real Linux system with overcommit_memory set to 0. Under WSL, mmap returns -1.

## Summary

ASan reserves a ~14 TiB shadow region with `MAP_NORESERVE |
PROT_READ|PROT_WRITE`, relying on Linux honouring a reservation with no commit
charge. WSL 1 implements Linux syscalls on the NT memory manager, which has no
lazy commit: a writable mapping is a commit. Every ASan-instrumented binary
fails to start under WSL 1.

## What the program required

ASan reserves a ~14 TiB shadow region with `MAP_NORESERVE |
PROT_READ|PROT_WRITE`, relying on Linux honouring a reservation with no commit
charge.

## What the environment provided

WSL 1 implements Linux syscalls on the NT memory manager, which has no lazy
commit: a writable mapping is a commit. The reservation is refused.

## Why the mismatch is not detected at the call site

The tell is in the report and is diagnostic gold: mapping the identical range
`PROT_NONE` and then `mprotect`-ing succeeds, because `PROT_NONE` charges no
commit under NT. Same range, same size, different answer depending on
protection.

## Manifestation

Every ASan-instrumented binary fails to start under WSL 1. Fixed in build
17127.

## Classification

Primary category: **temporal contract violation**. Secondary: *capability present but required property absent*.
Layers: library → compatibility_layer → kernel.
Finding ids that would diagnose it: `RS-VM-0012`.

## Why this is not an ordinary memory bug

The mirror image of RSC-0043: there, a POSIX host lacked a Windows primitive;
here, a Windows kernel lacks a POSIX one. Neither is a bug in either kernel.

## What RuntimeSkeptic would need

`reserve_then_commit` against `reserve_commit_model`, with the protection at
reservation time as an additional input. `RS-VM-0012` covers the model
mismatch but not the protection dependence.

## Remediation classes

- `ReserveAddressSpaceEarlier` — reserve the range before anything else can take it
- `SelectDifferentHostConfiguration` — run on a host configured differently

## Open questions

That `PROT_NONE` succeeds where `PROT_READ|PROT_WRITE` fails is a fact no
current profile field can express.

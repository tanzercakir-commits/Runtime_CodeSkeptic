---
id: RSC-0018
title: JIT code must land within branch range of a static symbol and no free page is close enough
category: resource-topology contradiction
secondary_category: capability present but required property absent
layers_involved: [language_runtime, operating_system, hardware]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0024]
status: sourced
provenance: public_report
source: https://github.com/LuaJIT/LuaJIT/issues/285
verified: 2026-07-25
---

# RSC-0018 — JIT code must land within branch range of a static symbol and no free page is close enough

**Source:** [Increase available allocation range in mcode_alloc()](https://github.com/LuaJIT/LuaJIT/issues/285)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> the v2.1 arm64 restriction on executable memory is +/-127MB from the `lj_vm_exit_handler` symbol

## Summary

LuaJIT emits direct relative branches from generated traces back into the
static VM stub `lj_vm_exit_handler`, so every machine-code block must be
allocated inside a hard window: plus or minus 2 GB on x86-64, plus or minus
127 MB on arm64. On Apple Silicon macOS and on Android arm64 the loader places
the LuaJIT image where the surrounding 127 MB is already occupied. Reported as
`LuaJIT with JIT compiler turned on is slower than LuaJIT with JIT compiler
off`, with a 95% allocation failure rate on Android arm64.

## What the program required

LuaJIT emits direct relative branches from generated traces back into the
static VM stub `lj_vm_exit_handler`, so every machine-code block must be
allocated inside a hard window: plus or minus 2 GB on x86-64, plus or minus
127 MB on arm64.

## What the environment provided

On Apple Silicon macOS and on Android arm64 the loader places the LuaJIT image
where the surrounding 127 MB is already occupied. `mcode_alloc()` probes and
finds nothing.

## Why the mismatch is not detected at the call site

Allocation failure is detected; the *consequence* is not surfaced. The JIT
aborts the trace and falls back to the interpreter, so the program keeps
working and simply becomes slower.

## Manifestation

Reported as `LuaJIT with JIT compiler turned on is slower than LuaJIT with JIT
compiler off`, with a 95% allocation failure rate on Android arm64. Only
`-jdump` reveals `failed to allocate mcode memory`.

## Classification

Primary category: **resource-topology contradiction**. Secondary: *capability present but required property absent*.
Layers: language_runtime → operating_system → hardware.
Finding ids that would diagnose it: `RS-VM-0024`.

## Why this is not an ordinary memory bug

The constraint is architectural: a relative branch has a fixed reach. The
program is asking the address space for a property - proximity - that the
allocator interface has no way to express.

## What RuntimeSkeptic would need

`request.max_displacement_bytes` and `request.displacement_reference`, which
the model carries and `RS-VM-0024` explicitly refuses to evaluate in v0.1.
This entry is the reason that refusal is honest rather than lazy.

## Remediation classes

- `ReserveAddressSpaceEarlier` — reserve the range before anything else can take it
- `ChooseDifferentBaseAddress` — choose a base the host can satisfy
- `NoKnownRemediationOnThisHost` — no remediation exists on this host

## Open questions

A user workaround forces the image base with `-Wl,-image_base,0x488800000000`.
Whether that is stable across loader versions is not established.

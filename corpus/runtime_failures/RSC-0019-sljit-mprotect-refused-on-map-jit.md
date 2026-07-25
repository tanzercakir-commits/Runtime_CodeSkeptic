---
id: RSC-0019
title: mprotect refused on a MAP_JIT region because a per-thread mask supersedes page protection
category: capability present but required property absent
secondary_category: error-code laundering
layers_involved: [library, language_runtime, operating_system, hardware]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0009, RS-VM-0011]
status: sourced
provenance: public_report
source: https://github.com/zherczeg/sljit/issues/99
verified: 2026-07-25
---

# RSC-0019 — mprotect refused on a MAP_JIT region because a per-thread mask supersedes page protection

**Source:** [Attempts to mprotect() with MAP_JIT failing on Apple Silicon as of macOS 11.2](https://github.com/zherczeg/sljit/issues/99)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> the `mmap()` with `MAP_JIT` call is succeeding, but the subsequent call to `mprotect()` (on line 185) fails with a generic 'permission denied' error.

## Summary

sljit allocated JIT memory with `mmap(..., MAP_JIT)` and then called
`mprotect()` to make it writable and executable at once - the POSIX contract
that a mapping's protection can be changed after the fact. On Apple Silicon,
W^X is enforced per thread in hardware. PCRE2's JIT stopped working after an
OS point release.

## What the program required

sljit allocated JIT memory with `mmap(..., MAP_JIT)` and then called
`mprotect()` to make it writable and executable at once - the POSIX contract
that a mapping's protection can be changed after the fact.

## What the environment provided

On Apple Silicon, W^X is enforced per thread in hardware. A `MAP_JIT` region
is never both writable and executable, and the toggle is
`pthread_jit_write_protect_np()`, not `mprotect()`.

## Why the mismatch is not detected at the call site

The allocation half of the contract held and the protection half did not. The
refusal is a generic EACCES that says nothing about W^X, so a day was spent
before Apple DTS explained it.

## Manifestation

PCRE2's JIT stopped working after an OS point release. Because PCRE2 can fall
back to non-JIT matching, the visible effect is a performance change, not a
crash.

## Classification

Primary category: **capability present but required property absent**. Secondary: *error-code laundering*.
Layers: library → language_runtime → operating_system → hardware.
Finding ids that would diagnose it: `RS-VM-0009`, `RS-VM-0011`.

## Why this is not an ordinary memory bug

`mprotect` is the correct call on every other POSIX platform. The platform
introduced a protection mechanism that sits above the one the API exposes.

## What RuntimeSkeptic would need

`request.simultaneous_write_execute` against
`protection.write_execute_simultaneous`, and
`protection.jit_entitlement_required`. Both are measurable, so `RS-VM-0009`
reaches `PROVEN`.

## Remediation classes

- `UseSeparateWriteAndExecuteMappings` — separate the writable and executable views
- `RequestPlatformEntitlement` — obtain the platform entitlement the operation needs

## Open questions

Whether `SLJIT_WX_EXECUTABLE_ALLOCATOR` is a complete workaround or only
avoids the failing call.

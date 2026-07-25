---
id: RSC-0024
title: A JIT's compile-time code-page constant is not a multiple of the system page size
category: resource-topology contradiction
secondary_category: capability present but required property absent
layers_involved: [language_runtime, operating_system]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0005, RS-VM-0006]
status: sourced
provenance: public_report
source: https://bugs.ruby-lang.org/issues/19400
verified: 2026-07-25
---

# RSC-0024 — A JIT's compile-time code-page constant is not a multiple of the system page size

**Source:** [YJIT fails to boot on ARM64 systems with 64 KiB pages](https://bugs.ruby-lang.org/issues/19400)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> assertion failed: `(left == right)` left: `16384`, right: `0`: code page size is not page-aligned

## Summary

YJIT's `CodeBlock` uses a compile-time 16 KiB code page because it flips
individual code pages between writable and executable, so its unit of
protection must be a whole multiple of the OS page. An ARMv8 kernel configured
with 64 KiB pages. A panic at boot, with the remainder reported in the
message.

## What the program required

YJIT's `CodeBlock` uses a compile-time 16 KiB code page because it flips
individual code pages between writable and executable, so its unit of
protection must be a whole multiple of the OS page.

## What the environment provided

An ARMv8 kernel configured with 64 KiB pages. `16384 % 65536 == 16384`.

## Why the mismatch is not detected at the call site

It is detected, cleanly, by the runtime's own assertion - which is why this
entry is worth keeping. It is the counterexample to the rest of the corpus: a
program that stated its granularity requirement in code and checked it.

## Manifestation

A panic at boot, with the remainder reported in the message. Three tests
failed together and the cause was in the message.

## Classification

Primary category: **resource-topology contradiction**. Secondary: *capability present but required property absent*.
Layers: language_runtime → operating_system.
Finding ids that would diagnose it: `RS-VM-0005`, `RS-VM-0006`.

## Why this is not an ordinary memory bug

The constant is correct for 4 KiB and 16 KiB hosts, which is what the fix
preserves: use the system page size only when it is 64 KiB.

## What RuntimeSkeptic would need

`request.required_page_size` with relation `MultipleOf` against
`virtual_memory.page_size`. `RS-VM-0005` is the closest existing rule.

## Remediation classes

- `MatchHostPageSize` — derive the page size at runtime instead of baking it in

## Open questions

None material. This is the well-handled case and is recorded as such.

---
id: RSC-0049
title: A wider address space collides with pointers whose high bits were being used as storage
category: platform-observed behavior treated as guaranteed behavior
secondary_category: resource-topology contradiction
layers_involved: [language_runtime, kernel, hardware]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0013]
status: sourced
provenance: vendor_documentation
source: https://docs.kernel.org/arch/x86/x86_64/5level-paging.html
verified: 2026-07-25
---

# RSC-0049 — A wider address space collides with pointers whose high bits were being used as storage

**Source:** [30.3. 5-level paging](https://docs.kernel.org/arch/x86/x86_64/5level-paging.html)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> It's known that at least some JIT compilers use higher bits in pointers to encode their information. It collides with valid pointers with 5-level paging and leads to crashes.

## Summary

JIT compilers took a dependency on an *accidental* property of x86-64: that
the top 17 bits of any valid user pointer are zero, making them free storage
for type tags and NaN-boxing. LA57 gives userspace 56-bit addresses and
removes that property. No incident is recorded here because the kernel
prevented one: it refuses to allocate above 47 bits by default and requires an
explicit high hint to opt in.

## What the program required

JIT compilers took a dependency on an *accidental* property of x86-64: that
the top 17 bits of any valid user pointer are zero, making them free storage
for type tags and NaN-boxing.

## What the environment provided

LA57 gives userspace 56-bit addresses and removes that property.

## Why the mismatch is not detected at the call site

Nothing would fail at the mapping call. A tagged pointer simply becomes a
different, valid address, and the program follows it.

## Manifestation

No incident is recorded here because the kernel prevented one: it refuses to
allocate above 47 bits by default and requires an explicit high hint to opt
in. This entry documents a contradiction that was anticipated instead of
shipped.

## Classification

Primary category: **platform-observed behavior treated as guaranteed behavior**. Secondary: *resource-topology contradiction*.
Layers: language_runtime → kernel → hardware.
Finding ids that would diagnose it: `RS-VM-0013`.

## Why this is not an ordinary memory bug

The runtimes were not wrong to use the bits - the bits were genuinely unused.
They were wrong to assume the property was a guarantee rather than an
observation, which is the category name.

## What RuntimeSkeptic would need

`request.pointer_storage_width_bits` against
`virtual_memory.max_user_address`. `RS-VM-0013` exists for exactly this and
this is its reference case.

## Remediation classes

- `WidenPointerStorage` — widen the storage the address is kept in
- `SelectDifferentHostConfiguration` — run on a host configured differently

## Open questions

Linear Address Masking and ARM Top-Byte-Ignore reopen the same question from
the other direction, by making high bits officially ignorable. Neither is
modelled here.

---
id: RSC-0020
title: MAP_32BIT accepted and satisfied with an address that is not 32-bit
category: semantic success violation
secondary_category: error-code laundering
layers_involved: [language_runtime, syscall_interface, kernel]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0013, RS-VM-0007]
status: sourced
provenance: public_report
source: https://lkml.iu.edu/hypermail/linux/kernel/0708.2/2556.html
verified: 2026-07-25
---

# RSC-0020 — MAP_32BIT accepted and satisfied with an address that is not 32-bit

**Source:** [Re: 2.6.23-rc3-mm1 - memory layout change? - lost support for MAP_32BIT? - mono crashes](https://lkml.iu.edu/hypermail/linux/kernel/0708.2/2556.html)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> mmap(NULL, 65536, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT, -1, 0) = 0x7fa21f5cb000

## Summary

Mono's code manager requests RWX pages with `MAP_32BIT` because its generated
code relies on addresses representable in 32 bits. A flexible-mmap-layout
change in the 2.6.23 `-mm` tree stopped special-casing `MAP_32BIT` on x86-64.
Every Mono-hosted application on the machine stopped starting.

## What the program required

Mono's code manager requests RWX pages with `MAP_32BIT` because its generated
code relies on addresses representable in 32 bits.

## What the environment provided

A flexible-mmap-layout change in the 2.6.23 `-mm` tree stopped special-casing
`MAP_32BIT` on x86-64. `mmap` returned success with 0x7fa21f5cb000 - a 47-bit
address.

## Why the mismatch is not detected at the call site

The flag is a request the kernel is free to honour or not, and the return
value carries no indication which happened. Mono took the pointer and used it.

## Manifestation

Every Mono-hosted application on the machine stopped starting. The reporter
found it as 'Beagle wouldn't start' and 'Evolution wouldn't start', with no
connection to the kernel upgrade visible.

## Classification

Primary category: **semantic success violation**. Secondary: *error-code laundering*.
Layers: language_runtime → syscall_interface → kernel.
Finding ids that would diagnose it: `RS-VM-0013`, `RS-VM-0007`.

## Why this is not an ordinary memory bug

Mono asked for a documented property and was told it had been granted. The
single most dangerous shape in this corpus: a success return that is false.

## What RuntimeSkeptic would need

`request.address_max` (or `pointer_storage_width_bits`) checked against what
the host will actually return. `RS-VM-0013` covers the truncation consequence;
the missing piece is a profile fact for whether `MAP_32BIT` is honoured.

## Remediation classes

- `WidenPointerStorage` — widen the storage the address is kept in
- `SelectDifferentHostConfiguration` — run on a host configured differently

## Open questions

No profile fact exists for per-flag honouring. A host that ignores a placement
flag silently is not currently representable.

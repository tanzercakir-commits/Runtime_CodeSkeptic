---
id: RSC-0053
title: Binaries with sections aligned below the host page size fail before their first instruction
category: loader/ABI mismatch
secondary_category: resource-topology contradiction
layers_involved: [application, operating_system, kernel]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0006]
status: sourced
provenance: vendor_documentation
source: https://asahilinux.org/docs/sw/broken-software/
verified: 2026-07-25
---

# RSC-0053 — Binaries with sections aligned below the host page size fail before their first instruction

**Source:** [Broken Software - Asahi Linux Documentation](https://asahilinux.org/docs/sw/broken-software/)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> If an ELF executable or library has sections which are not aligned to 16K pages, the loader will be unable to map the binary into memory and will signal this failure by causing a segmentation fault before the program even technically starts execution.

## Summary

Software built with 4 KiB section alignment - which is to say most software
built before ARM64 desktops existed. A distribution shipping a 16 KiB-page
kernel. The page's own fixed list is the evidence of scale - Chromium,
jemalloc, Rust, Zig, Redis, Emacs, box64, Wine, WebKitGTK, Qt WebEngine,
libvirt/QEMU, btrfs, lvm2, VS Code and more all needed changes.

## What the program required

Software built with 4 KiB section alignment - which is to say most software
built before ARM64 desktops existed.

## What the environment provided

A distribution shipping a 16 KiB-page kernel.

## Why the mismatch is not detected at the call site

Maximally hostile: a SIGSEGV from the kernel's ELF loader before the program's
first instruction, so there is no stack trace and no application-level error.

## Manifestation

The page's own fixed list is the evidence of scale - Chromium, jemalloc, Rust,
Zig, Redis, Emacs, box64, Wine, WebKitGTK, Qt WebEngine, libvirt/QEMU, btrfs,
lvm2, VS Code and more all needed changes. The still-broken list includes
hardened_malloc and Waydroid.

## Classification

Primary category: **loader/ABI mismatch**. Secondary: *resource-topology contradiction*.
Layers: application → operating_system → kernel.
Finding ids that would diagnose it: `RS-VM-0006`.

## Why this is not an ordinary memory bug

Nothing is wrong with the binaries on the platform they were built for. This
is the single clearest demonstration that page size is a compatibility
boundary, not an implementation detail.

## What RuntimeSkeptic would need

`request.required_page_size` from the ELF alignment against
`virtual_memory.page_size`. `RS-VM-0006`, and the requirement half is
`specified_guarantee` because it is in the file header.

## Remediation classes

- `MatchHostPageSize` — derive the page size at runtime instead of baking it in
- `SelectDifferentHostConfiguration` — run on a host configured differently

## Open questions

Asahi's own answer for the hardest cases is to virtualise a second kernel with
a different page size, which is an admission that no in-process reconciliation
exists.

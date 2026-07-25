---
id: RSC-0054
title: A one-line hardening directive refuses the write-then-execute transition every JIT needs
category: capability present but required property absent
layers_involved: [application, library, operating_system, kernel]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0009, RS-VM-0011]
status: sourced
provenance: public_report
source: https://github.com/php/php-src/issues/21450
verified: 2026-07-25
---

# RSC-0054 — A one-line hardening directive refuses the write-then-execute transition every JIT needs

**Source:** [mod-php: preg_match(): Allocation of JIT memory failed - caused by systemd MemoryDenyWriteExecute](https://github.com/php/php-src/issues/21450)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> Warning: preg_match(): Allocation of JIT memory failed, PCRE JIT will be disabled. This is likely caused by security restrictions.

## Summary

Every JIT needs memory that is at some point writable and then executable -
the write-then-execute transition, if not simultaneity.
`MemoryDenyWriteExecute=yes` installs a seccomp filter that refuses exactly
that. Enabling a hardening directive on a PHP-FPM or Apache unit silently
degrades regex JIT compilation.

## What the program required

Every JIT needs memory that is at some point writable and then executable -
the write-then-execute transition, if not simultaneity.

## What the environment provided

`MemoryDenyWriteExecute=yes` installs a seccomp filter that refuses exactly
that.

## Why the mismatch is not detected at the call site

The warning names the symptom and not the cause. Nothing connects a unit-file
directive to a regex engine's allocator.

## Manifestation

Enabling a hardening directive on a PHP-FPM or Apache unit silently degrades
regex JIT compilation. The report's value is precisely that it makes the
connection.

## Classification

Primary category: **capability present but required property absent**.
Layers: application → library → operating_system → kernel.
Finding ids that would diagnose it: `RS-VM-0009`, `RS-VM-0011`.

## Why this is not an ordinary memory bug

A userspace-imposed VM policy rather than a kernel change, but the same
category: software depended on being allowed to create W-then-X mappings and
the platform stopped permitting it.

## What RuntimeSkeptic would need

`protection.write_then_execute_transition` measured by probe - and this entry
is why the probe must *attempt* it rather than read a capability bit, since
the refusal comes from a seccomp filter the kernel would not otherwise
advertise.

## Remediation classes

- `RequestPlatformEntitlement` — obtain the platform entitlement the operation needs
- `SelectDifferentHostConfiguration` — run on a host configured differently

## Open questions

Whether a probe running inside the same unit would see the same restriction.
It would - which is an argument that profiles are per-service, not per-host.

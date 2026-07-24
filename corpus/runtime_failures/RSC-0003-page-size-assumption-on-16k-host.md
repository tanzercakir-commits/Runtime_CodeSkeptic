---
id: RSC-0003
title: Hard-coded 4 KiB page size breaks on a host whose page size is 16 KiB
category: resource-topology contradiction
layers_involved:
  - application
  - compatibility_layer
  - syscall_interface
  - operating_system
  - hardware
evidence_available:
  - specified_guarantee
  - unknown
finding_ids:
  - RS-VM-0006
  - RS-VM-0004
status: draft
provenance: pattern_reconstruction
---

# RSC-0003 — Hard-coded 4 KiB page size breaks on a host whose page size is 16 KiB

This entry describes a failure pattern reconstructed from general systems knowledge. It is not a citation of a specific reported incident. Replace with a verified incident (with source link) before counting it toward the Phase 0 exit criteria.

## Summary

An emulator mirrors a guest MMU whose page size is 4 KiB and uses the same constant for host mapping operations, on the assumption — universal on x86-64 — that the host page size is also 4 KiB. On a host with a 16 KiB page, `mprotect` on a 4 KiB region affects 16 KiB. Four logically independent guest pages share one host protection domain. Write-protection used for dirty tracking, copy-on-write or guest page-table emulation stops isolating what it was meant to isolate, and the emulator either faults on writes it should permit or fails to notice writes it needed to trap.

## Failure chain

| Layer | What happened |
| --- | --- |
| `application` / `compatibility_layer` | models memory in 4 KiB units and applies protections per unit |
| `syscall_interface` | protection calls operate on host pages, silently rounding the requested extent outward |
| `operating_system` | page size is 16 KiB; the call succeeds and affects four times the requested region |
| `hardware` | the MMU's translation granule is 16 KiB; no smaller protection domain exists |
| `application` | observes protection changes it did not request, and misses traps it expected |

## What the program required

```c
#define GUEST_PAGE_SIZE 4096
static_assert(GUEST_PAGE_SIZE == 4096, "guest page size");

// Write-protect one guest page to trap the next write for dirty tracking.
void protect_guest_page(uintptr_t guest_page) {
    void* host = host_addr_of(guest_page);
    if (mprotect(host, GUEST_PAGE_SIZE, PROT_READ) != 0)
        fatal("mprotect failed");
}
```

The requirement is not "a mapping of 4096 bytes". It is stronger and is stated nowhere:

> A protection change applied to `[addr, addr + 4096)` affects that range and no other byte.

That is a statement about the *granularity of the protection mechanism*, and it is only true when the host page size is 4096.

## What the environment provided

The host's page size is 16384. Protection is a property of a page-table entry, and a page-table entry covers a whole page. `mprotect` with a length of 4096 on such a host either fails an alignment check or succeeds and affects the enclosing 16 KiB page — in both cases the program's model is wrong.

The relevant facts, as they appear in an environment profile:

| Fact | Typical value | Evidence class |
| --- | --- | --- |
| `virtual_memory.page_size` | `16384` | `measured_capability` (probe reads it) or `specified_guarantee` (ABI documents it) |
| `virtual_memory.allocation_granularity` | `16384` | `measured_capability` |

Hosts with page sizes other than 4 KiB are not exotic. 16 KiB and 64 KiB configurations exist across current architectures, and the same architecture can be configured either way, so this is not reducible to an architecture check.

## Why the mismatch is not detected at the call site

- with a 4096-byte length on a 16 KiB-page host, `mprotect` either returns `EINVAL` for misalignment — which is a *detectable* error and the lucky case — or succeeds having rounded outward, which is not detectable at all;
- when it succeeds, there is no return value, no flag and no side channel that tells the caller how much memory was actually affected;
- `sysconf(_SC_PAGESIZE)` is available and would answer the question, but the program never asks: the constant was correct on every machine it was developed on.

## Manifestation

- guest pages spuriously reported as dirty, because a write to a neighbor tripped a shared protection domain — usually presenting as a performance problem (excessive dirty-page traffic) long before it presents as a correctness problem;
- writes that should have trapped and did not, because an earlier `mprotect` on a neighboring guest page already relaxed the shared host page — silent, and the worst outcome;
- `EINVAL` from `mprotect` or `mmap` at startup on the affected host, with an error message about invalid arguments that gives no hint that a page-size constant is the cause;
- code that works on one host family and fails on another with no source difference, which is typically misdiagnosed as a toolchain problem.

## Classification

**Primary category:** *resource-topology contradiction*. The request contradicts the geometry of the address space: the protection mechanism's unit size differs from the unit size the program's model is built on.

**Secondary category:** *capability present but required property absent* — `mprotect` is available, and the property "affects exactly the requested extent" is not.

**Finding IDs:**

| ID | Why |
| --- | --- |
| `RS-VM-0006` | host page size differs from the required page size — the direct diagnosis, emitted by `rule_page_size()` when the requirement declares `required_page_size` and the profile's `page_size` differs |
| `RS-VM-0004` | requested address does not satisfy host allocation granularity — fires as well when a base address that is 4 KiB-aligned is not 16 KiB-aligned |

## Why this is not an ordinary memory bug

Apply the discriminator: the same source is correct on a 4 KiB-page host and incorrect on a 16 KiB-page host, with no undefined behavior in either.

Every arithmetic operation is correct *with respect to the constant the program was compiled with*. There is no overflow, no out-of-bounds access, no invalid pointer. A sanitizer sees a well-formed program because the program is well-formed; what is wrong is a number, and the number is wrong only relative to a host property that no language-level tool models.

This is the clearest case in the taxonomy of a defect that lives in a *constant* rather than in a statement. It is also the case where the obvious fix is wrong, which the analyzer says explicitly:

> **Rejected fix:** "Round the mapping size up to the host page size" — Rounding fixes the allocation but not the program's page-granularity model: protection and dirty tracking still operate on host pages.

Rounding makes the calls succeed. It does not make four guest pages independently protectable, because on this hardware they are not.

## What RuntimeSkeptic would need

**From the requirement document:** `required_page_size: 4096`. This field exists in `MappingRequest` precisely for programs that mirror a guest MMU.

**From the environment profile:** `virtual_memory.page_size` with a real evidence class.

**Resulting analysis**, from `rule_page_size()`:

| Layer | Evidence class | Claim |
| --- | --- | --- |
| `application` | `specified_guarantee` | program requires a page size of 4096 |
| `operating_system` | `measured_capability` | host page size is 16384 |

```text
weakest fact = measured_capability
ceiling      = PROVEN
emitted      = RS-VM-0006, PROVEN, UNSUPPORTED, critical
modeled_fallback = "mappings are rounded up to the larger host page, so
                    distinct guest pages share one host page and their
                    protections collide"
conclusion   = "The host page size differs from the size the program has
                compiled in. No mapping request can repair this."
```

The rule handles both directions. When the host page is *smaller* than the required page, the modeled fallback is different — "one guest page spans several host pages, so protection changes are not atomic with respect to the guest model" — and it is a real defect too, just a quieter one.

When `page_size` is unknown in the profile, the rule emits `RS-VM-0017` with the title overridden to "Host page size was never established", confidence `HYPOTHESIS`, verdict `UNKNOWN`. (That title override is a known registry defect — see `docs/findings/registry.md` section 4.)

## Remediation classes

| Class | Note |
| --- | --- |
| `match_host_page_size` | derive the page size at run time instead of hard-coding it, or build a variant per host page size |
| `select_different_host_configuration` | a host whose page size is 4096 |

Neither is cheap. An emulator whose guest page size is fixed by the guest architecture cannot simply adopt the host's: it must either emulate protection at guest granularity in software, or group guest pages into host-page-sized protection units and accept coarser trapping. That is a design change, which is why the remediation is a *class* and not a patch.

## Open questions

- Should the analyzer distinguish "host page larger than required" from "host page smaller than required" with separate finding IDs? They have different consequences and different remediations, and today they share `RS-VM-0006` with different `modeled_fallback` prose.
- Should a mismatch that is an exact multiple in one direction be treated as milder? Grouping guest pages into a larger host page is at least implementable; the reverse direction breaks atomicity in a way that is harder to recover.
- Is `allocation_granularity` always equal to `page_size` on POSIX-like hosts? The profile models them as separate facts, which is right for Windows, and the relationship should be probed rather than assumed anywhere.
- **Unverified:** the specific host, architecture and emulator structure are illustrative. This entry is a reconstruction, not a report.

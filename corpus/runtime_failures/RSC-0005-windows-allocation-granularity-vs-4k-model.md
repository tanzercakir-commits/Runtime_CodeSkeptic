---
id: RSC-0005
title: Reservation base silently rounded down to a 64 KiB allocation granularity accepted as exact placement
category: invalid fallback
layers_involved:
  - application
  - compatibility_layer
  - syscall_interface
  - operating_system
evidence_available:
  - specified_guarantee
  - unknown
finding_ids:
  - RS-VM-0004
  - RS-VM-0005
  - RS-VM-0014
status: draft
provenance: pattern_reconstruction
---

# RSC-0005 — Reservation base silently rounded down to a 64 KiB allocation granularity accepted as exact placement

This entry describes a failure pattern reconstructed from general systems knowledge. It is not a citation of a specific reported incident. Replace with a verified incident (with source link) before counting it toward the Phase 0 exit criteria.

## Summary

A cross-platform memory manager, written against a model in which the page size and the reservation granularity are the same 4 KiB unit, is ported to a host where mapping happens at 4 KiB page granularity but *reservation* happens in 64 KiB units. Requests for a reservation at a 4 KiB-aligned base are satisfied at the enclosing 64 KiB-aligned base, and requests for a 4 KiB length reserve 64 KiB. The port's compatibility shim treats the rounded result as a successful exact placement — an accepted substitute that discards the property the request was made for. Adjacent sub-allocations collide, and the address space is consumed sixteen times faster than the accounting expects.

## Failure chain

| Layer | What happened |
| --- | --- |
| `application` | requests a reservation at an exact 4 KiB-aligned base, expecting `[base, base + len)` and nothing else |
| `compatibility_layer` | the portability shim passes the request through and returns the platform's rounded result as success |
| `syscall_interface` | reservation rounds the base down and the length up to the allocation granularity |
| `operating_system` | reservation granularity is 64 KiB; page granularity is 4 KiB; the two are different numbers |
| `application` | two logically disjoint reservations now overlap, and a third fails for lack of address space |

## What the program required

```c
// Portable shim written on a host where page size == reservation granularity.
void* reserve_exact(uintptr_t base, size_t len) {
#if defined(_WIN32)
    void* p = VirtualAlloc((LPVOID)base, len, MEM_RESERVE, PAGE_NOACCESS);
#else
    void* p = mmap((void*)base, len, PROT_NONE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) p = NULL;
#endif
    return p;                 // returned as-is; never compared against `base`
}

// Caller's model: disjoint slots, 4 KiB apart.
void* a = reserve_exact(kArenaBase + 0 * 4096, 4096);
void* b = reserve_exact(kArenaBase + 1 * 4096, 4096);
assert(a != b);               // holds; both pointers differ from each other
                              // only because the caller never checks either
                              // against what it asked for
```

Two requirements, neither stated:

1. `returned_base == requested_base` — exact placement at 4 KiB resolution;
2. the reservation covers `[base, base + len)` and no more, so that the next 4 KiB slot is still free.

## What the environment provided

The host distinguishes two granularities:

| Property | Value | Applies to |
| --- | --- | --- |
| page size | 4 KiB | protection, commit, mapping granularity |
| allocation granularity | 64 KiB | the granularity at which address space is reserved |

Reserving with an explicit base rounds the base *down* to a multiple of the allocation granularity and the length *up*. This is documented, deterministic, and not an error: the call succeeds and returns the rounded base. On the platform the shim was written for, the two numbers are equal, so the rounding is invisible and the shim is correct by coincidence.

The profile models these as two independent facts — `virtual_memory.page_size` and `virtual_memory.allocation_granularity` — precisely so that a host where they differ is representable. Evidence class `specified_guarantee` (documented) or `measured_capability` (probed).

## Why the mismatch is not detected at the call site

- the call **succeeds**. There is no error code, no warning, and no flag indicating that rounding occurred;
- the shim returns the platform's pointer without comparing it to the requested base, so the one place where the divergence is visible discards it;
- the caller's own sanity check (`a != b`) passes, because the two calls returned *different* values — the first returned the rounded base of slot 0, the second failed or returned a base inside the same 64 KiB block, and neither equals what was asked for;
- address-space accounting is computed from the requested lengths, not from the reserved lengths, so exhaustion arrives 16x earlier than the accounting predicts and appears as a leak.

## Manifestation

- two logically disjoint regions aliasing the same memory, presenting as data corruption with no memory-safety error at the point of corruption;
- reservation failures with an out-of-address-space error long before the program's own accounting says the space is used;
- a program that is correct on one host family and corrupts on another, with the divergence traceable to no source difference;
- if the shim happens to check for `NULL` only, a rounded-down base that lands outside the program's arena is used as if it were inside it.

## Classification

**Primary category:** *invalid fallback*. The platform performs a substitute operation — reservation at the enclosing granule — and the compatibility shim accepts it as satisfying the request. The substitute is a legitimate operation; it is invalid *as a fallback* because the caller's reason for specifying a base was placement, and the substitute discards exactly that.

**Secondary categories:**
- *capability present but required property absent* — reservation with an explicit base exists; the property "at 4 KiB resolution" does not;
- *resource-topology contradiction* — the program's model has one granularity where the host has two;
- *semantic success violation* — a success return accompanied a false postcondition.

**Finding IDs:**

| ID | Why |
| --- | --- |
| `RS-VM-0004` | requested address does not satisfy host allocation granularity — emitted by `rule_address_alignment()` when an exact-address request is not granularity-aligned; the remediation carries the rounded-down base so the reader sees where the mapping would actually start |
| `RS-VM-0005` | requested size is not a multiple of the host allocation granularity — emitted by `rule_size_granularity()`; impact `CONDITIONALLY_SUPPORTED`, because the mapping succeeds and the surplus is only a defect if the program relies on the bytes past its requested size being unmapped |
| `RS-VM-0014` | permitted fallback contradicts a required postcondition — fires if the requirement document declares exact placement and also lists `relocate` as permitted, which is how a shim's tolerance is expressed in the requirement schema |

## Why this is not an ordinary memory bug

Apply the discriminator: the same source is correct on a host where page size equals allocation granularity, and incorrect on a host where they differ, with no undefined behavior in either.

Every call is well-formed. Every returned pointer is valid and points into a real reservation. The aliasing that eventually corrupts data is not an out-of-bounds access — both regions are inside legitimately reserved address space; they are simply the *same* address space, which the program believes they are not. AddressSanitizer cannot flag an in-bounds write to a region the program deliberately reserved.

The defect is a relation between two numbers the program assumed were one number.

## What RuntimeSkeptic would need

**From the requirement document:** `address` set with `exact_address_required: true`, `size`, and optionally `required_alignment: 4096` to make the program's own model explicit.

**From the environment profile:** `virtual_memory.allocation_granularity` as a fact distinct from `page_size`.

**Resulting analysis**, from `rule_address_alignment()`:

| Layer | Evidence class | Claim |
| --- | --- | --- |
| `application` | `specified_guarantee` | program requires an exact mapping at `0x...1000` |
| `operating_system` | `specified_guarantee` | allocation granularity is 65536 bytes |
| `analyzer` | `specified_guarantee` | `0x...1000 % 65536 != 0` |

```text
weakest fact = specified_guarantee
ceiling      = PROVEN
emitted      = RS-VM-0004, PROVEN, UNSUPPORTED
host_capability  = "exact mappings must be aligned to the allocation
                    granularity of 65536 bytes"
modeled_fallback = "the request is rejected, or the base is rounded down and
                    the mapping starts below the requested address"
conclusion   = "The requested base address is not a multiple of this host's
                allocation granularity, so an exact placement is impossible."
```

The third evidence row is the analyzer's own arithmetic, labelled `specified_guarantee` because it follows from a definition rather than an observation, and attributed to the `analyzer` layer so the reader can see which claims are the tool's own.

`rule_size_granularity()` fires in parallel and reports the reserved size explicitly — *"the host reserves 65536 bytes; the extra 61440 bytes are addressable and will not fault"* — with the remediation "request 65536 bytes explicitly so the program's model matches the reservation".

## Remediation classes

| Class | Note |
| --- | --- |
| `choose_different_base_address` | round the base down to the granularity boundary *in the program's own model*, so the program and the host agree on where the region starts |
| `reserve_address_space_earlier` | reserve one large granularity-aligned block at startup and sub-allocate inside it, which makes the granularity a property of one call instead of thousands |

The second is the structural fix. It replaces N granularity-rounded reservations with one, and moves sub-allocation into the program where 4 KiB resolution is available (commit and protection still operate at page granularity).

**Rejected fix**, which this entry exists to name: *"the call succeeded, so the placement worked."* Success means the platform did something; it does not mean it did what was asked. A shim that returns the platform's pointer without comparing it to the request has thrown away the only evidence that would distinguish the two.

## Open questions

- Should `RS-VM-0004` distinguish "the base was rounded" from "the request was rejected"? They are different outcomes of the same misalignment on different platforms, and the remediations differ in urgency: a rejection is loud, a rounding is silent and therefore worse.
- Should the requirement schema express "the bytes past my requested size must not be addressable"? `RS-VM-0005` reports `CONDITIONALLY_SUPPORTED` because the analyzer cannot tell whether the program relies on that, and the reader has to decide. A field would let the analyzer decide.
- Does an equivalent granularity split exist on POSIX-like hosts with huge-page or superpage reservations? If so, this entry generalizes beyond one platform and the finding text should not read as platform-specific.
- **Unverified:** the shim structure, the arena layout and the failure ordering are illustrative. The 4 KiB page / 64 KiB reservation split is a widely known platform behavior, stated here as behavior and not attributed to any specific report. This entry is a reconstruction, not a report.

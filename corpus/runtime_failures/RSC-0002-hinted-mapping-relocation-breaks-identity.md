---
id: RSC-0002
title: Address hint honored for years, then relocated, breaking a pointer-identity assumption
category: platform-observed behavior treated as guaranteed behavior
layers_involved:
  - application
  - language_runtime
  - syscall_interface
  - operating_system
  - kernel
evidence_available:
  - specified_guarantee
  - unknown
finding_ids:
  - RS-VM-0007
  - RS-VM-0008
status: draft
provenance: pattern_reconstruction
---

# RSC-0002 — Address hint honored for years, then relocated, breaking a pointer-identity assumption

This entry describes a failure pattern reconstructed from general systems knowledge. It is not a citation of a specific reported incident. Replace with a verified incident (with source link) before counting it toward the Phase 0 exit criteria.

## Summary

A runtime passes a non-null address to `mmap` without an exact-placement flag and stores the result as the base of a region whose objects are addressed as `base + offset` with a compressed, offset-sized pointer representation. The kernel is free to return a different address; for years it did not, because the requested range was free at the point in startup where the call was made. A change in address-space layout — a new dependency, an allocator update, a kernel version, a different ASLR draw — makes the kernel return a different base. Everything continues to work until a computation assumes the old base.

## Failure chain

| Layer | What happened |
| --- | --- |
| `application` / `language_runtime` | requires the mapping to land at the requested base so that offsets encode addresses |
| `syscall_interface` | the address is a hint; a different return value is a documented success |
| `operating_system` | returns a different base because the requested range is no longer free at that moment |
| `kernel` | the range is now occupied by something placed earlier in startup |
| `application` | continues with a base it did not expect; every derived pointer is silently wrong |

## What the program required

```c
// The hint has been honored on every machine we have ever tested.
void* base = mmap((void*)kHeapBase, kHeapReserve, PROT_NONE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
if (base == MAP_FAILED)
    fatal("cannot reserve heap");

// Never compared against kHeapBase.
g_heap_base = (uintptr_t)base;
```

and elsewhere, the assumption that makes it load-bearing:

```c
// 32-bit compressed reference into a region assumed to start at kHeapBase.
static inline void* decode(uint32_t ref) {
    return (void*)(kHeapBase + (uintptr_t)ref);   // uses the constant, not g_heap_base
}
```

The requirement is `returned_address == requested_address`. It is nowhere asserted, and the two variables that should be equal — `kHeapBase` and `g_heap_base` — are never compared. The decode path uses the constant; the allocation path uses the returned value. As long as they agree, the program is correct.

Expressed as a requirement document, the distinguishing field is `exact_address_required: false` with `guest_host_identity_required: true`. That combination is the signature of this incident class: the program does not *ask* for exactness but *depends* on it.

## What the environment provided

Documented behavior: an address argument without an exact-placement flag is advisory. The kernel may return a different address, and doing so is success. Nothing in the contract promises the hint will be honored, and nothing promises the *conditions* under which it will be honored are stable.

What made the program appear correct was a coincidence of ordering: the call happened early enough in startup, and the requested range was outside everything the loader and allocator had placed. That coincidence is not a contract, and every element of it can change without any code change in the program:

| Change | Effect |
| --- | --- |
| a new shared library dependency | the loader places it somewhere; ASLR may put it in the range |
| an allocator upgrade | arena placement strategy changes |
| a kernel version | the address-space layout algorithm changes |
| a sanitizer or profiler in the process | large fixed reservations appear |
| a different ASLR draw | the same binary, the same host, a different run |

Evidence class: `specified_guarantee` for the hint semantics. The claim "this hint is always honored" is at best `observed_invariant` — and `observed_invariant` ceilings at `OBSERVED_INVARIANT`, never `PROVEN`, no matter how many runs agree.

## Why the mismatch is not detected at the call site

- the call returns a valid pointer and the only checked failure value is `MAP_FAILED`;
- the requested and returned addresses are never compared, because the code that consumes the base is in a different translation unit from the code that requests it;
- there is no error, no warning, and no observable difference at the call site — the divergence only becomes visible when a decoded pointer is dereferenced.

## Manifestation

- corruption or a crash at an address that is a fixed offset from a valid region, i.e. "close but wrong";
- symptoms that appear after an unrelated change — a dependency bump, a compiler upgrade, a new kernel — with no change to the affected code, so bisection points at the wrong commit;
- intermittent reproduction under ASLR: the same binary on the same host fails on some runs and not others, which is usually misattributed to a race;
- disappearance under a debugger, because attaching changes the address-space layout that caused the divergence.

## Classification

**Primary category:** *platform-observed behavior treated as guaranteed behavior*. The program depends on behavior that is permitted but never promised, and the dependency was invisible for as long as the behavior held.

**Secondary category:** *semantic success violation* — when the divergence does occur, a success return accompanies a false postcondition.

**Finding IDs:**

| ID | Why |
| --- | --- |
| `RS-VM-0007` | address hint may relocate while the caller assumes identity — the direct diagnosis, emitted by `rule_hinted_relocation()` when `hinted_mapping_may_relocate` is true and the requirement declares identity |
| `RS-VM-0008` | identity required without a translation layer — the structural form of the same fragility, reported when no stronger address-specific finding applies |

## Why this is not an ordinary memory bug

Apply the discriminator: the same source is correct on every run where the hint is honored, and incorrect on runs where it is not, with no undefined behavior at the mapping call in either case.

There is a subtlety worth stating: once a wrong base is decoded and dereferenced, the *resulting* access may well be out of bounds, and AddressSanitizer can catch that. But it catches it at the dereference, reporting a memory error in the decode path, with no indication that the cause is a mapping placed at an unexpected address several seconds earlier. The sanitizer finds the symptom and gives it the wrong name. The environment contradiction is upstream and is not a memory error at all.

Contrast with RSC-0001: there the program asks for exactness and the host refuses. Here the program does not ask, and the host obliges anyway — until it does not. RSC-0001 is a present, provable contradiction. This is a *latent* one, and it is the class ROADMAP Phase 6 (counterfactual exploration) exists to enumerate: *what other valid operating-system outcomes could have occurred?*

## What RuntimeSkeptic would need

**From the requirement document:** `address` set, `exact_address_required: false`, `guest_host_identity_required: true`. Detecting this without a hand-written requirement needs CodeSkeptic (Phase 5) to notice that a constant used as a hint is also used as a base in address arithmetic — a dataflow question, not a syntactic one.

**From the environment profile:** `hinted_mapping_may_relocate: true` with evidence `specified_guarantee`.

**Resulting analysis**, from `rule_hinted_relocation()`:

| Layer | Evidence class | Claim |
| --- | --- | --- |
| `application` | `specified_guarantee` | program requires the returned address to equal `kHeapBase` |
| `operating_system` | `specified_guarantee` | a hinted mapping is permitted to relocate on this host |
| `analyzer` | `bounded_counterexample` | a platform-legal outcome (relocation) reaches the caller's failure sink |

```text
weakest fact = bounded_counterexample
ceiling      = COUNTEREXAMPLE
emitted      = RS-VM-0007, COUNTEREXAMPLE, UNSUPPORTED
conclusion   = "This request may succeed today and fail on the next run,
                kernel version or address-space layout. The observed success
                is weaker than the required success."
```

`PROVEN` is unavailable and the ceiling makes that mechanical rather than a matter of judgement. Nothing is unsatisfiable: the mapping may well land on the hint. What is provable is that a legal outcome exists which the program does not survive, and `COUNTEREXAMPLE` is exactly that claim.

## Remediation classes

| Class | Note |
| --- | --- |
| `handle_relocation_in_caller` | compare the returned address against the request and rebase, or fail explicitly; the minimal correct fix |
| `add_address_translation_layer` | decode through the runtime base rather than the constant, removing the dependency |

A third option not in the enum but worth stating: request the range with a non-destructive exact-placement primitive and treat failure as failure. That converts a latent, layout-dependent defect into a deterministic startup error — strictly better, because it is detected at the call site.

**Rejected fix:**

| Proposal | Why it cannot work |
| --- | --- |
| It works on our machines, so ship it | Relocation is permitted rather than forbidden. Absence of relocation in observed runs is not a guarantee about future runs. |

## Open questions

- How would a trace-based mode distinguish "hint always honored" from "hint honored under the conditions we happened to test"? The honest answer is that it cannot: both produce identical traces, which is why the finding rests on the *permission* rather than on observation.
- Should the analyzer emit `RS-VM-0007` when the requirement declares identity but the profile's `hinted_mapping_may_relocate` is `unknown`? Currently the rule returns early and says nothing, which is silent. An `RS-VM-0017`-style unknown finding would be more honest.
- **Unverified:** the compressed-pointer detail and the timeline are illustrative. This entry is a reconstruction, not a report.

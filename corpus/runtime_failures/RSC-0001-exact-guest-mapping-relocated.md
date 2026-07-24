---
id: RSC-0001
title: Fixed guest mapping relocated by the host and reported to the caller as success
category: semantic success violation
layers_involved:
  - application
  - compatibility_layer
  - syscall_interface
  - operating_system
  - kernel
evidence_available:
  - specified_guarantee
  - unknown
finding_ids:
  - RS-VM-0001
  - RS-VM-0008
  - RS-VM-0014
status: draft
provenance: pattern_reconstruction
---

# RSC-0001 — Fixed guest mapping relocated by the host and reported to the caller as success

This entry describes a failure pattern reconstructed from general systems knowledge. It is not a citation of a specific reported incident. Replace with a verified incident (with source link) before counting it toward the Phase 0 exit criteria.

This is the motivating incident of ROADMAP section 3, and the reference case for the whole project.

## Summary

An emulator maps a guest memory region at a fixed host virtual address, `0x1000000000`, because it has no guest-to-host address translation layer and therefore depends on guest addresses being identical to host addresses. The host cannot grant that virtual-address range. Instead of failing, the compatibility layer permits the mapping to land elsewhere and reports success. The guest code later compares the returned address against the requested one, the comparison fails, and the process aborts.

## Failure chain

| Layer | What happened |
| --- | --- |
| `application` | requires `returned_address == requested_address` — guest/host address identity |
| `compatibility_layer` | has no address translation, so identity is load-bearing rather than convenient |
| `syscall_interface` | the mapping primitive available treats the address as a hint, or the exact variant fails |
| `operating_system` | cannot grant the requested range |
| `kernel` | the range is occupied or excluded by the process's address-space layout |
| `compatibility_layer` | accepts a different address and returns success |
| `application` | evaluates the identity check, fails it, terminates |

## What the program required

```c
static const uintptr_t kGuestBase = 0x1000000000;

void* requested = (void*)kGuestBase;
void* result = mmap(requested, kGuestSize, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

if (result == MAP_FAILED)
    return MAP_ERROR;        // the only failure the caller models

assert(result == requested); // the postcondition, declared nowhere
```

Expressed as a requirement document:

```json
{
  "operation": "virtual_memory_map",
  "requested_address": "0x1000000000",
  "size": 16384,
  "exact_address_required": true,
  "guest_host_identity_required": true,
  "translation_layer_available": false,
  "failure_sink": { "kind": "fatal_assert" }
}
```

The postcondition `returned_address == requested_address` is not an optimization. Without a translation layer, every guest pointer the emulator computes is `guest_base + guest_offset` and is dereferenced directly as a host pointer. If the base moves, every derived pointer is wrong, and none of them is checked.

## What the environment provided

The host's user address space does not have `[0x1000000000, 0x1000004000)` free for this process. Plausible causes, none of which the program can influence: a loader or runtime reservation, an allocator arena placed by ASLR, a sanitizer's shadow region, a security policy's excluded range, or an architecture whose usable virtual-address bits do not extend that far.

What the platform *does* guarantee is narrower than what the program assumed. On POSIX-like systems, an address passed to `mmap` without an exact-placement flag is a hint; returning a different address is a documented success. Exact placement requires an explicit flag, and the non-destructive form of that flag is not available everywhere. Where only the destructive form exists, "success" means the request overwrote whatever was there — a different failure, not a fix.

Evidence classes: the hint semantics are `specified_guarantee` (documented). Whether *this* range is available on *this* host is `measured_capability` once a probe has run, and `unknown` until then.

## Why the mismatch is not detected at the call site

Three separate reasons stack:

1. **The caller models two outcomes; the platform offers three.** Success-at-the-requested-address, failure, and success-elsewhere. The third has no branch.
2. **`MAP_FAILED` is the only sentinel checked.** A relocated mapping is a valid pointer to a valid mapping and passes every check the code performs.
3. **The compatibility layer erases the distinction.** Its own API returns a pointer and a status. A relocation and an exact placement produce the same status, so the information is gone before the caller sees it.

## Manifestation

- an assertion failure at a point in startup unrelated to memory setup, if assertions are enabled;
- with `NDEBUG`, no assertion — guest pointers resolve into unrelated host memory and the process corrupts itself, typically crashing much later inside guest code with a backtrace that implicates the guest;
- reproduction depends on address-space layout, so it varies between machines, between runs under ASLR, and disappears under a debugger or a sanitizer that changes the layout;
- the reflexive diagnosis is "the guest binary is corrupt" or "the emulator has a memory bug", both of which are wrong.

## Classification

**Primary category:** *semantic success violation*. A success status was returned and a caller postcondition was false.

**Secondary categories:**
- *unsupported exact capability* — on this host no legal outcome satisfies the requirement, so `RS-VM-0001` is the finding that names the obstacle;
- *invalid fallback* — the compatibility layer's decision to accept relocation is a fallback that discards exactly the property the request was made for;
- *platform-observed behavior treated as guaranteed behavior* — the identity assumption held on every development machine and was never a contract.

**Finding IDs:**

| ID | Why |
| --- | --- |
| `RS-VM-0001` | exact virtual-memory mapping cannot be satisfied — the direct diagnosis, emitted by `rule_range_availability()` |
| `RS-VM-0008` | guest/host identity required without a translation layer — the structural fragility behind it; suppressed when `RS-VM-0001` already fired, because the report should not say it twice |
| `RS-VM-0014` | permitted fallback contradicts a required postcondition — fires if the requirement document lists `relocate` among `permitted_fallbacks` while demanding identity |

## Why this is not an ordinary memory bug

Apply the discriminator: would the same source code, unchanged, be correct on some legal execution environment and incorrect on another, with no undefined behavior in either? Yes. On a host whose address space has that range free, every line is correct and the program runs.

No sanitizer sees anything. There is no out-of-bounds access at the mapping call, no use-after-free, no race, no undefined behavior. `mmap` returned a valid pointer to a valid mapping and the assertion compared two valid pointers. AddressSanitizer, Valgrind and static analysis all pass. The defect is a mismatch between an application postcondition and a host capability — a level at which none of those tools has a model.

## What RuntimeSkeptic would need

**From the requirement document:** `requested_address`, `size`, `exact_address_required: true`, `guest_host_identity_required: true`, `translation_layer_available: false`, `failure_sink: fatal_assert`. All of these exist in `runtime-skeptic.application-requirements.v1` today.

**From the environment profile:** an `unavailable_ranges` entry covering `[0x1000000000, 0x1000004000)` with evidence `measured_capability`, or `min_map_address` / `max_user_address` bounds that exclude it.

**Resulting analysis**, from `Analysis::rule_range_availability()`:

| Layer | Evidence class | Claim |
| --- | --- | --- |
| `application` | `specified_guarantee` | program requires an exact mapping at `0x1000000000` of 16384 bytes |
| `compatibility_layer` | `specified_guarantee` | no guest-to-host translation layer exists, so the guest address must equal the host address |
| `operating_system` | `measured_capability` | requested range intersects an unavailable range |
| `kernel` | `measured_capability` | conflicting range `[0x1000000000, 0x7000000000)` |

```text
weakest fact = measured_capability
ceiling      = PROVEN
emitted      = RS-VM-0001, PROVEN, UNSUPPORTED, critical
conclusion   = "No execution satisfying all constraints exists on this host
                profile. The address is unavailable and no translation layer
                can absorb the difference."
```

Severity is `critical` because the failure sink is `fatal_assert`, not because of the finding's registry default.

If the profile has no observation covering the range, the analysis produces `RS-VM-0017`, `HYPOTHESIS`, verdict `UNKNOWN`, and the instruction to extend the probe. Same program, same host, weaker evidence, weaker claim — see `docs/evidence_model.md` section 8.3.

## Remediation classes

| Class | Note |
| --- | --- |
| `add_address_translation_layer` | removes the requirement rather than satisfying it; the structural fix |
| `choose_different_base_address` | works only if a suitable range is available and stays available |
| `relax_exact_address_requirement` | requires the caller to tolerate the returned address, which requires the translation the emulator lacks |
| `reserve_address_space_earlier` | only helps if the range is free at startup and merely contended later |
| `select_different_host_configuration` | a host whose measured address space has the range free |

**Rejected fixes**, emitted with reasons:

| Proposal | Why it cannot work |
| --- | --- |
| Retry the mapping | The obstacle is a property of this host's address space, not a transient shortage. Every attempt fails identically. |
| Request a smaller size at the same address | The start address itself is unavailable; reducing the length does not make it available. |
| Ignore the returned address and continue | The program compares the returned address against the requested one and treats a mismatch as fatal. |

## Open questions

- Which specific occupant holds the range on a given host? Answering this needs a probe that enumerates the address-space layout, which is Phase 1.
- Does the compatibility layer relocate deliberately (a coded fallback) or by omission (never passing an exact-placement flag)? The two have different fixes and the distinction needs source access.
- Is a non-destructive exact-placement primitive available on the host? If not, `RS-VM-0016` applies as well, and "success" carries a second, independent caveat.
- **Unverified:** every concrete detail above — the address, the size, the emulator's structure — is illustrative. This entry is a reconstruction, not a report.

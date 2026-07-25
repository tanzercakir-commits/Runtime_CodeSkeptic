---
id: RSC-0006
title: Two-phase reserve/commit model ported to a lazy-commit host moves the failure point past every check
category: temporal contract violation
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
  - RS-VM-0012
status: draft
provenance: pattern_reconstruction
---

# RSC-0006 — Two-phase reserve/commit model ported to a lazy-commit host moves the failure point past every check

This entry describes a failure pattern reconstructed from general systems knowledge. It is not a citation of a specific reported incident. Replace with a verified incident (with source link) before counting it toward the Phase 0 exit criteria.

## Summary

A database or runtime is written against a two-phase memory model: reserve a large address range cheaply, then commit sub-ranges as they are needed, checking each commit for failure. Ported to a host that reserves lazily and has no distinct commit step, the "commit" call becomes a no-op or a hint that cannot fail. The program's carefully written out-of-memory handling — which exists, is tested, and is correct — now guards an operation that cannot fail. The shortage surfaces later, at first *touch*, as a fault or an out-of-memory kill, at a point with no error handling and no recovery path.

## Failure chain

| Layer | What happened |
| --- | --- |
| `application` | models reservation and commitment as two distinct, observable states with a checkable failure point at commit |
| `compatibility_layer` | maps the commit call onto an operation that always succeeds on this host |
| `syscall_interface` | there is no commit primitive; memory is backed on demand |
| `operating_system` | reserves address space lazily; the reservation succeeds for the full size |
| `kernel` | backing is allocated at first touch; if none is available, the access faults or the process is killed |
| `application` | the shortage appears at a load or store with no surrounding error handling |

## What the program required

```c
// Phase 1: reserve a large range. Cheap, no backing.
void* arena = reserve(kArenaSize);          // 64 GiB of address space
if (!arena) return OOM_AT_STARTUP;

// Phase 2: commit as the working set grows. This is the checkable point.
int grow_by(size_t bytes) {
    if (!commit(arena + used, bytes))       // the check the program relies on
        return OOM_HANDLED_GRACEFULLY;      // shed load, evict, flush, refuse
    used += bytes;
    return OK;
}
```

The requirement is not "memory is available". It is:

> Reservation and commitment are distinct, observable states, and the transition between them has a failure mode the program can detect and handle at a point of its choosing.

That is a statement about *when* a failure becomes visible. It is a temporal property, and it is the whole reason the two-phase code was written.

## What the environment provided

A host that reserves address space lazily: the reservation succeeds for the full requested size and physical backing is attached at first access. There is no distinct commit operation and therefore no commit-time failure. On such hosts an explicit "commit" is typically implemented as a protection change or an advisory hint, both of which succeed regardless of whether backing will be available later.

The consequence is not that the program gets less memory. It is that the *failure point moves*:

| Model | Where the shortage becomes visible | Is it checkable? |
| --- | --- | --- |
| two-phase reserve/commit | at the commit call | yes — a return value |
| lazy commit | at the first touch of a page | no — a load or a store |

The profile carries this as `virtual_memory.reserve_commit_model`, with values `posix_lazy`, `windows_reserve_commit` or `unknown`. Evidence class `specified_guarantee` — it is a documented platform property, not something a probe needs to discover, though a probe can confirm it.

An additional wrinkle worth recording: on lazy-commit hosts the behavior under exhaustion is itself configurable. Depending on system policy, an over-committed touch may fault, or the process may be selected for termination by an out-of-memory killer — in which case the program does not get a fault to handle either, it simply stops existing.

## Why the mismatch is not detected at the call site

- the shim's `commit()` returns success, always, and truthfully: the operation it performs did succeed;
- there is no API on such a host that answers "will touching this page succeed?", because the answer depends on system-wide state at the moment of the touch;
- the reservation succeeded for the full size, so every early sanity check confirms the program's model;
<!-- checked: 2026-07-25 -->
- the error-handling path the program wrote is not dead code — it is unreachable, which no compiler warning and no coverage report distinguishes from "not exercised in this test run".

## Manifestation

- a process killed by the system out-of-memory killer with no log line from the program, at a moment unrelated to any allocation;
- a fault on an ordinary load or store, in a function that does not allocate and has no memory-related code;
- graceful-degradation paths (load shedding, cache eviction, backpressure) that are correct, tested, and never execute in production;
- capacity behavior that differs qualitatively between platforms: on one, the service refuses work under pressure; on the other, it dies.

## Classification

**Primary category:** *temporal contract violation*. Every individual operation succeeds. What differs is the *ordering*: the point at which a failure can be detected has moved from a call the program checks to an access it does not.

**Secondary category:** *capability present but required property absent* — address-space reservation is available; the property "commitment is a separate, failable step" is not.

**Finding ID:**

| ID | Why |
| --- | --- |
| `RS-VM-0012` | reserve/commit semantics differ from the program's model — emitted by `rule_reserve_commit()` when the requirement sets `reserve_then_commit: true` and the profile's `reserve_commit_model` is `posix_lazy`; impact `CONDITIONALLY_SUPPORTED`, because the program runs and is merely undefended |

The `CONDITIONALLY_SUPPORTED` impact is the right call and worth defending: the host *can* satisfy the memory request. What it cannot satisfy is the program's expectation about where failure appears. That is a genuine defect and it is not an impossibility, so the verdict must not be `UNSUPPORTED`.

## Why this is not an ordinary memory bug

Apply the discriminator: the same source is correct on a two-phase host and undefended on a lazy-commit host, with no undefined behavior in either.

There is no invalid access. The page that faults is inside a reservation the program made deliberately, at an offset the program computed correctly. A sanitizer sees a legitimate access to legitimate address space; the fault comes from the kernel's inability to back it, which is not a property of the program's memory at all.

This is also the entry where the *absence* of a symptom is the symptom. On a well-provisioned machine the program behaves identically under both models, indefinitely. The defect is latent until resource pressure arrives, and resource pressure arrives in production.

## What RuntimeSkeptic would need

**From the requirement document:** `reserve_then_commit: true`. One boolean, and it is the field that makes this entire class decidable before deployment.

**From the environment profile:** `virtual_memory.reserve_commit_model`.

**Resulting analysis**, from `rule_reserve_commit()`:

| Layer | Evidence class | Claim |
| --- | --- | --- |
| `application` | `specified_guarantee` | program reserves address space and commits it later |
| `operating_system` | `specified_guarantee` | reserve/commit model is `posix_lazy` |

```text
weakest fact = specified_guarantee
ceiling      = PROVEN
emitted      = RS-VM-0012, PROVEN, CONDITIONALLY_SUPPORTED, medium
host_capability  = "this host reserves address space lazily; there is no
                    separate commit step and no failure point at commit time"
modeled_fallback = "the reservation appears to succeed for the full size and
                    the shortage surfaces later, at first touch, as a fault or
                    an out-of-memory kill rather than as a commit failure"
conclusion   = "The program's two-phase model maps onto a one-phase host.
                Failures move from a checked call site to an unchecked
                memory access."
```

Severity is the registry default `medium`, raised by `adjust_severity()` if the requirement declares a `fatal_assert` or `unchecked` failure sink. An `unchecked` sink is the common case here and raises it to `critical`, which is correct: the whole problem is that nothing checks.

## Remediation classes

| Class | Note |
| --- | --- |
| `reserve_address_space_earlier` | with the detail *"pre-fault or explicitly populate the range if the program needs the shortage to surface at a checkable point"* |

The remediation is narrow because the honest options are narrow. To restore a checkable failure point on a lazy-commit host the program must force backing to be allocated at the moment it wants to check — populating the mapping at reservation time, touching pages under a fault handler, or accounting against a system-imposed limit and refusing work before the limit is reached. Each has a real cost (startup latency, resident-set size, or a duplicated accounting model), and choosing among them is a design decision the analyzer will not make.

**Rejected fixes** worth stating even though the current rule does not emit them:

| Proposal | Why it cannot work |
| --- | --- |
| Check available memory before committing | The answer is stale before it is returned; another process can consume the memory between the check and the touch. |
| Handle the fault with a signal handler and recover | A fault from an unbacked page is not resumable in general, and an out-of-memory kill delivers no signal to handle. |

## Open questions

- Should the profile model the *exhaustion behavior* (fault versus process termination) as a separate fact? They demand different program responses, and the current `reserve_commit_model` enum does not distinguish them.
- Should there be a distinct finding for the reverse port — a program written for lazy commit, running on a host where reservation without commitment does not make memory usable? That direction is quieter but not harmless.
- Does an explicit populate-at-reservation flag exist on the target host, and does it fail at reservation time or merely pre-fault? The difference determines whether `reserve_address_space_earlier` actually restores a checkable failure point.
- **Unverified:** the arena size, the shim structure and the deployment scenario are illustrative. This entry is a reconstruction, not a report.

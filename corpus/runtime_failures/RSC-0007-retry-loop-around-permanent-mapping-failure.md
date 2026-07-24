---
id: RSC-0007
title: Retry loop with backoff around a fixed mapping that is permanently impossible on the host
category: permanent error treated as retryable
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
  - RS-VM-0015
  - RS-VM-0001
status: draft
provenance: pattern_reconstruction
---

# RSC-0007 — Retry loop with backoff around a fixed mapping that is permanently impossible on the host

This entry describes a failure pattern reconstructed from general systems knowledge. It is not a citation of a specific reported incident. Replace with a verified incident (with source link) before counting it toward the Phase 0 exit criteria.

## Summary

A compatibility layer wraps a fixed-address mapping in a retry loop with exponential backoff, on the theory that a mapping failure indicates transient resource contention. The failure is not transient: the requested range is permanently occupied in this process's address space. Every attempt fails identically. With a bounded loop the process pauses for the full backoff schedule and then fails with a message naming the last attempt; with an unbounded loop it hangs at startup in a state that looks like slow initialization. The retry is what makes the failure hard to diagnose, because it converts a clean, immediate, informative error into a delay.

## Failure chain

| Layer | What happened |
| --- | --- |
| `application` | requires a mapping at a fixed address; retries on failure, assuming transience |
| `compatibility_layer` | collapses distinct native error codes into one generic "try again" status |
| `syscall_interface` | returns a permanent error — the range is occupied, or the address is outside a permitted region |
| `operating_system` | the obstacle is a property of the address-space layout, stable for the process lifetime |
| `kernel` | the range is held by a mapping created before this code ran |
| `application` | retries, waits, retries, and either fails late or never finishes starting |

## What the program required

```c
static const uintptr_t kFixedBase = 0x1000000000;

void* map_arena(size_t size) {
    useconds_t backoff = 1000;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        void* p = mmap((void*)kFixedBase, size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (p != MAP_FAILED)
            return p;

        // errno was EEXIST. Treated as contention.
        usleep(backoff);
        backoff *= 2;
    }
    fatal("could not map arena after %d attempts", kMaxAttempts);
}
```

Two requirements, only one of which is deliberate:

1. a mapping at exactly `kFixedBase` (deliberate — the layer has no translation);
2. **that the operation eventually succeeds after retrying** (accidental — an inference from the shape of the code, and the one that is false).

The second requirement is what the retry loop *asserts*. Nothing in the program states it, and nothing in the platform supports it.

## What the environment provided

The native failure is permanent for the lifetime of the process's address-space layout:

| Native error | Meaning | Transient? |
| --- | --- | --- |
| `EEXIST` from a non-destructive fixed mapping | the range is already mapped | no — nothing in this process will unmap it |
| `EPERM` / policy refusal | the address is below a policy floor, or excluded | no — a system setting, not a resource |
| `EINVAL` | the address or length violates an alignment or bounds rule | no — arithmetic |
| `ENOMEM` | resource exhaustion | **sometimes** — the only genuinely retryable case |

Only the last row justifies a retry, and only under memory pressure that is expected to abate. A loop that treats all four identically retries three permanent conditions.

Evidence class: `specified_guarantee` for the meaning of each error code; `measured_capability` for whether *this* range is occupied on *this* host.

## Why the mismatch is not detected at the call site

- the compatibility layer discards `errno` when it maps the native failure onto its own status, so by the time the retry decision is made the information needed to make it correctly is gone;
- the retry loop's structure implies a classification that nobody wrote down and nobody reviewed: "failure ⇒ transient";
- from inside the loop, a permanent failure and a genuinely contended one are indistinguishable, because both present as N consecutive failures;
- the message emitted after exhaustion names the last attempt, which is identical to the first, so it carries no information about why the operation could never succeed.

## Manifestation

- startup that hangs, or takes exactly the sum of the backoff schedule before failing, so the symptom is reported as a performance problem;
- with an unbounded loop, a process that never becomes ready and never logs an error — the worst variant, because monitoring sees a live process;
- a failure message that names a resource shortage on a machine with abundant free memory, sending the investigation toward capacity;
- the reflexive fix — raise `kMaxAttempts`, lengthen the backoff — appears to change the behavior (it changes the timing), gets shipped, and makes the next occurrence harder to diagnose.

## Classification

**Primary category:** *permanent error treated as retryable*. The program retries an operation whose failure is structural on this host.

**Secondary category:** *error-code laundering*. The retry is only possible because the layer below collapsed `EEXIST`, `EPERM`, `EINVAL` and `ENOMEM` into one status. Laundering is the cause; the retry loop is the consequence. This entry is filed under the consequence because that is what is mechanically detectable in v0.1 — detecting the laundering itself requires observing the native error before and after a layer boundary, which needs the Phase 4 runtime wrapper.

**Finding IDs:**

| ID | Why |
| --- | --- |
| `RS-VM-0015` | retry loop targets a permanently impossible operation — emitted by `rule_retry_of_permanent_error()` |
| `RS-VM-0001` | exact virtual-memory mapping cannot be satisfied — the underlying impossibility that `RS-VM-0015` depends on |

The dependency is structural. `rule_retry_of_permanent_error()` runs last and fires only when another rule has already produced a `PROVEN` + `UNSUPPORTED` finding:

```cpp
void Analysis::rule_retry_of_permanent_error() {
    if (!req_.assumptions.retries_on_failure) return;
    if (!proven_unsupported()) return;   // impossibility must be established first
    ...
}
```

RuntimeSkeptic never reports "this retry is futile" on a hunch. It reports it only after proving the operation cannot succeed, which is the difference between this finding and a lint rule about retry loops.

## Why this is not an ordinary memory bug

Apply the discriminator: the same source is correct on a host where the range is free — the first attempt succeeds and the loop never iterates — and incorrect on a host where it is not, with no undefined behavior in either.

The loop is well-formed. It terminates exactly as written. It touches no invalid memory, leaks nothing, and races with nothing. No sanitizer, no static analyzer and no code review that does not know the target host's address-space layout can identify the defect, because the defect is a *false belief about the nature of an error code* — a belief encoded in control flow rather than in data.

It is also the entry that best illustrates why RuntimeSkeptic emits rejected fixes as a first-class output. The natural response to "it failed after 8 attempts" is "try 32". The finding says why that cannot help, in the report, before anyone spends a day on it.

## What RuntimeSkeptic would need

**From the requirement document:** `retries_on_failure: true`, optionally `max_retries`, plus the fields that establish the underlying impossibility (`address`, `exact_address_required`).

**From the environment profile:** whatever proves the impossibility — an `unavailable_ranges` entry, `min_map_address`, `max_user_address`, or `exact_mapping: UNSUPPORTED`.

**Resulting analysis.** `rule_range_availability()` fires first and emits `RS-VM-0001` as `PROVEN` / `UNSUPPORTED`. Then:

| Layer | Evidence class | Claim |
| --- | --- | --- |
| `application` | `specified_guarantee` | program retries the operation after failure |
| `analyzer` | `measured_capability` | another rule proved this request cannot succeed on this host |

```text
weakest fact = measured_capability
ceiling      = PROVEN
emitted      = RS-VM-0015, PROVEN, UNSUPPORTED, high
modeled_fallback = "the program exhausts N attempts and then takes its failure
                    path"  (or "retries without bound and never makes progress")
conclusion   = "A permanently impossible operation is being treated as a
                retryable error. This is error-code laundering: the platform's
                permanent refusal is reinterpreted as transient."
```

Severity is raised to at least `high` by `adjust_severity()` for a `retry_loop` failure sink, and to `critical` if the loop's exhaustion path is a fatal abort.

*Implementation note, recorded because it is visible in this entry's evidence chain:* the analyzer's own step is labelled `measured_capability` with source `"analyzer: cross-rule aggregation"`. The ceiling it produces is defensible — the rule can only fire behind a `PROVEN` finding — but the label describes the upstream fact rather than this step. A dedicated evidence class for analyzer-internal derivation would be more honest; none exists yet. See `docs/failure_taxonomy.md` section 5.

## Remediation classes

| Class | Note |
| --- | --- |
| `treat_error_as_permanent` | classify this failure as permanent and stop retrying; fail immediately with the native error preserved |

The real fix has two halves, and only the first is in the enum:

1. **Stop retrying** what cannot succeed — the finding's remediation class.
2. **Stop laundering** the error codes, so the retry decision can be made correctly. A layer that preserves `EEXIST` versus `ENOMEM` lets the caller retry the one case that is genuinely transient and fail fast on the three that are not. ROADMAP Phase 4 requires wrappers to preserve native error state for exactly this reason.

**Rejected fix:**

| Proposal | Why it cannot work |
| --- | --- |
| Increase the retry count or add a backoff | Neither changes the outcome; the operation is impossible on this host, not contended. |

## Open questions

- Should the analyzer emit a weaker finding when a retry loop wraps an operation whose impossibility is `UNKNOWN` rather than proven? Today it stays silent, which is safe but misses the case where the retry is unjustifiable regardless of the outcome.
- Should retry loops around operations that *are* transiently failable (genuine `ENOMEM`) be checked for an unbounded schedule? That is a liveness property and belongs to Phase 7.
- What does the requirement schema need to express "this loop retries on error code X but not Y"? Today `retries_on_failure` is one boolean and cannot distinguish a correctly discriminating loop from an indiscriminate one — which means a well-written loop and this one look identical to the analyzer.
- **Unverified:** the loop parameters, the address and the layer structure are illustrative. This entry is a reconstruction, not a report.

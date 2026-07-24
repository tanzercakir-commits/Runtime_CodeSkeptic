---
id: RSC-0004
title: JIT requiring simultaneous write and execute permission blocked by a write-xor-execute policy
category: capability present but required property absent
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
  - RS-VM-0009
  - RS-VM-0011
  - RS-VM-0010
status: draft
provenance: pattern_reconstruction
---

# RSC-0004 — JIT requiring simultaneous write and execute permission blocked by a write-xor-execute policy

This entry describes a failure pattern reconstructed from general systems knowledge. It is not a citation of a specific reported incident. Replace with a verified incident (with source link) before counting it toward the Phase 0 exit criteria.

## Summary

A JIT compiler allocates a code buffer with read, write and execute permissions, emits instructions into it through the same pointer it will later call, and calls it. The host enforces write-xor-execute: a mapping may be writable or executable, never both at once. The allocation either fails, or succeeds with execute permission absent, and the first call into the generated code faults. Executable memory is available on this host and writable memory is available on this host; the conjunction is not.

## Failure chain

| Layer | What happened |
| --- | --- |
| `application` / `language_runtime` | requires one mapping that is writable and executable simultaneously |
| `syscall_interface` | the protection argument requesting both is rejected, or accepted with execute dropped |
| `operating_system` | enforces write-xor-execute as a security policy; may additionally gate executable memory behind a per-process entitlement |
| `kernel` | no page-table entry in this process may carry both write and execute permission |
| `application` | calls into the buffer and takes a fault, or never gets that far |

## What the program required

```c
// Classic single-buffer JIT: one pointer, written through and then called.
void* code = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
if (code == MAP_FAILED)
    fatal("cannot allocate code buffer");

emit_prologue(code);          // writes
emit_body(code);              // writes, possibly interleaved with patching
emit_epilogue(code);          // writes

((code_fn)code)();            // executes, through the same mapping
```

Two requirements, only one of which is visible in the call:

1. an anonymous mapping may be executable at all;
2. write and execute permissions may be live on that mapping **at the same time** — required because the emitter patches earlier instructions (branch targets, inline caches, deoptimization stubs) after later ones have been written, and in some designs after execution has already begun.

The second is what fails. It is the difference between `simultaneous_write_execute` and `write_then_execute` in the requirement schema, and the two have entirely different remediations.

## What the environment provided

The host provides executable memory and writable memory as separate capabilities and refuses their conjunction within one mapping. Depending on the platform this appears as:

- the mapping call failing outright when both permissions are requested;
- the mapping succeeding with execute silently absent, so the failure moves to the first call;
- the mapping succeeding fully, but only for a process that carries an explicit entitlement or policy opt-in, and failing for one that does not;
- a per-thread mode in which a mapping alternates between writable and executable, so the two permissions exist but never overlap in time.

The profile models these as four independent facts:

| Fact | Question it answers |
| --- | --- |
| `protection.anonymous_executable_mapping` | can anonymous memory be executable at all? |
| `protection.write_execute_simultaneous` | can one mapping be writable and executable at once? |
| `protection.write_then_execute_transition` | is the RW → RX transition permitted? |
| `protection.jit_entitlement_required` | does the platform demand a process-level opt-in? |

They are separate because the answers are independent, and because the remediation depends on *which* of them is false. A host that answers no/yes/yes/no needs a two-mapping or flip-based JIT. A host that answers no/no/no/yes needs an entitlement and nothing else. Collapsing them into "JIT works: yes/no" would produce a finding with no actionable content.

Evidence class: `specified_guarantee` where the platform documents the policy, `measured_capability` from a probe. This is the category where measurement matters most, because "the mapping call accepted `PROT_EXEC`" and "the resulting page is actually executable" are different facts and only the second one matters.

## Why the mismatch is not detected at the call site

- when execute permission is silently dropped, the mapping call returns a valid pointer and there is no API to ask "what protection did I actually get?";
- when the platform reports the refusal, it may do so with a generic resource error, sending the investigation toward memory limits rather than toward policy — an instance of *error-code laundering*;
- the failure surfaces at the first indirect call into the buffer, which appears in a backtrace as a jump to a valid address, giving no indication that a permission is the cause.

## Manifestation

- `SIGSEGV` / `SIGBUS` / `EXCEPTION_ACCESS_VIOLATION` on the first execution of generated code, at an address that is inside a valid mapping;
- identical source producing different behavior between a signed or entitled build and a locally built binary — the classic "works in the release build, crashes in mine";
- allocation failure at JIT warm-up with an error that names memory rather than policy;
- correct behavior in a hardened deployment only after an operator disables the protection, which is the wrong fix and is often the first one tried.

## Classification

**Primary category:** *capability present but required property absent*. Executable memory exists. Writable memory exists. The required property is their simultaneity, and only that is missing.

**Secondary categories:**
- *unsupported exact capability* — if `anonymous_executable_mapping` is false, nothing about the request is achievable and `RS-VM-0010` applies;
- *error-code laundering* — when a policy refusal is reported as a resource error;
- *temporal contract violation* — the correct design on such a host is a lifecycle (`Allocated → Writable → CodeWritten → CacheFlushed → Executable`), and omitting the flush step on a non-coherent architecture is a separate defect in that category.

**Finding IDs:**

| ID | Why |
| --- | --- |
| `RS-VM-0009` | simultaneous write and execute permission is restricted — the direct diagnosis, emitted by `rule_write_execute()` |
| `RS-VM-0011` | executable memory requires a platform entitlement — emitted by `rule_jit_entitlement()` when the process-level gate is the obstacle; impact `CONDITIONALLY_SUPPORTED`, because the capability is reachable |
| `RS-VM-0010` | anonymous executable mapping is not available — emitted by `rule_executable_mapping()` in the stricter case where executable anonymous memory is unavailable regardless of permissions |

## Why this is not an ordinary memory bug

Apply the discriminator: the same JIT is correct on a host that permits RWX mappings and incorrect on a host that does not, with no undefined behavior in either.

Executing data as code through a valid, executable mapping is not a memory-safety violation. A sanitizer has nothing to report before the fault, and after the fault it reports a fault. The defect is that the program's memory model requires a permission combination this host declines to grant.

The direction of remediation is the point that distinguishes this entry from every other in the corpus, and RuntimeSkeptic states it as a rejected fix:

> **Rejected fix:** "Disable the platform's W^X enforcement" — RuntimeSkeptic does not treat a security control as a defect, and disabling it is not a portable remediation.

The program has to change. The platform will not. ROADMAP section 18 requires the tool to distinguish unsupported behavior from security-policy enforcement, and this is where that requirement earns its place: the analyzer's conclusion reads *"Write-xor-execute is a security policy, not a defect. The program's memory model has to change; the platform's will not."*

## What RuntimeSkeptic would need

**From the requirement document:** `protection: {read, write, execute}` or `simultaneous_write_execute: true`, plus `write_then_execute: true` if the program can tolerate the flip.

**From the environment profile:** the four protection facts listed above.

**Resulting analysis**, from `rule_write_execute()` on a host that refuses simultaneity but permits the RW → RX transition:

| Layer | Evidence class | Claim |
| --- | --- | --- |
| `application` | `specified_guarantee` | program requires simultaneous write and execute permission |
| `operating_system` | `measured_capability` | simultaneous write+execute is not available |
| `operating_system` | `measured_capability` | the RW to RX transition is available |

```text
weakest fact = measured_capability
ceiling      = PROVEN
emitted      = RS-VM-0009, PROVEN, UNSUPPORTED, critical
modeled_fallback = "the protection can be flipped between writable and
                    executable, but never both at once"
```

Note the value of the third row. It converts an unhelpful "this does not work" into "this does not work, and here is the shape of what does" — which is what makes `use_separate_write_and_execute_mappings` an actionable remediation rather than a guess.

## Remediation classes

| Class | Note |
| --- | --- |
| `use_separate_write_and_execute_mappings` | map the same pages twice — once writable, once executable — so neither mapping carries both permissions |
| `use_separate_write_and_execute_mappings` (flip variant) | serialize the JIT into write, flush the instruction cache, then flip to executable |
| `request_platform_entitlement` | declare the platform entitlement or policy opt-in that unlocks executable memory, where one exists |
| `select_different_host_configuration` | a host or process configuration whose measured capabilities permit the request |

The flip variant has a cost that belongs in the finding: patching already-emitted code now requires a protection change, so designs that patch frequently need restructuring (deferred patch lists, indirection through a writable stub table), not just a call reordering.

**Rejected fix:** disable the platform's enforcement — see above.

## Open questions

- Should `write_then_execute` require an explicit cache-flush step in the requirement model? On architectures with non-coherent instruction caches, flipping to executable without flushing produces execution of stale bytes — a *temporal contract violation* that the v0.1 model cannot express. This is ROADMAP Phase 7 (`property JitMemoryLifecycle`).
- Should per-thread write-protection modes be a distinct profile fact? They are neither "simultaneous" nor a plain RW → RX transition, and modeling them as either loses information.
- How should the analyzer treat a process that *could* carry an entitlement but does not? Today `RS-VM-0011` reports `CONDITIONALLY_SUPPORTED`, which is right, but the profile cannot express "this process, as configured, lacks it" versus "this platform has no such mechanism".
- **Unverified:** the specific platform behaviors, entitlement mechanisms and JIT structure are illustrative. This entry is a reconstruction, not a report.

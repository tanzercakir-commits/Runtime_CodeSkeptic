# Problem Statement

The failures RuntimeSkeptic targets are not wrong instructions; they are correct instructions executed against a layer that quietly supplied weaker semantics than the caller required.

**Status:** v0.1 built and shipping. The problem framing and terminology described here are stable and implemented in code (`include/runtimeskeptic/core/evidence.hpp`, `src/core/evidence.cpp`). The analysis capability described in "Scope of v0.1" is implemented and under test: the requirement/profile/analyzer path (`src/vm/analyzer.cpp`), the environment probe (`rs-env-probe` — Linux and macOS measured, Windows a stub that reports every fact unknown rather than guessing), and the command-line tools (`rs-check`, `rs-profile`, `rs-replay`, `rs-mcp`) all exist and build with nothing but a C++20 compiler and CMake. <!-- checked: 2026-08-02 -->

---

## 1. The central question

> **Does the environment in which the program will execute provide the exact semantics that the program assumes?**

Everything in this repository exists to answer that question for one narrow domain at a time. In v0.1 the domain is virtual-memory compatibility for C/C++ systems.

The question is deliberately not "will this program crash?" and not "is this code correct?". It is a question about a *pair*: a program's assumption and an environment's capability. Neither half is a defect on its own. The defect is the mismatch, and the mismatch is only decidable when both halves are written down explicitly.

---

## 2. The error family: Silent Contract Degradation

RuntimeSkeptic treats one error family as first class:

```text
A lower layer cannot provide the requested guarantee,
performs a weaker operation,
and continues as if the original guarantee had been satisfied.
```

The critical event is not the operating system's refusal. Operating systems refuse things constantly and say so precisely. The critical event is the **loss, weakening, reinterpretation, or laundering of semantics between layers** — the moment a layer converts "I could not do what you asked" into "done".

Instances of the family, across domains:

| Requested guarantee | Degraded to | Upper layer still assumes |
| --- | --- | --- |
| exact virtual-memory mapping at address A | mapping placed elsewhere | returned address == A |
| durable write | page-cache write | data survives power loss |
| atomic multi-byte update | partial update | readers never see a torn value |
| serializable isolation | a weaker isolation level | no write skew |
| hardware-backed key operation | software fallback | key never left the secure element |
| permanently impossible operation | a retryable error code | retrying will eventually succeed |
| simultaneous write+execute mapping | write-only mapping | the code page can be executed |

v0.1 models only the virtual-memory rows. The others are named here because they define the *shape* of the family, and because the evidence model was designed to survive their addition (ROADMAP Phase 8).

---

## 3. The failure chain

The ROADMAP states the chain abstractly (section 2). Restated with the layer names that actually exist in the code (`rs::Layer`, `src/core/evidence.cpp`):

```text
 application            requires semantic guarantee X
        |
        v
 compatibility_layer    approximates X with behavior Y
        |
        v
 operating_system       permits Y, cannot provide X
        |
        v
 compatibility_layer    reports success
        |
        v
 application            assumes X was satisfied
        |
        v
 application            crashes, corrupts state, retries forever,
                        or produces a wrong result
```

Two properties of this chain make it hard to debug by conventional means:

1. **The reported error is absent.** No layer returns a failure code. There is nothing for a log analyzer, a crash reporter, or an error-path review to find.
2. **The observable symptom is far from the cause.** The assertion that fires, the pointer that dereferences garbage, or the write that never reaches disk happens long after and often in a different subsystem from the call that degraded.

This is why RuntimeSkeptic's unit of output is not an error but a `Finding` (`include/runtimeskeptic/vm/finding.hpp`) carrying five separately named fields: `required`, `host_capability`, `adapter_capability`, `modeled_fallback`, `failure_sink`. Those five fields *are* the chain. A finding that cannot fill them is not a finding.

---

## 4. The motivating incident

Taken from ROADMAP section 3, and recorded as corpus entry `RSC-0001`.

An emulator requests a direct-memory mapping at a fixed guest address:

```c
void* result = mmap(reinterpret_cast<void*>(0x1000000000), size,
                    PROT_READ | PROT_WRITE, flags, -1, 0);

if (result == MAP_FAILED)
    return error;          // the only failure the caller handles

assert(result == requested);   // the postcondition nobody declared
```

The chain:

| Layer | Fact |
| --- | --- |
| `application` | requires `returned_address == requested_address` (guest/host address identity) |
| `compatibility_layer` | has no guest-to-host address translation, so identity is load-bearing |
| `operating_system` | cannot grant the requested virtual-address range |
| `compatibility_layer` | permits the mapping to land elsewhere and reports success |
| `application` | evaluates the assertion, fails, terminates |

RuntimeSkeptic's conclusion, produced without rendering a frame and potentially before the application finishes starting:

```text
PROVEN ENVIRONMENT CONTRADICTION
No execution satisfying all constraints exists on the selected host profile.
```

In the implemented vocabulary this is a `Finding` with `id = RS-VM-0001`, `confidence = PROVEN`, `support_impact = UNSUPPORTED`, emitted by `Analysis::rule_range_availability()` in `src/vm/analyzer.cpp`.

Note on identifiers: the ROADMAP's example uses the illustrative ID `RS-VM-0041` and the confidence label `PROVEN_ENVIRONMENT_CONTRADICTION`. The implemented registry starts at `RS-VM-0001` and the implemented confidence label is `PROVEN`. See `docs/findings/registry.md` and `docs/evidence_model.md`.

---

## 5. What this is not

RuntimeSkeptic is frequently mistaken for three other categories of tool. The distinctions are not marketing positioning; they determine what evidence the tool is allowed to collect and what claims it is allowed to make.

### 5.1 Not a sanitizer

AddressSanitizer, ThreadSanitizer, MemorySanitizer and UndefinedBehaviorSanitizer detect **violations of the language's own rules** in a run that actually happened. They answer: *did this execution do something the C++ abstract machine forbids?*

RuntimeSkeptic asks a different question about code that may be entirely well-defined. In the motivating incident there is no out-of-bounds access, no use-after-free, no data race, and no undefined behavior. `mmap` returned a valid pointer to a valid mapping. ASan sees nothing wrong because nothing is wrong *in the language*. What is wrong is a mismatch between an application-level postcondition and a platform capability — a level at which sanitizers, by construction, have no model.

A second difference: sanitizers require the failing execution to occur. RuntimeSkeptic's Phase 3 mode compares two documents (a requirement and an environment profile) and needs no execution at all.

### 5.2 Not a crash reporter or debugger

A crash reporter tells you where the process died. In this error family, the place the process died is the least informative point in the chain — it is the assertion, several layers and possibly seconds or minutes downstream of the degradation. A backtrace from the assertion contains no trace of the `mmap` call that returned the wrong address, and no representation at all of the fact that the address was *permitted* to differ.

RuntimeSkeptic's output is deliberately shaped the other way round: it starts from the requirement, names the layer that could not satisfy it, and states the failure sink last.

### 5.3 Not an observability or tracing platform

Metrics, structured logs and distributed traces record *what happened*. This error family is characterized by the absence of any anomalous record: every call returned success, every latency was normal, every counter was green.

More importantly, an observability platform's central claim is coverage — capture everything, ask questions later. RuntimeSkeptic's central claim is the opposite: refuse to answer questions for which no fact was established. `EvidenceClass::Unknown` and the `UNKNOWN` verdict exist precisely so the tool can decline. ROADMAP Risk 7 names "becoming a generic observability tool" as a project-ending failure mode; the mitigation is that traces are consumed only when they contribute to contract analysis, never for their own sake.

### 5.4 Summary

| Tool class | Question answered | Needs the failure to occur? | Models platform contracts? |
| --- | --- | --- | --- |
| Sanitizers (ASan/TSan/UBSan) | did this run violate language rules? | yes | no |
| Valgrind / DynamoRIO | what did this run do at instruction level? | yes | no |
| Crash reporters | where did the process die? | yes | no |
| Observability platforms | what happened, aggregated? | yes | no |
| Symbolic execution (KLEE, angr) | which inputs reach state S? | no | partially, as syscall models |
| **RuntimeSkeptic** | **can this environment satisfy this assumption?** | **no** | **yes, as measured or specified facts** |

The full "we will not do X because Y; use Z instead" list is in `docs/non_goals.md`.

---

## 6. Why the evidence model is part of the problem statement

A tool that predicts failures without executing the failure is only useful if its claims are calibrated. A single false `PROVEN` destroys the value of every other `PROVEN`.

The problem therefore has a second half, equal in weight to the first:

> **How do we know what we claim to know, and how strong is a conclusion built from facts of mixed strength?**

RuntimeSkeptic's answer is two separate, non-interchangeable ladders — `EvidenceClass` for how a single fact is known, `Confidence` for how strong a derived finding is — plus a mechanical ceiling rule that prevents a finding from outrunning its weakest supporting fact. This is implemented in `confidence_ceiling()` / `clamp_confidence()` in `src/core/evidence.cpp` and applied at exactly one place in the analyzer (`Analysis::emit()`), so no rule can opt out of it.

Full treatment: `docs/evidence_model.md`.

Two consequences worth stating in the problem statement itself:

- **"Not observed" is never "impossible".** A range the probe never tested does not become unavailable; it becomes `UNKNOWN`. `EnvironmentProfile::query_range()` returns `UNKNOWN` with the reason *"no probe observation covers this range; absence of observation is not evidence of availability"*.
- **`UNKNOWN` outranks `CONDITIONALLY_SUPPORTED`** in verdict aggregation (`combine()`). Calling a request "conditional" when a relevant fact was never established would falsely imply that the conditions are known.

---

## 7. Scope of v0.1

### 7.1 In scope

**Domain.** Virtual-memory compatibility only. One domain, per ROADMAP Risk 1.

**Inputs.** Two documents:

| Document | Schema | Written by |
| --- | --- | --- |
| Environment profile | `runtime-skeptic.environment-profile.v1` | `rs-env-probe` (Phase 1; Linux and macOS implemented, Windows a stub) or hand-authored fixture |
| Application requirement | `runtime-skeptic.application-requirements.v1` | by hand in Phase 3; by CodeSkeptic in Phase 5 |

**Output.** A compatibility result (`runtime-skeptic.compatibility-result.v1`) with an overall verdict in `{SUPPORTED, CONDITIONALLY_SUPPORTED, UNKNOWN, UNSUPPORTED}` and a list of findings, each with an evidence chain, remediation classes, and provably useless "rejected fixes".

**Operations modeled.** `virtual_memory_map`, `virtual_memory_protect`, `virtual_memory_reserve`, `virtual_memory_commit` (`rs::vm::OperationKind`). Any other operation causes the analyzer to record an `analyzer_limitation` and return `UNKNOWN` rather than guess.

**Properties checked.** Exact-address availability, address-space bounds, alignment against allocation granularity, size granularity, page size, hinted-mapping relocation, guest/host identity without translation, write-xor-execute, anonymous executable mappings, JIT entitlement, reserve/commit model, pointer truncation, self-contradictory fallback contracts, and retry loops around structurally impossible operations. That is 15 rules and 17 reachable finding IDs; see `docs/findings/registry.md`.

**Platforms named as targets.** Windows x64, Linux x86-64, macOS on Apple Silicon, and x86-64 processes under Rosetta where measurable. Linux and macOS - natively and as a translated x86-64 process - are now MEASURED; see `profiles/measured/`. <!-- checked: 2026-07-25 --> Windows is still only *modeled*, because its probe is a stub that reports every fact unknown rather than guessing.

### 7.2 Explicitly out of scope for v0.1

| Excluded | Belongs to |
| --- | --- |
| Runtime wrappers, semantic event traces, trace replay | Phase 4 |
| Automatic assumption extraction from C/C++ source | Phase 5 (via CodeSkeptic) |
| Counterfactual exploration of alternative syscall outcomes | Phase 6 |
| Temporal / lifecycle contracts and state machines | Phase 7 |
| Filesystem, loader, time, network, threading, GPU domains | Phase 8 |
| Learned invariants and statistical prediction | Phase 9 |

### 7.3 Known gaps inside the v0.1 scope

Stated plainly rather than left to be discovered:

- **`RS-VM-0018` (file mapping beyond end of file) is registered but unreachable.** The profile carries a `file_map_beyond_eof` fact and the registry defines the ID, but no rule in `src/vm/analyzer.cpp` emits it. Beyond-EOF behavior is a temporal property (map succeeds, access faults later), which is why it fits Phase 7 more naturally than Phase 3.
- **ROADMAP section 6 error classes without any finding ID:** mapping overlap, shared-memory semantic mismatch, virtual-address fragmentation risk, mapping lifetime inconsistencies. Fragmentation is inherently `PREDICTIVE` and belongs to Phase 9; lifetime and overlap belong to Phase 7.
- **The incident corpus is cited but not fully reproduced.** Phase 0 demanded at least 30 classified real incidents with at least 10 in the virtual-memory category; both counts are now met (the live figures are recomputed from the files by `tools/guards/check_corpus.py` and reported in `corpus/runtime_failures/README.md`, not restated here — a count kept in two places is how a corpus comes to disagree with itself). What matters more than the count is reproduction: a cited incident proves the contradiction is real, not that this analyzer diagnoses it. `corpus/runtime_failures/README.md` tracks that remaining gap and which of the earliest entries are still `provenance: pattern_reconstruction` drafts that count toward nothing by design.

---

## 8. What a good outcome looks like

The first success is not a universal runtime intelligence system. It is:

> A virtual-memory analyzer that can explain, before or during execution, why a required memory semantic cannot be satisfied on a selected host.

Concretely, for v0.1 (ROADMAP Phase 3 exit criteria):

- the motivating exact-mapping incident is diagnosed correctly from a requirement document and a profile;
- no unsupported platform fact is described as guaranteed;
- findings are byte-reproducible from the same requirement and profile (`docs/architecture/determinism.md`);
- every finding contains an evidence chain;
- the analysis runs in CI without launching the target application.

---

## 9. Related documents

| Document | Contents |
| --- | --- |
| `docs/failure_taxonomy.md` | the ten failure categories and how to tell them from ordinary memory bugs |
| `docs/evidence_model.md` | evidence classes, confidence levels, the ceiling rule, verdict aggregation |
| `docs/non_goals.md` | what this tool refuses to do, and which tool to use instead |
| `docs/findings/registry.md` | all 18 finding IDs and the ID stability policy |
| `docs/architecture/determinism.md` | canonical serialization, hashing, `profile_id` |
| `corpus/runtime_failures/README.md` | incident corpus structure and Phase 0 exit criteria |

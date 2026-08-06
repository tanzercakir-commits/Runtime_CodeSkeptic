# Non-Goals

What RuntimeSkeptic will not do, why, and which existing tool to use instead.

**Status:** ROADMAP Phase 0, expanding ROADMAP section 7. **Normative.** These are commitments, not a description of current limitations. Items that are merely *not yet implemented* live in `docs/problem_statement.md` section 7.3 and are labelled with the phase that will implement them; nothing on this page is scheduled for a later phase.

---

## How to read this page

Every entry has the same shape:

> **We will not do X, because Y. If you need X, use Z.**

A non-goal is a boundary, not an apology. ROADMAP section 7 closes with the reason the boundary exists at all:

> A narrow, evidence-driven tool with low false-positive rates is more valuable than a broad but unreliable "AI runtime analyzer."

Naming a specific alternative is part of the commitment. A non-goal without an alternative is an unmet need dressed up as a design decision.

---

## 1. We will not predict every possible runtime crash

**Because** crash prediction in general is undecidable, and a tool that attempts it produces findings whose base rate of correctness is unknown. RuntimeSkeptic's value proposition is the *calibration* of its claims (`docs/evidence_model.md`): a `PROVEN` finding is worth acting on precisely because the tool refuses to emit one without a `specified_guarantee` or `measured_capability` behind every step. Broadening the scope dilutes that, and the dilution is not visible to the reader.

What the tool does instead: answers one question — *can this environment satisfy this stated assumption?* — for one domain at a time, and returns `UNKNOWN` for everything else.

**If you need broad crash triage, use:** your platform's crash reporter for post-mortem triage, plus AddressSanitizer and UndefinedBehaviorSanitizer in CI to catch the language-level causes. These cover a large fraction of ordinary crashes and cover them well.

---

## 2. We will not replace AddressSanitizer, ThreadSanitizer or the other sanitizers

**Because** sanitizers answer a categorically different question. They detect violations of the C++ abstract machine's rules in an execution that actually happened: out-of-bounds accesses, use-after-free, uninitialized reads, data races, signed overflow, strict-aliasing violations. RuntimeSkeptic analyses code that is often entirely well-defined and still wrong on this host. In the motivating incident (`corpus/runtime_failures/RSC-0001-*.md`) `mmap` returns a valid pointer to a valid mapping and every access through it is in bounds; there is nothing for a sanitizer to find.

The relationship is complementary, not competitive, and the split is clean:

| Question | Tool |
| --- | --- |
| Did this run violate the language's rules? | AddressSanitizer, MemorySanitizer, UndefinedBehaviorSanitizer |
| Did this run have a data race or a lock-order inversion? | ThreadSanitizer |
| Is this program's assumption satisfiable on this host? | RuntimeSkeptic |

**If you need memory-error detection, use:** **AddressSanitizer** (`-fsanitize=address`) for spatial and temporal memory errors, **MemorySanitizer** for uninitialized reads, **UndefinedBehaviorSanitizer** for the rest, and **ThreadSanitizer** for races and lock-order inversions. Run them; RuntimeSkeptic assumes you have.

A practical warning: sanitizers and RuntimeSkeptic's virtual-memory domain interact. ASan's shadow memory reserves large, fixed regions of the address space, so a program that passes a fixed-address mapping test under ASan may fail without it, or the reverse. If you are analyzing fixed-address mappings, produce your environment profile from a process configured the same way as the process you care about — that is what `platform.process_arch` and `translation_mode` exist to record.

---

## 3. We will not perform dynamic binary instrumentation or full-program shadow execution

**Because** it is a different engineering discipline with different costs (10x-100x slowdowns are normal), and because the failures in scope are usually invisible at instruction granularity. A relocated `mmap` is a correct sequence of instructions; watching every load and store cannot tell you that the *address* was semantically wrong, because nothing in the instruction stream encodes the caller's postcondition.

RuntimeSkeptic's Phase 4 monitoring is deliberately much narrower: wrappers around a small set of virtual-memory APIs that record semantic inputs and outputs, preserve native error codes, and emit deterministic events. ROADMAP section 10.4 requires them to avoid recursion and avoid allocation inside hooks, which rules out a general instrumentation framework by construction.

**If you need instruction-level observation, use:** **Valgrind** (Memcheck for memory errors, Massif for heap profiling, Cachegrind/Callgrind for cache and call analysis), **DynamoRIO** or **Intel Pin** for custom dynamic instrumentation.

---

## 4. We will not do record-and-replay of whole program executions

**Because** deterministic whole-process replay requires capturing every source of nondeterminism — scheduling, signals, syscall results, RDTSC, address-space layout — and doing so correctly is a research programme on its own. RuntimeSkeptic replays a *semantic event trace*: a bounded sequence of domain-specific events (ROADMAP Phase 4), not a process.

The distinction matters for what each can prove. A whole-execution replay reproduces *the run that happened*. RuntimeSkeptic's Phase 6 counterfactual explorer asks *what other runs were legal* — which of the platform's permitted outcomes the program does not survive. That is the opposite direction, and no replay tool provides it, because the run being replayed is by definition the one that occurred.

**If you need to replay a specific failing execution, use:** **rr** on Linux (`rr record` / `rr replay`, with reverse execution in GDB), or your platform's time-travel debugging support. Record the failure with rr, then use RuntimeSkeptic to ask whether the assumption it revealed is satisfiable at all.

---

## 5. We will not become a profiler or a performance tool

**Because** performance and semantic compatibility need different data at different rates, and mixing them corrupts both: profilers sample, and a sampled view of syscall outcomes cannot support a `PROVEN` claim. RuntimeSkeptic records complete semantic events for a tiny number of operations; a profiler records incomplete samples for all of them.

There is exactly one performance number RuntimeSkeptic owns: the overhead of its own wrappers, which Phase 4 requires to be measured, benchmarked and documented per release.

**If you need performance analysis, use:** **perf** (`perf record`, `perf stat`, `perf mem`) on Linux, Instruments on macOS, ETW/Windows Performance Analyzer on Windows, or a continuous profiler for production.

---

## 6. We will not build a kernel tracing or eBPF-based observability layer

**Because** it would answer "what is the system doing?" instead of "is this assumption satisfiable?", and because the operational cost of a production-grade tracing layer would consume the project. ROADMAP Risk 7 names becoming a generic observability tool as a project-ending failure mode; the mitigation is that traces are consumed only when they contribute to contract analysis.

There is a narrower reason too. Kernel tracing produces `observed_invariant` evidence at best: it tells you what happened on the traced runs, never what is guaranteed. `observed_invariant` ceilings a finding at `OBSERVED_INVARIANT` and can never reach `PROVEN` (`docs/evidence_model.md`). A large investment in tracing infrastructure would buy evidence that is structurally incapable of producing the tool's strongest output.

**If you need kernel-level visibility, use:** **bpftrace** and **bcc** for ad-hoc eBPF tracing, **ftrace**/**perf trace** for syscall-level views, **strace**/**dtruss**/ETW for per-process syscall logs, or **SystemTap**. RuntimeSkeptic can consume such data as a trace input in Phase 4 and later; it will not produce it.

---

## 7. We will not build a general-purpose symbolic execution engine

**Because** ROADMAP section 10.6 requires the first implementation to use bounded, explainable reasoning, and ROADMAP Risk 8 identifies "building a general symbolic executor" as a specific way this project fails. Path explosion, constraint-solver timeouts and unexplainable counterexamples are exactly the properties a tool built on calibrated confidence cannot absorb: an analysis that times out has to report something, and "timed out" is not one of the confidence levels.

What v0.1 does instead is visible in `src/vm/analyzer.cpp`: fifteen named, total functions over two documents. No search, no solver, no path exploration. Every finding is traceable to the rule that produced it.

Phase 6 adds *bounded* counterfactual exploration — enumerating platform-legal syscall outcomes within an explicit bound — with three constraints that keep it from drifting into symbolic execution: the bounds are stated in the output, every path is explainable, and a bounded absence of counterexamples is never labelled as proof of safety. Evidence from it is `bounded_counterexample`, which ceilings at `COUNTEREXAMPLE`, never `PROVEN`. If a solver is needed, an established one is reused rather than written.

**If you need symbolic execution, use:** **KLEE** for LLVM bitcode, **angr** for binaries, **SymCC**/**SymQEMU** for compiler-based concolic execution, or **CBMC** for bounded model checking of C. If one of these produces a concrete input that reaches a memory-mapping call, RuntimeSkeptic can tell you whether the resulting request is satisfiable on your host.

---

## 8. We will not do runtime patching, hooking or interception of arbitrary functions

**Because** ROADMAP section 18 requires wrappers to avoid recursion, preserve `errno` and native error state, avoid unsafe allocation inside hooks, and be signal-aware — and general-purpose hooking frameworks satisfy none of those by default. A monitor that perturbs the behavior it observes cannot serve as evidence, and the Phase 4 exit criterion is explicit: *monitoring does not alter error codes*.

The permitted instrumentation strategies are, in order of preference: explicit wrapper calls, compile-time macro substitution, source rewriting, and linker interposition where supported. The first release prefers explicit wrappers precisely because they are the easiest to reason about and test.

**If you need arbitrary function interception, use:** **Frida** for dynamic instrumentation and hooking across platforms, **LD_PRELOAD**/`DYLD_INSERT_LIBRARIES` for symbol interposition, or Microsoft Detours on Windows. If you use Frida to discover which call degrades, encode that call as a requirement document and let RuntimeSkeptic decide whether it is satisfiable.

---

## 9. We will not infer kernel behavior from documentation alone

**Because** documentation describes intent and lags implementation, configuration and security policy. Two hosts running the same documented kernel version differ in `vm.mmap_min_addr`, in hardening patches, in mandatory access control policy, in address-space layout, and in whether a translated process mode is in play.

The mechanism: `specified_guarantee` and `measured_capability` are *separate* evidence classes even though they share the same confidence ceiling. They rank equally as evidence and remain distinguishable in the report, so a reader can always see whether a claim came from a document or from their machine. Where the two disagree, ROADMAP section 16 requires a differential test comparing platform documentation against measured behavior, and the measurement wins for the host it was taken on.

**If you need authoritative platform behavior, use:** `rs-env-probe` on the target host once Phase 1 exists. Until then, use a hand-authored profile and read the `hand_authored_fixture` warning that the analyzer attaches to every result derived from one.

---

## 10. We will not model arbitrary device drivers

**Because** driver behavior is vendor-specific, version-specific, frequently undocumented, and often not deterministically probeable without risking the machine. ROADMAP section 18 forbids destructive operations and requires strict range and size limits; a probe that characterizes an arbitrary driver's ioctl surface cannot honor that.

GPU and accelerator memory appears as ROADMAP Phase 8 Candidate Domain F, and would enter through the same gate as every other domain (Gate D: ten real incidents, a bounded operation model, measurable or specified behavior, actionable output) — as a *domain* with modelled operations, not as arbitrary driver emulation.

**If you need driver-level analysis, use:** the vendor's validation layers and debuggers — Vulkan validation layers, RenderDoc, Nsight, ROCm tooling — or the kernel's own driver-tracing facilities.

---

## 11. We will not support all operating-system versions

**Because** an environment profile is a measurement of one configuration, and claiming coverage of untested versions would violate the model's core rule. The honest mechanism already exists: a profile that was never taken on your host produces `unknown` facts, which produce `HYPOTHESIS` findings and an `UNKNOWN` verdict, together with an instruction to run the probe.

Initial platform families are Windows x64, Linux x86-64, macOS on Apple Silicon, and x86-64 processes under translation where measurable. Profiles carry version metadata and comparison tooling (`rs-profile diff`) so behavior drift across releases is detectable rather than assumed away.

**If you need a platform we do not cover:** contribute a profile. That is the intended extension path, and ROADMAP section 15 counts independently contributed profiles as a product metric.

---

## 12. We will not emulate an operating system

**Because** the project's value comes from comparing assumptions against *real* platform behavior. An emulated OS would answer questions about the emulator, and every answer would inherit the emulator's own fidelity gap — a gap with no place in the evidence model, since there is no evidence class for "true in our model of your kernel".

RuntimeSkeptic analyses emulators. It does not become one.

**If you need to run software for another OS or architecture, use:** QEMU, a hypervisor, or your platform's compatibility layer. Then profile *that* environment: `translation_mode` is a first-class field in `PlatformInfo` precisely so a translated process can be characterized as the distinct environment it is.

---

## 13. We will not emit probabilistic or AI-generated explanations as evidence

**Because** ROADMAP section 8.7 draws the line: machine learning and language models may cluster incidents, summarize traces, suggest candidate contracts, rank likely causes and generate human-readable prose. They must not replace deterministic evidence, solver output, or measured platform capability. The Phase 0 exit criteria include *"no claim depends solely on AI-generated interpretation"*, and Phase 9's include *"no opaque score is emitted without supporting measurements"*.

Mechanically, there is nowhere for such a claim to live. A finding's confidence is computed from the evidence classes in its chain, and every evidence class names a determinate origin: a specification, a probe, a trace set, a static analysis, a bounded exploration, or a stated heuristic. A model's opinion is not one of them, and an `EvidenceStep` with no checkable `source` is a documentation defect.

If a language model is used to *draft* a requirement document or a corpus entry, that provenance is recorded — see the mandatory `provenance:` field in `corpus/runtime_failures/README.md`, and note that every current corpus entry is marked `pattern_reconstruction` and counts toward no exit criterion.

**If you want AI assistance, use it for:** clustering an incident backlog, drafting candidate contracts for human review, and turning a finding into readable prose. Then have a human check the contract, and let the deterministic engine decide the verdict.

---

## 14. We will not claim proof where only a heuristic or a historical pattern exists

**Because** this is the failure mode that would make the tool worthless, and it is the one most easily committed by accident. It is therefore enforced in code rather than in review: `confidence_ceiling()` maps `heuristic_risk` to `PREDICTIVE` and `observed_invariant` to `OBSERVED_INVARIANT`, `clamp_confidence()` only ever weakens, `EvidenceChain::weakest_class()` returns `unknown` for an empty chain, and `Analysis::emit()` is the sole path by which a finding reaches the result.

A rule can ask for `PROVEN` all it likes. If it rests on a pattern, it gets `PREDICTIVE`.

**If you want heuristic findings, they are available** — labelled `PREDICTIVE` or `HYPOTHESIS`, and `HYPOTHESIS` findings must not fail CI by default (ROADMAP section 11). The tool does not hide weak signals; it refuses to mislabel them.

---

## 15. We will not treat observed behavior as a formal guarantee

**Because** "it worked every time we looked" and "the platform promises it" are different facts with different failure modes, and the entire category *platform-observed behavior treated as guaranteed behavior* (`docs/failure_taxonomy.md` section 10) exists because programs make this mistake. A tool that made the same mistake would be worse than useless: it would launder the program's unfounded assumption into a tool-issued endorsement.

Enforcement, in three places: `observed_invariant` ceilings at `OBSERVED_INVARIANT` and can never reach `PROVEN`; `query_range()` returns `UNKNOWN` rather than `SUPPORTED` for any range no observation covers; and `unavailable_ranges` entries require a real evidence class, so absence of observation cannot masquerade as a measurement.

**If you need to know whether something is guaranteed:** read the platform's specification and record it as `specified_guarantee` with a citation in the `source` field. That is a different fact from `measured_capability`, and both are kept.

---

## 16. We will not solve distributed-system correctness

**Because** it requires modelling partial failure, message reordering, clock skew and consensus — a scope that dwarfs the entire project, and one where the evidence model's dependence on a single characterizable environment breaks down. There is no such thing as "the environment profile" of a cluster.

Time and network appear as ROADMAP Phase 8 Candidate Domains C and D, scoped to *local* semantics — clock monotonicity, timer resolution, timeout overflow, partial send/receive, message-boundary assumptions on stream sockets — and each would enter through Gate D.

**If you need distributed-system verification, use:** **TLA+**/**PlusCal** or **Alloy** for specification and model checking, **Jepsen** for empirical consistency testing, or deterministic simulation testing of your own system.

---

## 17. We will not be a policy or compliance scanner

**Because** RuntimeSkeptic reports contradictions between assumptions and capabilities, not deviations from a house style. Its output is a semantic contradiction with an evidence chain, not a rule violation.

There is a sharper version of this boundary, and it is a safety requirement (ROADMAP section 18): the tool must avoid interpreting security restrictions as defects in the operating system, and must distinguish unsupported behavior from security-policy enforcement. This is why `RS-VM-0009` emits *"disable the platform's W^X enforcement"* as a **rejected fix**, with the reason that a security control is not a defect and disabling it is not a portable remediation. A compliance scanner's instinct — flag the deviation, recommend the override — is the exact behavior forbidden here.

**If you need policy enforcement, use:** **clang-tidy** and **cppcheck** for code-level policy, **RuleDSL** (the sibling project) for deterministic decision policies, or your organization's compliance tooling. RuleDSL is explicitly intended for severity policies and environment compatibility policies layered *on top of* RuntimeSkeptic findings; that is the supported integration.

---

## 18. We will not duplicate CodeSkeptic

**Because** the two projects answer adjacent questions and the split is the point:

```text
CodeSkeptic:     What does the program assume?
RuntimeSkeptic:  Can the selected environment satisfy that assumption?
```

Static C/C++ semantic analysis, dataflow, contract extraction, error-path analysis, fatal-sink identification and semantic API comparison stay in CodeSkeptic. RuntimeSkeptic consumes its summaries as versioned artifacts, not as a source dependency, and the repositories stay separate.

The owner made this a permanent v0.2 product boundary on 2026-08-06 in
[`docs/decisions/0001-standalone-product-boundary.md`](decisions/0001-standalone-product-boundary.md).
RuntimeSkeptic does not copy or vendor CodeSkeptic, add it as a submodule,
fetch or link it in the build, import its headers, or invoke its CLI. A future
producer may supply the same public, versioned requirement artifact as any
other independent producer, but it is not a RuntimeSkeptic dependency or core
integration. Changing that boundary requires a superseding owner decision and
plan; the old dated-exception escape hatch is closed.

The seam is already visible in the code: `Requirement::assumption_evidence` records whether a requirement was hand-authored (`specified_guarantee` — a human declaring intent) or statically inferred (`statically_inferred` — an inference that can be wrong). The second ceilings any finding built on it at `COUNTEREXAMPLE`, which is the honest consequence of not having asked a human.

**If you need static analysis of C/C++ semantics, use:** CodeSkeptic, or clang-tidy, clang static analyzer, cppcheck, PVS-Studio or Coverity. RuntimeSkeptic accepts a hand-written requirement document from any of them, or from you.

---

## Quick reference

| If you need... | Use |
| --- | --- |
| Memory-error detection | AddressSanitizer, MemorySanitizer, Valgrind Memcheck |
| Race and lock-order detection | ThreadSanitizer, Helgrind |
| Instruction-level dynamic analysis | Valgrind, DynamoRIO, Intel Pin |
| Whole-execution record and replay | rr, time-travel debugging |
| Performance profiling | perf, Instruments, Windows Performance Analyzer |
| Kernel and syscall tracing | bpftrace, bcc, ftrace, strace, ETW |
| Symbolic or concolic execution | KLEE, angr, SymCC, CBMC |
| Arbitrary function hooking | Frida, LD_PRELOAD, Detours |
| Distributed-system verification | TLA+, Alloy, Jepsen |
| Static C/C++ semantic analysis | CodeSkeptic, clang-tidy, clang static analyzer |
| Deterministic decision policy | RuleDSL |
| **Deciding whether a stated assumption is satisfiable on a host** | **RuntimeSkeptic** |

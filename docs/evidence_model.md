# Evidence Model

Two separate ladders — how a fact is known, and how strong a finding is — connected by one mechanical rule: a finding can never be stronger than its weakest supporting fact.

**Status:** ROADMAP Phase 0 (terminology), enforced from Phase 2 onward. **Implemented.** The enums, the ceiling function and the verdict aggregation live in `include/runtimeskeptic/core/evidence.hpp` and `src/core/evidence.cpp`; the single enforcement point is `Analysis::emit()` in `src/vm/analyzer.cpp`. The `Fact<T>` wrapper that makes "unknown" unavoidable at call sites is in `include/runtimeskeptic/core/fact.hpp`.

---

## 1. Why two ladders

ROADMAP section 8.2 requires every fact to carry an evidence class. ROADMAP section 8.3 requires that proof levels never be mixed. Those are two different requirements, and satisfying them with one vocabulary is impossible, because they measure different things:

| Ladder | C++ type | Attaches to | Answers |
| --- | --- | --- | --- |
| **Evidence class** | `rs::EvidenceClass` | a single fact | *How do we know this?* |
| **Confidence** | `rs::Confidence` | a whole finding | *How strong is this conclusion?* |

A finding is a conclusion drawn from several facts, usually contributed by different layers. Its strength is a function of *all* of them. Collapsing the two ladders would either let a strong conclusion be drawn from a weak fact, or forbid a proof assembled from two documented guarantees. Both are wrong.

The two enums are declared in the same header, deliberately adjacent, with the comment that they "must not be conflated".

---

## 2. Ladder 1 — Evidence classes

How we know a single fact. Ordered strongest to weakest; the numeric order is load-bearing, because `weakest()` compares the underlying integers.

| # | Name (wire form) | Meaning | Produced by |
| --- | --- | --- | --- |
| 0 | `specified_guarantee` | a documented, binding platform contract | curated API/OS specifications; analyzer arithmetic that follows from a definition |
| 1 | `measured_capability` | `rs-env-probe` observed it on this host | Phase 1 probe (**not implemented**) |
| 2 | `observed_invariant` | held across a recorded trace set | Phase 4 runtime wrapper; Phase 9 invariant miner |
| 3 | `statically_inferred` | derived from source analysis | Phase 5 CodeSkeptic integration |
| 4 | `bounded_counterexample` | found within an explicit exploration bound | Phase 6 counterfactual explorer; today, the analyzer's outcome enumeration |
| 5 | `heuristic_risk` | pattern match or trend, no proof | structural assessments; Phase 9 |
| 6 | `unknown` | not probed, not specified | absence — the default |

Two properties are enforced rather than documented:

**`unknown` is the default, not a fallback.** `Fact<T>` default-constructs to unknown, and `Fact<T>::known()` *rejects* a value supplied with evidence class `unknown`, storing the note `"rejected: value supplied with unknown evidence class"`. A caller cannot smuggle data in under an unknown label.

```cpp
// include/runtimeskeptic/core/fact.hpp
static Fact known(T value, EvidenceClass evidence, std::string source,
                  std::string note = {}) {
    if (evidence == EvidenceClass::Unknown) {
        f.note_ = "rejected: value supplied with unknown evidence class";
        return f;   // still unknown, still carries no value
    }
    ...
}
```

**Deserialization requires an explicit evidence class.** `fact_from_json()` sets a hard schema error — not a missing value — when a fact node lacks an `"evidence"` string or carries an unrecognized one. A profile author cannot omit the question.

### A note on ordering

The ROADMAP (section 8.2) lists the classes as: specified guarantee, measured capability, statically inferred assumption, observed invariant, bounded counterexample, heuristic risk. The implemented order swaps the middle two — `observed_invariant` (2) is stronger than `statically_inferred` (3) — and adds `unknown` as a seventh. The implementation is authoritative. The rationale for the swap: an invariant that held across a recorded trace set is a statement about behavior that actually occurred, whereas a statically inferred assumption is a statement about what source code appears to require, which can be wrong about the program's intent. `unknown` was added because a six-class ladder with no bottom rung forces callers to pick the least-wrong label for "we did not check", which is exactly the overclaiming the model exists to prevent.

---

## 3. Ladder 2 — Confidence levels

How strong a finding is (ROADMAP section 11). Ordered strongest to weakest; again the numeric order is load-bearing.

| # | Label (wire form) | Meaning | CI default |
| --- | --- | --- | --- |
| 0 | `PROVEN` | the constraints are unsatisfiable on this profile | must fail |
| 1 | `COUNTEREXAMPLE` | a platform-legal outcome or event sequence reaches a failure sink | should fail |
| 2 | `OBSERVED_INVARIANT` | an invariant that historically held has changed; evidence, not proof | should warn |
| 3 | `PREDICTIVE` | a trend suggests a likely future failure | optional |
| 4 | `HYPOTHESIS` | a plausible cross-layer explanation with incomplete evidence | must not fail CI |

The ROADMAP's illustrative report (section 10.7) prints `PROVEN_ENVIRONMENT_CONTRADICTION`. The implemented wire label is `PROVEN`. The longer phrase survives as prose in `Finding::conclusion`, not as an identifier.

---

## 4. The ceiling rule

> A finding can never be stronger than its weakest supporting fact.

Implemented as `confidence_ceiling()` in `src/core/evidence.cpp`.

| Weakest fact in the chain | Highest confidence the finding may claim |
| --- | --- |
| `specified_guarantee` | `PROVEN` |
| `measured_capability` | `PROVEN` |
| `observed_invariant` | `OBSERVED_INVARIANT` |
| `statically_inferred` | `COUNTEREXAMPLE` |
| `bounded_counterexample` | `COUNTEREXAMPLE` |
| `heuristic_risk` | `PREDICTIVE` |
| `unknown` | `HYPOTHESIS` |

### How it is applied

Three functions and one call site:

```cpp
// The weakest (numerically largest) class anywhere in the chain.
EvidenceClass EvidenceChain::weakest_class() const;

// The ceiling implied by that class.
Confidence confidence_ceiling(EvidenceClass weakest_fact);

// Never returns something stronger than the ceiling.
Confidence clamp_confidence(Confidence desired, EvidenceClass weakest_fact);
```

```cpp
// src/vm/analyzer.cpp — the single place where overclaiming is prevented.
void emit(Finding f) {
    f.severity = adjust_severity(f.severity, req_.failure_sink.kind);
    f.confidence = clamp_confidence(f.confidence, f.evidence.weakest_class());
    result_.overall = combine(result_.overall, f.support_impact);
    result_.findings.push_back(std::move(f));
}
```

Every rule in the analyzer states the confidence it *would like* and has no way to bypass `emit()`. A rule that asks for `PROVEN` while adding an `unknown` step to its own chain gets `HYPOTHESIS`, silently and unconditionally.

Two details that are easy to get backwards:

- **The clamp only ever weakens.** `clamp_confidence(desired, weakest)` returns the weaker of `desired` and the ceiling. A rule that asks for `HYPOTHESIS` while every fact is a `specified_guarantee` still gets `HYPOTHESIS`; the ceiling is a maximum, not a target.
- **An empty chain is `unknown`.** `EvidenceChain::weakest_class()` returns `EvidenceClass::Unknown` for an empty chain, so a finding emitted with no evidence at all collapses to `HYPOTHESIS`. Forgetting to record evidence cannot produce a strong claim.

### The mirror rule for severity

Severity is *not* clamped by evidence, because it answers a different question: how bad is this if it is real? It is adjusted by the program's failure sink instead (`adjust_severity()`):

| Failure sink | Effect on severity |
| --- | --- |
| `fatal_assert`, `process_exit` | `critical` |
| `unchecked` | `critical` — silent misbehavior surfaces far from its cause |
| `retry_loop` | at least `high` |
| `error_return` | `critical` is relaxed to `high`; the program can recover |
| `none`, `unknown` | unchanged |

So a `HYPOTHESIS` can be `critical` and a `PROVEN` can be `medium`. The two axes are independent by design, and a report that sorts on one must display the other.

---

## 5. Verdict aggregation

`SupportLevel` is the answer to "can this environment satisfy this requirement?" (ROADMAP Phase 2 exit criteria).

```text
Unsupported  >  Unknown  >  ConditionallySupported  >  Supported
```

`combine(a, b)` returns whichever is higher in that order; the analyzer folds every finding's `support_impact` into `result_.overall`.

| Verdict | Meaning |
| --- | --- |
| `SUPPORTED` | every relevant fact was established and every requirement is satisfiable |
| `CONDITIONALLY_SUPPORTED` | satisfiable, and the conditions are known and stated |
| `UNKNOWN` | at least one relevant fact was never established |
| `UNSUPPORTED` | at least one requirement cannot be satisfied |

### Why `UNKNOWN` outranks `CONDITIONALLY_SUPPORTED`

This is the ordering decision most likely to be questioned, so the reasoning is stated in the header itself and repeated here.

`CONDITIONALLY_SUPPORTED` is a *positive* claim with a *specific* content: "this works, and here are the conditions under which it works." It is actionable — a reader can check the conditions, satisfy them, and proceed. Emitting it asserts that the analyzer knows what the conditions are.

`UNKNOWN` means a relevant platform fact was never established. In that state the analyzer does not know the conditions, because it does not know the fact they would be stated over.

If `CONDITIONALLY_SUPPORTED` outranked `UNKNOWN`, a run in which one rule found a known, stateable condition and another rule found a gap in the profile would report `CONDITIONALLY_SUPPORTED`. The reader would take that as "works with caveats", enumerate the stated caveats, satisfy them, and ship — while the unestablished fact remains unestablished and unmentioned in the verdict. The tool would have converted an admission of ignorance into a qualified endorsement.

The ordering makes the failure mode the other way round: an unestablished fact drags the whole verdict to `UNKNOWN`, and the remedy printed alongside it is *"run `rs-env-probe` on the target host"*. Being told to go measure something is a recoverable annoyance; being told "works with these caveats" when the caveat list is incomplete is not.

The same principle appears at the finding level as `RS-VM-0017` ("availability of the requested range was never established", severity `info`, confidence `HYPOTHESIS`, support impact `UNKNOWN`), and in `AnalysisOptions::report_unknowns`, which controls whether those findings are printed but **not** whether they affect the verdict. Suppressing the noise never suppresses the `UNKNOWN`:

```cpp
// src/vm/analyzer.cpp — rule_range_availability()
if (verdict.level == SupportLevel::Unknown) {
    if (!options_.report_unknowns) {
        result_.overall = combine(result_.overall, SupportLevel::Unknown);
        return;                      // no finding printed, verdict still UNKNOWN
    }
    ...
}
```

### Note on `UNSUPPORTED` outranking `UNKNOWN`

A proven impossibility is more informative than a gap, and adding facts cannot make an impossibility go away. So `UNSUPPORTED` wins even when other facts are missing. The finding that produced it still carries its own evidence chain, so a reader can see which single fact decided the verdict.

---

## 6. "Not observed" is not "impossible"

ROADMAP section 10.1 states the requirement: the probe must never confuse *not observed* with *impossible*. Three mechanisms enforce it.

**1. Unavailability is a positive assertion.** A profile has two range lists, `unavailable_ranges` and `available_ranges`, and both hold `ClassifiedRange`, which carries an `EvidenceClass`. The header states the invariant directly: *"An 'unavailable range' with evidence Unknown is meaningless and must never be produced."* A range the probe never tested appears in **neither** list.

**2. Queries fall through to `UNKNOWN`, never to a default.** `EnvironmentProfile::query_range()` checks hard bounds, then unavailable ranges, then available ranges, and if nothing matched:

```cpp
verdict.level = SupportLevel::Unknown;
verdict.evidence = EvidenceClass::Unknown;
verdict.reason = "no probe observation covers this range; absence of "
                 "observation is not evidence of availability";
```

Note also that step 3 requires an available range to **fully contain** the request. Partial containment yields `UNKNOWN`, because it says nothing about the remainder.

**3. The symmetric error is also blocked.** "Not observed" is not "available" either. There is no path through `query_range()` that returns `SUPPORTED` without a `ClassifiedRange` that covers the request and carries a real evidence class.

The same discipline applies to scalar capabilities. Rules consult `Fact<T>::is_known()` and simply do not fire when the fact is absent — for example `rule_write_execute()` returns early if `write_execute_simultaneous` is unknown, rather than assuming either answer. The cost is silence where a fact is missing; `report_unknowns` exists so that silence can be made visible.

---

## 7. Profile origin is not an evidence class

`ProfileOrigin` (`include/runtimeskeptic/vm/profile.hpp`) records how a whole profile document came into existence:

| Wire form | Meaning |
| --- | --- |
| `measured` | produced by `rs-env-probe` on a real host |
| `hand_authored_fixture` | written by a human to model a platform, e.g. for tests and examples |
| `synthetic` | generated, e.g. by mutating another profile in a mutation test |
| `unknown` | not stated |

The header is explicit: *"This is NOT an evidence class; each individual fact still carries its own."*

### Why they must stay separate

They quantify over different things and cannot substitute for each other in either direction.

**Origin is a property of the document; evidence class is a property of a fact.** A single profile mixes evidence classes freely and legitimately. A `measured` profile can contain `page_size` with `measured_capability` (the probe called `sysconf`), `min_map_address` with `specified_guarantee` (read from a documented policy), and `hinted_mapping_may_relocate` as `unknown` (the probe has no test for it yet). Collapsing origin into evidence class would force one label onto all three.

**Neither implies the other.**

- A `hand_authored_fixture` may legitimately contain `specified_guarantee` facts. If a platform documents its allocation granularity, transcribing that number into a fixture does not weaken it — the guarantee comes from the specification, not from the transcription. Downgrading it because the file was typed by a human would be wrong in the opposite direction: it would understate what is known.
- A `measured` profile may contain `unknown` facts, for every capability the probe did not test. Upgrading them because the file came from a probe would be exactly the overclaim this model exists to prevent.

**They are consumed by different readers.** The evidence class is consumed by the ceiling rule and decides the *confidence label*. The origin is consumed by a human and answers a different question: *"was this verdict about my machine, or about a model of a machine?"* A `PROVEN` finding derived from a hand-authored fixture is correctly labelled `PROVEN` — the reasoning is sound given the stated facts — and the reader still needs to be told that the facts were not measured on their host.

The analyzer says so explicitly rather than adjusting the confidence:

```cpp
// src/vm/analyzer.cpp — Analysis::run()
if (profile_.origin == ProfileOrigin::HandAuthoredFixture) {
    result_.analyzer_limitations.push_back(
        "profile origin is 'hand_authored_fixture': its facts were not "
        "measured on a real host by rs-env-probe");
}
```

`AnalysisResult::profile_origin` is also carried into the result JSON, so the distinction survives into reports and CI output.

---

## 8. Worked examples

Four analyses of the same shape of request, differing only in what the profile establishes. The rule is the same each time; the confidence is not.

### 8.1 `PROVEN` — every fact is measured

**Requirement.** Exact mapping at `0x1000000000`, 16 KiB, guest/host identity required, no translation layer, failure sink `fatal_assert`.

**Profile.** `origin: measured`. `unavailable_ranges` contains `[0x1000000000, 0x7000000000)` with evidence `measured_capability`.

**Evidence chain built by `rule_range_availability()`:**

| Layer | Evidence class | Claim |
| --- | --- | --- |
| `application` | `specified_guarantee` | program requires an exact mapping at `0x1000000000` of 16384 bytes |
| `compatibility_layer` | `specified_guarantee` | no guest-to-host translation layer exists, so the guest address must equal the host address |
| `operating_system` | `measured_capability` | requested range intersects unavailable range `[0x1000000000, 0x7000000000)` |
| `kernel` | `measured_capability` | conflicting range `[0x1000000000, 0x7000000000)` |

*(The application steps carry `specified_guarantee` because the requirement document declares `assumption_evidence: specified_guarantee` — a hand-authored contract is authoritative about the program's own intent.)*

```text
weakest_class()      = measured_capability
confidence_ceiling() = PROVEN
desired              = PROVEN
result               = PROVEN, UNSUPPORTED, critical
```

The `critical` severity comes from the `fatal_assert` sink, not from the confidence.

### 8.2 `COUNTEREXAMPLE` — the platform permits the failure rather than forcing it

**Requirement.** Same address, but `exact_address_required: false` — the program passes a hint. Identity is still required.

**Profile.** `hinted_mapping_may_relocate: true`, evidence `specified_guarantee`.

**Evidence chain built by `rule_hinted_relocation()`:**

| Layer | Evidence class | Claim |
| --- | --- | --- |
| `application` | `specified_guarantee` | program requires the returned address to equal `0x1000000000` |
| `operating_system` | `specified_guarantee` | a hinted mapping is permitted to relocate on this host |
| `analyzer` | `bounded_counterexample` | a platform-legal outcome (relocation) reaches the caller's failure sink |

```text
weakest_class()      = bounded_counterexample
confidence_ceiling() = COUNTEREXAMPLE
desired              = COUNTEREXAMPLE
result               = COUNTEREXAMPLE, UNSUPPORTED, critical
```

Nothing here is unsatisfiable. The mapping may well land on the hint. `PROVEN` would be a false claim, and the ceiling makes it unavailable regardless of what the rule asked for. The rejected fix printed alongside is *"it works on our machines, so ship it"*.

### 8.3 `HYPOTHESIS` — the same request, downgraded because the profile is silent

**Requirement.** Identical to 8.1: exact mapping at `0x1000000000`, identity required, no translation layer, `fatal_assert` sink. Word for word the same document that produced `PROVEN`.

**Profile.** `origin: measured`, but the probe was run with a reduced test set. `min_map_address` and `max_user_address` are unknown, and both `unavailable_ranges` and `available_ranges` are empty — the probe never tested this part of the address space and, correctly, recorded nothing.

`query_range()` falls through all three checks:

```text
level    = Unknown
evidence = Unknown
reason   = "no probe observation covers this range; absence of observation
            is not evidence of availability"
```

**Evidence chain:**

| Layer | Evidence class | Claim |
| --- | --- | --- |
| `application` | `specified_guarantee` | program requires an exact mapping at `[0x1000000000, 0x1000004000)` |
| `operating_system` | `unknown` | no probe observation covers this range |

```text
weakest_class()      = unknown
confidence_ceiling() = HYPOTHESIS
desired              = HYPOTHESIS
result               = HYPOTHESIS, UNKNOWN, info   (RS-VM-0017)
```

The finding id changes too: not `RS-VM-0001` but `RS-VM-0017`, "availability of the requested range was never established", severity `info`, with the conclusion *"The analyzer cannot decide this request. Extend the probe to cover the range, or supply a profile that specifies it."*

**This is the important example.** The program did not change. The requirement did not change. The host may be the very same machine. What changed is what the tool can justify, and the tool reports the difference instead of hiding it. Overall verdict `UNKNOWN`, not `SUPPORTED`.

### 8.4 The ceiling firing against a rule that wanted more

Consider a hypothetical rule that concludes a mapping is impossible while resting one step on a trace-derived fact:

| Layer | Evidence class | Claim |
| --- | --- | --- |
| `application` | `specified_guarantee` | program requires an exact mapping at `0x1000000000` |
| `operating_system` | `observed_invariant` | across 4,000 recorded runs this range was always occupied by the runtime's arena |

The rule asks for `PROVEN`. It does not get it:

```text
weakest_class()      = observed_invariant
confidence_ceiling() = OBSERVED_INVARIANT
desired              = PROVEN
result               = OBSERVED_INVARIANT
```

Four thousand agreeing observations are not a guarantee. The next allocator release can move the arena. `OBSERVED_INVARIANT` is the strongest honest label, and the rule has no way to argue.

### 8.5 Summary of the four

| Example | Weakest fact | Ceiling | Emitted | Finding | Verdict |
| --- | --- | --- | --- | --- | --- |
| 8.1 measured range conflict | `measured_capability` | `PROVEN` | `PROVEN` | RS-VM-0001 | `UNSUPPORTED` |
| 8.2 permitted relocation | `bounded_counterexample` | `COUNTEREXAMPLE` | `COUNTEREXAMPLE` | RS-VM-0007 | `UNSUPPORTED` |
| 8.3 unprobed range | `unknown` | `HYPOTHESIS` | `HYPOTHESIS` | RS-VM-0017 | `UNKNOWN` |
| 8.4 trace-derived conflict | `observed_invariant` | `OBSERVED_INVARIANT` | `OBSERVED_INVARIANT` | — | `UNSUPPORTED` |

---

## 9. Evidence chains and layers

A finding's evidence is an ordered `EvidenceChain` of `EvidenceStep`s. Each step names a `Layer`, so a list of facts becomes a cross-layer narrative and ROADMAP section 8.6's six questions can be answered mechanically.

| Wire form | Typical contributor |
| --- | --- |
| `application` | the requirement document; CodeSkeptic-extracted assumptions |
| `library` | a third-party library's own contract |
| `language_runtime` | allocator, GC, language runtime placement |
| `compatibility_layer` | emulator, translator, adapter |
| `syscall_interface` | availability of a specific primitive, e.g. a non-destructive fixed mapping |
| `operating_system` | policy, documented behavior, probe results |
| `kernel` | address-space layout, conflicting ranges |
| `hardware` | architectural page size, physical address width |
| `analyzer` | facts contributed by RuntimeSkeptic itself: arithmetic, outcome enumeration |

The `analyzer` layer exists so that the tool's own reasoning is auditable in the same format as everything else. When `rule_address_alignment()` concludes `0x1000000001 % 65536 != 0`, that appears as an `analyzer` step with evidence `specified_guarantee` and source `"analyzer: alignment arithmetic"` — it is a guarantee because it follows from arithmetic, and it is labelled as the analyzer's own claim rather than the platform's.

Each step also carries `source` (probe id, document, source location) and an optional `detail`. A step with an empty `source` is a documentation defect: the reader has no way to check the claim.

---

## 10. Rules for anyone adding a rule or a fact

1. Never construct a `Fact<T>` with a value and evidence `unknown`. The constructor rejects it; the rejection is silent, so the fact simply stays unknown.
2. Never write to `unavailable_ranges` from an untested observation. Not observed goes in neither list.
3. Never set `Finding::confidence` outside `emit()`. State the confidence you want; let the ceiling decide.
4. Every `EvidenceStep` needs a real `source`. "the profile" is not a source; a profile name or a probe id is.
5. If a required fact is unknown, return early or emit an `UNKNOWN` finding. Do not assume the permissive answer, and do not assume the restrictive one.
6. Do not downgrade confidence because a profile is a fixture. Say so in `analyzer_limitations` instead; origin and evidence class are separate axes.
7. A rule that lowers the verdict must emit a finding carrying an evidence chain (ROADMAP Phase 2 exit criterion: *every non-supported result must include a machine-readable reason*). The one permitted exception is the `report_unknowns == false` path above, which lowers the verdict to `UNKNOWN` without printing — and `UNKNOWN` is itself the machine-readable reason.

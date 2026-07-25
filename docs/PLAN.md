# Plan and status

The whole ROADMAP, phase by phase, with the honest state of each exit
criterion. **This file is the spine.** It is updated in the same commit as the
work it describes, and `tools/guards/check_plan.py` fails CI when it drifts.

Status markers are load-bearing and mechanically checked:

| Marker | Means |
|---|---|
| `[done]` | the criterion is met AND something that runs proves it |
| `[partial]` | met for some inputs, with the gap named on the same line |
| `[open]` | not met |
| `[blocked]` | not met and cannot be, with the blocker named |
| `[n/a]` | out of scope for this phase by an explicit decision |

A `[done]` with no evidence pointer is a lie the guard rejects. Every `[done]`
must name a test, a tool invocation, or a committed artifact.

---

## Where the project actually is

**Phases 0-3 are the built surface. Phase 4 and beyond are not started.**

```
Phase 0  taxonomy + corpus        PARTIAL   docs complete, corpus 1/30
Phase 1  environment probe        PARTIAL   Linux + macOS x2 measured; Windows is a stub
Phase 2  semantic IR + evaluator  DONE
Phase 3  VM analyzer MVP          PARTIAL   one exit criterion never measured
Phase 4  runtime wrapper          OPEN      not started
Phase 5  CodeSkeptic integration  BLOCKED   owner's instruction: do not touch CodeSkeptic
Phase 6  counterfactual           OPEN
Phase 7  temporal contracts       OPEN
Phase 8  further domains          OPEN      gated on Gate D
Phase 9  learned invariants       OPEN
Phase 10 productization           OPEN
```

---

## Phase 0 — Research boundary and failure taxonomy

### Deliverables

- `[done]` `docs/problem_statement.md` — committed
- `[done]` `docs/failure_taxonomy.md` — committed, ten categories
- `[done]` `docs/evidence_model.md` — committed; enforced by `tests/unit/test_evidence.cpp`
- `[done]` `docs/non_goals.md` — committed, normative
- `[partial]` `corpus/runtime_failures/` — 11 entries, of which 1 is
  `reported_incident` and 2 are `source_citation`; 8 are
  `pattern_reconstruction` and count toward nothing
- `[done]` finding-ID registry — `docs/findings/registry.md`, 25 ids

### Exit criteria

- `[open]` **at least 30 classified real incidents** — 1 counts
  (`RSC-0011`, shadPS4 issue #4157). The corpus README's own accounting says
  0/30; that is now stale by one, and the entry it turns on rests on a
  citation this environment cannot re-fetch.
- `[open]` **at least 10 in the virtual-memory category** — 1 counts
- `[done]` each category has a concrete evidence model —
  `docs/failure_taxonomy.md`, cross-checked by `tools/guards/check_registry.py`
- `[done]` terminology stable enough to publish — `docs/evidence_model.md`
- `[done]` no claim depends solely on AI-generated interpretation — every
  entry in `corpus/runtime_failures/` carries `provenance:`, and
  `pattern_reconstruction` entries are excluded from the count by rule

**The gap that matters:** this is the largest single hole in the project. The
taxonomy is derived from imagination, not from incidents, and until 30 real
ones exist nobody can say whether the ten categories are the right ten.

`docs/scenarios/assessment.md` S5 and S2 name the cheapest way in: mimalloc,
jemalloc and V8 publish their shipped configurations, and "a customer said it
broke on Mac" is a real incident with a real artifact behind it.

---

## Phase 1 — Virtual memory environment probe

### Deliverables

- `[done]` `rs-env-probe` — `tools/rs-env-probe/`, Linux and macOS implemented
- `[done]` environment profile schema — `schemas/environment-profile.v1.json`,
  validated against every committed artifact by
  `tools/guards/validate_schemas.py`
- `[open]` Windows x64 profile fixtures — the probe is
  `src/probe/vm_probe_unimplemented.cpp`; it emits a schema-valid profile in
  which every fact is unknown, deliberately, rather than guessing
- `[done]` Linux x86-64 fixtures — measured on every run by
  `tools/guards/run_all.sh` and `.github/workflows/ci.yml`
- `[done]` macOS Apple Silicon fixtures —
  `profiles/measured/macos-14-arm64-native.measured.json`
- `[done]` Rosetta fixtures —
  `profiles/measured/macos-14-arm64-rosetta-x86_64.measured.json`
- `[done]` probe conformance tests — `tests/conformance/test_probe.cpp`
- `[done]` profile-diff command — `rs-profile diff`

### Exit criteria

- `[done]` **repeated runs on the same stable host produce equivalent
  canonical profiles** — `tools/campaign/check_reproducible.sh`, which runs the
  probe as TWO PROCESSES. The in-process test was green while this was false.
- `[done]` failures preserve native error information — `errno` and
  `kern_return_t` names are carried into range notes by
  `src/probe/vm_probe_linux.cpp` and `src/probe/vm_probe_macos.cpp`
- `[done]` probe cannot silently classify untested behavior as supported —
  `tests/unit/test_profile.cpp`, `query_range` falls through to `UNKNOWN`
- `[done]` profile schema is versioned — `runtime-skeptic.environment-profile.v1`,
  published as `schemas/environment-profile.v1.json`
- `[done]` deterministic canonicalization — `tests/conformance/test_probe.cpp`
- `[partial]` **at least three platform families** — Linux, macOS-native and
  macOS-under-Rosetta are three *process environments* on two *families*.
  Windows is the missing third family and the ROADMAP names it explicitly.

---

## Phase 2 — Runtime semantic IR and contract evaluator

### Exit criteria

- `[done]` classifies as SUPPORTED / UNSUPPORTED / CONDITIONALLY_SUPPORTED /
  UNKNOWN — `tests/unit/test_analyzer.cpp`, all four discriminate
- `[done]` every non-supported result carries a machine-readable reason —
  enforced at `Analysis::emit()` in `src/vm/analyzer.cpp`; the one permitted
  silent path (`report_unknowns == false`) still lowers the verdict

**Phase 2 is the one phase that is genuinely complete.**

---

## Phase 3 — Virtual memory compatibility analyzer MVP

### Exit criteria

- `[done]` motivating exact-mapping incident diagnosed correctly — shadPS4
  issue #4157 against a MEASURED Rosetta profile, `RS-VM-0001`:
  `contracts/gtav-rage-direct-memory-mapping.json` and
  `tests/groundtruth/contracts/exact-mapping-in-carveout.json`
- `[done]` no unsupported platform fact described as guaranteed — the ceiling,
  `tests/unit/test_evidence.cpp`
- `[done]` findings reproducible from the same inputs —
  `tests/conformance/test_probe.cpp`
- `[done]` each finding contains an evidence chain — an empty chain collapses
  to HYPOTHESIS by construction; `tests/unit/test_evidence.cpp`
- `[open]` **expected false-positive rate is low on curated examples** —
  NEVER MEASURED. This is also ROADMAP Gate B. Nothing in the repository
  establishes it, and the campaign's expectations were written by the same
  author as the rules.
- `[done]` runs in CI without launching the application — `.github/workflows/ci.yml`

### The MVP's seven demonstrations (ROADMAP §14)

- `[done]` 1. exact mapping impossible — `RS-VM-0001`, ground truth
- `[done]` 2. hinted mapping may relocate — `RS-VM-0007`, ground truth
  produces `relocated` against a real kernel
- `[done]` 3. page-size mismatch — `RS-VM-0006`, ground truth
- `[done]` 4. W^X / executable-memory restriction — `RS-VM-0009`/`0011`
- `[partial]` 5. reserve/commit mismatch — `RS-VM-0012` exists but no
  execution has ever confirmed it; the claim is about behaviour under memory
  pressure, which the harness cannot provoke safely
- `[open]` 6. **valid host operation rejected by caller assumption** — no
  contract in the repository demonstrates this direction
- `[done]` 7. invalid fallback reported as success — `RS-VM-0014`

---

## Phase 4 — Runtime wrapper library

`[open]` Not started. No libruntimeskeptic, no src/monitor, no tools/rs-replay.
ROADMAP §21 lists all three; none exists.

This is the phase that would produce `observed_invariant` evidence, which
today nothing in the project can generate.

---

## Phase 5 — CodeSkeptic static integration

`[blocked]` **By the owner's explicit instruction: CodeSkeptic is not to be
modified.** Recorded here so the blocker is visible rather than inferred.

### A boundary that was crossed and walked back

An extractor (tools/rs-extract) was built here to close the "no path from source to a
requirement" gap, and it worked - it recovered the shadPS4 issue #4157 pattern
from source text and reached the same `RS-VM-0001 / UNSUPPORTED` verdict as the
hand-written contract.

It also broke `docs/non_goals.md` section 18, which is normative and reserves
contract extraction and fatal-sink identification for CodeSkeptic. **It was
removed** on 2026-07-25, in full: the tool, the library, the header, the tests,
the fixture, the build wiring and the CI step. `git log` is the only place it
survives.

The decision was the owner's and the reasoning was theirs: RuntimeSkeptic is to
stay a pure runtime project. A merge with CodeSkeptic may be considered later,
and if it happens, extraction arrives from the side that owns it rather than as
a duplicate that had quietly grown here.

`tools/guards/check_non_goals.py` stays, now passing, and fails again the
moment an extractor reappears under this repository.

What was learned is kept in `docs/PROGRESS.md` - the recogniser designs and the
three bugs that only realistic input exposed - so a future extractor does not
have to rediscover them.

---

## Phases 6-10

`[open]` All. Phase 6 (counterfactual), 7 (temporal), 8 (further domains,
gated on Gate D: ten real incidents per domain), 9 (learned invariants),
10 (productization).

ROADMAP §19 Risk 1 is *excessive scope*, mitigated by "remain
virtual-memory-only through the first useful releases". Opening any of these
before Phase 0's corpus and Phase 3's false-positive rate are settled would be
that risk materialising.

---

## Decision gates (ROADMAP §20)

- `[done]` **Gate A** (after Phase 1) — profiles are reproducible
  (`tools/campaign/check_reproducible.sh`), differ meaningfully across
  platforms (16 KiB vs 4 KiB pages, W^X refused vs permitted, SIGBUS vs
  zero-fill on ONE machine — `profiles/measured/`), and expose real
  constraints (the 384 GiB carveout).
- `[open]` **Gate B** (after Phase 3) — it diagnoses real failures and its
  evidence beats ordinary logs, but the **false-positive rate has never been
  measured**, so Gate B is NOT passed. See `docs/PROGRESS.md` for why this is
  the highest-value open item.
- `[blocked]` **Gate C** — Phase 5 is blocked.
- `[n/a]` **Gate D** — no new domain is proposed.

---

## Cross-cutting work not owned by a phase

- `[done]` ground-truth harness — `tests/groundtruth/`, 14 cases, and its own
  selftest, because the comparison table was untested
- `[partial]` rule coverage by execution — 13 of the 20 reachable rules;
  `tools/campaign/groundtruth_coverage.py`
- `[open]` §17 **evidence bundle** — `analysis_bundle/` with manifest, hashes
  and replay status is specified in the ROADMAP and does not exist
- `[blocked]` §16 **differential test: hand-written vs statically extracted
  contract** — needs a second, independent producer of contracts, and by the
  Phase 5 decision that producer lives in CodeSkeptic. Blocked on the same
  instruction that blocks Phase 5, not on effort.
- `[partial]` documentation accuracy — `tools/guards/check_docs.py` exists
  because several documents were found asserting things about the code that
  had stopped being true. Now checks both directions: an absence claim must
  match the filesystem, and a named path must exist. Still `[partial]` because
  only paths and a fixed list of phrases are mechanical; the rest of the prose
  is unchecked and always will be.
- `[done]` the guards are tested — `tools/guards/selftest.py`, 25 cases, each
  requiring a check to fail against a deliberately wrong throwaway repository
  before it is trusted on this one; first in `tools/guards/run_all.sh`
- `[done]` dates are taken from git, not from the author —
  `tools/guards/check_dates.py`. The author is a language model that saw three
  different dates for one day in a single session, and a `<!-- checked: -->`
  marker proves nothing if the date in it is whatever the author believed.
- `[done]` the owner's use cases are written down and assessed against real
  runs — `docs/scenarios/`, ten scenarios, statuses checked by
  `tools/guards/check_plan.py` on the same vocabulary as this file

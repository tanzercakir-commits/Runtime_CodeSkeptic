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

**Phases 0-3 are complete and their executable gates are green. Phase 4 is
implemented locally and awaits authoritative multi-platform CI; Phase 5 remains
decision-blocked.**

```
Phase 0  taxonomy + corpus        DONE      corpus 44/30, vm 35/10
Phase 1  environment probe        DONE      Linux + macOS x2 + Windows measured
Phase 2  semantic IR + evaluator  DONE
Phase 3  VM analyzer MVP          DONE      Gate B passed; all seven demonstrations
                                            and strict execution coverage green
Phase 4  runtime wrapper          PARTIAL   implementation green locally; CI pending (T-009)
Phase 5  CodeSkeptic integration  BLOCKED   owner's instruction: do not modify it
Phase 6  counterfactual           OPEN      gated by Phase 5 / Gate C
Phase 7  temporal contracts       OPEN      dependency-ordered after earlier gates
Phase 8  further domains          OPEN      additionally gated by Gate D
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
- `[done]` `corpus/runtime_failures/` — 54 entries, **44 counting**
  (`sourced`, each with a fetched `source:` and a quoted line); 10 remain
  `pattern_reconstruction` and count toward nothing by rule.
  `tools/guards/check_corpus.py` recomputes both numbers rather than trusting
  the README
- `[done]` finding-ID registry — `docs/findings/registry.md`, 27 ids

### Exit criteria

- `[done]` **at least 30 classified real incidents** — **44 count**, from
  emulators, JITs, allocators, sanitizers, Windows/Wine and kernel changes.
  Every one cites a public report, commit or vendor document that was fetched
  and read; `tools/guards/check_corpus.py` computes the number from the files.
  Caveat recorded in the corpus README: only 6 of the 44 have been re-fetched
  by a second reader.
- `[done]` **at least 10 in the virtual-memory category** — **35 count**,
  computed by `tools/guards/check_corpus.py` from the `category:` field
- `[done]` each category has a concrete evidence model —
  `docs/failure_taxonomy.md`, cross-checked by `tools/guards/check_registry.py`
- `[done]` terminology stable enough to publish — `docs/evidence_model.md`
- `[done]` no claim depends solely on AI-generated interpretation — every
  entry in `corpus/runtime_failures/` carries `provenance:`, and
  `pattern_reconstruction` entries are excluded from the count by rule

**What the corpus changed.** The taxonomy was derived from imagination and is
now testable against 44 real incidents. All ten categories have entries. Two
findings from the set itself:

- **The dominant shape is not refusal, it is a misleading success.** Roughly a
  third of the entries are a call that returned success and gave the program
  something other than what it asked for - a relocated mapping, an ignored
  alignment hint, a decommit that did nothing, an `mmap` honouring `MAP_32BIT`
  with a 47-bit address. That is the category the project was built around and
  the corpus supports it.
- **The model has four gaps the corpus names.** No field expresses the
  granularity of a W^X toggle (RSC-0027), the VMA-count limit (RSC-0040), "the
  same address in a *future* process" (RSC-0041, RSC-0047), or a program that
  *requires* destructive `MAP_FIXED` (RSC-0052). Each is a concrete extension
  with a citation behind it rather than a guess.

---

## Phase 1 — Virtual memory environment probe

### Deliverables

- `[done]` `rs-env-probe` — `tools/rs-env-probe/`, Linux and macOS implemented
- `[done]` environment profile schema — `schemas/environment-profile.v1.json`,
  validated against every committed artifact by
  `tools/guards/validate_schemas.py`
- `[done]` Windows x64 profile fixtures — **measured on a real Windows host**
  (`windows-latest`, 10.0.26100) and committed as
  `profiles/measured/windows-server-2025-x86_64.measured.json`, published by
  `.github/workflows/windows-probe.yml` with the two-process reproducibility step
  green in that run. Allocation granularity 65536 against a 4096 page size, both
  `measured_capability`. The probe (`src/probe/vm_probe_windows.cpp`) also
  cross-compiles clean under mingw-w64 and runs under Wine
  (`profiles/measured/wine-9.0-on-linux-x86_64.measured.json`), which is **not**
  a substitute: for the project's whole life until 2026-07-26 those two were the
  only evidence there was, and the probe had in fact never executed on any
  machine at all — the unimplemented stub's `#if` did not exclude Windows, so the
  linker took the stub and every "Windows measurement" was synthetic while every
  job stayed green. `tools/guards/check_probe_platforms.py` is the rule that now
  makes that unrepresentable. (T-004)
- `[done]` Linux x86-64 fixtures — measured on every run by
  `tools/guards/run_all.sh` and `.github/workflows/ci.yml`
- `[done]` macOS Apple Silicon fixtures —
  `profiles/measured/macos-14-arm64-native.measured.json`
- `[done]` Rosetta fixtures —
  `profiles/measured/macos-14-arm64-rosetta-x86_64.measured.json`
- `[done]` probe conformance tests — `tests/conformance/test_probe.cpp`
- `[done]` profile-diff command — `rs-profile diff`

### Exit criteria

- `[done]` **repeated runs on the same stable host produce equivalent canonical
  profiles** — `tools/campaign/check_reproducible.sh`, which runs the probe as
  **five separate processes** and compares every id. The in-process test was green
  while this was false. Measured on `de72b5e`: **15 macOS probe runs across three
  environments** (`macos---apple-clang`, `native-arm64`, `rosetta-x86_64`, five
  each) all agreeing, published to `refs/measurements/<sha>/*/reproducible.txt`,
  plus Linux and Windows. **What this line cost:** it read `[done]` through
  2026-07-26 while the macOS job failed exactly this criterion on roughly every
  second push — a landmark our own mapping sat on was recorded with a different
  `note` from the same landmark when free, and the note is inside the hashed
  subtree, so `profile_id` alternated between two values. Fixed by
  `ladder_record()`. Fifteen agreeing runs put a 50/50 bistable defect at 2⁻¹⁴;
  a rarer instability is not excluded by any number of runs, only made less
  likely, which is why the run count is a variable (`RUNS=`) and not a constant.
- `[done]` failures preserve native error information — `errno` and
  `kern_return_t` names are carried into range notes by
  `src/probe/vm_probe_linux.cpp` and `src/probe/vm_probe_macos.cpp`
- `[done]` probe cannot silently classify untested behavior as supported —
  `tests/unit/test_profile.cpp`, `query_range` falls through to `UNKNOWN`
- `[done]` profile schema is versioned — `runtime-skeptic.environment-profile.v1`,
  published as `schemas/environment-profile.v1.json`
- `[done]` deterministic canonicalization — `tests/conformance/test_probe.cpp`
- `[done]` **at least three platform families** — Linux, macOS (native and under
  Rosetta) and **Windows**, the last measured on a real `windows-latest` host
  (10.0.26100) and published to `refs/measurements/<sha>/windows-x86_64` with the
  two-process reproducibility step green in that run. Windows establishes two
  arenas totalling `[0x10000000000, 0x7ffffc000000)` — 127 TiB — where until
  2026-07-26 it established **nothing at all** (`available: 0, unavailable: 0`,
  so `RS-VM-0001/0002/0003` answered UNKNOWN for every address on the platform).
  Wine is deliberately **not** counted: the probe detects it via
  `wine_get_version` and renames itself `wine-on-posix-x86_64` so the two cannot
  be confused.

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
- `[done]` **expected false-positive rate is low on curated examples** —
  **MEASURED: 0 false positives in 1292 requests** that 13 real programs were
  observed to perform successfully on a measured Linux host
  (`docs/campaigns/2026-07-false-positive-rate.md`,
  `campaigns/false-positive/2026-07-linux-x86_64.json`, reproduce with
  `tools/campaign/run_false_positive.sh`). The contracts were transcribed from
  `strace`, not authored, so the tool is not grading its own homework. After
  the probe fix the address population is answered too: **0 false positives in
  640 observed addresses, 99.8% of them answered**
  (`campaigns/false-positive/2026-07-linux-x86_64-after-T013.json`). **A second
  operating system now** — macOS 14 arm64, 10 programs, **37 requirements, 0
  false positives** (`campaigns/false-positive/2026-07-macos-14-arm64.json`),
  under `dtrace` watching the mach traps that macOS actually allocates through.
  **And a third** — Windows 10.0.26100, 11 programs, **247 requirements, 0
  false positives** (`campaigns/false-positive/2026-08-windows-x86_64.json`),
  under ETW, on the one host where allocation granularity (64 KiB) differs from
  page size (4 KiB) and `RS-VM-0005` therefore fires — 174 times, as
  information on SUPPORTED verdicts, which is exactly the T-019 behaviour that
  keeps it out of the false-positive count (§9). **Known measurement limits, carried rather than hidden:** the macOS
population is smaller than Linux for measured loader reasons; only the Linux
`strace` lane observes pre-rounding addresses; and the successful-program
campaign cannot measure false negatives. Those limits do not reopen this
criterion: its claimed false-positive populations are measured, while false
negatives are graded independently by the ground-truth execution oracle.
- `[done]` runs in CI without launching the application — `.github/workflows/ci.yml`

### The MVP's seven demonstrations (ROADMAP §14)

- `[done]` 1. exact mapping impossible — `RS-VM-0001`, ground truth
- `[done]` 2. hinted mapping may relocate — `RS-VM-0007`, ground truth
  produces `relocated` against a real kernel
- `[done]` 3. page-size mismatch — `RS-VM-0006`, ground truth
- `[done]` 4. W^X / executable-memory restriction — `RS-VM-0009`/`0011`
- `[done]` 5. reserve/commit mismatch — `RS-VM-0012`,
  confirmed at both failure points with bounded controls. Windows CTest
  `test_windows_reserve_commit_job` proves reservation succeeds and
  `MEM_COMMIT` fails synchronously with native error 1455 inside a
  worker-only Job Object. `tools/campaign/cgroup_reserve_commit_lane.sh`
  proves Linux reservation and `mprotect` succeed before first touch is
  SIGKILLed in a 64 MiB cgroup-v2 leaf whose `memory.events.local`
  increments `oom_kill` (CI run 31046034092).
- `[done]` 6. **valid host operation rejected by caller assumption** —
  `RS-VM-0013`, confirmed by execution:
  `tests/groundtruth/cases/pointer_truncation.c` does `mmap(NULL)`, proves the
  returned mapping is valid and writable, and shows the address does not survive
  32-bit storage. The one ground-truth case here that needs the host to
  **succeed**; its outcome is the new `lost`, held against the analyzer's
  `UNSUPPORTED`. The other six all need the host to refuse or relocate.
- `[done]` 7. invalid fallback reported as success — `RS-VM-0014`

---

## Phase 4 — Runtime wrapper library


`[partial]` The ABI, wrappers, trace/replay path, installed SDK, v0.2 packages,
samples, benchmark and safety guards are implemented and pass a
warnings-as-errors Linux build. CI run 31053293069 proved the runtime suite on
Apple Clang and identified three packaging/Windows test defects now fixed
locally; the fixed commit still needs exact-head Linux/GCC, Linux/Clang, Apple
Clang and MSVC evidence before T-009 can be consumed. (T-009)

### Deliverables

- `[done]` `libruntimeskeptic` and install rules - `src/CMakeLists.txt`, with
  fixed-capacity recorder in `src/runtime/runtime.cpp`; the install CTest
  configures and links a clean out-of-tree CMake consumer
- `[done]` stable pure-C ABI - `include/runtimeskeptic/runtime/runtime.h`,
  `runtime_posix.h`, `runtime_windows.h`; compiled as C by
  `tests/conformance/test_runtime_c_api.c`
- `[done]` event schema - `schemas/runtime-trace-record.v1.json`, embedded and
  rejected adversarially by `tests/unit/test_schema.cpp`
- `[done]` deterministic writer, bounded reader and pure lifecycle replay -
  `src/runtime/runtime.cpp`, `src/runtime/trace.cpp`, `rs-replay trace`, and
  `tests/unit/test_trace.cpp`; requested and page-rounded effective ranges,
  Windows reservation identity and reset semantics are represented explicitly
- `[done]` sample integrations - `tools/guards/validate_schemas.py` executes
  and validates a fresh trace from `samples/runtime_monitor_posix.c`; the
  POSIX sample/replay round trip was also run from `build-wsl-phase4/bin/`,
  and `samples/runtime_monitor_windows.c` is compiled in MSVC CI
- `[done]` overhead benchmark - `rs-runtime-benchmark` emits
  `runtime-skeptic.runtime-overhead.v1`; CTest and
  `tools/guards/validate_schemas.py` execute and validate a fresh artifact

### Exit criteria

- `[partial]` wrapper behavior matches native calls and does not alter native
  error state - `tests/conformance/test_runtime.cpp` passes on Linux/GCC and
  CI run 31053293069 passed the runtime suite on Apple Clang; the corrected
  Windows reset case still needs exact-head MSVC CI evidence (T-009)
- `[done]` `tests/unit/test_trace.cpp` proves trace order is contiguous and
  byte-stable for deterministic recorded
  execution - concurrency and double-flush tests in `test_runtime` and
  `test_trace`
- `[done]` semantic violations are detected at the call boundary without
  replacing successful results - checked-relocation and replay-recomputation
  tests in `tests/conformance/test_runtime.cpp` and `tests/unit/test_trace.cpp`
- `[done]` `tests/conformance/test_runtime_compile_disabled.c` proves monitoring
  disables at build; runtime disable is covered by the
  runtime-off snapshot test in `tests/conformance/test_runtime.cpp`
- `[done]` recursion, buffer overflow, fork-child and assertion modes are
  fail-closed for evidence - `tests/conformance/test_runtime.cpp` and
  `tests/conformance/test_runtime_assert.cpp`
- `[done]` overhead is measured and documented - `benchmarks/README.md` and
  the schema-valid `rs-runtime-benchmark` artifact; CI checks execution, not a
  noisy hosted-runner timing threshold
- `[done]` the structural safety contract is guarded -
  `tools/guards/check_runtime_safety.py` proves one native call, error-state
  ordering, allocation-free fixed recorder and OS-call-free replay; four
  adversarial guard selftests make each protection fail on demand

A single runtime event is an observation, not an `observed_invariant`.
Promotion to an invariant requires repeated traces and belongs to the later
invariant phase.

---

## Phase 5 — CodeSkeptic static integration

`[blocked]` **By the owner's explicit instruction: CodeSkeptic is not to be
modified.** Recorded here so the blocker is visible rather than inferred. (T-011)

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

`[open]` Phase 8 — additional runtime domains, gated on Gate D (ten real
incidents per domain, a bounded operation model, measurable behaviour,
actionable output). (T-023)

`[open]` Phases 6 (counterfactual), 7 (temporal), 9 (learned invariants),
10 (productization). (untracked)

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
- `[done]` **Gate B** (after Phase 3) — it diagnoses real failures
  (ground-truth harness, 24 registered cases, 0 contradicted), its evidence beats
  ordinary logs (the §17 bundle), and **the false-positive rate is measured at
  0 across three operating systems**: Linux x86-64 (1292 requirements), macOS
  14 arm64 (37), Windows 10.0.26100 (247), under three different tracers
  (`strace`, `dtrace` mach-traps, ETW), no requirement authored — all
  transcribed from what each kernel actually did
  (`docs/campaigns/2026-07-false-positive-rate.md`). Both grounds that held
  this open are closed: the noisy `RS-VM-0005` rule (T-019 made its
  precondition declarable, conditional share 42.1% → 0), and the
  single-platform coverage (T-018 macOS, T-022 Windows). Windows is where the
  two landings meet — `RS-VM-0005` fires on 174 real allocations there, the
  one host whose granularity differs from its page size, and every one is
  `SUPPORTED` with an `info` note rather than a gate-breaking condition.
  **Named limitations, carried not hidden:** only `strace` sees the
  pre-rounding request so the address rules are under-exercised against real
  software (§9.3), and the campaign measures no false negatives because no
  observed program was ever refused — that half of correctness is the
  ground-truth harness's job, not this campaign's.
- `[blocked]` **Gate C** — Phase 5 is blocked. (T-011)
- `[n/a]` **Gate D** — no new domain is proposed.

---

## Cross-cutting work not owned by a phase

- `[done]` ground-truth harness — `tests/groundtruth/`,
  24 registered cases (23 direct plus the bounded cgroup lane), 25/25
  adversarial pairing comparisons, three derivation checks, and case-specific
  outcome/verdict oracles that reject typo outcomes and wrong-case output.
- `[done]` rule coverage by execution — `tools/campaign/groundtruth_execution_coverage.py`
  measures kernel outcomes, not textual finding presence, and
  validates manifest/contract/argv bindings, rejects contradicted or unasserted
  evidence, requires every execution exclusion to retain unit coverage, and
  `--require-complete` fails while any synthetic-only backlog remains.
  CI run 31046034092 aggregates the exact Linux gcc, Linux clang, and physical
  macOS ledger artifacts; the remaining rules are an explicit, reasoned
  not-checkable set rather than hidden backlog. (T-021 consumed)
- `[done]` §17 **evidence bundle** — `rs-check --bundle DIR` writes a directory
  with the verbatim inputs, `findings.json`, `report.md`, `hashes.txt` and a
  `manifest.json` (`runtime-skeptic.analysis-bundle.v1`, validated against a
  freshly emitted manifest by `tools/guards/validate_schemas.py`). It
  self-certifies by re-running the analysis from the written files, and
  `rs-replay DIR` re-derives the verdict from the bundle alone, catching both an
  edited file (hash mismatch) and a manifest that lies about its verdict
  (re-derivation). `tests/unit/test_evidence_bundle.cpp`, and a cross-process
  round-trip in CI. (T-007)
- `[blocked]` §16 **differential test: hand-written vs statically extracted
  contract** — needs a second, independent producer of contracts, and by the
  Phase 5 decision that producer lives in CodeSkeptic. Blocked on the same
  instruction that blocks Phase 5, not on effort. (T-011)
- `[partial]` documentation accuracy — `tools/guards/check_docs.py` exists
  because several documents were found asserting things about the code that
  had stopped being true. Now checks both directions: an absence claim must (untracked)
  match the filesystem, and a named path must exist. Still `[partial]` because
  only paths and a fixed list of phrases are mechanical; the rest of the prose
  is unchecked and always will be.
- `[done]` the guards are tested — `tools/guards/selftest.py`, 117 cases, each
  requiring a check to fail against a deliberately wrong throwaway repository
  before it is trusted on this one; first in `tools/guards/run_all.sh`. This
  number read `25` while there were 58, surviving two earlier increases, so
  `selftest.py` now recomputes it from itself and fails on disagreement — which
  is how it was correct again at 63.
- `[done]` dates are taken from git, not from the author —
  `tools/guards/check_dates.py`. The author is a language model that saw three
  different dates for one day in a single session, and a `<!-- checked: -->`
  marker proves nothing if the date in it is whatever the author believed.
- `[done]` the owner's use cases are written down and assessed against real
  runs — `docs/scenarios/`, ten scenarios, statuses checked by
  `tools/guards/check_plan.py` on the same vocabulary as this file
- `[done]` **verdict diff across profiles** — `rs-profile impact`,
  `src/vm/impact.cpp`, `tests/unit/test_impact.cpp`. Answers "did this kernel
  change break any of OUR contracts?" where `rs-profile diff` only answers
  "did the platform change?". Two of the 13 cases run against
  `profiles/measured/`. (T-001)

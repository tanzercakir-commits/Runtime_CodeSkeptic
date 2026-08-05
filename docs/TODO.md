# TODO — the compass

**This is the file to follow.** Four documents, four different jobs, and
confusing them is how a project gets lost:

```
docs/scenarios/     THE SPIRIT   why this exists and who it is for.
                                 Read it when the direction feels unclear.
                                 It is NOT a work list and never becomes one.

docs/TODO.md        THE COMPASS  what we are doing, in order, one at a time.
  (this file)                    Every item names what would prove it done.

docs/PLAN.md        THE MAP      where the project stands against its own
                                 ROADMAP, criterion by criterion.

docs/PROGRESS.md    THE PAST     what changed, what was learned, what was
                                 wrong. Append-only, newest first.
```

The compass and the map are kept in step by `tools/guards/check_todo.py`: every
`[open]` and `[blocked]` criterion in `docs/PLAN.md` must carry the id of an
item here, or be named below as deliberately untracked with a reason. Neither
document can quietly disagree with the other.

**`Now` holds at most three items, and the guard enforces it.** A list where
everything is urgent is not a compass, it is a wall.

Each item carries:

| Field | Means |
|---|---|
| **Serves** | the scenario it is for — the spirit, not decoration |
| **Plan** | the `docs/PLAN.md` criterion it closes |
| **Done when** | something that RUNS and what it must print. Not a feeling. |
| **First step** | enough to start from cold, after any amount of forgetting |

When an item is finished it **leaves the compass**: its account goes into
`docs/PROGRESS.md` (the past, newest first) in the same commit, and its line is
removed from here. The compass carries only the working method and the work
still ahead; the wake lives in the log, where `docs/PROGRESS.md` already keeps
what each finished item changed and taught. Work that completes without a
PROGRESS entry is work that will be redone.

---

## Cold start — read this before anything else

For the session that begins after any amount of forgetting, on any model.
Everything here is measured or enforced, not remembered.

**Where the project stands (2026-08-05):**

```
Phase 0-3   DONE. Gate B, all seven demonstrations, T-012 bounded pressure,
            and strict rule-execution coverage are green.
Phase 4     NOW: implementation green on Linux/GCC; cross-platform CI pending (T-009).
Phase 5     BLOCKED by the owner's standing CodeSkeptic instruction (T-011).
Phase 6-10  dependency-ordered behind Phase 5 / Gate C; Phase 8 also needs
            its own Gate D evidence.
Gate B      false-positive rate measured 0 on THREE operating systems:
              Linux x86-64       1292 requirements  (strace)
              macOS 14 arm64       37 requirements  (dtrace, mach traps)
              Windows 10.0.26100  247 requirements  (ETW)
            174 RS-VM-0005 notes on Windows, all SUPPORTED.
```

**What "finish this project" now means.** Phase 3 is closed. Complete the
Phase 4 runtime wrapper against every executable ROADMAP exit criterion,
then honor Gate C: Phase 5 and the dependency-ordered later phases do not
start until the owner explicitly lifts the CodeSkeptic restriction. Every
completed item is consumed into `docs/PROGRESS.md`.

**How to verify the tree before believing anything, including this file:**

```
bash tools/guards/run_all.sh          every guard must run and pass
cmake --build build -j4 && ctest --test-dir build
```

**How to reach CI** (api.github.com answers 403 for this repo; the git
protocol is the only channel):

```
git fetch origin '+refs/status/*:refs/remotes/status/*' \
                 '+refs/measurements/*:refs/remotes/measurements/*' \
                 '+refs/ci-logs/*:refs/remotes/ci-logs/*'
refs/status/<sha>/<job>/<state>      every job, always
refs/measurements/<sha>/<job>        artifacts, on success
refs/ci-logs/<sha>/<job>             diagnostics, on failure
```

**The credential rule, violated once and not again:** the GitHub PAT lives in
`~/.rs-cred` and is used ONLY via `git config credential.helper '!/root/.rs-cred'`.
Nothing may print, cat, grep or sed that file. Diagnose auth failures from
git's own error output alone. (A token was leaked into a transcript on
2026-07-30 by a masking sed that guessed the wrong prefix. The file's own
header now states the rule.)

**The working method that produced everything below, in one line:** measure
before writing, publish the raw data next to the parsed claim, and when CI
must be the instrument, make each push answer ONE question. T-018 took eight
CI rounds; four were measurements and four were fixes the measurements
demanded, and none was a guess.

**One open observation, deliberately not an item:** `004ee5c`'s `linux-gcc`
job failed at `gt_constrained` while `linux-clang` passed the same commit;
5/5 local runs pass, and the failure predates nothing relevant. The
diagnostics channel now carries the constrained lane's own output (it did not
then, twice over - see fcbbdeb). Do not chase it; the next red run will carry
its evidence.

**A rule for maintaining THIS file:** `plan.md` and `ROADMAP.md` are frozen by
hash guards; `docs/PLAN.md` updates status and current evidence; this file
and `docs/PROGRESS.md` carry the changing queue and immutable history. A
weekly scheduled session (Mondays 06:00 UTC) reads the CI channels and
reports drift; it cannot push.

---

## Now

### T-009 — Phase 4 runtime wrapper `[now]`

**Serves:** every scenario that needs observed call semantics or replay.
**Plan:** `docs/PLAN.md` Phase 4.
**Done when:** all Phase 4 deliverables and exit criteria are executable:
selected POSIX and Windows virtual-memory calls have semantics-preserving C
wrappers; native error state is unchanged; versioned semantic events record
requested/returned addresses, protection changes and lifecycle; writer,
reader and replay are deterministic; immediate violations are caught at the
call boundary; monitoring can be disabled at compile time and runtime; sample
integrations and a reproducible overhead benchmark are committed; conformance,
replay, recursion/allocator-safety and malformed-trace tests pass on Linux,
macOS and Windows.
**Current evidence:** the ABI/schema are frozen; native/disabled/checked
wrappers, canonical trace writer, bounded reader, pure replay, sample,
benchmark, installed out-of-tree SDK consumer and structural safety guard pass
23/23 CTest cases locally under Linux/GCC with warnings as errors. CI run
31053293069 proved the runtime suite on Apple Clang and exposed only missing
transitive C++20 metadata in the installed consumer; on MSVC it exposed API
header order, the same consumer metadata and an invalid reset-test protection.
All three root causes are fixed locally. The v0.2 Linux package executes its
analyzer demo, trace replay and benchmark, and two identical-source builds
produce SHA-256 `54a0703352452e1f63c7e47e64bb2d3b461d5014bac9f5d8bd96fec8f6f5937e`.
**First step:** obtain one exact-head green CI matrix on Linux/GCC,
Linux/Clang, physical macOS/Apple-Clang and Windows/MSVC. Fix evidence, not the
gate, if any lane disagrees; consume T-009 only after all are green.

---

## Next

Phase 5 / T-011 is the next dependency gate after T-009 and remains blocked
by the owner's standing instruction. No Phase 6-10 item is promoted across
Gate C while that instruction remains in force.

---

## Later

### T-023 — Open the second runtime domain: Filesystem first `[later]`

**Serves:** the "other layers" this project has always named — filesystem,
loader, time, networking (`docs/scenarios/`, and the public pitch). Virtual
memory is one domain of many; the thesis is general and this is where it widens.
**Plan:** `docs/PLAN.md` Phase 8 — additional runtime domains
**Done when:** ONE new domain is fully through **Gate D** (ROADMAP §20) — ≥10
real documented incidents, a bounded operation model, a probe that MEASURES the
host's behaviour, a false-positive campaign reporting its rate on a named
population (target 0), and 2–3 reproduce-exact diagnosis cards — and
`docs/PLAN.md` Phase 8 moves `[open]` → `[partial]` with the domain named.

**The guardrails, before any code — this is what the tool's credibility rests
on:** Gate D gates every domain, so the corpus comes FIRST and code after. One
domain at a time (ROADMAP §18). Never add an extractor and never modify
CodeSkeptic (`docs/non_goals.md` §18; `tools/guards/check_non_goals.py` fails the
build the moment one reappears). The false-positive rate is the north star,
measured the way VM's was — a rule that fires on a request that actually
succeeded is a bug, not a finding.

**First step — VM's proven playbook, in order.** Start with **Filesystem
Semantics** (ROADMAP §18 Candidate A: independently *measurable* the way VM was,
and an abundant real-incident record — case-collision on case-insensitive hosts,
non-atomic cross-filesystem rename, fsync-durability drift):

```
1  corpus        >=10 real incidents in corpus/runtime_failures/<domain>/  (Gate D-1)
2  operation     docs/domains/<domain>/operation-model.md - a CLOSED operation
                 set every incident maps to                               (Gate D-2)
3  probe         rs-env-probe <domain>: MEASURE real host behaviour, facts carry
                 evidence classes, unmeasured stays `unknown`             (Gate D-3)
4  schema+rules  version the schema under schemas/, RS-<DOMAIN>-xxxx ids in the
                 registry, every finding through clamp_confidence
5  campaign      tools/campaign/ observe-and-replay, rate in docs/campaigns/ (Gate D-4)
6  cards         2-3 reproduce-exact diagnosis cards + Gate D sign-off in PROGRESS
```

Dynamic Loader/ABI (Candidate B) is the strong second choice and the most
VM-adjacent, but its behaviour is harder to *measure* and leans on cited specs —
pick it only with a concrete loader-incident corpus already in hand.

### T-008 — Fleet aggregation `[later]`

**Serves:** S8 (500 applications, one policy question)
**Plan:** no criterion; a scenario capability
**Done when:** a run over a manifest of applications aggregates by finding id
and answers "how many are affected, and which".

Straightforward, and worth nothing before T-002. Answering "31 applications
affected" with an unmeasured false-positive rate is worse than not answering.

### T-010 — Downstream-consequence modelling `[later]`

**Serves:** S2's second half (allocator, JIT, guard page, signal layout)
**Plan:** no criterion; a scenario capability
**Done when:** a requirement can declare which of its subsystems are
page-size-dependent, and the report names those rather than guessing.

**Deliberately last, and flagged.** This is the easiest place in the whole
project to start generating plausible prose, which Phase 0 forbids by name. If
it is ever built, the consequences must come from a field the program's author
filled in — not from the analyzer's imagination.

### T-033 — Phase 6 bounded counterfactual explorer `[later]`

**Serves:** latent portability failures whose legal bad outcome has not occurred
in the observed run.
**Plan:** `docs/PLAN.md` Phase 6.
**Done when:** a bounded explorer enumerates legal outcomes and short event
sequences, reproduces at least five corpus failures, emits the triggering
assumption and path, and labels bounded absence as `NO COUNTEREXAMPLE FOUND`,
never as proof; deterministic and complexity-bound guards pass.
**First step:** version the counterfactual result schema and encode hinted
relocation as the first one-step outcome family.

### T-034 — Phase 7 Trace/State DSL `[later]`

**Serves:** temporal compatibility claims that cannot be expressed by one call.
**Plan:** `docs/PLAN.md` Phase 7.
**Done when:** a versioned DSL, compiler and deterministic monitor reject
invalid definitions, replay valid traces, and return a minimal violating
subsequence for protection-transition and mapping-lifecycle properties.
**First step:** freeze the smallest state-machine grammar over Phase 4 events
and write parser rejection tests before implementing evaluation.

### T-035 — Phase 9 learned invariants `[later]`

**Serves:** repeatable regression and drift detection over traces and profiles.
**Plan:** `docs/PLAN.md` Phase 9.
**Done when:** an optional reproducible miner reports candidate invariants and
resource trends with the complete observation set and model parameters, can
reproduce its output byte-for-byte, and never labels an inferred invariant as
proof or hides it behind an opaque score.
**First step:** define the candidate-invariant artifact and a deterministic
baseline miner over Phase 4 trace fixtures.

### T-036 — Phase 10 productization `[later]`

**Serves:** external users consuming RuntimeSkeptic in CI and release systems.
**Plan:** `docs/PLAN.md` Phase 10.
**Done when:** the stable CLI and C API, schemas, SARIF, GitHub Actions example,
reproducible evidence bundles, signed-profile verification, registry,
tutorials, benchmark corpus, plugin boundary and private-profile boundary all
have executable conformance tests and a release archive passes them on Linux,
macOS and Windows.
**First step:** turn every Phase 10 deliverable into a versioned public
contract and release-conformance test before adding integrations.

---

## Blocked

### T-011 — CodeSkeptic integration `[blocked]`

**Blocker:** the owner's standing instruction — CodeSkeptic is not to be
modified, and no merge before many real-life tests exist.
**Serves:** S10 (PR review)
**Plan:** `docs/PLAN.md` Phase 5, Gate C, and §16 (differential test)
**Done when:** a contract produced by CodeSkeptic and a hand-written contract
for the same source reach the same verdict on the same profile, as a test — and
the extracted one is labelled `COUNTEREXAMPLE`, never `PROVEN`, per the ceiling
in `docs/scenarios/README.md`.

**Owner's update, 2026-07-30:** CodeSkeptic is FINISHED, per the owner. The
blocker on this item is the owner's standing instruction, so it stays until the
owner lifts it in so many words — but what it would unblock has narrowed to
almost nothing: the differential test CONSUMES CodeSkeptic's emitted contract
bundles (`rs-check` already loads the
`application-requirements-bundle.v1` shape its `--runtime-assumptions` mode
emits) and modifies nothing. The day the owner says go, the first step is an
emitted bundle from CodeSkeptic checked into `contracts/` here and compared
against a hand-written contract for the same source.

Blocked by a decision, not by difficulty, and the decision looks right: the
alternative was two extractors drifting apart in two repositories. An extractor
was built here on 2026-07-25, worked, broke `docs/non_goals.md` §18, and was
removed; what it learned is in `docs/PROGRESS.md`.

The §16 differential test needs a second, independent producer of contracts, and
by this decision that producer lives in the other repository. Same blocker.

---

## Deliberately not tracked

Each needs a reason, so this cannot quietly become a way to empty the list.

- **ROADMAP Phases 6, 7, 8, 9, 10** retain their `(untracked)` PLAN tags because
  the PLAN contract is immutable except for status markers. The executable work
  is nevertheless ordered here as T-033, T-034, T-023, T-035 and T-036. This
  section names the intentional tag exception without reopening the frozen map;
  the compass owns sequencing and each item still has a runnable completion
  test.
- **Documentation accuracy** (`docs/PLAN.md`, `[partial]`) — permanently
  partial by nature. Only paths and a fixed list of phrases are mechanical;
  prose is not compiled and never will be. `tools/guards/check_docs.py` covers
  what can be covered, and there is no end state to reach.

---

## Done

Finished items leave this compass when they are complete. Their accounts move
to docs/PROGRESS.md, newest first, in the same change. The false-positive
campaign, Gate B, evidence bundles, platform probes, and every review-hardening
round are recorded there. Read the log for what each changed and taught; counts
are derived by tools and are never restated here.


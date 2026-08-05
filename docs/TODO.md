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

**Where the project stands (2026-08-01):**

```
Phase 0-2   DONE
Phase 3     PARTIAL, but the finish line moved: GATE B PASSED. The one thing
            still open in Phase 3 is demonstration 5 (RS-VM-0012, reserve/
            commit under memory pressure), which is BLOCKED - the harness
            cannot provoke exhaustion safely (T-012), not a matter of effort.
Phase 4+    not started; Phase 5 blocked by the owner's instruction
Gate B      false-positive rate measured 0 on THREE operating systems:
              Linux x86-64     1292 requirements   (strace)
              macOS 14 arm64     37 requirements   (dtrace, mach traps)
              Windows 10.0.26100 247 requirements  (ETW)
            174 RS-VM-0005 notes on Windows, all SUPPORTED (T-019's payoff:
            the noisy rule speaks as information, not a gate-breaking
            condition, on the one host where it fires).
```

**What "finish this project" now means.** Gate B was the last *measured* exit
criterion. What is left is either blocked by the owner (Phase 5 / CodeSkeptic,
T-011) or is new scope (Phase 4 runtime wrapper T-009, Phase 6+). There is no
unfinished measurement standing between the tool and its own Phase 3 claim.
The honest next moves are on the compass below, none of them urgent.

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

**A rule for maintaining THIS file:** `ROADMAP.md` is frozen by hash
(`tools/guards/check_roadmap.py`); `docs/PLAN.md` moves only its status
markers; this file and `docs/PROGRESS.md` are the ones that change. A weekly
scheduled session (Mondays 06:00 UTC) reads the CI channels and reports drift;
it cannot push.

---

## Now

*(empty. The fourth re-test's findings — T-030/T-031/T-032 — are closed and in
`docs/PROGRESS.md`. The decisive one: inputs are no longer hand-checked field by
field; they are validated against the published schema by a real C++ validator
(`src/core/schema.cpp`, embedded at build) that gates the two domain entry
points, and the boundary matrix PROVES that validator equals Python's
`jsonschema` across 639 mutations — 0 divergences, 0 false-greens. Awaiting the
reviewer's fifth re-test; nothing merged to `main`, LinkedIn still NO-GO by the
owner. Full account: `docs/reviews/2026-08-02-independent-review.md`.)*

---

## Next

*(empty. Gate B is passed and no measured criterion is open. What is left is
`Later` scope or owner-blocked work — nothing forced. The most defensible
next pick is **T-021** (the synthetic-only coverage backlog: real, bounded,
and the same "grade every claim" standard that has driven everything above),
but it is a `[later]` and not urgent. The honest state of the project is that
its Phase 3 promise is measured and kept.)*

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

### T-021 — The synthetic-only backlog: argued with, never shown a kernel `[later]`

**Serves:** the same standard T-020 served, one bucket further in — a rule that
only ever meets a profile someone wrote by hand has never been contradicted by
anything
**Plan:** `docs/PLAN.md` Cross-cutting, "rule coverage by execution"
**Done when:** the synthetic-only bucket has shrunk, and every rule remaining in
it carries a written reason it cannot be executed — at which point it stops being
a backlog and becomes the "not checkable by execution" list, which is already
required to name its reason per line.

T-020 emptied the `NO COVERAGE OF ANY KIND` bucket. The next bucket up is not a
defect and is not nothing: those rules have unit tests against synthetic
profiles and have never been run against a kernel. **The count is deliberately
not written here** — the tool prints it on every push, over both hosts, and this
project has already had one stale count ("13 of the 20 reachable") sitting in
two documents while the tool said something else. Read the output.

Two honest cautions before anyone treats this as a number to drive down:

- **Some of these can never be executed here, and saying so is the work.**
  `RS-VM-0016` needs a host with no non-destructive exact-placement primitive;
  every runner this project can reach has one. That is a reason, and once it is
  written down the rule belongs in the "not checkable" list, not the backlog.
- **A ground-truth case that cannot fail is worse than none.** The harness
  compares a prediction against what the kernel actually did; a case constructed
  so the prediction is trivially right adds a green row and no information.

**First step:** take the four rules whose ground-truth case is most obviously
constructible (`RS-VM-0005`, `RS-VM-0006`, `RS-VM-0009`, `RS-VM-0010` are
plain-mapping properties) and write one case each. Then re-read the bucket and
decide whether the rest are backlog or reasons.

### T-008 — Fleet aggregation `[later]`

**Serves:** S8 (500 applications, one policy question)
**Plan:** no criterion; a scenario capability
**Done when:** a run over a manifest of applications aggregates by finding id
and answers "how many are affected, and which".

Straightforward, and worth nothing before T-002. Answering "31 applications
affected" with an unmeasured false-positive rate is worse than not answering.

### T-009 — Phase 4 runtime wrapper `[later]`

**Serves:** every scenario, indirectly
**Plan:** `docs/PLAN.md` Phase 4
**Done when:** something in this repository can produce `observed_invariant`
evidence, which nothing can today.

This is the only evidence class the project defines and cannot generate. It is
also the honest answer to T-001's trap and to S4: a runtime observation is what
decides a displacement constraint.

### T-010 — Downstream-consequence modelling `[later]`

**Serves:** S2's second half (allocator, JIT, guard page, signal layout)
**Plan:** no criterion; a scenario capability
**Done when:** a requirement can declare which of its subsystems are
page-size-dependent, and the report names those rather than guessing.

**Deliberately last, and flagged.** This is the easiest place in the whole
project to start generating plausible prose, which Phase 0 forbids by name. If
it is ever built, the consequences must come from a field the program's author
filled in — not from the analyzer's imagination.

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

### T-012 — Reserve/commit under memory pressure `[blocked]`

**Blocker:** the ground-truth harness cannot provoke memory pressure safely on a
shared runner, and a test that can take down its host is not a test.
**Serves:** S3
**Plan:** `docs/PLAN.md` Phase 3, MVP demonstration 5
**Done when:** a ground-truth case reserves address space, has the commit
refused under real pressure, and the harness records the refusal — inside a
bound that cannot affect anything else on the machine.

`RS-VM-0012` and `rule_reserve_commit()` exist and are exercised by contracts;
what has never happened is a real host demonstrating the behaviour. Unblocking
needs either a dedicated machine or a `cgroup`-bounded child, and the second is
worth investigating when T-004 brings a Windows runner into scope.

---

## Deliberately not tracked

Each needs a reason, so this cannot quietly become a way to empty the list.

- **ROADMAP Phases 6, 7, 9, 10** (counterfactual, temporal, learned invariants,
  productization) — ROADMAP §19 Risk 1 is *excessive scope*, mitigated by
  "remain virtual-memory-only through the first useful releases". Opening any of
  them while the first domain is still the only one measured would be that risk
  materialising. (Phase 8 — further runtime domains — is now on the compass as
  **T-023**, still gated on Gate D and deliberately in `Later`, not `Now`.)
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


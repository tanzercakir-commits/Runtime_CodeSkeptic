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

When an item is finished it moves to `Done` here **and** gets an entry in
`docs/PROGRESS.md` in the same commit. The guard checks that too: work that
completes without leaving a trace in the log is work that will be redone.

---

## Now

### T-001 — Verdict diff: which contracts change between two profiles `[now]`

**Serves:** S9 (new kernel — who is affected?), most of S7 (platform matrix)
**Plan:** `docs/PLAN.md` cross-cutting, "verdict diff across profiles"
**Done when:** a command takes a set of contracts and two profiles and prints
only the contracts whose verdict *changed*, with both verdicts and the finding
id responsible; a test pins at least one real change using the two committed
macOS profiles, which differ in 138 places.

**Why this one first.** It is the only genuinely new capability in the ten
scenarios that needs **no new probe, no new rule and no new evidence**. Every
part already exists: contracts are files, profiles are files, the analyzer is
deterministic and its output is JSON. What is missing is a loop and a
comparison. Everything else on this list needs measurement, a platform, or
research.

**First step:** `rs-profile diff` already computes fact-level differences.
This is the verdict-level equivalent, and it belongs next to it — probably
`rs-check --profile A --against B` over a directory of contracts, emitting the
changed set. Decide the interface before writing it; a second tool with a third
argument convention would be worse than no tool.

**Trap to avoid:** a contract whose verdict is `UNKNOWN` on both profiles has
not "not changed" — it has never been answered. Report those separately or the
output will read as reassurance.

---

### T-002 — Measure the false-positive rate `[now]`

**Serves:** S6 (CI gate — the commercially interesting one)
**Plan:** `docs/PLAN.md` Phase 3 exit criterion, and Gate B
**Done when:** a documented count exists: N real shipped configurations
evaluated against a measured host, M returned `UNSUPPORTED`, and each of the M
is examined and classified as a true or false positive with a reason.

**Why it matters more than any feature.** This is not a capability, it is the
**permission to be believed**. S6 blocks a merge on this tool's verdict; doing
that on an unmeasured false-positive rate is how a tool gets switched off in its
first week, and a switched-off guard is worse than none — the project has
already made that argument to itself about `tools/guards/check_docs.py`.

It is also the last unmeasured Phase 3 exit criterion and the only thing
standing between the project and Gate B.

**First step:** the method has to be honest about who wrote the expectations.
The campaign's current expectations were written by the same author as the
rules, which makes them a consistency check, not a measurement. Take software
**known to run correctly** on the measured Linux host, write or extract its real
shipped configuration, and count. Every `UNSUPPORTED` is a false positive unless
that configuration genuinely does not ship.

**Trap to avoid:** counting `UNKNOWN` as a pass. It is not a false positive, but
a tool that answers `UNKNOWN` for everything has a perfect false-positive rate
and no value. Report the `UNKNOWN` share next to the number, always.

---

### T-003 — Corpus: 30 classified real incidents `[now]`

**Serves:** S5 (allocators), S2 (page size), and the honesty of the taxonomy
**Plan:** `docs/PLAN.md` Phase 0, both exit criteria
**Done when:** `corpus/runtime_failures/` holds 30 entries whose `provenance:`
is `reported_incident` or `source_citation`, at least 10 of them virtual-memory,
and the README's own accounting agrees with the count.

**The largest single hole in the project.** One entry counts today. The ten
failure categories were derived from imagination rather than from incidents, so
nobody can yet say whether they are the right ten — and every rule in the
analyzer inherits that uncertainty.

**First step:** the cheapest sources are named in `docs/scenarios/assessment.md`
S5 and S2 — mimalloc, jemalloc and V8 publish their shipped configurations and
their bug trackers are public, and "a customer said it broke on Mac" is a real
incident with a real artifact behind it. Emulators (Box64, QEMU, Wine) are the
richest vein for the virtual-memory ten.

**Trap to avoid:** `pattern_reconstruction` entries are pleasant to write and
count toward nothing, by rule. Eight of the eleven current entries are that.
Writing a ninth is motion, not progress.

---

## Next

### T-004 — Windows probe `[next]`

**Serves:** S3 (Wine — the scenario that makes it unavoidable), S7
**Plan:** `docs/PLAN.md` Phase 1 — Windows fixtures, and "three platform families"
**Done when:** `rs-env-probe` returns measured facts on Windows x64 and a
committed profile exists; `tools/campaign/check_reproducible.sh` passes there
as two processes, not two calls in one.

The ROADMAP names three platform families. Linux, macOS-native and
macOS-under-Rosetta are three *process environments* on two *families*, and
`src/probe/vm_probe_unimplemented.cpp` is honest about the rest: every fact
unknown, deliberately, rather than guessed. S3 is dead until this exists —
comparing a measured host against an all-unknown one yields `UNKNOWN`,
correctly and uselessly.

---

### T-005 — Execute the rules that have never run `[next]`

**Serves:** the credibility of every other row
**Plan:** `docs/PLAN.md` cross-cutting, "rule coverage by execution"
**Done when:** the coverage tool reports executed and synthetic-only counts
**separately**, and the executed number has risen.

13 of the 20 reachable rules have been executed against a real kernel. The
remaining 7 (0010, 0016, 0019, 0020, 0022, 0023, 0025) mostly fire when a host
does *not* support something, which needs synthetic profiles — and a synthetic
profile is not ground truth. Saying so in the number is the point; a single
percentage that mixes both is the flattering-direction error this project has
already made once, when the coverage tool reported 100% by grepping prose.

---

### T-006 — A contract for "valid host operation rejected by caller assumption" `[next]`

**Serves:** the symmetry of the whole argument
**Plan:** `docs/PLAN.md` Phase 3, MVP demonstration 6
**Done when:** a contract and a ground-truth case exist where the **host is
fine** and the program's own assumption is what fails, and the harness confirms
it.

Six of the seven MVP demonstrations point the same way: the host refuses
something the program needs. This one points the other way, and its absence is
why the tool can still be read as "a list of things platforms will not do".
`RS-VM-0014` (permitted fallback contradicts a required postcondition) is the
closest existing rule and the natural place to start.

---

### T-007 — The §17 evidence bundle `[next]`

**Serves:** S6, S8 — anything where a verdict has to survive leaving the machine
**Plan:** `docs/PLAN.md` cross-cutting, §17
**Done when:** an analysis emits a directory containing the requirement, the
profile, the findings, a manifest with hashes and a replay status, and re-running
from the bundle alone reproduces the verdict.

A verdict that cannot be replayed by someone else is an opinion with a machine
behind it. Everything needed already exists — profiles are hashed, `profile_id`
covers the facts subtree, the analyzer is deterministic. This is assembly.

---

## Later

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

- **ROADMAP Phases 6–10** (counterfactual, temporal, further domains, learned
  invariants, productization) — ROADMAP §19 Risk 1 is *excessive scope*,
  mitigated by "remain virtual-memory-only through the first useful releases".
  Opening any of them before T-002 and T-003 close would be that risk
  materialising. Gate D gates Phase 8 and no new domain is proposed.
- **Documentation accuracy** (`docs/PLAN.md`, `[partial]`) — permanently
  partial by nature. Only paths and a fixed list of phrases are mechanical;
  prose is not compiled and never will be. `tools/guards/check_docs.py` covers
  what can be covered, and there is no end state to reach.

---

## Done

Nothing yet under this system. Items land here with the commit that finished
them, and the reason they are not deleted is that a compass with no wake behind
it cannot show whether the heading has been holding.

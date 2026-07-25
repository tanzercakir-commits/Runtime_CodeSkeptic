# Progress log

Append-only. Newest first. **One entry per working session**, written in the
same commit as the work.

Its job is to stop the same ground being re-covered and the same wrong turn
being re-taken. Three things belong in every entry and nothing else does:

1. **what changed**, with the commit
2. **what was learned**, especially what turned out to be wrong
3. **what to do next**, precise enough to start from cold

`docs/PLAN.md` says where the project stands. This says how it got there and
what it already tried. A decision recorded here does not need to be
re-litigated; a mistake recorded here does not need to be re-made.

---

## 2026-07-25 — the scenarios, and a ceiling nobody had noticed

**Changed.** `docs/scenarios/` — the owner's ten use cases, kept byte-for-byte,
plus an assessment of each against a real run. `tools/guards/check_dates.py` is
new; `check_plan.py` now checks the scenario statuses on the same vocabulary as
PLAN.md, and its entry-continuation rule was wrong for paragraphs.

**Learned, and it changes what this project can promise.** The scenarios end
with a pipeline — source → CodeSkeptic → RuntimeSkeptic → an AI report saying
"Confidence: 94%" — and Scenario 1 asks for `Confidence: PROVEN`. Both are
reasonable and **they cannot both happen**, for a reason already enforced in
`include/runtimeskeptic/core/evidence.hpp`.

Two contracts, same address, same measured host:

| Contract | Application evidence | Verdict |
|---|---|---|
| `tests/groundtruth/contracts/exact-mapping-in-carveout.json` | `specified_guarantee` | `UNSUPPORTED` `PROVEN` |
| `contracts/gtav-rage-direct-memory-mapping.json` | `statically_inferred` | `UNSUPPORTED` `COUNTEREXAMPLE` |

Anything extracted from source text is `statically_inferred`, which
`confidence_ceiling()` caps at `COUNTEREXAMPLE`. So **an automatically extracted
requirement can never produce a PROVEN verdict** — by design, because a text
match must not be able to manufacture a proof. The verdict, the rule and the
remediation are identical; only the honesty of the label differs. Any report
template written in advance for that pipeline should say `COUNTEREXAMPLE`, and
a percentage would be a third quantity this project has deliberately never
defined.

**Also learned: only one of the ten scenarios works end to end.** Five do part
of what they describe, three are not started, one is blocked by instruction.
The cheapest missing capability is S9's second half — re-evaluating a set of
contracts against two profiles and reporting which verdicts *changed*. It needs
no new probe, no new rule and no new evidence, only a loop.

**The owner asked whether the plan and progress files need dating**, and
offered to supply the date. They do, and the date should come from neither of
us. This session alone produced three dates for one day: the environment header
said 2026-07-24, a later reminder said 2026-07-25, the container clock said
2026-07-25T20:34Z. `check_dates.py` therefore blames every `## YYYY-MM-DD`
heading here against the commit that wrote it, refuses any date later than the
newest commit, and names `<!-- checked: -->` markers older than 90 days. Git
records when work happened; the author only remembers.

**Next.** Ranked in `docs/scenarios/assessment.md`: contract-set re-evaluation
across two profiles, then the false-positive rate (Gate B), then the Windows
probe.

---

## 2026-07-25 — what the removal left behind, and a test for the guards

**Changed.** Three documents still described `rs-extract` after the code was
deleted; fixed. `tools/guards/check_docs.py` gained a third check,
`tools/guards/check_non_goals.py` a second, and `tools/guards/selftest.py` is
new: 18 cases that require every check to fail on purpose before it is
believed.

**Learned — deleting code does not delete the claims about it.** Commit
`d6276e0` removed the tool, the library, the header, the tests, the fixture and
the build wiring, and the tree was verified clean. Three sentences survived it:

| Where | Said |
|---|---|
| `schemas/application-requirements.v1.json` | requirements may be "written by rs-extract" |
| `docs/domains/shadps4-case-study.md` | the tool "now recovers a bounded subset from source text" |
| `docs/PLAN.md` §16 | the differential test is "now possible since rs-extract exists" |

All three were true when written, one day earlier. None was caught, and the
case-study line was carrying a **fresh `<!-- checked: 2026-07-25 -->` marker**
— it had been looked at that same day and still passed, because every check in
the guard keyed on language of ABSENCE. Nothing ever asked whether a path a
document *claims to have* is there. 196 repository paths are named across the
documents; 9 did not resolve.

**And the deeper one: the guards had no tests.** Five of them, all green, and
green is exactly what a guard whose regex matches nothing also reports. This
project has already been bitten by that shape twice — a crashing ground-truth
case counted as a confirmed refusal, and the comparison table ran green while
discarding compiler warnings. Silence read as success both times. `selftest.py`
builds a deliberately wrong throwaway repository per case and requires the
guard to fail on it, with the right message, then to pass once it is corrected.

**Next.** Unchanged, and now unblocked of bookkeeping: the false-positive rate
(Gate B) and the Phase 0 corpus.

---

## 2026-07-25 — rs-extract removed; the boundary holds

**Changed.** `tools/rs-extract`, `src/extract`, `include/runtimeskeptic/extract`,
`tests/unit/test_extract.cpp`, `tests/extract/`, their build wiring and both CI
steps — deleted. `tools/guards/check_non_goals.py` stays and now passes.

**Decision and why.** The owner chose removal over an exception: RuntimeSkeptic
is to remain a pure runtime project, and a merge with CodeSkeptic may be
considered later. Extraction then arrives from the side that owns it, rather
than as a duplicate that had grown here while nobody was checking section 18.

**Kept, because the code is gone and the knowledge should not be.** If an
extractor is ever built in the right repository, these were the findings:

*The five recognisers that worked.* `mmap`/`mach_vm_allocate`/`VirtualAlloc`
carrying `MAP_FIXED` with a literal address; a mapping call requesting
`PROT_WRITE|PROT_EXEC` together; `mprotect` adding `PROT_EXEC` without
`PROT_WRITE`; a `#define` of a page-size constant; a bounded loop containing a
mapping call. Against the shadPS4 issue #4157 pattern these recovered the
address `0x1307200000`, the size `0x20000`, `exact_address_required`, and the
`fatal_assert` sink — matching the hand-written contract field for field, and
reaching the same verdict against the measured Rosetta profile.

*Three bugs that only realistic input exposed, all worth pre-empting:*

1. **Reading one line at a time misses every `MAP_FIXED` site.** Real calls
   wrap, and the flag sits on the continuation line. Join until the parentheses
   balance.
2. **The loop bound is not the first literal.** `for (int i = 0; i < 30; i++)`
   opens with `0`, so every retry loop reads as unbounded. Take the largest
   literal in the header.
3. **Whole-word matching rejects macro suffixes.** `_` is an identifier
   character, so `LJ_PAGESIZE` does not match `PAGESIZE`. The page-size
   recogniser rejected the exact constant it was written for.

*Two invariants worth keeping in any future version.* Every candidate must
carry `statically_inferred` — a text match that could reach a stronger evidence
class would let `grep` produce a proof — and every candidate must state what
the producer could not determine at that specific site, not a generic
disclaimer.

**Confirmed on request — and the first answer was wrong.** The owner asked
whether CodeSkeptic had been touched. The answer given was "neither clone has a
GitHub remote", which is false: `/tmp/CodeSkeptic` has
`origin = github.com/tanzercakir-commits/CodeSkeptic.git`. The conclusion
survived the correction but the reasoning behind it did not, and a right answer
resting on a wrong reason is worth less than it looks. What is actually true,
each line separately checked:

| Checked | Result |
|---|---|
| `git ls-remote origin` | GitHub HEAD is `3444ef3` — the commit the clone started from |
| local `HEAD` | `de8bb69`, on branch `feat/runtime-assumptions`, **1 commit ahead and unpushed** |
| `git reflog` | three entries: clone, checkout, commit. **No push, ever.** |
| working tree | clean, in both `/tmp/CodeSkeptic` and `/tmp/cs-verify` |
| `/tmp/cs-verify` origin | `/tmp/CodeSkeptic` — a local path, not GitHub |

So GitHub's CodeSkeptic is byte-identical to what it was, and the one local
commit (`--runtime-assumptions`, 2026-07-24 23:09Z, 17 files) predates the
instruction and has never left this container. The push token grants
`Runtime_CodeSkeptic` only, but that is now a second line of defence rather
than the argument.

**Next.** Unchanged: the §18 conflict is closed, so the open items are the
false-positive rate (ROADMAP Gate B, never measured) and the Phase 0 corpus
(1 of 30).

---

## 2026-07-25 — the process itself

**Changed.** `docs/PLAN.md` (the spine), this file, and `tools/guards/` — five
guards wired into CI: plan structure, documentation drift, finding-registry
consistency, JSON-schema-versus-code, and the non-goals conflict.

**Learned, and it is the reason this file exists.** The project's documents had
drifted from the code, and nothing noticed:

| Document | Claimed | Actually |
|---|---|---|
| `docs/architecture/determinism.md` | "`tests/unit/` and `tests/conformance/` are empty" | 13 suites |
| `docs/architecture/determinism.md` | "the probe does not exist yet" | Linux + macOS implemented |
| `docs/architecture/determinism.md` | `profile_name` is inside `profile_id` | removed from the hash |
| `docs/evidence_model.md` | "Phase 1 probe (**not implemented**)" | implemented |
| `docs/findings/registry.md` | "The 18 registered ids" | 25 |
| `docs/findings/registry.md` | "`RS-VM-0018` is unreachable" | a rule emits it |
| `README.md` | "the macOS probe has never run" | measured, both lanes |
| `README.md` | "no extractor is bundled" | `rs-extract` exists |
| `corpus/.../RSC-0008.md` | "no rule exists", "a field that does not exist yet" | both exist |
| `corpus/.../RSC-0007.md` | quotes `proven_unsupported()` | renamed to `established_impossibility()` |
| `corpus/.../README.md` | "all 8 entries", "0/30" | 11 entries, 1 counts |

Every one of these was written truthfully and became false while nobody was
looking. That is not a discipline problem, it is a missing test.

**Also learned: reading the whole document matters.** `docs/non_goals.md` §18
is normative and reserves contract extraction and fatal-sink identification
for CodeSkeptic. `rs-extract`, built earlier the same day, does both. Grepping
the roadmap for what to build next would never have surfaced that; reading it
end to end did, and `tools/guards/check_non_goals.py` now fails until the
owner decides.

**Next.**

1. Decide the §18 conflict — remove `rs-extract`, rename it, or amend the
   non-goal with an expiry. The guard is red until then.
2. **Measure the false-positive rate.** ROADMAP Gate B and the Phase 3 exit
   criterion both turn on it and neither has ever been evaluated. The method
   that would be honest: take software known to run correctly on a measured
   host, extract or write its real shipped configuration, and count the
   `UNSUPPORTED` verdicts. Every one is a false positive unless the
   configuration genuinely does not ship.
3. Phase 0 corpus: 1 of 30. It is the largest hole and it is research, not
   code.

---

## 2026-07-25 — ground truth, and what it cost to trust it

**Changed.** `tests/groundtruth/` — 14 cases pairing a single-claim contract
with a program that performs that request for real, plus `selftest.sh` for the
comparison table itself. `tools/campaign/groundtruth_coverage.py`.
Commits `f43b5ed`, `f3c65e9`, `fcd6f00`.

**Learned.** Every prior test checked that the tool was *coherent*; none ran
the program and looked. The harness found a false positive on its first run,
on Linux, before reaching macOS: the analyzer called a POSIX-guaranteed
zero-fill read `UNSUPPORTED`, because a contract could not distinguish a read
inside the final partial page from a read a whole page past the end.

It also found three bugs in itself, each from running on realistic input
rather than imagined input: line-at-a-time reading missed every `MAP_FIXED`
site; the loop bound took the first literal so `for (i = 0; i < 30; i++)` read
as 0; whole-word matching rejected `LJ_PAGESIZE`. And two harness defects
where silence read as success — compiler warnings discarded, and a crashing
case counted as a confirmed refusal.

**Coverage is 13 of the 20 reachable rules.** A green harness over 65% of the
rules is not a tested analyzer, and the number is printed before it is quoted.

---

## 2026-07-25 — macOS measured, and six wrong answers about one boundary

**Changed.** macOS probe measured on both lanes; `profiles/measured/` holds
the results. Commits `2d9ea6c` through `a509cca`.

**Learned.** The Rosetta 2 commpage and Apple GPU carveout are real and
byte-identical to shadPS4's source comment — `[0xfc0000000, 0x1000000000)` and
`[0x1000000000, 0x7000000000)` — and present on **native arm64 too**, which
makes shadPS4's `ARCH_X86_64` guard narrower than the condition.

Classifying that refusal took six attempts. Every wrong answer was corrected
by reading a measurement, never by reasoning about the code, and two of the
six refuted a change that sounded right when I made it:

- `min_map_address` was the probe's own ASLR slide, moving ~48 MiB between two
  runs. Six campaign contracts were returning a confident `UNSUPPORTED` off it.
- removing the scan's `min_address` filter, on the correct-sounding argument
  that it made the candidate set depend on our load address, made
  reproducibility *worse* — the filter was suppressing the dependency, not
  creating it.

**Also learned.** Reading a profile invented provenance: an unknown fact with
no note came back with `"declared unknown"`, and `profile_id` hashes the note,
so a document that made a round trip named a different host than one that did
not. Hidden for the project's life because every fact in every profile so far
had been known.

**Then.** The GitHub Actions quota was exhausted — three macOS jobs per push at
10× billing, roughly 75 billable minutes a commit. Per-push cost is now zero
macOS jobs; measurement is weekly plus a button.

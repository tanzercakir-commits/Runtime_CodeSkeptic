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

**Confirmed on request.** CodeSkeptic was never modified. Both local clones
show zero working-tree changes, zero unpushed commits and no push in the
reflog; neither has a GitHub remote, and the token grants push to
`Runtime_CodeSkeptic` only.

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

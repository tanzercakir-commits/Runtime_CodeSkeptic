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

*(`Now` is empty. The next item is `T-004`, the Windows probe — promote it
deliberately rather than by drift.)*

---

## Next

### T-004 — Windows probe `[now]`

**Serves:** S3 (Wine — the scenario that makes it unavoidable), S7
**Plan:** `docs/PLAN.md` Phase 1 — Windows fixtures, and "three platform families"
**Done when:** a profile measured on a **real Windows host** is published by
`.github/workflows/windows-probe.yml` and committed, with the two-process
reproducibility step green in that run.

**The code is written and it is not done.** `src/probe/vm_probe_windows.cpp`
cross-compiles clean with `-Wall -Wextra` under mingw-w64, the whole project
builds for Windows, and the probe runs correctly under **Wine** — where it
measures a 64 KiB allocation granularity against a 4 KiB page, a working
reserve/commit model, non-destructive exact placement, and `error` for
file-mapping past EOF. Every one of those is the right answer for the Win32
model and **none of them is a Windows measurement.**

Wine reproduces Win32 well enough that all of it succeeds, which is exactly why
it cannot stand in: the probe detects Wine through `wine_get_version` and
renames itself `wine-on-posix-x86_64`, and the CI job refuses to publish a
profile whose name or version says Wine.

**First step:** dispatch `windows-probe.yml`. It is weekly plus a button, at
2x billing — a fifth of macOS, and the quota resets on 1 August.

**Trap to avoid:** the profile will look plausible whatever it says, because
this author has never seen a real one. The two facts to check by hand against
Microsoft's documentation before believing the run are
`lpMaximumApplicationAddress` (the model wants an *exclusive* bound and Win32
reports the last usable byte) and whether `dwAllocationGranularity` is really
64 KiB on that runner rather than assumed.

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

Items land here with the commit that finished them, and they are not deleted:
a compass with no wake behind it cannot show whether the heading has been
holding.

### T-003 — Corpus: 30 classified real incidents `[done]`

**Serves:** S5 (allocators), S2 (page size), and the honesty of the taxonomy
**Plan:** `docs/PLAN.md` Phase 0, both exit criteria
**Done when:** 30 entries whose `provenance:` is a real report, at least 10
virtual-memory, and the README's accounting agrees with the count.
**Met:** **44 counting, 35 virtual-memory**, 54 files.
`tools/guards/check_corpus.py` computes both numbers from the files and fails
when the README disagrees, so the accounting cannot drift back.

43 entries added on 2026-07-25, each fetched and read before being written,
each carrying a quoted line: emulators (FEX, QEMU, box64, Dolphin), JITs and
runtimes (LuaJIT, V8, Mono, ART, YJIT, HotSpot, Dart, SpiderMonkey), Apple
Silicon W^X, allocators and sanitizers (ASan, mimalloc, jemalloc, Go,
ClickHouse), Windows and Wine (PostgreSQL, Firefox, WSL 1, Cygwin), and kernel
changes (`mmap_min_addr`, LA57, the stack guard gap, `MAP_FIXED_NOREPLACE`
introduced *and* reverted).

**What it changed.** All ten taxonomy categories now have real entries, and the
dominant shape across the set is not refusal but **misleading success** —
roughly a third are a call that returned success and gave the program something
else. The corpus also names four concrete gaps in the model: W^X toggle
granularity (RSC-0027), the VMA-count limit (RSC-0040), "the same address in a
future process" (RSC-0041, RSC-0047), and a program that *requires* destructive
`MAP_FIXED` (RSC-0052).

**The honest bound.** Only 6 of the 44 have been re-fetched by a second reader.
`tools/campaign/verify_corpus_sources.py` exists to close that and **cannot run
in the authoring environment**, whose proxy returns 403 to every plain HTTP
client — it reports exit 2 rather than passing vacuously. Six is the number
until it runs somewhere with ordinary network access.

---

### T-013 — Probe the region where programs actually map `[done]`

**Serves:** S9, S7, and the credibility of `RS-VM-0001` on Linux
**Plan:** `docs/PLAN.md` Phase 1 — the scan, and Phase 3's false-positive rate
**Done when:** a re-run of `tools/campaign/run_false_positive.sh` answers
something other than `UNKNOWN` for the majority of the 639 observed MAP_FIXED
addresses, and `tools/campaign/check_reproducible.sh` still passes.

**Found by T-002, and it is the most valuable thing that campaign produced.**
`scan_address_space()` in `src/probe/vm_probe_linux.cpp` establishes 56 windows
of 4 MiB — 224 MiB of a 128 TiB space — at powers of two plus four hand-picked
landmarks. Every landmark is a plausible *emulator* base, because the profile
was built for the shadPS4 question. None is near `mmap_base`.

```
observed MAP_FIXED addresses by 1 TiB bucket:
  0x7f0000000000   629      <- where the dynamic loader actually works
  0x7e0000000000     7
  0x000000000000     3
inside a probe window:  2 of 639
```

So on Linux the address rules — the project's flagship — are not wrong against
real software, they are **silent**. That is honest behaviour (`RS-VM-0017`) and
a useless answer.

**Met.** `src/probe/vm_probe_linux.cpp` now samples two arenas; the re-run is
`campaigns/false-positive/2026-07-linux-x86_64-after-T013.json` and
`docs/campaigns/2026-07-false-positive-rate.md` §4.

| | before | after |
|---|---|---|
| addresses answered `UNKNOWN` | 637 of 639 (99.7%) | **1 of 640 (0.2%)** |
| addresses answered `SUPPORTED` | 1 | **537** |
| **false positives** | 0 | **0** |

Coverage was not bought with wrong answers.

```
0x7c0000000000 .. 0x7ff000400000   kernel mmap arena  (shared libs, big malloc)
0x550000000000 .. 0x58f000400000   ELF_ET_DYN_BASE    (a PIE executable's text)
```

**The first step in the original entry was wrong**, and it was the trap. It
said to derive the region from `/proc/self/maps`. That is this process's ASLR
slide — the exact mistake `min_map_address` already made once. Both arenas are
derived from `max_user_address` instead, a kernel constant identical in every
process, and a sample returning `EEXIST` counts the same as one that succeeds:
`EEXIST` proves the kernel hands this space out, and proves nothing about the
host. `check_reproducible.sh` agrees across two processes.

**The second arena was found by a test, not by thinking.** A new conformance
case asks the profile about the address it is *executing from*. It failed: the
test binary is position-independent and lives at `0x55…`, four TiB below the
mmap arena. `tests/conformance/test_probe.cpp`,
`the_scan_covers_where_this_process_is_actually_mapped`.

---

### T-002 — Measure the false-positive rate `[done]`

**Serves:** S6 (CI gate)
**Plan:** `docs/PLAN.md` Phase 3 exit criterion, and Gate B
**Done when:** a documented count exists: N real configurations evaluated
against a measured host, M returned `UNSUPPORTED`, each of the M examined.
**Met:** `docs/campaigns/2026-07-false-positive-rate.md`,
`campaigns/false-positive/2026-07-linux-x86_64.json`,
`tools/campaign/run_false_positive.sh`.

**N = 1292, M = 0.** Zero false positives in 1292 mapping requests that 13 real
programs were **observed** to perform successfully on the measured host. The
contracts were not written by anyone — `tools/campaign/observe_requirements.py`
transcribes them from `strace`, keeping only calls that succeeded in all three
runs. There is nothing to examine because nothing was refused.

Two things belong next to that number and are in the report: 42% of real
mappings trip `RS-VM-0005` (allocation granularity), which is correct and
noisy; and the address population is 99.7% `UNKNOWN`, which produced `T-013`.

The Phase 3 criterion and Gate B are `[partial]`, not `[done]`: the rate is
measured and low **for the rules this population exercises**, and the address
rules were not among them.

### T-001 — Verdict diff: which contracts change between two profiles `[done]`

**Serves:** S9 (new kernel — who is affected?), most of S7
**Plan:** `docs/PLAN.md` cross-cutting, "verdict diff across profiles"
**Done when:** a command takes a set of contracts and two profiles and prints
only the contracts whose verdict *changed*; a test pins at least one real change
using the two committed macOS profiles. **Met:** `rs-profile impact`,
`src/vm/impact.cpp`, and `tests/unit/test_impact.cpp` — 13 cases, two of them
against the measured profiles.

```
$ rs-profile impact ROSETTA.json NATIVE.json tests/groundtruth/contracts/*.json
REGRESSED  tests/groundtruth/contracts/page-size-at-most-4kib.json
    SUPPORTED -> UNSUPPORTED   A build assuming PAGE == 4096
      findings appeared    : RS-VM-0005 RS-VM-0006
...
14 contract(s): 5 regressed, 0 improved, 0 never answered, 9 unchanged
```

**What it found on its first run.** `rs-profile diff` reports 138 fact-level
differences between the two Apple Silicon lanes. Five of them move a verdict —
and one is a claim about the platform worth keeping: on **one machine**, a
translated x86-64 process is granted simultaneous write+execute that the native
arm64 process is refused. Both `measured_capability`. A JIT ported from x86-64
to native arm64 on the same Mac loses RWX and gains an entitlement requirement.

Full account in `docs/PROGRESS.md`.

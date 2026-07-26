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

### T-015 — Fitting in the address space is not enough to reserve it `[now]`

**Serves:** the credibility of every `SUPPORTED` the analyzer emits
**Plan:** `docs/PLAN.md` cross-cutting — rule coverage, and Gate B's false-positive
claim
**Done when:** `oversized-reservation-4pib` is `held` on **both** a 4-level and a
5-level Linux host, and the rule that changed says why in
`docs/findings/registry.md`.

**Found by a 5-level-paging CI runner, and every host before it hid the defect.**

```
oversized-reservation-4pib    SUPPORTED   refused   CONTRADICTED
    mmap of 4503599627370496 bytes (4096.0 TiB) was refused: ENOMEM
```

On a 4-level host 4 PiB does not fit below `max_user_address`, so the analyzer
answers UNSUPPORTED, the kernel refuses, and the prediction holds — **for the wrong
reason**. On an LA57 host `max_user_address` is 2^56, 4 PiB fits, the analyzer
answers SUPPORTED, and the kernel refuses anyway.

So the rule behind that verdict treats *fits within the address space* as
sufficient for a reservation to succeed. It is not: `ENOMEM` here is overcommit and
VA accounting, not bounds. This is a **false positive** in the dangerous
direction — the analyzer told a caller a 4 PiB reservation would work.

**Not to be fixed by adjusting the expectation.** The harness said so itself, and
the contract describes what the program does.

**Answered, and the condition is a measured fact rather than a sentence.**
`VirtualMemoryModel::max_single_reservation` is the largest **power-of-two**
reservation the host actually granted — a power of two because the exact figure
moves between two runs as the process's own mappings shift, and a fact that moves
is a fact about the probe. `RS-VM-0026` gives three honest bands:

```
size > 2 x granted        the next power of two was measured to FAIL and this is
                          larger still           -> UNSUPPORTED, PROVEN
granted < size <= 2x      between the largest success and the smallest failure;
                          nothing measured it    -> CONDITIONAL, HYPOTHESIS
size <= granted           the host granted at least this much -> nothing to say
```

`RS-VM-0027` covers the fact being absent, above 4 GiB — the next power of two
above the largest request this project has ever observed a real program make
(1.96 GiB across 1292 observations, p99 32 MiB). Below that, silence, or every
ordinary mapping on every fixture would carry the finding.

**Still open on this item:**

1. **Done.** All three probes measure it. macOS reports 70368744177664 and the
   first real Windows profile reports the same 64 TiB, both `measured_capability`.
   The macOS helper was compiled and run **on Linux** under the full CI warning set,
   and the Windows one cross-compiles clean under mingw-w64, so neither was written
   blind. `RS-VM-0027` now fires only on a profile that genuinely has not been
   measured.
2. **`oversized-reservation-4pib` is `held` on a 4-level host and unverified on a
   5-level one**, because CI lands on LA57 hardware only sometimes. The
   `Done when` above asks for both, and the second cannot be arranged on demand.
   The unit tests pin the logic against a synthetic 56-bit profile in the meantime.
3. **`exact-mapping-above-user-space` derives its address from a constant**
   (`0x800000000000`), which is not above user space on an LA57 host. Renaming is
   not the fix; deriving it from the measured bound is.

**Also note:** `exact-mapping-above-user-space` is the same host difference from
the other side. `0x800000000000` is not above user space on an LA57 host, and the
case's own name stops being true there. Renaming it is not the fix; deriving the
address from the measured bound is.


---

---

## Next

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

### T-014 — T-013 was done for Linux only, and macOS says so `[done]`

**Serves:** S3, S9 — and the credibility of `RS-VM-0001` on the platform half
this project's corpus is about
**Plan:** `docs/PLAN.md` Phase 1 — the scan; Gate B "three platform families"
**Done when:** `the_scan_covers_where_this_process_is_actually_mapped` passes on
the macOS runner, and it passes because the macOS probe establishes the region —
not because the test was weakened.

**Found by CI, not by reasoning, and it had been true for a while.**
`scan_allocation_arenas()` exists in `src/probe/vm_probe_linux.cpp` and nowhere
else:

```
grep -c scan_allocation_arenas src/probe/vm_probe_*.cpp
  linux    3
  macos    0     <- still the landmark ladder T-013 replaced
  windows  0
```

So on macOS the profile has nothing to say about the address the test process is
executing from — the exact defect T-013 was raised for, on a platform T-013 did
not touch. The conformance test asserts coverage on **every** platform, so it has
been failing on macOS since the day it was added. Invisible because `ci.yml` ran
macOS only in `expensive-platforms`, gated off pushes.

**Not the same fix twice.** Linux's arenas come from `TASK_SIZE`
(`max_user_address` and `ELF_ET_DYN_BASE = TASK_SIZE/3*2`), both kernel
constants. macOS has no such published constant: `dyld` places the main
executable low (around `0x1_0000_0000`), the shared cache elsewhere, and
`malloc` zones elsewhere again. Deriving the region from `/proc/self/maps` was
the trap T-013 already avoided and the macOS equivalent (`mach_vm_region`) is the
same trap wearing a different name — it reports **this** process's slide.

**First step: done, and the runner answered it.** `90dc74b` shipped the
self-diagnosing failure and macOS printed the two addresses on the next push:

```
code page      : 0x1023a4000        max_user_address: 0x7ffffe000000
  nearest below: [0x100000000, 0x100004000)   gap 0x23a0000    (16 KiB wide)
  nearest above: [0x200000000, 0x200004000)   gap 0xfdc5c000
heap page      : 0x7be800000
  nearest below: [0x400000000, 0x400400000)   gap 0x3be400000
  nearest above: [0x7bf400000, 0xabe000000)   gap 0xc00000     KERN_NO_SPACE
containing     : (none) - a scan-window gap, not a query bug, for both
established    : 19 available, 49 unavailable, and NO arena
```

Which settles the derivation question without needing `mach_vm_region`:

1. **The executable arena anchors on `0x1_0000_0000`.** The code page is
   `0x1023a4000` — 37 MiB above 4 GiB. That is the documented `__PAGEZERO` size
   and default `__TEXT` base for a 64-bit Mach-O on both x86_64 and arm64, plus
   an ASLR slide. It is a per-architecture constant, not this process's layout,
   which is the property Linux's `ELF_ET_DYN_BASE` has and `/proc/self/maps`
   does not.
2. **The heap arena is bounded from above by something already measured.** The
   heap page is `0x7be800000`, 12 MiB below a `KERN_NO_SPACE` region starting at
   `0x7bf400000` that the probe *already found*. So the second arena does not
   need a new constant — it needs the existing ladder's lowest high-address
   refusal as its ceiling, the same way Linux's mmap arena uses
   `max_user_address`.
3. **The problem is not too few landmarks, it is landmark width.** The ladder
   does have entries at `0x100000000`, `0x200000000` and `0x400000000`. They are
   16 KiB and 4 MiB wide *points*. What is missing is the merged-run technique
   `scan_one_arena()` already implements: sample on a stride and collapse
   contiguous non-structural runs into one range.

**The stride question is settled, and the answer is that there is no stride.**
A 32 MiB stride was written first and rejected in one run by a test, not by a
runner: the walk correctly closed at the last window it had placed, and the heap
page sat in the 16 MiB tail before the refused band. Shrinking to 8 MiB would
have worked by fitting a number to one runner's morning. Contiguous windows have
no tail by construction and assert only what was placed — affordable here and not
on Linux purely because this arena spans 60 GiB rather than 128 TiB.

**Written, and verified as far as it can be from here.**

| | |
|---|---|
| `include/runtimeskeptic/probe/arena_walk.hpp`, `src/probe/arena_walk.cpp` | the walk, with the Mach calls injected — compiled and tested on **every** platform |
| `src/probe/vm_probe_macos.cpp` | ~50 lines wiring `try_place` and `describe_region` into it |
| `tests/unit/test_arena_walk.cpp` | 9 cases against the layout `refs/ci-logs/90dc74b/macos---apple-clang` reported. Made to fail on demand: a 32 MiB window breaks 6 of them |

Local: 14/14 ctest, 13/13 guards, `-Werror -Wconversion -Wsign-conversion`.

**What is still NOT verified, and only the runner can do it:** that this compiles
under apple-clang, that `try_place`'s Placement really maps onto what the walk
expects on a live task, that 12,294 windows of `mach_vm_allocate` cost
milliseconds rather than seconds, and that
`tools/campaign/check_reproducible.sh` still agrees across two processes — the
arena's bounds are constants, so it should, but "should" is the word this project
does not accept.

**The runner also settled the `linux---gcc` "flake", and it was the same class of
bug on the other platform.** `max_user_address: 0xfffffffffff000` — 56-bit, 5-level
paging. `TASK_SIZE` is 2^56 there, so both Linux arenas sat in the top 4 TiB of a
64 PiB space while the code and heap pages were at 47 bits, because the kernel
refuses to allocate above 47 bits without an explicit high hint.
`corpus/runtime_failures/RSC-0049-la57-vs-jit-pointer-tagging.md` says exactly
that, and nothing connected it to the arena. Fixed by `arena_ceiling_for()`,
which is a byte-identical no-op on a 4-level host — same `profile_id`, same
bounds, `check_reproducible.sh` still agreeing.

**Met on a real macOS host** (`15ea3f3`): build, 14/14 ctest including
`test_probe`, all 13 guards, and the ground-truth harness through all 14 cases.
macOS green for the first time in the project's history. `test_probe` takes 0.15 s
for ~10 full probes, so the 15,360-window walk costs ~15 ms — the cost question is
answered.

**Three things this item is NOT finished by, all recorded rather than quietly
dropped:**

1. **CLOSED at `5ac82e5`.** `check_reproducible.sh` passes on the macOS runner and
   all six CI jobs are green; two processes agree on `profile_id` there for the first
   time. It took five rounds, and the account below is kept because each round was a
   different producer answering the same unanswerable question. The history first:
   `check_reproducible.sh` on the macOS runner FAILED with

   ```
   available_ranges:    35 vs 32 entries
   unavailable_ranges:  83 vs 74 entries
   ```

   My argument for why it would hold was wrong. `OccupiedByUs` is treated as
   usable, so our own image cannot move which windows are *refused* — true on
   Linux, where `MAP_FIXED_NOREPLACE` answers `EEXIST` for any existing mapping;
   **false on macOS**, where `try_place()` returns `Refused` for a covering region
   that grants no access, and a macOS process is full of its own `PROT_NONE`
   reservations that move with ASLR. The arena is filing ~80 host limitations that
   are its own guard pages.

   **Decided and implemented.** `try_place()`'s rule was not changed — it is right
   for the commpage and the carveout. The walk resolves the ambiguity instead, and
   the arena's top came down to the commpage start so that the resolution is sound.
   `the_output_does_not_move_when_our_own_reservations_move` is the test, and it
   fails on demand. What was decided:

   > Bound the arena at the **commpage start `0xfc0000000`** instead of the
   > carveout start `0x10_0000_0000`. Then no documented platform no-access band
   > lies inside it, so a refusal inside the arena is *ours* and is treated as
   > held-by-probe — the same argument `EEXIST` already gets — counted in the note
   > and recorded as no limitation at all. Available output becomes one stable
   > range; the commpage and the carveout stay with the ladder, which probes them
   > at documented addresses.
   >
   > **Residual risk, which must be stated rather than discovered:** if a macOS
   > version puts an undocumented platform no-access band inside
   > `[0x1_0000_0000, 0xfc0000000)`, the arena would claim it available. The note's
   > held-not-placed count is what would expose that, so it has to stay readable.
2. **Done.** The Linux arena's last 64 GiB stride was unclaimed, which an LA57
   runner exposed. The arena now probes a window that *ends at* its top, the
   candidate ladder no longer probes a window crossing `max_user_address` (that
   refusal was an artefact and was the only Linux "limitation" the probe
   published), and the campaign was re-measured:
   `…-after-top-window.json`, **0 false positives on both populations, and
   otherwise identical**. The null result is the point — the fix matters on a host
   class this machine is not. §5 of `docs/campaigns/2026-07-false-positive-rate.md`.
3. **Done.** `refs/measurements/<sha>/<job>` now publishes on success
   (`tools/ci/publish_measurement.sh`), and its first green run returned the
   observation: **native macOS arm64 raises SIGBUS past end of file** (signal 10),
   prediction held. First time any of that claim has been measured; the Rosetta
   half is still open, because the runner is native.

**Also, and now closed:** Windows had no arena at all — `available: 0,
unavailable: 0` on a real runner, so `RS-VM-0001/0002/0003` answered UNKNOWN for
every address on the platform. It has one, and it is the first of the three whose
bounds were **measured before they were written**:

```
a8bc15f: the runner's own occupancy, 1 TiB buckets over 128 TiB
  0x0            =       6299648      lowest occupied 0x7ffe0000 (KUSER_SHARED_DATA)
  0x10000000000  =       2633728
  0x7f0000000000 =    4340531200      99.8%: image, DLLs, stacks, heaps
  largest free run 139217018867712    126.6 TiB, contiguous

arena = [arena_floor_for(max_user_address, 1 TiB), max_user_address)
      = [0x7f0000000000, 0x7fffffff0000)      1 TiB in 64 MiB windows = 16,384
```

`arena_floor_for()` is in `probe/arena_walk.hpp`, not in the Windows probe, for
the reason `arena_ceiling_for()` is: a derivation that only a Windows runner can
check has moved the platform dependency, not removed it. Likewise the refusal
decision — whose is this? — is `probe/windows_regions.hpp`, with `VirtualQuery`
injected, so `tests/unit/test_arena_walk.cpp` drives **the probe's own code** on
every platform this project builds on rather than a mirror of it. Made to fail on
demand: restricting the window scan to its base breaks 2 of the 6 new cases,
including the reproducibility one, 6 times over.

| file | what it is for |
|---|---|
| `include/runtimeskeptic/probe/windows_regions.hpp`, `src/probe/windows_regions.cpp` | is a placement refusal ours or the host's, with `VirtualQuery` injected |
| `arena_floor_for()` in `probe/arena_walk.{hpp,cpp}` | the arena's floor from a system constant, never from our layout |
| `tests/unit/test_arena_walk.cpp` | 20 cases; 6 of them Windows, against the layout `a8bc15f` measured |

**Not yet measured on a runner.** Everything above is checked off-Windows; the
walk itself has never executed on Windows. The number to read is `placed` /
`held_by_probe` / `refused` / `held_no_access` in the arena note, and the one
that matters is `refused`: if it is not 0, either a system band lies in the top
TiB or the walk is filing our own layout as a limitation.

**Second, and separately:** `linux---gcc` failed this same test on `650d510`
with C++ identical to the `52f541e` that passed it, and it passes 200/200 here.
That is nondeterminism on the runner and it is NOT the macOS gap. The test's
failure message now carries the address, the nearest established ranges, whether
a containing range exists, and the per-arena granted/held/refused split, so the
next occurrence is read rather than guessed. Do not fix that one until it has
been read once.

---

### T-004 — Windows probe `[done]`

**Serves:** S3 (Wine — the scenario that makes it unavoidable), S7
**Plan:** `docs/PLAN.md` Phase 1 — Windows fixtures, and "three platform families"
**Done when:** a profile measured on a **real Windows host** is published by
`.github/workflows/windows-probe.yml` and committed, with the two-process
reproducibility step green in that run.

**The two pieces that needed no runner are done, and both came back negative.**
Answered on 2026-07-26; full account in `docs/PROGRESS.md`.

1. **`lpMaximumApplicationAddress + 1` is not documented either way**, so it
   became an experiment — and the first experiment was wrong. A reservation at the
   top page is refused by Windows' no-access guard region, which is equally
   consistent with "in bounds" and "out of bounds". `VirtualQuery` separates them,
   because it fails only when the address is outside the process's space. Measured
   inclusive; `+ 1` stands, now for a measured reason.
2. **64 KiB granularity is not documented as a value at all** — neither
   `SYSTEM_INFO` nor `VirtualAlloc` states it. So it stays `measured_capability`
   and a host reporting something else is a **finding**, not a probe bug. What
   *is* documented is the reserve-rounds-to-granularity /
   commit-rounds-to-page asymmetry, which is the RSC-0044 mechanism as a
   `specified_guarantee`.

**The runner arrived.** Of the three billing options this item listed, one was
measured and eliminated and one is proven:

| Option | Outcome |
|---|---|
| a small spending limit | **tried, did not work.** A repository-scoped `Actions Windows` SKU budget at $10 hard-stop; nine minutes, two dispatches, `0 ms`, job never started. This item claimed it "lifts it today" and that was wrong. |
| make the repository public | **done, and it opened instantly.** |
| wait for the reset | not needed |

**Windows then ran and failed to build** — `std::back_inserter` without
`<iterator>`, MSVC 19.51.36248.0. Fixed, and `tools/guards/check_includes.py`
now catches the class without a Windows runner. The `Measure` step has still
never executed, so the measurement leg of the end-to-end cycle remains the one
untested piece.

**What is written and what it is not.** `src/probe/vm_probe_windows.cpp`
cross-compiles clean with `-Wall -Wextra` under mingw-w64, the whole project
builds for Windows, and the probe runs correctly under **Wine** — measuring a
64 KiB allocation granularity against a 4 KiB page, a working reserve/commit
model, non-destructive exact placement, and `error` for file-mapping past EOF.
Every one is the right answer for the Win32 model and **none is a Windows
measurement.** The probe detects Wine through `wine_get_version` and renames
itself `wine-on-posix-x86_64`; the CI job refuses to publish a profile whose
name or version says Wine.

---

**MET on 2026-07-26, `4b9ebf4`.** `windows-x86_64: success`, the first green run of
this workflow in the project's history, and the first Windows profile that is not a
stub:

```
origin  measured        os 10.0.26100        x86_64 / process x86_64
page_size                  4096                     measured_capability
allocation_granularity     65536                    measured_capability
min_map_address            0x10000                  measured_capability
max_user_address           0x7fffffff0000           measured_capability
max_single_reservation     70368744177664           measured_capability
exact_mapping              CONDITIONALLY_SUPPORTED  measured_capability
fixed_noreplace_available  true                     measured_capability
reserve_commit_model       windows_reserve_commit   measured_capability
file_map_beyond_eof        error                    measured_capability
```

Committed as `profiles/measured/windows-server-2025-x86_64.measured.json`. The
two-process reproducibility step passed in that run, which is the other half of the
`Done when` above.

**64 KiB allocation granularity is now measured**, not read from a document that
never states it — so `RSC-0044`'s mechanism is confirmed on the platform it is
about, and a host reporting something else is a finding rather than a probe bug.

**Three defects had to be removed first, and each hid the next:**

1. `vm_probe_unimplemented.cpp` guarded itself with `!LINUX && !MACOS`, true on
   Windows, so **the Windows probe was never in the binary** — every "measurement"
   was synthetic while every job was green.
2. `windows-probe.yml`'s push filter named the implementation but not the file that
   can replace it, so the commit fixing (1) triggered nothing.
3. Two steps ran a **bash heredoc under `shell: pwsh`**, so `What was measured` had
   failed on every Windows run there had ever been, and `Refuse a profile that is
   not from real Windows` had never executed at all.

**What is still open, and it is not small:** `available: 0, unavailable: 0`. The
Windows probe establishes no address ranges — no arena, no landmark ladder — so
every address question on Windows answers UNKNOWN. That is T-013 and T-014's work,
third platform, and it is now reachable for the first time.

**The control-plane half is a named human dependency, not a design** — and the
measurement half is no longer one.

The run is triggered by a push to the workflow's own path, which works. Everything
about *scheduling* — is it queued, did it fail at startup, is the quota out — is only
readable through the GitHub API, which this sandbox cannot reach for this repository,
and the substitute is a person opening the Actions tab. That cost stands.

**What no longer stands is the same claim about the measurement plane.** The full
loop has now been run and observed: probe change pushed → run started → measurement
published to `refs/measurements/<sha>/<lane>` → fetched over the git protocol → read,
with nobody opening the Actions tab. That was written here as an assumption and is
now a measurement. `docs/PROGRESS.md` has the account.

---

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

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

## Cold start — read this before anything else

For the session that begins after any amount of forgetting, on any model.
Everything here is measured or enforced, not remembered.

**Where the project stands (2026-07-30):**

```
Phase 0-2   DONE
Phase 3     PARTIAL - Gate B is the only open exit, and T-022 is the only
            ground it still stands on
Phase 4+    not started; Phase 5 blocked by the owner's instruction
Gate B      false-positive rate measured 0 on TWO operating systems:
              Linux x86-64   1292 requirements   (strace)
              macOS 14 arm64   37 requirements   (dtrace, mach traps)
            remaining: Windows (T-022), the one host where allocation
            granularity (64 KiB) differs from page size (4 KiB)
```

**How to verify the tree before believing anything, including this file:**

```
bash tools/guards/run_all.sh          17 guards; all must pass
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

### T-022 — The campaign's third operating system: Windows `[now]`

**Serves:** the same thing T-018 served, and the platform whose address-space
behaviour differs most in kind
**Plan:** `docs/PLAN.md` Phase 3, "expected false-positive rate is low"
**Done when:** the observe-and-replay loop runs on `windows-latest` against
real programs and the measured false-positive rate is published beside the
Linux and macOS ones.

**The instrument is identified and the observer is not written.** The ETW
feasibility round decoded a real NT Kernel Logger trace on the runner
(`refs/measurements/3af0f9f/etw-feasibility`) and the events are there:

```
VirtualAlloc   x379   BaseAddress, RegionSize, ProcessId, Flags
VirtualFree    x335   BaseAddress, RegionSize, ProcessId, Flags
Process/Start          ImageFileName, CommandLine, ProcessId
SystemConfig/CPU       PageSize, AllocationGranularity, HighestUserAddress
```

Note the last line: the kernel trace publishes the host's own
`AllocationGranularity` and `HighestUserAddress`, which is an independent
check on the probe's numbers and worth taking while the observer is being
written.

**What will be different from both existing lanes:**

- **`Flags` is a bitmask of `MEM_COMMIT`/`MEM_RESERVE`/`MEM_TOP_DOWN`, not
  protection.** Windows separates reserve from commit, which is `RS-VM-0012`'s
  whole subject and has never been exercised against a real kernel.
- **`ProcessId` filtering replaces `progenyof()`.** ETW is system-wide; the
  trace will contain every process on the runner, and the observer must select
  by the pid tree itself. Getting this wrong means measuring the false-positive
  rate against the CI agent.
- **64 KiB allocation granularity against a 4 KiB page size.** This is the one
  host where the two differ, which is exactly the distinction §8.2 shows both
  existing lanes could not exercise.

**First step:** take the `VirtualAlloc`/`Process` records the feasibility
round already published (`git fetch origin
'+refs/measurements/*:refs/remotes/measurements/*'` then `git show
refs/remotes/measurements/3af0f9f6c37546899624be5b32c66827d20e6423/etw-feasibility:etw-feasibility.txt`)
and write the parser against THAT file, then run one round and compare its
counts against a `logman` trace of a single known process before trusting
anything.

**The shape of the work, from the macOS lane that just closed:** the observer
plugs into `observe_requirements.py` beside the strace and dtrace lanes; the
scorer (`false_positive_rate.py`) and the bundle format need nothing - they
are OS-blind. The CI wiring goes in `windows-probe.yml`, whose path filter
must name every campaign file it runs (the macOS lane forgot this and the fix
for the thing the workflow measures did not trigger the workflow - twice now
in this repository). Publish through
`tools/ci/publish_measurement.sh measurements "fp-campaign-windows" <sha> <files>`,
and publish the RAW decoded events next to the parsed bundles: the macOS
parser was saved by exactly that habit when a decimal flags word was being
read as hex and agreeing with the correct answer by accident.

**When it closes:** update campaign doc §8.4 and the Gate B line in
`docs/PLAN.md` (both currently name T-022), move this item to Done with the
measured numbers, and write the PROGRESS entry in the same commit -
`check_todo.py` enforces the last two.

---

## Next

*(empty - T-022 is the whole path to Gate B, and Gate B is the whole path to
a finished Phase 3. After it: T-021, then the `Later` set, unless the owner
lifts the Phase 5 block first.)*

---

## Later

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

### T-018 — The false-positive campaign leaves Linux `[done]`

**Serves:** Gate B's credibility — "0 false positives" measured on one OS is a
claim about one OS
**Plan:** `docs/PLAN.md` Phase 3, "expected false-positive rate is low"
**Done when:** the campaign's observe-and-replay loop runs on a second operating
system against real programs, and the measured false-positive rate is published
next to the Linux one.
**Done:** macOS 14 arm64, 10 programs, **37 requirements, 0 false positives**,
published in `docs/campaigns/2026-07-false-positive-rate.md` §8 beside the Linux
figures. `campaigns/false-positive/2026-07-macos-14-arm64.json`.

**Read §8.1 before quoting the rate.** 37 against Linux's 1292 is thirty-five
times smaller and the two do not weigh the same. The gap is measured, not
guessed: macOS's dyld shared cache maps the system libraries in ONE operation
where `ld.so` does one `mmap` per object, and that is where most of the 1292
come from. This is loader-against-loader, not program-against-program.

**The instrument, and the four rounds it took to get right:**

```
1  does dtrace run under the runner's SIP?   SIP disabled - yes.
   And dtruss is DISQUALIFIED: it prints three arguments per syscall,
   and MAP_FIXED is mmap's fourth. A tracer that drops the field under
   test is the mingw-flags mistake with a different vendor.

2  does the observer see anything?           ONE call per program.
   Wrong door.

3  which door, then?                         mach traps. On macOS real
   allocation goes through _kernelrpc_mach_vm_allocate_trap /
   _kernelrpc_mach_vm_map_trap, which dtrace's syscall:: provider never
   sees. BSD mmap is the MINORITY path on this OS.

4  is the mach layout right?                 The raw records said no.
```

Round 4 is the one worth remembering. Every mach line prints its own raw
`arg0..arg5` beside the parsed line, and that caught a **decimal** flags word
being read as **hex** — where the misreading AGREED with the correct one on
this data (`0x3C000001` and `0x1006632961` both have bit 0 set, so the
`VM_FLAGS_ANYWHERE` test came out right by accident). A wrong reading that
produces the correct answer on the data at hand cannot be found downstream at
all.

**A result that arrived as an absence:** `RS-VM-0005` fired **zero** times on
a 16 KiB-granularity host — four times Linux's granularity, where naively it
should fire more. All 37 requested sizes are exact multiples of 16384, because
the mach traps are handed already-rounded sizes by the allocator. The rule has
nothing to say because the situation does not arise on this path. So **the 42%
that forced T-019's decision is a Linux `mmap` phenomenon, not a universal
one** — a gate calibrated on it would have been calibrated on one OS's calling
convention.

**Two inversions the mach path required**, either of which would have silently
manufactured findings: `VM_FLAGS_ANYWHERE` is the OPPOSITE of `MAP_FIXED` (on
mach, "put it wherever" is a flag you set), and `mach_vm_allocate_trap` carries
no protection argument at all — the requirement records the platform default
and declares that protection was not observed.

### T-019 — `RS-VM-0005` is correct on 42% of all real mappings `[done]`

**Serves:** Gate B — a rule that fires on nearly half of everything is not
wrong, it is unusable, and the gate cannot be passed while it is both
**Plan:** `docs/PLAN.md` Decision gates, Gate B, second ground
**Done when:** a decision is written into `docs/findings/registry.md` and
`docs/PROGRESS.md`, the code matches it, and the campaign is re-run so its
document reports the rate under the new behaviour next to the old one.
**Done:** all three. `docs/campaigns/2026-07-false-positive-rate.md` §7,
`campaigns/false-positive/2026-07-linux-x86_64-after-T019.json`.

**The decision: (b) and (a) are the same decision, taken together.** The item
posed them as alternatives; implemented, each is half of the other. The rule's
own conclusion had always named its precondition - "only a defect if the
program relies on the bytes past its requested size being unmapped" - and the
requirement model could not say it. Now it can: `relies_on_unmapped_beyond_size`,
on the exact precedent of `accesses_beyond_eof` (a behavioral claim belongs to
the caller, not to the rule's imagination).

```
declared     UNSUPPORTED, high. The guarantee holds in no execution on a
             rounding host - stronger than the old CONDITIONALLY_SUPPORTED,
             which told a guard-page scheme "works, with conditions" about
             a host where it cannot work.
undeclared   info note on a SUPPORTED verdict. Still emitted - the campaign
             analysis rejected deleting the fact - so nothing is hidden.
```

Re-measured, same workloads: conditional share 42.1% -> 0.0%, RS-VM-0005
emitted for the same 544 mappings as before, 0 false positives in both
populations. The (a)-warning in this item ("do not choose (a) because it makes
the number look better") is answered by that middle line: the count did not
move, only the claim attached to it.

Two things found in passing, both fixed: `adjust_severity()` raised `info` to
`critical` for fatal sinks while the registry's own §3.2 said "`info` is never
raised" - the code now matches the published sentence; and severity adjustment
applied to findings on SUPPORTED verdicts at all, where there is no failure
for a sink to catch.

### T-020 — Two rules had coverage of no kind at all `[done]`

**Serves:** the project's own standard — a rule nothing has ever run is a claim,
and this repository's whole method is that claims get graded
**Plan:** `docs/PLAN.md` Cross-cutting, "rule coverage by execution"
**Done when:** `tools/campaign/groundtruth_coverage.py` prints `0` in its
`NO COVERAGE OF ANY KIND` bucket.
**Done:** it does — `0 have none at all`, on a freshly measured host. Four new
cases in `tests/unit/test_analyzer.cpp` (54 → 58).

Splitting the coverage buckets on 2026-07-30 made this visible for the first
time; it had been hiding inside "13 of the 20 reachable" while the tool itself
said 9. `RS-VM-0016` and `RS-VM-0025` were exercised by nothing whatsoever — not
a ground-truth case, not a unit test, not a synthetic profile.

Both are now graded synthetically, and the synthetic test is the **honest**
instrument for each, for opposite reasons:

```
RS-VM-0016  no non-destructive exact-mapping primitive is available
            Linux >= 4.17 has MAP_FIXED_NOREPLACE, macOS has a non-destructive
            VM_FLAGS_FIXED, Windows has VirtualAlloc2. EVERY runner this
            project can reach reports the primitive present, so a measurement
            cannot produce the case. A synthetic profile is not a weaker test
            here - it is the only one.

RS-VM-0025  the program can use only a small part of this host's address space
            PREDICTIVE per ROADMAP section 11: a statement about how a host
            behaves AS IT FRAGMENTS, not about what was measured. A measurement
            cannot confirm it by construction.
```

**What was wrong in the guess:** this item predicted the `RLIMIT_AS`-constrained
lane would already trip `RS-VM-0025` and that half the work was done and
unmeasured. It does not. `max_user_address` is probed with a **one-page**
`MAP_FIXED_NOREPLACE`, which `RLIMIT_AS` does not charge — so the constrained
host reports the same architectural ceiling as the unconstrained one, and the
rule's `upper <= top/4` test sees nothing unusual. The asymmetry that made the
constrained lane useful for T-015 is exactly what makes it useless here.

Each rule also got its **negative** half — the host that should stay silent —
because a rule tested only where it fires is a rule that might fire everywhere.

### T-005 — Execute the rules that have never run `[done]`

**Serves:** the credibility of every other row
**Plan:** `docs/PLAN.md` cross-cutting, "rule coverage by execution"
**Done when:** the coverage tool reports executed and synthetic-only counts
**separately**, and the executed number has risen.

**Closed, and the separation immediately found something.** The tool now prints
four buckets instead of two, and runs **in CI on every push** over both hosts the
Linux job produces (unconstrained + `RLIMIT_AS`-constrained), publishing to
`refs/measurements/<sha>/linux---*/coverage.txt`:

```
executed against a real kernel     10 / 27   (was 9; RS-VM-0026 via the
                                              constrained host)
synthetic-only (unit tests)        11        argued with, never shown a kernel
not checkable by execution          4        each with its reason on the line
NO COVERAGE OF ANY KIND             2        RS-VM-0016, RS-VM-0025
```

The last number is the point: it was **invisible** while the buckets were mixed —
"backlog" lumped rules that unit tests argue with daily together with rules
nothing has ever exercised. `RS-VM-0016` (no non-destructive exact-mapping
primitive) and `RS-VM-0025` (address-space-lottery dependency) have no execution
AND no unit test. They are now the sharpest edge of the backlog.

Also fixed on the way: the tool's `NOT_EXECUTABLE` list still excused
`RS-VM-0013` with a reason `pointer_truncation.c` had disproven — a stale excuse
that would only have spoken up on the day the case broke. And the body of this
very item said "13 of the 20 reachable" while the tool said 9/23: the number was
hand-run, so it lied within a day. It is CI-run now, which is the actual closure.

---


### T-007 — The §17 evidence bundle `[done]`

**Serves:** S6, S8 — anything where a verdict has to survive leaving the machine
**Plan:** `docs/PLAN.md` cross-cutting, §17
**Done when:** an analysis emits a directory containing the requirement, the
profile, the findings, a manifest with hashes and a replay status, and re-running
from the bundle alone reproduces the verdict.

**Closed.** `rs-check --bundle DIR` emits it; `rs-replay DIR` re-derives the
verdict from the bundle alone.

```
analysis_bundle/
├── manifest.json                 tool + schema versions, host key, hashes,
│                                 finding IDs, replay status
├── environment_profile.json      the profile, VERBATIM bytes
├── application_requirements.json  the requirement, VERBATIM bytes
├── findings.json                 rs-check's own JSON, same code path
├── report.md                     the human report
└── hashes.txt                    sha256sum(1) format: `sha256sum -c` works
```

| what the design refuses to do | why |
|---|---|
| re-serialise the inputs | a re-serialised profile could differ from the bytes the host produced, and then the bundle records something that was never run. They go in verbatim |
| trust its own verdict | `write_bundle` re-runs the analysis from the FILES ON DISK and records `reproduced` or `diverged`. A canonical round-trip that lost a fact shows up as `diverged`, which is why replay happens at write time and not as an assertion |
| write empty §17 files it cannot fill | `static_assumptions.json` and `runtime_trace.jsonl` need the Phase 4 monitor and Phase 5 extractor, which do not exist. The manifest's `absent_components` says so, because a missing file reads as "there was no trace" and this reads as "not produced yet" |

Two lies, two catches. `rs-replay` hashes every stored file against the manifest,
so an **edited file** is caught even if the verdict still re-derives; and it
re-derives the verdict, so a **manifest that claims the wrong verdict** is caught
even though every hash still matches. `tests/unit/test_evidence_bundle.cpp` has a
case for each, plus a cross-process round-trip in `ci.yml` (emit with `rs-check`,
replay with `rs-replay`, reading only the bundle).

The manifest schema is not checked against a committed sample — a sample would
drift the moment the emitter changed, which is the drift the guard exists to
prevent. `validate_schemas.py` runs the real emitter and validates what it wrote.

---
### T-017 — `max_single_reservation` is measured hintless and labelled as if it were not `[done]`

**Serves:** the honesty of `RS-VM-0026`, which is now the rule T-015 rests on
**Plan:** `docs/PLAN.md` Phase 1 — the probe's measured facts
**Done when:** the profile carries the largest reservation granted **with** a hint
above `DEFAULT_MAP_WINDOW` as well as without one, both `measured_capability`, and
`RS-VM-0026` states which of the two it compared against and why.

**Found by reading, not by a runner, and it is true today on every host.**

```
vm_probe_linux.cpp:196      try_map(nullptr, size, PROT_NONE, ... MAP_NORESERVE)
oversized_reservation.c:35  mmap(NULL,    length, PROT_NONE, ... MAP_NORESERVE)
```

Both hintless. Linux does not open 5-level paging to a hintless `mmap`:
`find_start_end()` in `arch/x86/kernel/sys_x86_64.c` widens the search only when
`addr > DEFAULT_MAP_WINDOW` (2^47 − PAGE_SIZE), and
`Documentation/arch/x86/x86_64/5level-paging.rst` says so explicitly, for backward
compatibility. So with `addr = NULL` the search ends at 128 TiB **even on an LA57
host**.

Two consequences:

1. **The fact is mislabelled.** The comment claims *"the largest reservation the
   kernel actually grants"*. What is measured is *the largest granted inside the
   default mmap window*. On a 4-level host those coincide, which is why nothing
   saw it; on LA57 they diverge, and a caller passing a high hint can get more.
2. **The 4 PiB refusal may hold for the wrong reason there too.** If the case is
   hintless, an LA57 kernel refuses 4 PiB because it does not fit the 128 TiB
   default window — not because of overcommit. T-015's old story said the bounds
   reason *evaporates* on a 5-level host; it does not, it **moves** from
   `max_user_address` to `DEFAULT_MAP_WINDOW`, and the analyzer models neither.

The verdict still comes out right (granted stays ~2^46, so `2^52 > 2 x granted`
fires) — by way of a fact that is mislabelled on that host. Right answer, wrong
label, which is the shape this project exists to hunt.

**Closed. Both are measured now, and the pair is published.**

```
this 4-level host:  hintless 0x400000000000   hinted 0x400000000000   AGREE
warning: "a hint above DEFAULT_MAP_WINDOW does not change what this host will
  reserve (0x400000000000 either way), so max_single_reservation means what it
  says here. On a host with 5-level paging it would not"
```

`max_single_reservation_hinted` is a first-class fact in the schema and the model.
Linux probes above `DEFAULT_MAP_WINDOW`; macOS probes high in the space, where the
platform documents no such window and the two are *expected* to agree — measured
rather than assumed, because a number expected to match and never checked is an
assumption in a fact's clothes. **Windows is unknown on purpose**, with the reason
in the profile: a `VirtualAlloc` base is a requirement, not an advisory hint, so
there is no second question to ask and inventing one would be a false analogy.

The probe emits the comparison either way — agreement as evidence, divergence as
a finding — so the first LA57 runner to come along publishes the answer with
nobody present to ask. That was the point of measuring both rather than
relabelling one.

`RS-VM-0026` now **says which figure it used**. It compares against the hintless
one by default, because that is what an unhinted caller actually gets: a program
asking for 4 PiB with `addr = NULL` is bounded by the default window whatever the
hardware could give, and answering from the hinted number would report a
capability the caller cannot reach. It uses the hinted figure only when the
requirement names an address above the hintless probe's own reach, and then says
so in `host_capability` and in the evidence chain.

**What stays open, and it is not this item.** `oversized_reservation.c` is still
hintless, so on an LA57 host the 4 PiB refusal would still be a default-window
refusal rather than an accounting one. That is a property of the *case*, not of
the fact, and the fact now makes it visible: the profile will say the two numbers
diverge on such a host, and the case can then be paired against the right one.

---


### T-015 — Fitting in the address space is not enough to reserve it `[done]`

**Serves:** the credibility of every `SUPPORTED` the analyzer emits
**Plan:** `docs/PLAN.md` cross-cutting — rule coverage, and Gate B's false-positive
claim
**Done when:** the fix is `held` against a real kernel **on a host where the two
reasons diverge** — where the request fits the address space and still exceeds
what the host grants — and the rule that changed says why in
`docs/findings/registry.md`.

**The `Done when` used to name a 5-level-paging host.** That was a mistake in the
item, not a limitation of the world: it assumed the only way to open the gap is to
RAISE the ceiling. Lowering what the host grants opens exactly the same gap, and
`RLIMIT_AS` does it on any Linux host, deterministically and for free.

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
2. **Done, and without waiting for hardware.** The divergence was reachable all
   along by lowering the grant instead of raising the ceiling. Under `RLIMIT_AS`
   the two measurements move apart because they use different calls:
   `find_max_single_reservation()` maps gigabytes with `mmap`, which the kernel
   charges against the limit in `may_expand_vm()` (and `MAP_NORESERVE` is not an
   exemption), while `find_max_user_address()` probes ONE PAGE with
   `MAP_FIXED_NOREPLACE`, which never reaches it. Measured:

   ```
   ulimit -v            max_user_address        max_single_reservation
   unlimited            0x7ffffffff000 (2^47)   2^46
   4194304  (4 GiB)     0x7ffffffff000 (2^47)   2^31
   1048576  (1 GiB)     0x7ffffffff000 (2^47)   2^29
   ```

   `reservation-above-granted-1tib` asks for 2^40 — which fits below 2^47 on both
   hosts, so `RS-VM-0021`'s reason is silent in both and cannot be what decides:

   ```
                    analyzer                       kernel      pairing
   unconstrained    SUPPORTED                      satisfied   held
   constrained      UNSUPPORTED / PROVEN (0026)    refused     held
   ```

   One contract, two hosts, opposite verdicts, both held. The analyzer says it
   itself: *"the largest reservation this host granted was 2147483648 bytes,
   while 140737488347136 bytes of address space exist."*

   `tools/campaign/constrained_lane.sh` runs the probe AND the cases under the
   limit, and **refuses to run** unless the divergence actually opened — the
   ceiling unmoved, the grant lowered, the request inside the space and above
   `2 x granted`. A lane that quietly stopped discriminating would report green
   for a fix nothing tested. Made to fail on demand: at `ulimit -v 900000000` the
   request lands in the CONDITIONAL band and the lane exits 1.

   It also reaches two bands the LA57 round never touches — `size <= granted`
   (SUPPORTED, and granted) and the CONDITIONAL band between — because the limit
   is a dial and the hardware was not.

   **Honest about the mechanism:** this `ENOMEM` is `RLIMIT_AS`, not overcommit or
   VA accounting. `RS-VM-0026` does not ask why; it compares a request against a
   measured grant. The claim under test — *fitting is not sufficient* — is
   identical.
3. **Done.** `exact-mapping-above-user-space` no longer names its ceiling as a
   constant. `tests/groundtruth/derive_contract.py` rewrites the request address
   and the postcondition that names it from the profile's measured
   `max_user_address`, and `tests/groundtruth/manifest.json` asks for it with
   `derive_address_from`. Nothing else in the contract moves, so the derived
   document is the committed one with a single measured number substituted and is
   still schema-valid.

   ```
   5-level host (LA57)      fffffffffff000     <- the constant was 63 PiB below this
   the measured host        7ffffffff000
   an unmeasured profile    <none>             <- the constant stands
   ```

   The third row is the one that keeps a synthetic profile from quietly becoming
   a measurement. All three run in `tests/groundtruth/selftest.sh` on every push,
   on whatever host is there — an LA57 machine is not needed to check the LA57
   case, which is the same move `arena_ceiling_for()` made for the same hardware.
   Made to fail on demand: restoring the constant breaks two of the three rows.


---

---


### T-006 — A contract for "valid host operation rejected by caller assumption" `[done]`

**Serves:** the symmetry of the whole argument
**Plan:** `docs/PLAN.md` Phase 3, MVP demonstration 6
**Done when:** a contract and a ground-truth case exist where the host is fine and
the program's own assumption is what fails, and the harness confirms it.

**Closed.** `RS-VM-0013` (returned address does not fit the caller's pointer
storage) had a rule and unit tests but had never met a real kernel. It does now:

```
pointer-truncation-32bit   UNSUPPORTED   lost   held
  the host returned a valid, writable mapping at 0x7f...; stored in 32 bits it
  becomes 0x...., a different address the program can no longer reach
```

`tests/groundtruth/cases/pointer_truncation.c` does the most ordinary thing there
is — `mmap(NULL, 65536, RW)` — proves the mapping is valid by round-tripping a
sentinel through the full pointer, then shows the address does not survive a
32-bit slot. The kernel does nothing wrong; the 32-bit `pointer_storage_width` is
the program's, and it is the whole failure. That is the direction the other six
demonstrations do not have, and the reason the tool could still be read as "a list
of things platforms will not do".

It needed a **sixth ground-truth outcome**, `lost`, because the existing five are
all host-side: the program did not `fault` (a host access refusal) and the kernel
did not `relocate` (a host placement choice). `lost` names the category demo 6
exists for — a correct host result the caller's own assumption discards — and it
is `held` against `UNSUPPORTED` through the runner's else branch, so `run.sh`
needed no pairing change; `selftest.sh` pins `unsupported\|lost\|held` and
`supported\|lost\|CONTRADICTED` so it cannot silently drift. Real shape:
LuaJIT without GC64 (RSC-0018), MAP_32BIT assumptions (RSC-0020).

Rule coverage by execution rose 8 → **9 of 23** (`RS-VM-0013` now `checked on:
host` in `tools/campaign/groundtruth_coverage.py`).

---


### T-016 — macOS reported its own layout as the top of the address space `[done]`

**Serves:** every verdict the analyzer emits about a macOS address
**Plan:** `docs/PLAN.md` Phase 1 — the probe's measured bounds
**Done when:** `exact-mapping-above-user-space` is `held` on **both** macOS lanes
and `the_scan_covers_where_this_process_is_actually_mapped` passes on both.

**The ceiling half is done and measured.** `d6abf18`:

```
native-arm64    max_user_address  0x600000000000 -> 0x7ffffe000000    lane GREEN
rosetta-x86_64  max_user_address                 -> 0x7ff800000000
```

Both bisections and the survey now go through `address_is_usable()`: a mapping of
OUR OWN at an address is the strongest possible evidence the address exists, and
treating it as a refusal put the ceiling **35 TiB low** on native. Everything in
that band had been answering a confident false `UNSUPPORTED`.
`mach_vm_region` at the new ceiling reports nothing covering it, which is
explanation (a) confirmed and (b) excluded.

**What it exposed, and this is the remaining half.** With an honest ceiling,
`rosetta-x86_64` now fails coverage instead:

```
heap page      : 0x7f9ab0028000                      <- 140 TiB
arena          : [0x100000000, 0xfc0000000)          <- 60 GiB, and 15042 windows
                                                        placed, 0 refused: correct,
                                                        and in the wrong place
nearest below  : [0x400000000000, 0x400000400000)    gap 0x3f9aafc28000
nearest above  : [0x7fffffc00000, 0x800000000000)    KERN_INVALID_ADDRESS
```

A **translated** x86-64 process puts its heap at `0x7f…`, like Linux, not at
`0x7be800000` like the native lane the arena was designed from. So macOS needs a
second arena near the top of the space, and this is now the third platform to say
so:

```
Linux    two arenas   mmap base (top of space) + ET_DYN base
Windows  two arenas   top TiB (image, DLLs) + 1..127 TiB (NT heap)
macOS    ONE arena    [__TEXT base, commpage) — right for native, 140 TiB short
                      for Rosetta
```

**Both halves are now written; the second is unverified.** `scan_one_macos_arena()`
takes its bounds as parameters and the ours-rule inside `describe` is
`base >= bottom && base < top` — the arena's own bounds, which is what
`probe/arena_walk.hpp` already said it should be: *"The bounds of the arena ARE
the rule."* For the low arena that predicate evaluates exactly what
`no_access_here_is_ours()` does, so it is a byte-identical no-op there.

The high arena is `[high_arena_floor(max_user_address, 4 TiB, commpage),
max_user_address)` in 64 GiB contiguous windows — 64 placements, the Windows
sizing rather than Linux's sampling, because `arena_walk` asserts only what it
places. `high_arena_floor()` lives in `probe/arena_walk.hpp` and returns **0**
twice on purpose: when the ceiling was never measured, and when the arena would
reach back into the low one. Both are refusals to guess, and both are tested.

**Closed on `9311e1c`: eight green, and both lanes agree on one ceiling.**

```
                    before          after
native-arm64    0x600000000000  ->  0x7ffffe000000    35 TiB low
rosetta-x86_64  0x7ff800000000  ->  0x7ffffe000000    34 GiB low
                                    ^ and mach_vm_region finds NO region at or
                                      above it, on both - the genuine top

high arena  [0x7bfffe000000, 0x7ffffe000000)   64 placed / 62 placed, 0 refused
```

Identical on both lanes, which is what a kernel constant should be and what the
old value never was. It took **three** distinct defects, each found by the one
before it: the ceiling read our own layout as a refusal; the arena covered only
where a native process lives; and the ceiling search read a system reservation's
floor as the top of the world.

---


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

top arena = [arena_floor_for(max_user_address, 1 TiB), max_user_address)
          = [0x7f0000000000, 0x7fffffff0000)   1 TiB in 64 MiB windows = 16,384
low arena = [1 TiB, 0x7f0000000000)          126 TiB in 64 GiB windows =  2,016
```

**Two, not one, and the runner is why.** The top arena ran on `82ec86d` and
worked — 16312 placed, **0 structurally refused**, which confirms by measurement
that no system band lies inside it. The same run failed coverage anyway:
`test_probe`'s heap page was `0x2f78000e000` (2.97 TiB) while `rs-env-probe`'s was
in the 1 TiB bucket. Same host, same push, different processes. On Windows the
image and every DLL go to the top TiB while an NT heap goes low, so it is the
Linux shape (two arenas) and not the macOS one (one).

`[min_map_address, 1 TiB)` is left to the landmark ladder on purpose:
KUSER_SHARED_DATA at `0x7ffe0000` is there, and it is the one band whose presence
inside an arena would break `arena_walk`'s treat-a-covered-refusal-as-held rule.

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

**The top arena is measured; the low one is not yet.** `refused` is the number to
read in each note. It was 0 for the top arena, which is what the design needs. If
the low arena's is not 0, either a system band lies between 1 and 127 TiB or the
walk is filing our own layout as a limitation — and because that arena records at
the resolution of one 64 GiB window, a non-zero `refused` also means it is
over-claiming and wants narrowing.

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

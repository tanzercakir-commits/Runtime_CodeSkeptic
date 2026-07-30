# Campaign — the false-positive rate, measured

**Host:** Linux x86-64, measured by `rs-env-probe` in the same session
**Data:** `campaigns/false-positive/2026-07-linux-x86_64.json` (T-002, first run),
`campaigns/false-positive/2026-07-linux-x86_64-after-T013.json` (after the probe
fix this campaign motivated) and
`campaigns/false-positive/2026-07-linux-x86_64-after-top-window.json` (after the
LA57 top-window fix — §5, and it changed nothing here)
**Reproduce:** `tools/campaign/run_false_positive.sh`

> **Read §4 before quoting anything from §1–3.** The first measurement found a
> defect in the probe, that defect was fixed as `T-013`, and the campaign was
> re-run. The body below is the *first* run, kept because it is what found the
> problem; §4 is where the project stands now.

ROADMAP Gate B and the Phase 3 exit criterion both turn on this number, and
neither had ever been evaluated. Not through laziness — the obvious method is
disqualified.

## Why the obvious method does not count

The campaign's contracts were written by the same author as the rules. When
they agree, that measures consistency. A false-positive rate produced that way
is a tool grading its own homework, and `docs/PLAN.md` said so rather than
publishing a flattering number.

**So the contracts were not written at all.** `tools/campaign/observe_requirements.py`
runs real programs, records the mapping requests they actually made with
`strace`, keeps only the ones that **succeeded** and that appear in **every one
of three runs**, and converts each mechanically into a requirement document.

The ground truth is then not an opinion. The kernel performed these operations
on this host, minutes ago.

> **A false positive is an `UNSUPPORTED` verdict on a request this host was
> observed to perform successfully.**

`UNSUPPORTED` means *no execution satisfying all constraints exists on this
host profile*. The kernel did it. Both cannot be true.

### What the generated contracts refuse to assert

The harness transcribes; it does not interpret.

| Field | Set from | Never set, and why |
|---|---|---|
| `exact_address_required` | `MAP_FIXED` present | a hint without MAP_FIXED is a preference |
| `guest_host_identity_required` | — | **always false.** Nothing in a trace reveals whether the program cared where the mapping landed. Inventing it would manufacture `RS-VM-0007`, one of the findings under test. |
| `failure_sink` | — | recorded as the weakest kind, so severity is never inflated |
| `assumption_evidence` | `observed_invariant` | `docs/evidence_model.md`: "held across a recorded trace set" |

## The programs

Thirteen, all of which exited 0, chosen so each does real work rather than
printing a version string — a JIT that never compiles anything maps nothing
interesting.

```
python3   perl    ruby    php     git     openssl   jq
redis     gzip    xz      ffmpeg  node (V8, hot loop)   java (HotSpot, hot loop)
```

## The result

```
SHAPE     13 programs, 1292 requirements, 0 rejected
          SUPPORTED                748   57.9%
          CONDITIONALLY_SUPPORTED  544   42.1%
          UNKNOWN                    0    0.0%
          UNSUPPORTED                0    0.0%   <- FALSE POSITIVES

ADDRESS   13 programs,  639 requirements, 0 rejected
          SUPPORTED                  1    0.2%
          CONDITIONALLY_SUPPORTED    1    0.2%
          UNKNOWN                  637   99.7%   <- read this first
          UNSUPPORTED                0    0.0%
```

**Zero false positives in 1292 observed, successful mapping requests.** The
analyzer does not refuse ordinary work that real software performs on this
host.

That is the headline and it is real. Three things have to be said next to it,
because the number alone is more flattering than the truth.

---

## 1. The address population is unanswerable, and the probe is why

99.7% `UNKNOWN`, all `RS-VM-0017` — *availability of the requested range was
never established*. That is the analyzer behaving **correctly**: it declined to
claim knowledge it did not have. The reason it did not have it is a defect, and
it is in the probe.

```
probe establishes 56 windows of 4 MiB     =  224 MiB
of an address space of                       128 TiB     (0.00017%)

observed MAP_FIXED addresses, by 1 TiB bucket:
  0x7f0000000000   629      <- where the dynamic loader actually works
  0x7e0000000000     7
  0x000000000000     3

falling inside a probe window:  2 of 639
```

`scan_address_space()` in `src/probe/vm_probe_linux.cpp` samples powers of two
plus four hand-picked landmarks (`0x1000000000`, `0x4000000000`,
`0x6fffff0000`, `0x7fff00000000`). Every one of those is a plausible *emulator*
base — the profile was built for the shadPS4 question — and none of them is
anywhere near `mmap_base`, where every ordinary program on the machine actually
maps.

**Consequence:** on Linux, the address rules — `RS-VM-0001`, `0002`, `0003`,
the project's flagship — cannot be evaluated against real software at all. They
are not wrong here; they are silent. The false-positive rate above is therefore
a rate **for the rules this population exercises**: page size, allocation
granularity, protection, executable mapping. It is not a rate for the analyzer
as a whole, and quoting it as one would be exactly the overclaiming this
project exists to prevent.

This is now `docs/TODO.md` `T-013`.

## 2. Forty-two per cent of real mappings trip `RS-VM-0005`

Every one of the 544 `CONDITIONALLY_SUPPORTED` verdicts is the same finding:
*requested size is not a multiple of the host allocation granularity*.

The rule is not wrong. `mmap(NULL, 53867, ...)` really does reserve 56 KiB
rather than 53867 bytes, and a program that later computes with 53867 really
can walk off the end of what it thinks it owns. The text already hedges: "the
mapping succeeds but covers more address space than requested. This is only a
defect if…".

But real software passes unrounded sizes **constantly** — it is the normal way
to call `mmap`, and the kernel rounds up by specification. A rule that fires on
42% of all mappings is, in a CI gate, indistinguishable from noise, and this
project has already written down what happens to a noisy guard: it gets
switched off, and a switched-off guard is worse than none.

Not a false positive. Worth a decision before anyone gates a build on this
tool: either the rule needs a condition (fires only when the program is
observed to *use* the unrounded size), or it belongs at `info` severity.

## 3. What this method cannot see

- **No false negatives were measurable.** Across 13 programs and three runs
  each, there was **not one failing `mmap` or `mprotect`**. Real software on a
  healthy host does not get refused, so this population contains no case where
  the analyzer could have wrongly said `SUPPORTED`. The other half of
  correctness is untouched by this campaign.
- **One host, one OS.** Linux x86-64. The macOS lanes have measured profiles
  but no traced programs; `strace` is Linux-only and the equivalent would be
  `dtruss` under SIP.
- **Startup only.** These traces cover process startup and a short workload.
  A long-running server's steady-state allocation behaviour is not here.
- **The shape population under-asserts by construction** — no addresses, no
  identity requirements, no failure sinks. That is what makes it fair; it is
  also what makes it narrow.

---

## What the harness caught in itself, first

On its first run, `rs-check` rejected 16 requirements: *`exact_address_required`
is true but no `request.address` was given*. The harness was setting the flag
from `MAP_FIXED` while omitting the address from the shape contract — an
internally inconsistent document.

The analyzer refused it rather than guessing, which is the behaviour the
unrecognized-fields work was for. The bug was the harness's; the guard was the
analyzer's. Worth recording, because it is the first time in this project that
a new tool's first output was wrong and something else said so immediately.

---

# 4. After the probe fix (T-013)

Data: `campaigns/false-positive/2026-07-linux-x86_64-after-T013.json`

The hole in §1 was the whole reason `T-013` existed, and closing it changed
the answer:

| | first run | after `T-013` |
|---|---|---|
| address population, `UNKNOWN` | 637 of 639 — **99.7%** | **1 of 640 — 0.2%** |
| address population, `SUPPORTED` | 1 | **537** |
| address population, `CONDITIONALLY_SUPPORTED` | 1 | 102 |
| **address population, false positives** | 0 | **0** |
| shape population, evaluated | 1292 | 1293 |
| **shape population, false positives** | 0 | **0** |

**638 of 640 observed addresses now get a real answer, and every one of those
answers agrees with what the kernel did.** The coverage was not bought with
wrong answers — the false-positive count stayed at zero on both populations.

What changed in the probe is in `src/probe/vm_probe_linux.cpp`: two arenas,
both derived from `max_user_address` and neither from `/proc/self/maps`.

```
0x7c0000000000 .. 0x7ff000400000   the kernel's mmap arena: every shared
                                   library, every large malloc
0x550000000000 .. 0x58f000400000   ELF_ET_DYN_BASE: a PIE executable's text
```

The second one was found by a test, not by reasoning. A new conformance case
asks the profile about the address the test is *executing from*; it failed,
because the test binary is position-independent and lives at `0x55…`, four TiB
below the mmap arena and nowhere near any emulator landmark.

**The one remaining `UNKNOWN` is worth keeping.** It is HotSpot reserving its
heap at `0x82a00000` — a low, deliberately-chosen compressed-oops base that no
arena covers. The profile has nothing to say about it and says so. That is the
correct answer, and it is a better advertisement for the model than the 537
that resolved.

# 5. After the top-window probe (LA57)

Data: `campaigns/false-positive/2026-07-linux-x86_64-after-top-window.json`

The arenas in §4 stopped up to one 64 GiB stride short of their tops, because
the run closed at the last window actually probed. On this host that cost
nothing. On a **5-level-paging** runner it cost everything: `mmap_base` derives
from `DEFAULT_MAP_WINDOW` rather than `TASK_SIZE` there, so it lands at
`0x7ffa…` — inside exactly the stride the walk never reached — and the
conformance case reported zero coverage for the heap. See `docs/PROGRESS.md`;
it presented as a flaky test for two days.

The arena now probes a window that **ends at** its top, and both arenas reach
their declared bounds:

```
0x7c0000000000 .. 0x7ffffffff000   the kernel's mmap arena, to max_user_address
0x550000000000 .. 0x590000000000   ELF_ET_DYN_BASE, to the arena's own top
```

That changes the sample set on every host, so `profile_id` moved and this
measurement exists to say what it did to the answers:

| | after `T-013` | after the top window |
|---|---|---|
| shape population, evaluated | 1293 | 1292 |
| shape population, `SUPPORTED` | 749 | 748 |
| shape population, `CONDITIONALLY_SUPPORTED` | 544 | 544 |
| **shape population, false positives** | 0 | **0** |
| address population, evaluated | 640 | 639 |
| address population, `SUPPORTED` | 537 | 536 |
| address population, `UNKNOWN` | 1 — 0.16% | 1 — **0.16%** |
| **address population, false positives** | 0 | **0** |

**It changed nothing here, and that is the honest result.** The one-observation
difference in each population is run-to-run variation in what the programs did,
not a difference in what the analyzer answered: the same 544 `RS-VM-0005`, the
same single `RS-VM-0017`, the same zero false positives. Reporting this as an
improvement would be false — the fix matters on a host class this machine is not,
and the value of re-measuring is precisely that it is *not* an improvement.

One thing did get smaller. The candidate ladder used to probe `0x7fffffc00000`
with a 4 MiB window, which ends at `0x800000000000` — past `max_user_address`.
The kernel refused it and the probe filed
`[0x7fffffc00000, 0x800000000000)` as a host limitation. Once the arena began
placing a window that ends *at* `max_user_address`, that entry contradicted a
successful placement, and `available_and_unavailable_ranges_do_not_overlap`
failed. The refusal was an artefact of where the window was put, and the part
genuinely beyond the top is what `max_user_address` already states, so the ladder
now skips a candidate whose window would cross it. `unavailable_ranges` on this
host is consequently empty — the profile records no limitation it cannot defend.

# 6. After RS-VM-0026 (T-015): the numbers that did not move, and why that matters

Data: `campaigns/false-positive/2026-07-linux-x86_64-after-T015.json` — **byte-identical
to §5**.

`RS-VM-0021` compared a request's size against the width of the address space and,
when it fitted, said nothing further. Its own rejected-fix text asserted the
reasoning: *"the limit is the width of the address space, not the amount of free
memory in it."* A 5-level-paging CI runner disproved that in one line:

```
oversized-reservation-4pib   SUPPORTED   refused   CONTRADICTED
    mmap of 4503599627370496 bytes (4096.0 TiB) was refused: ENOMEM
```

On a 4-level host 4 PiB does not fit, the verdict is UNSUPPORTED, the kernel
refuses, and the prediction holds **for the wrong reason**. On a 56-bit host it
fits, the verdict became SUPPORTED, and the kernel refused anyway. A false positive
in the dangerous direction — the analyzer told a caller a 4 PiB reservation would
work.

The profile now carries `max_single_reservation`, the largest power-of-two
reservation the host actually granted (this host: 70368744177664 bytes, 64 TiB),
and `RS-VM-0026` compares against it.

**And the campaign did not move by a single requirement.**

| | after the top window (§5) | after `RS-VM-0026` |
|---|---|---|
| shape population, evaluated | 1292 | 1292 |
| shape, `SUPPORTED` / `CONDITIONALLY_SUPPORTED` | 748 / 544 | 748 / 544 |
| address population, evaluated | 639 | 639 |
| address, `SUPPORTED` / `UNKNOWN` | 536 / 1 | 536 / 1 |
| **false positives, both populations** | 0 | **0** |

That is the finding, not a footnote. **The corpus does not contain the defect.**
Across 1292 requirements observed from 13 real programs, the largest single request
was **2103443456 bytes — about 1.96 GiB** — and the 99th percentile was 32 MiB.
Nothing a real program on this machine asked for comes within four orders of
magnitude of the 64 TiB this host grants, so no amount of running this campaign
harder would ever have found the bug.

It took a **host** the project had never run on, not a program. That is the same
lesson as `<iterator>` on MSVC and `declare -A` on bash 3.2, arriving for the third
time in one week — and it is the argument for the measurement channel rather than
for a bigger corpus.

**What this does to the published rate.** Nothing, on this host. The claim below is
unchanged and now rests on a rule that no longer asserts sufficiency from
necessity: a request the host was measured to refuse gets `UNSUPPORTED`, a size in
the untested band between the largest success and the smallest failure gets
`CONDITIONALLY_SUPPORTED`, and a profile that never measured the fact gets
`UNKNOWN` for anything above 4 GiB — the next power of two above anything this
corpus has observed.

## The reproducibility trap, and how it was avoided

`min_map_address` was once the probe's own ASLR slide recorded as a host fact,
and six campaign contracts returned a confident `UNSUPPORTED` off it. Sampling
the arena walks straight back into that: which windows are free depends on
where *this* process's libc landed.

Two rules keep it out of the profile:

1. **Bounds come from `max_user_address`** — a kernel constant, measured,
   identical in every process — and never from `/proc/self/maps`.
2. **A sample returning `EEXIST` counts exactly as one that succeeds.**
   `EEXIST` means the address is already held *by us*: proof that the kernel
   hands this part of the space out, and proof of nothing about the host.
   Treating it as a third outcome is what would make the recorded set depend on
   our own layout.

The split between granted and already-held goes to `notes`, which is outside
the facts subtree and therefore outside `profile_id`.

`tools/campaign/check_reproducible.sh` — which runs the probe as **two
processes**, because the in-process test was once green while this was false —
reports identical `profile_id`s.

# 7. After T-019: the 42% becomes information, and the reliance becomes a fact

Data: `campaigns/false-positive/2026-07-linux-x86_64-after-T019.json`

Section 2 ended with a decision owed: `RS-VM-0005` was correct on 42% of all
real mappings and unusable in a gate, and the choice was "either the rule needs
a condition (fires only when the program is observed to *use* the unrounded
size), or it belongs at `info` severity". The decision (T-019) is the synthesis
of the two, on the exact precedent of `accesses_beyond_eof`: the rule's own
precondition - it had always said "only a defect if the program relies on the
bytes past its requested size being unmapped" - became a declarable fact,
`relies_on_unmapped_beyond_size`.

```
declared      the caller states it depends on the unmapped tail. On a host
              that rounds, that guarantee holds in NO execution:
              UNSUPPORTED, severity high, and the finding names the bytes.
undeclared    the rounding is still true and still recorded - section 2
              rejected deleting it - but as an info note on a SUPPORTED
              verdict. What no caller gets any more is a condition nobody
              stated.
```

The re-measurement, same workloads, same host:

```
                              old rule (§5/§6)        after T-019
SHAPE    evaluated                 1292                   1292
         SUPPORTED             748  57.9%             1292  100.0%
         CONDITIONALLY_SUP.    544  42.1%                0    0.0%
         UNSUPPORTED             0                       0
         RS-VM-0005 emitted    544 (as condition)      544 (as info)

ADDRESS  evaluated                  639                    640
         SUPPORTED                    1   0.2%           639   99.8%
         UNKNOWN                    637  99.7%             1    0.2%
         UNSUPPORTED                  0                    0
```

Three things to read out of that, one of them a surprise:

- **The fact did not move.** 544 mappings still carry unrounded sizes and the
  finding is still emitted for every one - identical count, different verdict
  weight. Nothing was hidden to make a number look better: the conditional
  share went from 42.1% to 0 because the *claim* attached to the fact was
  wrong, not the observation.
- **The ADDRESS population flipped from 99.7% UNKNOWN to 99.8% SUPPORTED.**
  The earlier sections measured before the T-013/T-014 ladder landed in the
  probe; this re-run is the first time this document's own harness saw a
  profile that records the region where programs actually map. One request
  stays UNKNOWN. (640 vs 639: one more address requirement survived
  observation this run; the harness keeps only requests present in every run,
  so the population can move by ones.)
- **Not one false positive in either population** under the new behaviour, and
  one caller class gained a protection it never had: a guard-page scheme that
  declares its reliance now gets UNSUPPORTED before it ships to a
  64 KiB-granularity host, instead of the same `medium` condition every
  unrounded `read()` buffer produced.

The sink table also lost a lie in passing: the registry has always said
"`info` is never raised", and `adjust_severity()` raised it to `critical` for
fatal sinks anyway. The code now matches the published sentence, and severity
adjustment no longer applies to findings on a SUPPORTED verdict at all - a
fact has no failure for a sink to catch.

# 8. After T-018: the second operating system

Data: `campaigns/false-positive/2026-07-macos-14-arm64.json`, measured on a
`macos-14` Apple Silicon runner. Reproduce with the same
`tools/campaign/run_false_positive.sh`; the tracer is chosen by platform.

```
SHAPE  10 programs, 37 requirements, 0 rejected
       SUPPORTED                 37  100.0%
       CONDITIONALLY_SUPPORTED    0    0.0%
       UNKNOWN                    0    0.0%
       UNSUPPORTED                0    0.0%   <- FALSE POSITIVES
```

**Zero false positives on a second operating system.** The requirement
documents were transcribed from what the kernel did, exactly as on Linux; no
contract here was written by anyone.

## 8.1 Read the population before the rate

**37 requirements against Linux's 1292.** Thirty-five times smaller, and no
sentence in this document should be read as if the two numbers weigh the
same. Where the difference comes from, measured rather than guessed:

- **macOS's allocation path is not `mmap`.** The first round produced ONE
  `mprotect` and ZERO `mmap`s across an entire `python3` run. Real allocation
  goes through **mach traps** — `_kernelrpc_mach_vm_allocate_trap`,
  `_kernelrpc_mach_vm_map_trap` — which `dtrace`'s `syscall::` provider never
  sees. BSD `mmap` is the minority path on this OS. The observer watches both
  doors now, and that is what took the count from 10 to 37.
- **The dyld shared cache maps the system libraries in one operation.** On
  Linux, `ld.so` performs a separate `mmap` per shared object, and that is
  where most of the 1292 come from. The comparison is not
  program-against-program; it is loader-against-loader.
- **Only 4 distinct sizes appear** (16 KiB, 32 KiB, 272 KiB, 4.02 MiB) across
  10 programs, versus Linux's long tail. That is the same fact from the other
  side: fewer, larger, uniform allocations.

## 8.2 The finding that did not appear, and why that is a result

**`RS-VM-0005` fired zero times here**, on a host whose allocation
granularity is 16 KiB — four times Linux's. The obvious reading is that the
rule is broken on macOS. It is not:

```
37 of 37 requested sizes are exact multiples of 16384
0 unaligned sizes in the entire population
```

**The mach traps are handed already-rounded sizes.** `mach_vm_allocate` is
called by an allocator that has done the rounding itself, so the kernel never
sees the ragged request that `mmap(NULL, 53867, ...)` represents on Linux.
The rule has nothing to say because the situation does not arise on this
path — not because it fails to notice it.

This is worth stating next to §7: the 42% that forced T-019's decision is a
**Linux `mmap` phenomenon**, not a universal one. A gate calibrated on it
would have been calibrated on one OS's calling convention.

## 8.3 What this round cost, and what it caught

Four CI rounds, each one a measurement rather than a guess:

| Round | Question | Answer |
|---|---|---|
| 1 | does `dtrace` run under the runner's SIP? | SIP **disabled**; it runs. And `dtruss` is disqualified — it prints three arguments, and `MAP_FIXED` is `mmap`'s fourth |
| 2 | does the observer see anything? | ONE call per program. Wrong door |
| 3 | which door, then? | `mach_trap:::` — inventory published at `refs/measurements/e66cd64/mach-inventory` |
| 4 | is the mach layout right? | The raw records said no: a **decimal** flags word was being read as **hex**, and the misreading AGREED with the correct one on this data (`0x3C000001` and `0x1006632961` both have bit 0 set). A wrong reading that produces the right answer is the worst kind, and only printing the raw arguments beside the parsed line exposed it |

Two inversions the mach path required, either of which would have been a
silent, productive source of false findings:

- **`VM_FLAGS_ANYWHERE` is the opposite of `MAP_FIXED`.** On mach, "put it
  wherever" is a flag you SET. Reading it the Linux way round would have
  labelled every ordinary allocation an exact-placement demand.
- **`mach_vm_allocate_trap` carries no protection argument at all.** The
  requirement records the platform default AND declares in
  `extraction_limitations` that protection was not observed — a default
  presented as a measurement is the thing this project exists to object to.

## 8.4 What is still not measured

- **No `address` population on macOS.** Every observed allocation used
  `VM_FLAGS_ANYWHERE`, so there is no `MAP_FIXED`-equivalent request carrying
  a concrete address. The address rules remain unexercised against real macOS
  software — the same gap §1 records for Linux, arriving by a different route.
- **Three programs absent** from the runner (`php`, `redis`, `ffmpeg`), and
  the harness lists them rather than quietly covering less.
- **Windows is not measured.** The ETW feasibility round decoded a real
  kernel trace and found the events (`VirtualAlloc` / `VirtualFree`, with
  `BaseAddress`, `RegionSize`, `ProcessId`, `Flags`), so the instrument is
  identified and the observer is not written.
- **No false negatives, again.** Not one failing allocation in 10 programs ×
  3 runs. Same blind spot as Linux, for the same reason.

## Verdict on the criterion

`docs/PLAN.md` Phase 3, *expected false-positive rate is low on curated
examples* — **measured at 0 false positives on both populations: 1292 shape
requirements and 639 address requirements, with exactly one answered `UNKNOWN`.**

*(This line used to read "0 of 1933 … with 99.8% answered", a sum computed by
hand. §5's re-measurement moved both inputs by one and the sum went stale, and
`tools/guards/check_campaign.py` could not see it: the guard requires every
number a data file publishes to appear in the prose, and a total that appears in
no data file is outside what it can check. Quoting each population's own figure
instead puts the claim back inside the guard's reach — which is a better fix than
correcting the arithmetic.)*

Still `[partial]`, and the remaining gaps are named rather than hidden:

- ~~**One host, one OS.** Linux x86-64.~~ **Two operating systems now** (§8):
  Linux x86-64, 1292 requirements, and macOS 14 arm64, 37. Read §8.1 before
  weighing those together — the macOS population is 35x smaller, for reasons
  that are measured and are about the platform's loader, not about the
  harness. **Windows is still not measured**; its instrument is identified
  (ETW) and its observer is not written.
- **No false negatives are measurable by this method.** Not one failing `mmap`
  in 13 programs × 3 runs. The other half of correctness is untouched.
- ~~**`RS-VM-0005` still fires on 42% of real mappings** (§2). Correct, and a
  decision is owed before anyone gates a build on it.~~ **Resolved by T-019**
  (§7): the reliance is a declarable fact now, the conditional share is 0, and
  the finding is still emitted for all 544 as information.

ROADMAP **Gate B** likewise stays `[partial]`. The blocker has moved from
"never measured" to "measured on one platform, with the noisy rule named",
which is a different and much better problem.

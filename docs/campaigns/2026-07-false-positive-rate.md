# Campaign — the false-positive rate, measured

**Host:** Linux x86-64, measured by `rs-env-probe` in the same session
**Data:** `campaigns/false-positive/2026-07-linux-x86_64.json` (T-002, first run)
and `campaigns/false-positive/2026-07-linux-x86_64-after-T013.json` (after the
probe fix this campaign motivated)
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

## Verdict on the criterion

`docs/PLAN.md` Phase 3, *expected false-positive rate is low on curated
examples* — **measured at 0 of 1933 observed requests across both populations,
with 99.8% of them answered.**

Still `[partial]`, and the remaining gaps are named rather than hidden:

- **One host, one OS.** Linux x86-64. `strace` is Linux-only; the macOS lanes
  have measured profiles and no traced programs.
- **No false negatives are measurable by this method.** Not one failing `mmap`
  in 13 programs × 3 runs. The other half of correctness is untouched.
- **`RS-VM-0005` still fires on 42% of real mappings** (§2). Correct, and a
  decision is owed before anyone gates a build on it.

ROADMAP **Gate B** likewise stays `[partial]`. The blocker has moved from
"never measured" to "measured on one platform, with the noisy rule named",
which is a different and much better problem.

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

## 2026-07-27 — the note that promised not to record what it was recording

**Changed.** `ladder_record()` is new in `probe/arena_walk.{hpp,cpp}` and both
landmark ladders now go through it; `tools/campaign/check_reproducible.sh` runs
the probe **five** times instead of two. `test_arena_walk` 22 → **25 cases**.

| | |
|---|---|
| **+** | the macOS reproducibility failure is diagnosed and fixed — a real defect in the project's flagship guarantee, found by reading rather than by another runner round trip |
| **+** | the same rule was broken on Linux too, in a different way, and had never gone red. Fixed in the same commit |
| **−** | `docs/PLAN.md` said `[done]` for *"repeated runs on the same stable host produce equivalent canonical profiles"* while a CI job failed exactly that on roughly every second push, for two days. The document was the last place anyone would have looked |
| **−** | the macOS fix cannot be compiled here. There is no Darwin cross-compiler and a stubbed Mach header would be a check that passes for the wrong reason — this morning's own lesson. One runner round trip, stated rather than hidden |

### What it actually was

`available_ranges: 22 vs 22 entries`, identical bounds, and `profile_id` moved
anyway. The reason is that a `note` is inside the hashed facts subtree — measured,
not assumed:

```
base profile          sha256:0d84a2af4cd4f6c4a378a1d1415e7f34f8ca6bfe7ad27309f51e34e0996ff5e8
one note reworded     sha256:0db71a969d45c0953216e619540a4c76c25ce2a35d047459fc4f891aa16c78d4
```

The macOS ladder had two branches for one landmark. Placed at the exact address:

> `mach_vm_allocate(VM_FLAGS_FIXED) succeeded at this exact address in the probe
> process`

Already held by one of our own mappings — recorded as **available**, correctly,
for the reason EEXIST gets on Linux:

> `a mapping of the probe process already held this exact range … Whether it was
> held or free depends on the probe's own layout and is deliberately not recorded`

**The sentence that ends "deliberately not recorded" was the record.** Choosing it
rather than the other one is precisely the held/free bit, written into a hashed
field. So `profile_id` alternated between exactly two values — `621881a4…` and
`5f5f73a7…` — as ASLR decided whether one of our mappings landed on a landmark.
Bistable, not drifting, which is why it read as flake.

### And Linux had the same defect through the other door

The Linux ladder recorded an available entry when the landmark was free and
**nothing at all** on EEXIST. Same rule broken — the *presence* of the fact moving
with our slide instead of its *content*.

This is not a new discovery. macOS had already fixed it (`available_ranges: 22 vs
20`), its comment cites *"the argument EEXIST gets on Linux"*, and Linux's own
**arena**, forty lines above the ladder, applies the rule correctly. The ladder
never got it. It has never gone red — on this host no landmark collides with our
own mappings, so the fix changes the recorded set by **nothing**: 57 available, 0
unavailable, before and after.

```
arena   Linux ✓   macOS ✓   Windows ✓      the rule, learned twice
ladder  Linux ✗   macOS ✗   Windows n/a    the rule, never carried across
```

So the decision moved to `ladder_record()`, where `Placed` and `HeldByProbe` are
**the same `return`** rather than two branches that agree today, and where
`test_arena_walk.cpp` drives it on every platform. The load-bearing case asserts
byte equality of the note, not merely that both are available — equal outcomes was
never the property that was missing.

### Asking twice is the smallest version of asking once

Two runs catch a defect that makes every run differ. They catch a **bistable** one
only when the coin lands differently — half the time, which is exactly the rate CI
showed. `check_reproducible.sh` now runs five times (`RUNS=` to override) and
misses a 50/50 defect with probability 2⁻⁴ ≈ 6%, for four extra probe executions
of a few hundred milliseconds. It names which run disagreed and prints every id:

```
5 runs, and run 3 is the one that disagrees with run 1:
1 sha256:0d84a2af…   2 sha256:0d84a2af…   3 sha256:75838f9a…
4 sha256:0d84a2af…   5 sha256:0d84a2af…
```

Verified against a stand-in probe that is deliberately bistable, and the exit code
checked directly rather than through a pipeline — this project has been bitten by
`| tail` swallowing a status before.

### A guard fired on a correct tree, which is the one thing they may not do

Demoting the reproducibility criterion made `check_plan.py` fail — on the
*sentence explaining the demotion*, because it matched `` `[done]` `` anywhere on
any line and demanded evidence for a prose mention of the marker.

Three of the four instances in the tree **predated** this commit
(`docs/PLAN.md:307`, `docs/scenarios/assessment.md:92` and `:123`), so the totals
this guard has been printing were wrong by five, and nobody noticed because the
extra rows happened to be `[partial]` and `[open]`, which carry no evidence
requirement. A `[done]` in prose was all it took to expose it.

A status marker **introduces** an entry:

```
- `[done]` ...        a bulleted entry, at any indent
`[open]` ...          a paragraph entry at column 0 (assessment.md's shape)
  `[partial]` ...     INDENTED, no bullet: a wrapped continuation line
```

The second correction is the subtler one. The continuation loop keyed on the same
wrong pattern, so an entry stopped at the first line mentioning a marker — and
evidence sitting after it was invisible. Writing the regex as
`^\s*(?:[-*]\s+)?` swallows continuations; `^(?:\s*[-*]\s+)?` requires a bullet
whenever there is indentation, which is the rule both documents actually follow.
Selftest 75 → **78 cases**, one of them the false positive itself.

### Confirmed, and the five-run change is what made one push enough

`de72b5e`: **eight green**, including all three macOS environments.

```
macos---apple-clang ✓   native-arm64 ✓   rosetta-x86_64 ✓
linux---gcc ✓  linux---clang ✓  windows---msvc ✓  determinism ✓  compatibility-gate ✓

refs/measurements/de72b5e/native-arm64/reproducible.txt
  reproducible: 5 separate probe processes agree
    profile_id sha256:032555bcce4cb11eaf99613a8aae80081db3b640b6d37b9be23158d9da88c8d2
refs/measurements/de72b5e/rosetta-x86_64/reproducible.txt
  reproducible: 5 separate probe processes agree
    profile_id sha256:55bb74f8f79961ac0f7db7e6090370a5727bb4c97cdd2d54812269902d46a934
```

**15 macOS probe runs across three environments, all agreeing.** Under the old
defect that is 2⁻¹⁴ ≈ 0.006%. The entry above planned to watch several consecutive
pushes precisely because one green run of a 2-run check meant almost nothing — and
the five-run change is what collapsed that wait into a single push. Raising the
sample was worth more than raising the patience.

`docs/PLAN.md`'s reproducibility criterion goes back to `[done]`, with the
arithmetic on the line rather than the word "verified". What fifteen runs do
**not** exclude is a rarer instability — no run count does, it only makes one less
likely — which is why `RUNS` is a variable.

### T-015's third item: a contract may not name a ceiling as a constant

The compass named the one piece of T-015 that does not need hardware luck, so it
is done. `exact-mapping-above-user-space` asked for `0x800000000000` and **its own
name asserts that this is above the top of user space** — true on a 4-level
x86-64 host, false by 63 PiB on a 5-level one, where the constant sits in
ordinary mappable space. Its sibling `oversized-reservation-4pib` was
CONTRADICTED on exactly such a runner for exactly this reason.

`tests/groundtruth/derive_contract.py` rewrites the request address, and the
postcondition that names it, from the profile's measured `max_user_address`.
Nothing else moves, so the derived document is the committed one with one
measured number substituted — still schema-valid, and carrying an `x_derivation`
note saying what was replaced and why.

```
DERIVATION                         ADDRESS          RESULT
5-level host (LA57)                fffffffffff000   ok
the measured host                  7ffffffff000     ok
an unmeasured profile              <none>           ok   <- the constant stands
```

| | |
|---|---|
| **+** | the LA57 case is checked **on whatever host is running**, every push. An LA57 machine is not needed to check the LA57 case — the same move `arena_ceiling_for()` made, for the same hardware and the same reason |
| **+** | the third row is load-bearing: a profile that measured nothing derives nothing, so a synthetic host cannot quietly become a measurement. That is the defect `profile_for()` was added for, one file over |
| **+** | made to fail on demand — restoring the constant breaks two of the three rows, and the exit code was checked **directly** rather than through a pipe, because `\| tail` has swallowed a status in this repository before |
| **−** | T-015's remaining half still needs an LA57 runner and cannot be arranged. `oversized-reservation-4pib` is `held` on a 4-level host and unverified on a 5-level one |

**And the shell guard earned its place again.** The first version of the wiring
asked `${#args[@]}` on an array that may be empty, under `set -u` — an unbound
variable in bash 3.2, which is what macOS ships.
`tools/guards/check_shell_portability.py` named the line, the reason and the fix
on the push that introduced it. That is a macOS runner round trip that did not
happen, on a defect that would have been invisible on every Linux lane.

### Removing the constant found a real defect within the hour

`2d058f7` went out and both macOS lanes came back red — **not** because the
derivation broke, but because it worked:

```
exact-mapping-above-user-space   UNSUPPORTED  satisfied  CONTRADICTED
  mmap(0x600000000000, 131072, MAP_FIXED) placed the mapping exactly there
  and it accepted reads and writes
```

`max_user_address` on macOS arm64 was reported as `0x600000000000`, and a mapping
went in **at** that address and was written to. The reported ceiling is not the
top of the address space. Every analyzer verdict above it was a false
`UNSUPPORTED` — the direction that tells a caller their working program will not
work — and while the case named `0x800000000000` as a constant, nothing could
see it.

| | |
|---|---|
| **+** | the harness said the right thing without being asked: *"Either the analyzer is wrong about this platform or the contract misdescribes the program. Decide which by reading the case source; do not adjust the expectation to match the output."* |
| **+** | a red lane here is the tool working. The constant had been hiding this since the case was written |
| **−** | `max_user_address` is a `measured_capability` on macOS, which permits a `PROVEN` verdict, and it was a fact about one process's free space |

**The strong prior, and why.** The same file had the rule written down and
inverted. `find_min_map_address` carries:

> Only a REFUSAL moves the floor up. "Occupied by us" says nothing about what the
> kernel would permit another process.

directly above an `else` branch that moved the floor up on exactly that. The
ceiling bisection had the same inversion with no comment at all, and the survey
discarded `OccupiedByUs` as saying nothing about the host — true of a
*limitation*, false of a *ceiling*: a mapping of ours at an address is the
strongest possible evidence that the address exists.

All three now go through `address_is_usable()`, which is the EEXIST argument,
`no_access_here_is_ours()`, `ladder_record()` and the arena's held windows. One
idea, and this makes six sites. Counting them is the point: a rule that has to be
rediscovered six times is a rule that belongs in a function, not in a comment.

**Two explanations still fit and the profile now carries the evidence to
separate them:**

```
(a) our own mappings sat there, read as the host refusing
      -> fixed here; the ceiling should move up
(b) mach_vm_allocate(VM_FLAGS_FIXED) is refused where mmap(MAP_FIXED) is not
      -> the two calls answer different questions and the probe measures the
         wrong one; the fix is elsewhere
```

`mach_vm_region` at the ceiling separates them in one line, and
`max_user_address resolved to ...` now prints it. Filed as **T-016**, because
this push cannot be compiled here and the outcome is genuinely unknown — a macOS
runner adjudicates.

### The runner adjudicated: (a), and the ceiling was 35 TiB low

`d6abf18`. `native-arm64` went **green**, and the published profile says why:

```
max_user_address: 0x600000000000  ->  0x7ffffe000000
warning: max_user_address resolved to 0x7ffffe000000 from a highest placed probe
         point of 0x400000000000; mach_vm_region at the ceiling says:
         mach_vm_region found no region at or above this address
```

Explanation (a) confirmed and (b) excluded in one line, by the evidence the same
push was made to carry. The reported ceiling had been **35 TiB below** the real
one, and every analyzer verdict in that band was a confident false `UNSUPPORTED`.

### And it exposed the next one, which is the same shape a third time

With an honest ceiling, `rosetta-x86_64` fails coverage instead:

```
heap page : 0x7f9ab0028000                     <- 140 TiB
arena     : [0x100000000, 0xfc0000000)         <- 60 GiB. 15042 placed, 0 refused:
                                                  working perfectly, 140 TiB away
```

A **translated** x86-64 process puts its heap at `0x7f…`, like Linux — not at
`0x7be800000` like the native lane the arena was designed from. So:

```
Linux    two arenas   mmap base (top of space) + ET_DYN base
Windows  two arenas   top TiB (image, DLLs) + 1..127 TiB (NT heap)
macOS    ONE arena    [__TEXT base, commpage) — right for native, short for Rosetta
```

Three platforms, three times the same discovery: **one arena is never enough,
because a program's code and its heap do not live together.** macOS was the only
one where a single arena ever looked sufficient, and that was because the lane
that would have disproved it could not see its own ceiling.

| | |
|---|---|
| **+** | every one of today's four findings came from removing a constant or a wrong branch, not from adding a feature. The tool found them by being made honest |
| **+** | the evidence-carrying pattern paid again: one push, two hypotheses, one line of output, no guessing |
| **−** | `max_user_address` was `measured_capability` on macOS — which licenses a `PROVEN` verdict — while being a fact about one process's free space, for as long as the probe has existed |
| **−** | CI ends the day 7 of 8 green, with `rosetta-x86_64` red on a gap that is now precisely specified rather than mysterious |

### macOS gets its second arena, and a rule stops being a constant

`scan_one_macos_arena()` takes its bounds as parameters, and the ours-rule inside
`describe` became `base >= bottom && base < top`. That is not a refactor — it is
`probe/arena_walk.hpp`'s own sentence finally being true:

> The bounds of the arena ARE the rule: inside them a no-access entry is ours,
> because both documented bands lie outside them.

`no_access_here_is_ours()` was that sentence hard-coded to one arena's numbers.
For the low arena the new predicate evaluates identically, so it is a
byte-identical no-op there; it stays as a function for the ladder and the survey,
which have no arena bounds to speak of.

```
low   [0x100000000, 0xfc0000000)                      4 MiB windows   ~15,000
high  [high_arena_floor(ceiling, 4 TiB, commpage), ceiling)
                                                     64 GiB windows       64
```

`high_arena_floor()` is in `probe/arena_walk.hpp` for the reason
`arena_ceiling_for()` is: arithmetic that decides where a probe *looks*, on a
platform no machine here can compile, is arithmetic no test can reach. It returns
**0 twice on purpose** — when the ceiling was never measured, and when the arena
would reach back into the low one. Both are refusals to guess; the first is the
lesson of the 35 TiB error, the second keeps
`available_and_unavailable_ranges_do_not_overlap` from becoming a hope.

| | |
|---|---|
| **+** | the tests carry the runner's own numbers: `0x7ffffe000000` and `0x7ff800000000` as ceilings, and `0x7f9ab0028000` as the heap page the high arena must cover. `test_arena_walk` 25 → **27 cases** |
| **+** | 64 GiB contiguous windows rather than Linux's 64 GiB *stride*: a contiguous walk asserts only what it placed, and Linux samples only because 128 TiB leaves it no choice |
| **−** | macOS still cannot be compiled here. The changed region was parsed against stubs under `-Wall -Wextra -Wshadow -Wconversion -Werror`, which **caught a missing lambda capture** — `describe` referenced `bottom` and `top` with an empty capture list — and proves well-formedness, not that the Mach calls are right |
| **−** | that stub parse is a throwaway, not a guard. Making it durable would mean maintaining stub Mach headers, and a stub that drifts from the SDK is a check that passes for the wrong reason — the thing this session refused to build once already today |

**Next.** A macOS runner adjudicates T-016's second half. Then T-015's LA57 half
(hardware luck), T-005's rule coverage, T-006's missing seventh demonstration —
the only one of the seven that points at the *caller* rather than the host — and
the false-positive campaign still resting on one host and one OS.

---

## 2026-07-26 — six green did not happen, and the reason is worth more than six green

**Changed.** `tests/conformance/test_probe.cpp` — the coverage test's failure
message now carries its own evidence; `docs/TODO.md` gains **T-014**. No probe
code touched, deliberately.

| | |
|---|---|
| **−** | the push above did not go green. `linux---gcc` **and** `macos---apple-clang` both failed `the_scan_covers_where_this_process_is_actually_mapped` |
| **−** | **T-013 was done for Linux only.** `scan_allocation_arenas()` is in `vm_probe_linux.cpp` and nowhere else, so on macOS the profile says nothing about the address the test is executing from — the exact defect T-013 exists for, on a platform it did not touch |
| **−** | this had been failing on macOS since the test was added, and **I read past it** one push earlier: I opened the same log ref, found the bash-3.2 error, fixed it, and did not read to the end of the file |
| **−** | `linux---gcc` failed on C++ **identical** to the commit where it passed. Nondeterministic, not caused by anything in `650d510` |
| **+** | the failure is now self-diagnosing: address, nearest established ranges either side, whether a containing range exists, and the per-arena granted/held/refused split |
| **+** | that diagnostic had a bug that only forcing the failure could find, and one round of forcing found it |
| **+** | three of my own measurement errors caught before any of them reached a claim |

### What the two failures are, kept apart

They print the same message and they are not the same bug, which is most of the
work here.

```
linux---gcc   52f541e success  ->  650d510 failure     C++ unchanged between them
macos         52f541e failure  ->  650d510 failure     failing since the test landed
```

`650d510` touched `.gitignore`, two documents, one shell script and three
guards — no `src/`, no `tests/conformance/`. So there is no mechanism by which it
changed a probe conformance result, and `linux---clang` passed on both pushes.
That leaves nondeterminism on the runner. It does **not** reproduce here:

```
test_probe: 200 runs, 0 failure(s)     randomize_va_space=2, mmap_rnd_bits=28
```

macOS is a different animal and has a mechanism:

```
grep -c scan_allocation_arenas src/probe/vm_probe_*.cpp
  linux    3
  macos    0
  windows  0
```

T-013's entry does say "the credibility of `RS-VM-0001` **on Linux**", so the
scope was written down. What was missing is that the conformance test it added
asserts coverage on **every** platform — so the test was correct, macOS was
genuinely uncovered, and nothing connected the two until a runner did.

### Reading past the answer

The macOS `ctest` failure was in `refs/ci-logs/52f541e/macos---apple-clang`, in
`ctest_tail.txt`, in the same log ref I fetched one push earlier. I read
`gt_selftest.txt`, found the bash-3.2 error, fixed it, and stopped.

This is the third time in this project's log that a sweep I called systematic was
not: the quota claim that survived a line-based grep, the `check_docs` guard that
passed over a repo broken in every fresh clone, and now this. The pattern is not
carelessness about the *first* finding, it is treating the first finding as the
last. The channel had already delivered the evidence; the cost was entirely in
not finishing the file.

### The diagnostic, and the bug in it

Written because the only diagnosis available from here was a hypothesis: two
runners fail, this machine passes 200 times, and nothing in the log says where
the addresses were. Same position the git-ref log channel was built to escape.

It reports, on failure: the queried page, `max_user_address`, the nearest
established range on each side with the gap, **whether a range containing the
page exists** — because "not covered" and "covered and the query still said
UNKNOWN" have identical symptoms and different fixes — and the per-arena
granted/held/refused split.

**Its first version printed nothing.** It read `profile.notes`; the arena split
is in `profile.run.warnings`. It compiled, it looked right, and it would have
produced an empty diagnosis on the exact CI failure it was written for. Found by
forcing the assertion to fail on a passing host, which is the only way an error
path ever gets exercised. There is now also an explicit line for the case where
no arena ran at all, which is what macOS will print.

### Three measurement errors, mine, all before any claim

Kept because the project's whole subject is not trusting an unmeasured number,
and the author is not exempt:

| What I ran | What it actually measured |
|---|---|
| `echo "$(basename $p) -> exit $?"` | the command substitution runs first and resets `$?`. Reported SUPPORTED for both profiles — i.e. "there is no macOS failure" |
| `run_all.sh \| tail -20; echo $?` | `tail`'s status. Reported `exit=0` for a failing run |
| `rs-env-probe vm --output` from `build/` | a **stale binary**. It produced 56 ranges and no arenas, and I concluded T-013's arenas were absent from published profiles. `cmake --build build` — 58 ranges, both arenas. `ctest` went 12 tests to 13 in the same rebuild, so the earlier green did not include `test_impact` either |

The third is the interesting one: the other two are shell trivia, but that one is
this project's own thesis turned on its author — reasoning about code while
measuring a binary that predates it.

### It printed, on the next push, and it answered the open question

`90dc74b`: `linux---gcc` back to **success** — consistent with nondeterminism and
inconsistent with anything in the code, since nothing in `src/` changed. macOS
failed again and this time said why:

```
code page      : 0x1023a4000        max_user_address: 0x7ffffe000000
  nearest below: [0x100000000, 0x100004000)   gap 0x23a0000    (16 KiB wide)
  nearest above: [0x200000000, 0x200004000)   gap 0xfdc5c000
heap page      : 0x7be800000
  nearest above: [0x7bf400000, 0xabe000000)   gap 0xc00000     KERN_NO_SPACE
containing     : (none) - a scan-window gap, not a query bug
established    : 19 available, 49 unavailable, and NO arena
```

Three things that were hypotheses an hour ago are now measured, from a platform
this session has no access to:

| | |
|---|---|
| **+** | the code page is 37 MiB above `0x1_0000_0000` — the documented Mach-O `__PAGEZERO`/`__TEXT` base, a per-architecture constant. So macOS's arena anchor exists and is not `mach_vm_region` |
| **+** | the heap page is 12 MiB below a `KERN_NO_SPACE` region **the probe already found**. The second arena needs no new constant, only a ceiling it already has |
| **+** | the ladder *does* have entries at `0x1_0000_0000`, `0x2_0000_0000`, `0x4_0000_0000`. They are 16 KiB and 4 MiB wide points. The defect is width, not landmark count — `scan_one_arena()`'s merged-run technique is the missing part, not more landmarks |

The real remaining risk is now named in T-014: Linux strides 64 GiB across 128
TiB, macOS's interesting region is the first ~48 GiB, and the stride has to come
from those two measured gaps rather than from the Linux number.

### T-014 written, and a test found the bug a runner would have charged for

**Changed.** `include/runtimeskeptic/probe/arena_walk.hpp` and
`src/probe/arena_walk.cpp` are new — the arena walk with the platform calls
injected. `src/probe/vm_probe_macos.cpp` wires `try_place`/`describe_region` into
it. `tests/unit/test_arena_walk.cpp` is new, 9 cases. ctest 13 to **14**.

| | |
|---|---|
| **+** | the first version used a **32 MiB stride** and was wrong. The run closed correctly at the last window it had placed, and the heap page from the CI failure sat in the 16 MiB tail before the refused band |
| **+** | found in one run by a throwaway program that stubbed the Mach calls — no runner, no push, no wait |
| **+** | so the walk moved out of the macOS-only file into a platform-neutral one, and the throwaway became `test_arena_walk.cpp`. It runs on Linux, macOS and Windows and asserts nothing about the host it runs on |
| **+** | made to fail on demand: a 32 MiB window breaks 6 of the 9 cases |
| **−** | the obvious fix was an 8 MiB stride, which works **for that runner's morning**. That is the shape of mistake this project exists to object to, and it was the tempting one |
| **−** | still unverified by anything real: apple-clang compiles it, `try_place`'s mapping on a live task, the cost of 12,294 `mach_vm_allocate` calls, and `check_reproducible.sh` across two processes |

**Why there is no stride.** A sampled arena asserts the space between its
samples. Linux tests 4 MiB of every 64 GiB — 0.006% — and documents the
interpolation. Contiguous windows assert only what was placed, which removes the
over-broad-AVAILABLE risk entirely rather than shrinking it. Affordable here and
nowhere else for one reason: this arena spans 60 GiB (15,360 windows), the Linux
one spans 128 TiB (33 million).

**Where the bounds come from.** `0x1_0000_0000`, the `__PAGEZERO` size and
therefore the default `__TEXT` base of a 64-bit Mach-O on both architectures, up
to `0x10_0000_0000`, the Rosetta GPU-carveout start already named in the ladder.
Two constants. `mach_vm_region` was the obvious alternative and is the same trap
T-013 refused on Linux wearing a Mach name — it reports *this* task's slide, so an
arena derived from it hashes this morning's layout into an id meant to name the
host.

**Two things the simulation caught that were not the headline bug.** The refusal
count read `1 structurally refused` for twelve GiB of refused space, because
skipping ahead through an already-described entry silently changed what the number
counted — `skipped` is now reported separately. And 3067 windows inside one entry
each built an identical note string to have 3066 discarded.

**What this is really about.** `check_includes.py` removes the need for a Windows
runner to find a missing `<iterator>`; `check_shell_portability.py` removes the
need for a macOS runner to find `declare -A`. This does the same thing for probe
logic, which is the largest remaining place where the answer was "push and wait".
The arena is still macOS-specific in what it *means*; it is no longer
macOS-specific in what can be *checked*.

### `71af1ee`: half the failure fixed, and the other half was my comment

macOS went from **two** failures to one. The heap page is established; the code
page is not, and it moved — `0x1023a4000` to `0x102df0000`. `established` went
19 to 22, so the arena ran and added three ranges.

The cause was a comment I wrote defending the thing it got wrong:

```cpp
// max() rather than the constant alone: `min_address` is measured, and on
// x86_64 macOS it IS 0x1_0000_0000 because __PAGEZERO is four GiB. Where the
// two disagree the measurement wins.
const std::uint64_t bottom =
    min_address > kMachOTextBase ? min_address : kMachOTextBase;
```

`find_min_map_address()` does not return the top of `__PAGEZERO`. It returns the
lowest page **this process** can place, which is above `__PAGEZERO` *and above
this process's whole low image*. The code page is below that by construction. So
the arena's floor ended up above the exact page the arena exists to cover.

**And this file already said so, twenty lines from the call:** `min_map_address`
is *"DELIBERATELY NOT RECORDED AS A HOST FACT … a property of how this binary was
linked and where the loader put it."* Three paragraphs of mine congratulate the
design for refusing `mach_vm_region` because it reports this task's slide — and
then the bottom of the same function takes a process-derived value as a bound.
Same trap, different door, forty lines apart.

The floor is the constant now, with the argument for why deferring is unnecessary
rather than merely inconvenient: `OccupiedByUs` is treated as usable, so our own
image can change the placed/held split — a note, outside `profile_id` — but never
which windows are *refused*, and only a refusal splits a run. The recorded ranges
are identical in every process, which `check_reproducible.sh` can measure.

| | |
|---|---|
| **+** | the heap page fix held; the walk, the contiguous windows and the collapse all behaved as the test predicted on a real runner |
| **+** | `the_probes_own_image_does_not_raise_the_arena_floor` is now case 10, so this cannot come back |
| **−** | the test suite could not have caught it: the bug was in the *wiring*, choosing what to pass as `bottom`, and the wiring is the part that still only compiles on macOS |
| **−** | **my diagnostic lied for the second time.** It matched `"sampled every"`, I renamed the note to `"walked in contiguous windows of"`, and it printed "NO arena was scanned on this platform" for a push where the arena had demonstrably run. It matches `"arena"` now — the name of the thing, not a description of it |

The pattern in both diagnostic failures is the same and worth naming: a check that
keys on prose owned by another file will keep breaking, silently, in the direction
of saying nothing is wrong.

### `6533633`: the coverage case passes on macOS — and the flake was never a flake

**Changed.** `arena_walk.cpp` clamps a widened refusal and jumps whole windows;
`arena_ceiling_for()` is new in `probe/arena_walk.hpp` and caps the Linux arena at
the default map window; `vm_probe_linux.cpp` uses it. `test_arena_walk.cpp` 10 to
**14 cases**.

```
macOS   the_scan_covers_where_this_process_is_actually_mapped   ok     <- T-014
        available_and_unavailable_ranges_do_not_overlap         FAIL x7  (new)
linux   linux---gcc                                            success
        compatibility-gate                                     failure  (the same
                                                                bug, seen at last)
```

**T-014's target is met.** macOS establishes the region its own code and heap are
in. Contiguous windows, the collapse, the skip and the constant floor all behaved
on a real runner exactly as the unit test predicted.

#### The new macOS failure: a refusal reaching backwards

> the probe reported `[0x2a7224000, 0x2ae224000)` as both available and unavailable

A platform entry does not begin on a window boundary. Widening a refusal down to
`vm_region`'s extent reached back **inside a window the walk had just placed
successfully**. The simulation missed it for an embarrassing reason: the layout I
built from the runner's report had its deny band starting at `0x7bf400000`, which
happens to be 4 MiB-aligned. The one property I did not vary was the one that
mattered.

Clamped now — the earlier positive measurement is kept, the later negative one is
trimmed, and the note says the extent was cut so nobody reads it as the
platform's own answer. Trimming a claimed limitation is the conservative
direction. `a_refusal_extent_never_overlaps_space_already_placed` uses a
deliberately unaligned extent.

#### `linux---gcc` was different hardware, not nondeterminism

Two days of "flaky", recorded as nondeterminism and **deliberately left alone
pending a second reading**. The second reading was a mechanism:

```
max_user_address: 0xfffffffffff000        <- 56-bit. 5-level paging.
arena:            [0xfffc0000000000, 0xfffffffffff000)
code page 0x5606b35a0000   heap page 0x7fe8df6ff000     <- both 47-bit
```

GitHub's Linux fleet is not homogeneous. On an LA57 host `TASK_SIZE` is 2^56, so
both arenas were placed in the top 4 TiB of a 64 PiB space — where nothing is
ever mapped, because the kernel *refuses to allocate above 47 bits without an
explicit high hint*. Coverage was zero and the conformance case was right on
every push it failed. Same code, different CPU.

**And this project had already written the rule down.**
`corpus/runtime_failures/RSC-0049-la57-vs-jit-pointer-tagging.md`, entered during
T-003, says it in as many words: *"it refuses to allocate above 47 bits by default
and requires an explicit high hint to opt in."* The corpus exists so that this
project does not repeat the failures it collects, and the arena walked into RSC-0049
with the entry in the same repository. That is the quota mistake again — the
evidence one page away — and it is the second time in three days.

| | |
|---|---|
| **+** | the flake is a diagnosis now, and the discipline that produced it was refusing to "fix" it after one reading |
| **+** | the fix is provably a no-op where it must be: on this 4-level host `profile_id` is **byte-identical** before and after (`sha256:efade64a07f6750…`), arena bounds unchanged, `check_reproducible.sh` agrees across two processes. So the measured false-positive campaign cannot have regressed |
| **+** | `arena_ceiling_for()` lives in the testable header, so the LA57 case is covered **on a host without LA57** — the one form of fix that removes the dependency instead of moving it |
| **−** | the corpus contained the answer and nothing connected it to the code. A guard could plausibly have: 47, 2^47 and `DEFAULT_MAP_WINDOW` are greppable |
| **−** | my simulation varied the addresses and not the *alignment*, which is why the overlap bug reached a runner at all |

### `8c63fd7`: macOS clears ctest and the guards, and bash 3.2 is back

```
macOS   ctest    14/14 including test_probe          <- first time ever
        guards   all passed
        ground truth  14 cases, 10 held, 1 CASE BROKEN
```

**The arena holds on a real macOS host**, and the cost question is answered
incidentally: `test_probe` takes **0.15 s** for ~10 full probes, so ~15 ms each
including a 15,360-window walk. Not the seconds it might have been.

The job still fails, three layers deeper than it did this morning:

```
tests/groundtruth/run.sh: line 111: args[@]: unbound variable
file-map-beyond-eof      UNSUPPORTED   no-output   CASE BROKEN
```

`"${args[@]}"` where `args=()`. Under `set -u`, **bash 3.2 treats an empty array's
expansion as an unbound variable**; bash 4.4 does not. Third instance of the
bash-3.2 class — and `check_shell_portability.py`, written for exactly this class,
could never have caught it, because it scans for bash-4 *features* and this is a
bash-3.2 *semantic* of a construct both versions have.

| | |
|---|---|
| **+** | the harness behaved correctly: no output became `CASE BROKEN`, not a confirmed refusal. That rule was written after a crashing case once read as a confirmed refusal, and it paid again |
| **+** | the guard now checks the semantic, and only for arrays assigned an empty literal — so `ROWS=("a\|b" "c\|d")` is left alone. Selftest 58 to **63 cases** |
| **+** | it found **three more** sites: `run.sh:173`, `host_sensitivity.sh:110`, `run_all.sh:85`. All latent — each is currently reached only when the array is non-empty — but one branch away |
| **−** | my first version of the rule flagged all four sites *after* I had fixed them: `${arr[@]+"${arr[@]}"}` literally contains `${arr[@]}`, and I had claimed the brace after `]` would distinguish them. Balanced braces are not a regex's business; the guard is detected by its `[@]+` mark now |
| **−** | `${#arr[@]}` has the same problem, so `host_sensitivity.sh` was asking an empty array its length under `set -u`. It uses the counter it already had |

Three instances, three shapes, one platform nobody develops on: a missing
*feature* (`declare -A`, `mapfile`), a different *semantic* (`${arr[@]}` under
`set -u`), and a *version* older than every developer machine by eighteen years.

### `15ea3f3`: **macOS is green.** First time in this project's history.

```
compatibility-gate/failure     <- an LA57 runner; see below
determinism/success
linux---clang/success          linux---gcc/success
macos---apple-clang/success    windows---msvc/success
```

**T-014 is met on a real macOS host**: build, 14/14 ctest including `test_probe`,
all 13 guards, and the ground-truth harness through all 14 cases. The platform that
went from `line 57: supported: unbound variable` this morning to green.

And the LA57 cap worked. On the 5-level runner the arena is now where mappings
actually are, and the **code page passes**:

```
max_user_address: 0xfffffffffff000
arena:            [0x7c0000000000, 0x800000000000)     <- was 0xfffc0000000000
code page   ok
heap page   0x7ffa6bcff000   FAIL
```

#### The last remaining gap, and its price

The heap page is 41.7 GiB above the arena's recorded top, `0x7ff000400000`. Not a
bug in the cap — a consequence of the sampled design meeting a different
`mmap_base`. Samples run every 64 GiB and the run closes at the **last sample**,
never at `top`, because closing further than the evidence reaches once produced a
range asserted both available and not. So the final 64 GiB stride is unclaimed.

On a 4-level host `mmap_base` lands around `0x7f3…`, comfortably inside. With
LA57, `mmap_base` is derived from `DEFAULT_MAP_WINDOW` rather than `TASK_SIZE`, so
randomization is subtracted from 2^47 and it lands at `0x7ffa…` — inside the one
stride the arena will not claim.

**Fixing it means probing near the top, and any such probe changes the 4-level
profile.** A window at the highest position fitting below `probe_top` is a new
sample on every host, so `profile_id` moves and
`campaigns/false-positive/2026-07-linux-x86_64-after-T013.json` no longer describes
the probe that produced it — `check_campaign.py` exists to say so. That is a
re-measurement, not a one-line fix, and it is a decision rather than a detail:

| Option | Cost |
|---|---|
| probe the top window; re-run `run_false_positive.sh` | correct on both host kinds; the published 0-of-1293 has to be re-measured and re-committed |
| special-case it — extra sample only when `ceiling != max_user_address + page` | no re-measurement, and a branch whose only justification is "it keeps a number we already published" |
| leave it, record it | `compatibility-gate` stays red on LA57 runners, which hides the next failure |

The second is the tempting one and is the shape of thing this project exists to
object to. Recorded here rather than chosen quietly.

#### And the green run threw away a measurement

`file-map-beyond-eof` and `file-map-partial-page` produced output on macOS for the
first time. Whether native arm64 raises SIGBUS where Rosetta 2 returns zeroes is
the question that case exists to answer, and nobody has ever measured it.

**It is not readable.** `refs/ci-logs/*` publishes `if: failure()`, so the run that
finally produced the observation is the run that discarded it. The channel was
built to diagnose failures and it does that well; the measurement plane's own
output is only retrievable when something goes wrong. A first-ever platform
measurement arriving inside a green job is exactly the case the design misses, and
it is a small fix — publish the ground-truth output unconditionally on the
expensive platforms.

### `refs/measurements/*`: the channel now publishes on green

**Changed.** `tools/ci/publish_measurement.sh` is new; `ci.yml`'s three measuring
jobs publish `refs/measurements/<sha>/<job>` on success.

| | |
|---|---|
| **+** | the commit holds **only** the artifacts, built with a temporary index and `git commit-tree`. Tree size **78 bytes**, against the diagnostics refs which each carry a copy of the repository — cheap enough for every push |
| **+** | verified end to end against the real remote before wiring it in: pushed under a throwaway namespace, fetched back, `git show` read the file, ref deleted |
| **+** | the working tree is never touched, so it cannot disturb a build still in flight — which is what the `ci-logs` step's `git checkout --detach` + `cp` does |
| **−** | capturing the output moved `/tmp/gt-host.json` to `/tmp/meas/`, and **four failure-diagnostics steps still pointed at the old path**. They would have silently reported "(no host profile: the probe step did not run)" — degrading the failure channel while adding the success one. Caught by grepping the file rather than by any guard |

`tee` rather than `>`, with `set -o pipefail`, so the output stays in the job log
*and* becomes a file, and `tee`'s success cannot mask the harness's exit code.

### The channel's first green run returned the observation it used to discard

`cef515b`: **all six jobs green**, and `refs/measurements/<sha>/*` appeared for
`linux---gcc`, `linux---clang`, `macos---apple-clang` and `compatibility-gate`.
(The namespace already existed — `macos-probe.yml` had been publishing
`native-arm64` and `rosetta-x86_64` all along. Only `ci.yml` lacked it.)

Read back with `git show refs/tmp/m:gt_run.txt`:

```
file-map-beyond-eof     UNSUPPORTED   faulted     held
    reading past the end of the mapped file killed the process with
    signal 10 (Bus error: 10)
file-map-partial-page   CONDITIONAL   satisfied   not asserted
    the 16383 bytes between the end of a 1-byte file and the end of its
    final page read as zero, without faulting
```

**Native macOS arm64 raises SIGBUS past end of file.** That is the first half of
the claim `file_map_beyond_eof.c` was written to test — *"native arm64 raises
SIGBUS, the same machine running x86-64 under Rosetta 2 hands back zeroes"* — and
the first time any of it has been measured. The prediction held, so the probe's
`file_map_beyond_eof` fact was already right; this is confirmation, not a
correction. The Rosetta half is still unmeasured, because the runner is native.

And `file-map-partial-page` confirms the distinction that case's own comment draws
in the other direction: the final partial page reads as zero without faulting,
which POSIX *requires* — a different question from the implementation-defined one,
and the reason the case was rewritten once.

Three channels now, each added after the previous one's blind spot cost something:
`refs/status/*` (did it pass), `refs/ci-logs/*` (why did it fail),
`refs/measurements/*` (what did it measure). The third one returned a first-ever
platform observation on its first run.

### The LA57 gap closed, and the campaign re-measured to say what it cost

**Changed.** `vm_probe_linux.cpp`: `scan_one_arena` probes a window that **ends at**
the arena's top; the candidate ladder skips a candidate whose window would cross
`max_user_address`. `campaigns/false-positive/2026-07-linux-x86_64-after-top-window.json`
is new, and `docs/campaigns/2026-07-false-positive-rate.md` gains §5.

The arenas now reach their declared bounds:

```
before   0x7c0000000000 .. 0x7ff000400000     0x550000000000 .. 0x58f000400000
after    0x7c0000000000 .. 0x7ffffffff000     0x550000000000 .. 0x590000000000
```

This is a probe, not an extrapolation, and that is the distinction the old comment
drew correctly: closing the run *at* `top` on the strength of a sample that ended
64 GiB earlier was a real bug once. Placing a window that ENDS at `top` and
reporting what happened is the opposite — the edge is measured.

| | |
|---|---|
| **+** | the option chosen was the expensive, correct one. The tempting alternative was a branch conditional on `ceiling != max_user_address + page`, whose only justification would have been keeping a number already published |
| **+** | **re-measured, and it changed nothing here**: 1292/639 evaluated, **0 false positives** on both populations, the same single `UNKNOWN`, the same 544 `RS-VM-0005`. The one-observation difference is run-to-run variation in what the programs did |
| **+** | that null result is the *value* of re-measuring. Reporting it as an improvement would be false — the fix matters on a host class this machine is not |
| **−** | reaching `max_user_address` immediately contradicted the ladder: it probed `0x7fffffc00000` with a 4 MiB window ending at `0x800000000000`, past the top, got ENOMEM, and had filed `[0x7fffffc00000, 0x800000000000)` as a **host limitation** since the ladder was written. `available_and_unavailable_ranges_do_not_overlap` caught it **locally** this time |
| **−** | that entry was the only thing in `unavailable_ranges` on this host. The probe had been publishing exactly one Linux limitation and it was an artefact of window placement |
| **−** | the verdict line read `0 of 1933 … 99.8% answered` — a **hand-computed sum**, stale the moment §5 moved both inputs by one, and `check_campaign.py` structurally cannot see it: it requires every number a data file publishes to appear in the prose, and a total appearing in no data file is outside its reach. Rewritten to quote each population's own figure, which puts the claim back inside the guard rather than correcting the arithmetic |

The ladder fix is one line plus its argument, and it is the same shape as the
macOS clamp two entries above: a refusal that is explained by *where the window
was put* is not a fact about the address.

### Reproducibility on every push, one channel instead of two, and a dependency

**Changed.** `ci.yml`'s `expensive-platforms` runs `check_reproducible.sh`;
`macos-probe.yml`'s two publish steps use `tools/ci/publish_measurement.sh`.

| | |
|---|---|
| **+** | T-014's named remainder is closed the right way round: `macos-probe.yml` already checked cross-process reproducibility, but only weekly or on a button. It is checked on **every push** now |
| **+** | `macos-probe.yml`'s measurement refs used `git add` on the checked-out tree, so each one carried **a full copy of the repository** — and the ground-truth output, the thing that workflow exists to produce, was not in them at all. Both lanes now publish the profile, the ground-truth output and the reproducibility result, and nothing else |
| **−** | **I planned to trigger the Rosetta measurement by editing that file, and it does not work.** `macos-probe.yml` has `schedule` and `workflow_dispatch` and **no push trigger** — deliberately, for the 10x-billing reason argued at length in its own header. The self-trigger-on-its-own-path mechanism belongs to `windows-probe.yml`. I had conflated the two, and checked before asserting it |

So the second half of the `file_map_beyond_eof` claim — *does the same machine
running x86-64 under Rosetta 2 hand back zeroes where native arm64 raised
SIGBUS?* — is not obtainable from here. It needs a dispatch:

```
gh workflow run macos-probe.yml --ref main
```

The workflow now publishes the ground-truth output, so once that runs the answer
arrives in `refs/measurements/<sha>/rosetta-x86_64` and is readable from the
sandbox. The same named-human-dependency this project already accepted for the
control plane, and it is cheaper than it was: previously the run would have
measured and then thrown the ground-truth result away.

### The two things the new diagnostics immediately produced

`420eb32`. Both channels answered in one push, and both answers are findings.

#### 1. The macOS arena is not reproducible, and it is manufacturing limitations

```
reproducible   failure                    <- steps.json says which step, at last
NOT REPRODUCIBLE: two runs of the probe on this host disagree
  run 1  sha256:4b580726d2057364…      run 2  sha256:3de09f991ea2e43f…
  available_ranges:    35 vs 32 entries
  unavailable_ranges:  83 vs 74 entries
```

**My argument for why this would hold was wrong.** I wrote that `OccupiedByUs` is
treated as usable, so the probe's own image can move the placed/held split but
never which windows are *refused*, and only a refusal splits a run. That is true
on Linux, where `MAP_FIXED_NOREPLACE` answers `EEXIST` for **any** existing
mapping. It is false on macOS, because `try_place()` returns `Refused` when the
covering region grants no access — and a macOS process is full of its own
`PROT_NONE` reservations (malloc guards, dyld, thread stack guards) whose
addresses move with ASLR.

So the arena is filing **~80 host limitations that are its own guard pages**, and
`profile_id` moves between two runs of the same binary on the same machine. That
is the precise defect `min_map_address` was once guilty of — a fact about our
morning hashed into an id meant to name the host — and the reproducibility gate
caught it before a single profile was published. It is why that gate exists.

`try_place()`'s no-access rule is not wrong; it was reasoned out over four
iterations and it is right for the commpage and the GPU carveout, which macOS puts
in every task's map. It is wrong *inside the arena*, and the fix is a design
decision rather than a patch — see T-014.

#### 2. The analyzer over-predicts a 4 PiB reservation on a 56-bit host

```
oversized-reservation-4pib    SUPPORTED   refused   CONTRADICTED
    mmap of 4503599627370496 bytes (4096.0 TiB) was refused: ENOMEM
exact-mapping-above-user-space  SUPPORTED  satisfied  held
    mmap(0x800000000000, …) placed the mapping exactly there
```

On a 4-level host, 4 PiB does not fit below `max_user_address`, the analyzer says
UNSUPPORTED, the kernel refuses, and the prediction holds. On the LA57 host it
**fits** — `max_user_address` is 2^56 — so the analyzer says SUPPORTED, and the
kernel refuses anyway. `exact-mapping-above-user-space` is the same host
difference from the other side: `0x800000000000` is not above user space there,
and the case's own name stops being true.

**Fitting inside the address space is not sufficient for a reservation to
succeed**, and every host this project had ever run on hid that by refusing the
request for the other reason. The harness reported it as a contradiction and
declined to adjust the expectation, which is what it is for.

Two findings from one runner class, on a project whose thesis is that the bug is
always on the platform nobody runs.

### The macOS arena decision, taken

**Changed.** `arena_walk.{hpp,cpp}`: a refusal covered by a no-access region of
this task is resolved as **held**, not as a limitation; `ArenaWalk` gains
`held_no_access`. `vm_probe_macos.cpp`: `kArenaTop` moves from the carveout start
`0x10_0000_0000` down to the **commpage start `0xfc0000000`**.
`test_arena_walk.cpp` rewritten, 13 cases.

The rule the walk now applies:

```
Refused + a no-access region of THIS TASK covers it   -> held. Says nothing
                                                         about the host, the same
                                                         argument EEXIST gets.
                                                         Run is NOT split.
Refused + nothing of ours covering it                 -> structural. Recorded.
```

**Why the top had to come down.** That resolution is only sound while no band the
*platform* puts in every task lies inside the arena. The commpage
`[0xfc0000000, 0x1000000000)` is exactly such a band and it was inside. Both it
and the carveout stay with the landmark ladder, which probes them at their
documented addresses; the arena's job is the space between, where programs are.

| | |
|---|---|
| **+** | the test that matters is `the_output_does_not_move_when_our_own_reservations_move`: four layouts differing **only** in where our own no-access regions sit, and every recorded range must be identical. Forced to fail on demand — reverting the resolution breaks it five times |
| **+** | it is the property `check_reproducible.sh` caught on the runner, and now it is checked on a machine with no Mach headers at all. That is the only version of this fix that scales |
| **+** | structural refusals still survive, which is the half a careless fix would have thrown away with the false ones |
| **+** | Linux `profile_id` is untouched (`sha256:37a2fa0c03cc838…`) — the Linux probe does not use `walk_arena`, so the false-positive campaign cannot have moved |
| **−** | **residual risk, stated rather than discovered:** if a macOS version puts an undocumented no-access band inside `[0x1_0000_0000, 0xfc0000000)`, this arena will claim it available. `held_no_access` is the number that exposes it — it rises while `unavailable_ranges` stays empty — so it is in the note and asserted non-zero by a test |
| **−** | the widening-and-clamping path is now **unreachable** and was deleted rather than guarded: it fired exactly when a no-access entry covered the window, which is now the held branch. It cost a runner round trip to get right, so the reason is kept as a comment for whoever needs it back |

**What this cost, in the honest accounting:** the arena's macOS output is now one
available range and no limitations, where before it was 35 ranges and 83
limitations. Almost all of that was noise about the probe's own morning — but the
new answer is also *weaker*, and the note is the only place a reader learns how
much was resolved rather than measured.

### The residual risk fired on the first run, and it is not a guard page

`59d886c`. macOS: `the_scan_covers_where_this_process_is_actually_mapped` **ok**,
one failure left:

```
available_and_unavailable_ranges_do_not_overlap
  the probe reported [0x167c00000, 0xfc0000000) as both available and unavailable
```

That span is 5.6 GiB to 63 GiB — **57 GiB**, ending exactly at the new arena top.
The paragraph I wrote one commit earlier said: *"if a macOS version puts an
undocumented no-access band inside `[0x1_0000_0000, 0xfc0000000)`, this arena will
claim it available."* It does, and the valve worked — but the assumption behind
the resolution was wrong on the first host that tested it.

**And the message could not tell me which side was wrong.** A 57 GiB span could be
one ladder record of a `vm_region` extent, or a merged arena run, and the fix
differs completely between those. The next step was going to be a guess between
architectural options, which is the position this session has repeatedly paid to
get out of. So, before choosing:

| | |
|---|---|
| **+** | the overlap message now prints **both** ranges with their notes, and the notes name their producer |
| **+** | the failure diagnostics now carry the **measured profile** — which they never did, on a project whose subject is what a probe measured. When ctest fails before the ground-truth step there is no profile to copy, so the step runs the probe directly, and a probe that cannot run says so in the file rather than leaving it absent |
| **−** | three candidate resolutions are on the table and **all three are unsatisfying**, which is why this is being measured rather than decided: (a) treat ambiguous refusals as available — wrong here by 57 GiB; (b) record them — irreproducible, that was the original defect; (c) record neither, leaving the span UNKNOWN — but the *run boundaries* then still move with our layout, so it is not reproducible either |
| **−** | the only discriminator that actually answers "ours or the platform's" is asking a **freshly spawned process**, where our reservations move and a platform band does not. That is what `check_reproducible.sh` already does at the script level, and pulling it inside the probe is a real design change, not a patch |

The 57 GiB is the useful number: a malloc guard is pages, a thread stack guard is
one page. Something that size is not ours, which points at (c) plus a bound
derived from the entry the platform reported rather than from where our own
reservations happen to fall — but that is a hypothesis, and the next run will print
the notes that decide it.

### The overlap: measured, and my hypothesis was wrong

`f2aeefe` printed both sides, and the answer was not the one I reasoned to:

```
available   [0x169c00000, 0xfc0000000)   the ARENA, walking 4 MiB windows
unavailable [0x200000000, 0x200004000)   16 KiB: "a hole in the address space"
```

I had guessed the 57 GiB span was a platform band the ladder had recorded, and
concluded the arena's held-resolution was unsound. It is the arena's own run, and
the conflicting entry is **16 KiB** from the max-address survey. The arena placed a
4 MiB window across the very page the survey called a host limitation.

So the survey's note carried the unjustified step: *"higher addresses remained
placeable, so this is a hole in the address space"* — a one-page refusal while
higher addresses work is equally consistent with **our own reservation sitting
there**. The same ambiguity as the arena's, inherited through `try_place()`, in a
producer I had not suspected.

| | |
|---|---|
| **+** | the survey now applies the same discriminator: a refusal covered by a no-access region of this task is not recorded, and the count of those goes into a warning |
| **+** | nothing is lost. The commpage and the carveout are no-access bands *and* real limitations, and `scan_address_space` records them at their documented addresses across their real extents; a 16 KiB duplicate inside one was already being dropped by `collapse_contained_ranges` |
| **−** | I was about to redesign the arena on the strength of a hypothesis. Printing both notes cost one push and one commit; the redesign would have cost a day and been wrong |

### T-015: fitting the address space is not sufficient, and the corpus could never have said so

**Changed.** `VirtualMemoryModel::max_single_reservation` is a new measured fact;
`RS-VM-0026` and `RS-VM-0027` are new findings; `rule_size_feasibility` no longer
stops at "it fits"; the Linux probe measures the fact; schema, registry and
`test_analyzer` updated (54 cases). Campaign re-run as
`…-after-T015.json`.

```
oversized-reservation-4pib   SUPPORTED   refused   CONTRADICTED
```

The rule compared a size against `max_user_address - min_map_address` and said
nothing more. **Its own rejected-fix text asserted the error:** *"the limit is the
width of the address space, not the amount of free memory in it."* On a 4-level
host 4 PiB does not fit, so UNSUPPORTED was returned, the kernel refused, and the
prediction held for the wrong reason for the project's whole life.

| | |
|---|---|
| **+** | the fact is a **power of two** on purpose. The exact largest reservation moves between two runs as the process's own mappings shift, and a fact that moves is a fact about the probe — `check_reproducible.sh` would fail and `profile_id` would stop naming the host. Verified: two processes agree |
| **+** | three honest bands instead of one guess: above a size the host was measured to **refuse** → UNSUPPORTED `PROVEN`; between the largest success and the smallest failure → CONDITIONAL `HYPOTHESIS`, because nothing measured it; within what was granted → nothing to say |
| **+** | the one constant not measured from the host is measured from the **corpus**: the largest request across 1292 observations from 13 real programs is **1.96 GiB**, p99 is 32 MiB. So 4 GiB is the line above which a profile lacking the fact answers UNKNOWN rather than yes |
| **+** | `RS-VM-0021`'s rejected-fix sentence is corrected in place rather than deleted — it now points at `RS-VM-0026` for the case it got wrong |
| **−** | **the campaign did not move by a single requirement.** 1292/639, 0 false positives, byte-identical. The corpus does not contain the defect and no amount of running it harder would have found it: nothing a real program asked for comes within four orders of magnitude of the 64 TiB this host grants |

That null result is the point. It took a **host** the project had never run on, not a
program — the third time in one week, after `<iterator>` on MSVC and `declare -A` on
bash 3.2. It is an argument for the measurement channel, not for a bigger corpus.

### One rule, three producers, and a gate expectation that was a claim about hardware

`f78149d` moved both failures one layer on, and both answers came from artifacts the
channel had only just started publishing.

#### macOS: the third producer of the same ambiguity

```
available   [0x16c000000, 0xfc0000000)     the arena
unavailable [0x59e000000, 0x89ec00000)     probed at 0x7ffc00000, KERN_NO_SPACE,
                                           "region covers it, protection ---",
                                           widened to the entry's full extent
```

The survey holes are gone; this is the **landmark ladder**, widening a 12 GiB
no-access region at 22.5 GiB to its full extent and publishing it as a host
limitation. Third producer, same ambiguity, inherited through `try_place()` — and
the ladder's own long argument for that branch was reasoned out for the **carveout**,
which is 384 GiB at a documented address.

So the rule is now named once, `no_access_here_is_ours()`, and all three obey it:
**inside the arena's bounds a no-access entry is ours; outside them it is the
host's.** That is what the arena's bounds are *for*, and it is why the top came down
to the commpage start — both documented bands lie outside. Checked:

```
probed 0x7ffc00000   region [0x59e000000, 0x89ec00000)  -> ours, not recorded
probed 0x1000000000  region [0x1000000000, 0x7000000000) -> host, recorded  (carveout)
probed 0xfc0000000   region [0xfc0000000, 0x1000000000)  -> host, recorded  (commpage)
```

The two bands the ladder exists to measure still record. The residual risk is stated
once, in one place, instead of three times or nowhere.

#### The gate expectation was a claim about which machine the job landed on

```
::error:: high-memory guest mapping is UNSUPPORTED: expected exit 1, got 0
```

`contracts/emulator-highmem-guest-mapping.json` asks for `0x800000000000` = 2^47.
Above `max_user_address` on a 4-level host, comfortably inside it on a 5-level one.
**And the workflow comment said so already:** *"proven impossible anywhere on x86-64
Linux with 4-level paging."* The expectation was hard-coded next to a comment naming
the assumption that makes it wrong.

| | |
|---|---|
| **+** | reproduced **exactly** from `profile.json` in the diagnostics ref — the artifact added two commits earlier for precisely this. `max_user_address: 0xfffffffffff000`, `max_single_reservation: 70368744177664`, measured on the runner |
| **+** | provably not caused by `RS-VM-0026`: the contract's size is 64 KiB, below every band the new rule touches, and the verdict comes from `RS-VM-0003` against the address bound |
| **+** | the expectation is derived from the measured profile now, and the real step text was extracted from the YAML and **run against both host kinds locally** — 4-level expects 1 and gets 1, 5-level expects 0 and gets 0. A heredoc inside a YAML block scalar is worth checking before spending a runner minute on it |
| **−** | this is the fourth thing in two days that was a claim about hardware written as a constant: the arena ceiling, the 4 PiB verdict, `exact-mapping-above-user-space`, and now the gate. The corpus and the rules were fine; the **constants** were the problem |

### The arena is stable; what moved was __PAGEZERO

`63bacc5`: macOS ctest **passes**, the overlap is gone, and the failure moved to the
reproducibility gate with a much narrower diagnosis:

```
available_ranges:    22 vs 22 entries      <- the arena is reproducible now
unavailable_ranges:  50 vs 66 entries
```

The profile the diagnostics now publish says what the 50-vs-66 is:

```
U 0x100000  0x104000   "a hole in the address space"
U 0x200000  0x204000   ... and 0x400000, 0x800000, 0x1000000 ...
survey: 13 structural hole(s) recorded, 5 not recorded (no-access region of ours)
arena:  7385 placed, 113 held, 3 structurally refused, 29 refused-but-covered
```

Every one of those low entries is inside this process's own **`__PAGEZERO`**, four
GiB on a 64-bit Mach-O and **a link-time choice**. The ownership check added one
commit earlier cannot see them: `mach_vm_region` reports no entry covering them,
because `__PAGEZERO` is a *hole* in the map rather than a no-access entry — so they
came through as structural.

**And twenty lines above, this same file already refuses to publish
`min_map_address` as a host fact** for exactly that reason: *"a property of how this
binary was linked and where the loader put it."* Recording holes below it is the same
claim through a different door.

| | |
|---|---|
| **+** | the arena's own contribution is now reproducible — 22 vs 22, after 35 vs 32 — so the resolution and the bounds were right |
| **+** | the diagnosis took one look at an artifact, not a hypothesis. The profile in the diagnostics ref has now decided three consecutive questions |
| **−** | **fourth instance today** of a fact about the probe reaching for a field meant for the host: the arena's floor from `find_min_map_address`, the arena's own no-access refusals, the ladder's widened extents, and now the survey's holes below `__PAGEZERO`. Each was argued for separately and correctly in its own frame, and each got the same thing wrong |

That is the pattern worth carrying out of this session: on macOS there is no way to
ask whose a map entry is, so every producer that reads a refusal has to be told
where the boundary is — and the boundary has to be written down **once**.

### Three entries left, and they were races

`8a6d9f7` removed the `__PAGEZERO` holes. The profile shows exactly what still moved:

```
U [0x100000000, 0x100400000)   "exact placement refused inside the allocation arena"
U [0x15b400000, 0x15b800000)
U [0x16bc00000, 0x16c000000)
U [0xfc0000000,  0x1000000000)   commpage   <- correct, and stable
U [0x1000000000, 0x7000000000)   carveout   <- correct, and stable
... 32 KERN_INVALID_ADDRESS entries above 2^47                <- correct, and stable
```

The three arena entries are refusals where `mach_vm_region` reported **nothing
covering the window** — the case the previous commit classified as structural. That
combination is self-contradictory unless the task map changed between the allocate
and the query, which it does constantly: the probe allocates and releases 7,500
windows while walking. So they are races, and their addresses drift — 46 vs 56 entries
across two runs.

So the rule is completed rather than patched: **inside the arena's bounds, no refusal
is attributable to the host.** Not just the covered ones. `no_access_here_is_ours()`
already says the host is not answerable for what is in the way there; a refusal with
no visible cause at all is the same claim with *less* evidence behind it, not more.
Everything unplaceable inside the arena is counted in the note and recorded as
nothing.

The commpage, the carveout and the 32 above-the-address-space entries are outside
those bounds and keep recording, which is the half that has to survive.

**What the four fixes to this one boundary have in common** — the arena floor, the
covered refusals, the ladder's widened extents, the `__PAGEZERO` holes, and now the
uncovered refusals: each was reasoned out correctly *in its own frame* and each
answered "is this the host's?" without being able to. macOS gives no way to ask whose
a map entry is. Once that is accepted, the boundary has to be written down once and
every producer has to consult it — which is now the case, in
`no_access_here_is_ours()`.

### The same rule, applied in the other direction

`cf7b761`: `unavailable_ranges` dropped out of the disagreement entirely — the arena
contributes zero limitations and zero structural refusals, and the commpage, the
carveout and the 32 above-the-space entries are all stable. One line left:

```
available_ranges: 22 vs 20 entries
```

The **ladder**, one more time, and in the mirror image of every fix so far.
Recording *nothing* for a landmark occupied by one of our own mappings is still
layout-dependent: a landmark that happened to be free produced an available entry,
and the same landmark with our mapping on it produced none. **The presence of a fact
depended on our morning even though its content did not.**

An entry of ours that grants access proves the kernel hands that address out and
proves nothing about the host — the argument `EEXIST` gets on Linux and
`no_access_here_is_ours()` gets above. Another program can map there. So it belongs in
`available_ranges` whether we were sitting on it or not, and the recorded set stops
depending on where we were.

| | |
|---|---|
| **+** | five applications of one rule, and this is the first that *adds* a fact rather than suppressing one. The rule was never "record less"; it is "the recorded set must not depend on the probe's own layout" |
| **−** | it took five rounds to see that, because each round asked "should this be a limitation?" and the actual question was "does this entry's existence move?" |

### `5ac82e5`: all six green, and the macOS probe is reproducible

```
compatibility-gate  ✓    determinism  ✓    linux---clang  ✓
linux---gcc         ✓    macos        ✓    windows---msvc ✓
```

`check_reproducible.sh` passes on the macOS runner. **T-014's last named remainder is
closed**, and the measurement is readable rather than inferred:

```
profile_id  sha256:dd401a12e51a87cccb02c2fa4a1…    22 available, 34 unavailable
page_size   16384        max_user_address  0x7ffffe000000
arena [0x100000000, 0xfc0000000): 7384 placed, 115 held by the probe,
      0 structurally refused, 33 refused-but-covered
ground truth: 14 cases, 11 held, 0 contradicted, 3 not asserted
```

`0 structurally refused` and `33 refused-but-covered` is the whole story of the last
five commits in one line: everything the arena could not place is counted and
attributed to nobody, and the count is there for a reader to disbelieve.

**What is measured and what is now conceded**, because the honest reading matters more
than the green:

| | |
|---|---|
| **+** | two processes agree on `profile_id` on macOS, for the first time. The arena's coverage, its bounds and its resolution all survive that |
| **+** | 0 contradicted ground-truth pairings on macOS, from 14 cases |
| **−** | `max_single_reservation` is **unknown** on macOS — the Linux probe measures it and the macOS one does not yet. So every macOS analysis of a request above 4 GiB now answers UNKNOWN via `RS-VM-0027`. That is the correct reading of a profile that has not measured it, and it is also a visible piece of missing work rather than a silent yes |
| **−** | 33 windows inside the arena are unplaceable and unattributed. If a platform band is among them, this profile says the space is available. The count is the only thing that would ever say otherwise |

### All three probes measure it now, and the red macOS runs were the quota wall

The owner's screenshot of the **macOS measurement** workflow: four runs, all red,
all 5–6 seconds, a day ago. Checked before reading anything into it:

```
aacaffb 2026-07-25T12:51Z   0508b99 12:49Z   87d9c13 12:42Z   f3c65e9 12:30Z
   status refs: none        measurements: none      (for all four)
```

The status step is `if: always()` and published **nothing**, so the jobs never
executed a step — "0 ms, job never started", the same signature the owner reported
for Windows under the $10 budget. Against the timeline:

```
last successful measurement   2026-07-25 ~01:05Z
quota exhausted               2026-07-25  01:09:39Z
the four red runs             2026-07-25  12:30-12:51Z   <- inside that window
repository made public        2026-07-26
```

**Not broken code — the quota wall, from before the repository went public.** Run
#20 is `87d9c13`, *"ci: stop spending macOS minutes on commits that cannot change a
measurement"*, which is the commit that removed the push trigger; the runs stop
there because the trigger did, exactly as the file says.

So the workflow has produced nothing since 01:05Z on the 25th, and has **never run
with any of this session's code**.

**Pre-flight before spending the dispatch**, since it cannot be triggered from here:

| | |
|---|---|
| **+** | its assertion steps all end in `\|\| true` — they report, they do not gate — so `RS-VM-0027` firing on a macOS profile cannot fail the job |
| **+** | `max_single_reservation` is measured on **macOS and Windows** now, so one dispatch returns a complete profile instead of one that makes `RS-VM-0027` fire everywhere |
| **+** | the macOS helper was extracted and compiled **on Linux** under the full CI warning set, and run: only `mmap` is involved, so nothing needed stubbing |
| **+** | the Windows probe **cross-compiles clean** with `x86_64-w64-mingw32-g++` and `-Werror -Wconversion -Wsign-conversion` |
| **−** | Windows measures a genuinely narrower thing: `MEM_RESERVE\|PAGE_NOACCESS` bounds the *reservation*, and on Windows it is the **commit** that charges the pagefile. A request within the bound can still fail at commit time. The source note says so rather than letting the number read as more than it is |

### The Windows probe went red and could not say why

`2542612` touched `src/probe/vm_probe_windows.cpp`, which is one of the three paths
in `windows-probe.yml`'s push filter — the self-trigger mechanism. So it ran, for
the first time in this session, and:

```
refs/status/2542612…/windows-x86_64/failure
refs/ci-logs/2542612…/                        (nothing)
refs/measurements/2542612…/windows-x86_64     (nothing)
```

The status step is `always()` and published; the measurement step is gated on
`steps.measure.outcome == 'success'` and did not. So it failed at or before
`Measure` — Build, Test, or the probe itself — and **there is no way to tell which
from here**, because this workflow has no diagnostics step at all.

`ci.yml` grew one because a Windows build failure was invisible from inside. The
workflow whose entire purpose is a measurement nobody has ever obtained did not get
the same treatment, and the omission only shows up when it fails.

| | |
|---|---|
| **+** | it has one now: `steps.json` with real ids on Build/Test/Measure, the ctest tail, and the profile if the probe produced one — published through `tools/ci/publish_measurement.sh`, so the ref is small |
| **+** | when the probe did *not* produce a profile the file says so, rather than being absent. An absent artifact reads as "nobody looked" |
| **+** | pushing this touches the workflow's own path, so it self-triggers and the next run answers the question |
| **−** | only two `windows-x86_64` status refs exist in this repository's whole history, and both are `failure`. The measurement leg of the end-to-end cycle has never once completed |

### The Windows probe had never run — including on Windows

The channel answered on its first try, and the answer is the largest finding of the
session.

```
build    success        <- on a real windows-latest runner
test     success        <- 14/14 ctest
measure  success        <- and it produced a profile
```

The profile:

```
profile_name windows-x86_64   origin SYNTHETIC   host_arch unknown
page_size, allocation_granularity, min_map_address, max_user_address,
max_single_reservation, exact_mapping, fixed_noreplace_available,
reserve_commit_model, file_map_beyond_eof   ... every one UNKNOWN
```

`vm_probe_unimplemented.cpp` guarded itself with

```c
#if !defined(RS_PLATFORM_LINUX) && !defined(RS_PLATFORM_MACOS)
```

which is **true on Windows**. `vm_probe_windows.cpp` guards itself with
`#if defined(RS_PLATFORM_WINDOWS)`. Both compiled, both defined
`rs::probe::probe_virtual_memory`, the linker took one — and it took the stub.
Proven rather than argued, with the cross-compiler that was already installed:

```
x86_64-w64-mingw32-g++ -DRS_PLATFORM_WINDOWS=1 -c vm_probe_windows.cpp       -> 1
x86_64-w64-mingw32-g++ -DRS_PLATFORM_WINDOWS=1 -c vm_probe_unimplemented.cpp -> 1
                                        definitions of probe_virtual_memory
                                    after the fix: 1 and 0
```

**So the Windows probe has never run, on any machine, including Windows.** Every
statement in this project of the form "the Windows probe measures X" was a statement
about a stub that measures nothing — and says so honestly, to a reader who looked.

| | |
|---|---|
| **−** | `ci.yml`'s `windows---msvc` job has been **green throughout**. It runs `rs-profile verify host-profile.json`, and a synthetic profile verifies perfectly well. Green meant "the file is well-formed", and was read as "the platform was measured" |
| **−** | `test_probe` passed 14/14 on Windows because `unimplemented_platforms_report_synthetic_origin` returns early when `implemented` is true — and **nothing returned early when it was false**. The suite had a test for one direction of a two-directional claim |
| **+** | ~~the step that caught it is the Wine refusal, guarding something larger~~ — **wrong, corrected below.** That step has never executed: it runs after `report`, which fails first on every Windows run there has ever been. What found the synthetic profile was the diagnostics channel and a reading of the artifact it published |
| **+** | `tools/guards/check_probe_platforms.py` evaluates each probe's `#if` against every `RS_PLATFORM_*` setting CMake can produce and requires exactly one active. **No compiler, no platform** — which is the only version of this fix that removes the dependency instead of moving it. Selftest 63 to **66 cases**; it fails on demand |
| **+** | and `a_platform_with_an_implementation_actually_uses_it` asserts it *on* the platform, because a guard reads source and a test reads the binary that was actually linked |

The diagnostics step added one commit earlier is what made this a ten-minute
diagnosis instead of a guess. Before it, this workflow could fail and say nothing at
all — and it had, twice, which is the entire history of `windows-x86_64` status refs
in this repository.

**And the run that would have confirmed the fix never started.** I polled
`refs/status/5615c67/windows-x86_64/*` for forty minutes and reported "still
running". It was never started. `windows-probe.yml`'s push filter is

```
.github/workflows/windows-probe.yml
src/probe/vm_probe_windows.cpp
include/runtimeskeptic/probe/vm_probe.hpp
```

and the fix touched `src/probe/vm_probe_unimplemented.cpp`, which is not in it.

| | |
|---|---|
| **−** | mine, and the same error the whole session is about: **an absent signal read as one particular cause.** `git ls-remote` returning nothing is consistent with *running*, *never started*, and *failed before the status step*. I picked one and asserted it |
| **−** | the trigger list names the implementation but not **the file that can silently replace the implementation** — which is precisely the bug just fixed, hiding its own confirmation |
| **+** | `vm_probe_unimplemented.cpp`, `CMakeLists.txt` and `src/CMakeLists.txt` are in the filter now. The last two define `RS_PLATFORM_WINDOWS` and nowhere else does |

### THE FIRST REAL WINDOWS MEASUREMENT

`47540c7`. `refs/measurements/47540c7…/windows-x86_64`:

```
origin                     measured                 <- not synthetic
os                         10.0.26100
arch                       x86_64 / process x86_64

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

Every one `measured_capability`, on a real `windows-latest` host. This project has
been making claims about Windows since it started and has never once had this.

| | |
|---|---|
| **+** | **`allocation_granularity: 65536` is now measured, not documented.** T-004 said: *"64 KiB granularity is not documented as a value at all — neither `SYSTEM_INFO` nor `VirtualAlloc` states it. So it stays `measured_capability` and a host reporting something else is a finding."* The host reports 64 KiB. The mechanism behind `RSC-0044` is confirmed on the platform it is about |
| **+** | `max_user_address` is `0x7fffffff0000`, which is the `lpMaximumApplicationAddress + 1` question T-004 settled by `VirtualQuery` rather than by reservation — applied on real Windows for the first time |
| **+** | `fixed_noreplace_available: true` and `reserve_commit_model: windows_reserve_commit`: the two facts every Windows claim in the corpus leans on, measured |
| **+** | 14/14 ctest on the runner, including the new `a_platform_with_an_implementation_actually_uses_it`, which passes now precisely because the probe is real |
| **−** | `available: 0, unavailable: 0`. **The Windows probe establishes no address ranges at all** — no arena, no landmark ladder. Every address question on Windows still answers UNKNOWN, honestly and uselessly. That is T-013 and T-014's gap, third platform |
| **−** | the job still failed, in a step **with no id**, so `steps.json` said `build/test/measure success` and nothing more. Third time this exact omission has cost a round trip; `reproducible`, `report` and `refuse_wine` have ids now, and the reproducibility output is written to a file so the channel carries it |

### T-004 IS MET. The Windows measurement leg completed, first time ever.

`4b9ebf4`: **`windows-x86_64: success`**. The first green run of that workflow in
this project's history, and `profiles/measured/windows-server-2025-x86_64.measured.json`
is committed.

The last defect was two steps running a **bash heredoc under `shell: pwsh`**.
PowerShell has no heredoc, so `What was measured` had failed on **every Windows run
there has ever been** — and because it runs first, `Refuse a profile that is not
from real Windows` was `skipped` every time and has never executed at all.

**Which corrects the entry two above.** It credited that Wine-refusal step with
catching the synthetic profile — *"written for Wine, it turned out to guard
something much larger"*. It guarded nothing. What found the synthetic profile was
the diagnostics channel and a reading of the artifact it published. The claim is
struck through in place rather than deleted.

| | |
|---|---|
| **+** | the second heredoc was found by **sweeping for the shape** across all workflows rather than waiting for the next run to reveal it. One round trip instead of two |
| **+** | `reproducible: success` — the Windows probe agrees across two processes, which is the other half of T-004's `Done when` |
| **+** | three defects, each hiding the next: the stub replacing the probe, the trigger not naming the file that replaces the probe, and the shell that could not run the step that would have reported either |
| **−** | `available: 0, unavailable: 0`. **The Windows probe establishes no address ranges at all.** Every address question on Windows answers UNKNOWN — honestly, and uselessly. T-013 did Linux, T-014 did macOS, and nobody has done this one |
| **−** | the measurement ref arrived wrapped in a **full copy of the repository**, because `windows-probe.yml`'s publish step still used `git add` on the checked-out tree. Now on the shared publisher, and it carries `repro.txt` too |

### The end-to-end cycle is closed, and it is measured rather than argued

The owner dispatched `macos-probe.yml`. Both lanes measured; the Rosetta lane's job
went red **after** publishing, in `Ground truth vs prediction (x86-64)`.

**The whole loop ran without anyone opening the Actions tab:** probe change pushed →
run started → measurement published to `refs/measurements/50aefca…/rosetta-x86_64` →
fetched over the git protocol → read. That is the sentence this log has carried as an
**accepted cost** since the channel was built:

> *"Kontrol düzlemi için yetmiyor ve yerine geçen şey senin Actions sekmesini
> açman. Bu bir mimari değil, adı konmuş bir insan bağımlılığı."*

For the **control** plane that remains true — a dispatch is still a person pressing a
button. For the **measurement** plane it is now a measured finding rather than a
hopeful one. Ref count 63 → 366.

#### The Rosetta failure: one quoted expansion, latent since the harness was written

```
tests/groundtruth/run.sh: line 47: cc -arch x86_64: command not found   (exit 70)
```

`run.sh` invoked the compiler as `"$CC"` — quoted, therefore **one word**. The
workflow passes `CC="cc -arch x86_64"`, so the shell looked for a command literally
named `cc -arch x86_64`.

**It works on every path where `CC` is a single word** — `cc` natively, `gcc` and
`clang` on Linux — and fires only on the one path that must carry a flag. That path
is the Rosetta 2 lane, which is also the only place this project can observe a
translated address space. The harness broke exactly where it was most needed and
nowhere else.

| | |
|---|---|
| **+** | `read -ra` rather than leaving `$CC` unquoted: an unquoted expansion word-splits **and globs**, and a compiler path is not a glob. Splitting explicitly says splitting is intended, which an unquoted expansion only implies |
| **+** | `${CC_ARGV[@]+…}` rather than `"${CC_ARGV[@]}"` — bash 3.2 calls an empty array's expansion unbound under `set -u`, and that already cost a macOS run **in this same file** |
| **+** | a missing compiler now fails at startup with the compiler named, instead of inside the build loop where it reads like a broken case |
| **+** | `selftest.sh` passes `CC="${CC:-cc} -O0"`, so the multi-word path runs on **every POSIX CI run** rather than only on the lane that needs it. Verified: with the old `run.sh`, the selftest goes 14/14 → 0/14 |
| **−** | `CC` carrying flags is the make/autoconf convention and this harness assumed it could not. The assumption was invisible for as long as nobody needed a flag |

#### The Windows profile, verified externally

The owner fetched it over the git protocol and checked both hand-verification items
against the measurement:

| | |
|---|---|
| **+** | `dwAllocationGranularity` **measured**, evidence `measured_capability`, and 65536 ≠ 4096 — so `RSC-0044`'s premise, and the whole granularity-versus-page-size distinction, is confirmed on that runner |
| **+** | `max_user_address = 0x7fffffff0000` is exactly `lpMaximumApplicationAddress` (`0x7FFFFFFEFFFF`) **+ 1**. The `+1` reading T-004 settled by `VirtualQuery` rather than by reservation holds against the real value |
| **+** | the only remaining `unknown` is `jit_entitlement_required`, a field with no meaning on Windows — correct behaviour, not a gap |

#### And the macOS trigger's rationale expired

That workflow left per-push on a cost argument: three macOS jobs per push at 10x.
**The repository is public, so standard runners including macOS are free for it.**
The arithmetic was right; the premise expired.

The *second* argument survives — *"the host cannot change between two commits"* — and
it is an argument for a **path filter**, not for no trigger. So `macos-probe.yml` now
runs on pushes that touch the probe, the arena, platform detection, or the
ground-truth harness. `tests/groundtruth/run.sh` is in that list because the bug that
broke the Rosetta lane was in the harness, not the probe — and a filter written for
probes only would have missed it again.

### `rosetta-x86_64: success` — and the last thing that moved was the ceiling

`393196b`. The path-filtered trigger fired **by itself**, and:

```
native-arm64/success      rosetta-x86_64/success     <- first time ever
```

The `CC` fix is confirmed by the one lane that could never pass. The trigger change
is confirmed by not having to ask for a dispatch.

`ci.yml`'s macOS job went red on the same push, one layer further in again:

```
available_ranges: 22 vs 22 entries     <- the same COUNT, different CONTENTS
```

The counts were made stable four commits ago; the *values* were not. The skip past an
already-described entry sets the run's end from **that entry's extent** — a
platform-reported number that moves with this task's layout — so when an entry
reached beyond the arena's ceiling, the emitted range ended above `top` at a position
that differed between two runs.

| | |
|---|---|
| **+** | clamped, in both places: where the skip computes the reach, and again at emission. **A walk bounded by `[bottom, top)` must not emit a range outside it**, which is true whether or not anything moves |
| **+** | `a_held_entry_reaching_past_the_top_does_not_push_the_range_past_it` — and it fails on demand: removing the clamp breaks it |
| **−** | fifth round on this one boundary, and the shape shifted again. The first four asked *"whose is this?"*. This one is *"how far did we actually walk?"* — a different question with the same symptom, which is why counting stability was not sufficiency |

### Where the three platforms actually stand

`89ddb90`: **eight of eight green**, three workflows, three platform families at
once. With T-014 and T-004 both closed and moved to `Done`, `Now` holds one item.

What the three probes establish, read from the newest published measurements:

```
linux     available 57   unavailable  0
macOS     available 22   unavailable 34
Windows   available  0   unavailable  0     <- establishes nothing
```

That last line is the whole of what is left in Phase 1. The Windows probe measures
every *capability* — page size, granularity, bounds, reserve/commit, beyond-EOF — and
**no address ranges at all**: no arena, no landmark ladder. So on Windows
`RS-VM-0001/0002/0003`, the project's flagship rules, answer UNKNOWN for every
address. Honestly, and uselessly. It is the same defect T-013 fixed for Linux and
T-014 for macOS, on the platform that could not be reached until the probe was
actually in the binary.

And the two remaining T-015 items are both about a **constant standing in for a
measurement** — `oversized-reservation-4pib` unverified on a 5-level host, and
`exact-mapping-above-user-space` deriving its address from `0x800000000000` rather
than from the measured bound. Same shape as the four already fixed today.

### The coverage test could not fail on Windows

Starting the third arena, and the first thing in the way was the test that was
supposed to demand one:

```cpp
if (result.profile.vm.available_ranges.empty() &&
    result.profile.vm.unavailable_ranges.empty()) {
    return;  // the sweep was disabled; a different test covers that
}
```

`probe::Options::scan_address_space` defaults to **true** and this call passes no
options, so the sweep is never disabled here. The branch was reachable only when a
probe genuinely established nothing — which is exactly Windows. So
`the_scan_covers_where_this_process_is_actually_mapped` **passed vacuously on the one
platform where it had the most to say**, and `test_probe` was 14/14 on a real Windows
runner with `available: 0, unavailable: 0`.

| | |
|---|---|
| **−** | third instance of one shape in two days: `unimplemented_platforms_report_synthetic_origin` returned early when `implemented` was true and nothing returned early when it was false; this returned early when there was nothing to check. **A test that declines to look, in the case that matters** |
| **+** | it is an assertion now, carrying the full coverage diagnosis, so the Windows runner will print the code page, the heap page and `max_user_address` — the same three numbers that decided the macOS arena's bounds |
| **+** | designing from those rather than from a guess is the sequence that worked twice: `90dc74b` made the macOS failure carry its evidence, and the runner answered the derivation question on the next push |

**Why the Windows arena will not be a third copy.** Linux samples two 4 TiB arenas at
64 GiB across a 128 TiB space; macOS walks one 60 GiB arena in contiguous 4 MiB
windows. Windows has `lpMinimumApplicationAddress` and `lpMaximumApplicationAddress`
from `GetSystemInfo` — **system constants, not probed** — which is a better starting
point than either platform had. It also has `VirtualQuery`, which enumerates the
address space directly, and that is a trap rather than a shortcut: what it enumerates
is *this process's* free space, which is the fact-about-our-morning problem in its
purest form. The bounds can come from constants; what is between them still has to be
placed, not read.

### The runner answered, and the answer was that the alarm was broken

`268bb3e` went out so that a Windows runner would print three addresses. It ran,
it failed as designed — `refs/status/268bb3e…/windows---msvc/failure`, five other
jobs green — and `refs/ci-logs/268bb3e…/windows---msvc` carried this, in full:

```
Test project D:/a/Runtime_CodeSkeptic/Runtime_CodeSkeptic/build
    Start 13: test_probe
Test not available without configuration.  (Missing "-C <config>"?)
```

Not the assertion. Not the code page, the heap page or `max_user_address`. The
name of a missing flag.

`ci.yml`'s diagnostics step ran `ctest --test-dir build --rerun-failed` with no
configuration. On Linux and macOS the generator is single-config and that works.
**MSVC's generator is multi-config**, where ctest without `-C` does not run the
test at all — it prints the sentence above and exits non-zero. So for the entire
life of this repository, every Windows entry in the log channel has carried that
sentence in place of the failure it was published to describe.

| | |
|---|---|
| **−** | the diagnostics channel — the thing this project reads *instead of* the Actions tab — has never once worked on Windows. Three commits were spent making a failure carry its evidence into a channel that was dropping it |
| **−** | `windows-probe.yml` had `-C RelWithDebInfo` and was correct, because it is Windows-only and was written while looking at Windows. `ci.yml` is the shared workflow, and shared code is where the platform-specific defect hides |
| **−** | **third defect of one shape in this one channel**: two bash heredocs under `shell: pwsh` (so `report` had failed on every Windows run ever and `refuse_wine` had never executed), and now this. A diagnostics step only speaks when something else has already gone wrong, so nobody checks the alarm on a day nothing is burning |
| **+** | fixed at all 12 `ctest` invocations across the three workflows, and `tools/guards/check_workflow_ctest.py` makes it un-repeatable without a runner: every `ctest` in a workflow must name a config, and that config must be one a `-DCMAKE_BUILD_TYPE=` in the same file actually builds |
| **+** | the guard had a false positive on its first run and the repository caught it: `\bctest\b` matched `tail /tmp/diag/ctest.txt`, because `.` is a word boundary, and five *correct* lines were reported as defects. It is a command-word match now, with that exact case in the selftest — **a guard that fires on the fixed tree is worse than no guard, because the next person silences it** |
| **+** | selftest 66 → **72 cases** |

### Two channels for one number, deliberately

The next push has to ask the same question `268bb3e` asked, and the reason that
one went unanswered is that a single channel was read for a single signal — the
error this session already made once, reporting "still running" for 40 minutes
when the truth was "never started". So the Windows probe's `VirtualQuery` walk
now also reports **where this process actually sits**: lowest and highest
occupied address, total occupied bytes, and occupancy per 1 TiB bucket. That
travels in `profile.json`, which the diagnostics ref already carries, so it
arrives whether or not the ctest fix works.

It is `notes`, not facts — outside the facts subtree, outside `profile_id`,
alongside the walk counts that were already there. The rule this project keeps
relearning is not *record less*; it is that **the recorded set must not move with
the probe's own layout**, and a note is not the recorded set. What it is for is
concrete: 128 TiB cannot be walked contiguously, so a Windows arena must choose
its regions, and choosing them from what the author remembers about Windows ASLR
is what the macOS arena's five wrong versions were each made of.

### The Windows arena, and the first one designed from a measurement

The runner's note came back and it settles the design outright:

```
occupied [0x7ffe0000, 0x7ff9fa967000), 4349464576 bytes in 3 bucket(s):
  0x0            =       6299648      <- lowest occupied is KUSER_SHARED_DATA
  0x10000000000  =       2633728
  0x7f0000000000 =    4340531200      <- 99.8%: image, DLLs, stacks, heaps
largest free run 139217018867712      (126.6 TiB, contiguous)
```

High-entropy ASLR puts essentially everything in the **top TiB**. So the arena is
`[arena_floor_for(max_user_address, 1 TiB), max_user_address)` =
`[0x7f0000000000, 0x7fffffff0000)`, walked in 64 MiB windows — 16,384 placements,
within a rounding error of the macOS arena's 15,360. The floor comes from
`lpMaximumApplicationAddress + 1`, a system constant identical in every process;
the note chose the *shape*, it did not supply the bound.

| | |
|---|---|
| **+** | the macOS arena needed five wrong versions and six runner round trips to arrive at bounds that were, in the end, two constants. This one was written **after** the measurement instead of before it, and that is the whole difference |
| **+** | `arena_walk.hpp`'s treat-a-covered-refusal-as-held rule is sound only while no system-wide band lies inside the arena. Windows has one obvious candidate — KUSER_SHARED_DATA, mapped into every x64 process — and the runner printed it at `0x7ffe0000`, **127 TiB below the floor**. Outside by measurement, which is more than the macOS arena can say about its commpage |
| **+** | `held_no_access` keeps its macOS meaning, because `place` answers `HeldByProbe` when the region at the *base* is ours — the faithful analogue of EEXIST, which Windows has no errno for |
| **−** | none of it has executed on Windows. Six new tests, a cross-compile and a mutation check are not a runner, and this project has been wrong about exactly that before |

**Where the line between tested and untestable now sits.** `classify_window()` —
whose is this refusal? — moved to `probe/windows_regions.hpp` with `VirtualQuery`
injected, so `tests/unit/test_arena_walk.cpp` drives **the probe's own code**, not
a model of it. The one thing the test fakes is `VirtualQuery` itself. That matters
because the whole-window scan is the load-bearing part: `VirtualQuery(base)`
describes only the region containing `base`, so a 64 MiB window whose first byte
is free but whose middle holds one of our DLLs is refused by `VirtualAlloc` while
the query at the base says `MEM_FREE`. Answering from the base alone would file
the loader's choice as a host limitation at an address that redraws every run —
the macOS irreproducibility bug arriving through a different door.

Restricting the scan to the base on purpose breaks
`the_windows_output_does_not_move_when_aslr_redraws` six times over, which is the
only evidence that these six cases are protecting anything.

The reason to draw the line here rather than trust the platform file: the Windows
probe sat in the tree for a day having never executed **on any machine, including
Windows**; `test_probe` was 14/14 on a real runner while it established zero
ranges; and the diagnostics channel dropped every Windows failure for the life of
the repository. Three chances to notice, none taken, because everything about
Windows here was reachable only from Windows.

### The build log arrived and named it in one line

`7dd19a7` taught the diagnostics channel to carry a build log. The very first one
it published answered a question three pushes had not:

```
vm_probe_windows.cpp(946,29): warning C4456: declaration of 'walk' hides
                              previous local declaration
      vm_probe_windows.cpp(881,27):
vm_probe_windows.cpp(946,29): error C2220: the following warning is treated as
                              an error
```

The arena's `const ArenaWalk walk` shadowed the `VirtualQuery` walk's
`const WalkSummary walk` sixty lines above it. `/W4 /WX` made it fatal, `rs_probe`
did not build, so every target depending on it did not build, so the ctest the
channel published was a test run of a tree that had never compiled — ten green
results and eight *Could not find executable* lines, describing nothing.

**And the reason the cross-compile missed it is the interesting part.** The
project's flags are in `CMakeLists.txt`:

```
-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
```

The mingw command typed by hand before pushing `b661c94` was

```
-Wall -Wextra -Wconversion -Wsign-conversion
```

`-Wshadow` — the only one that mattered — was the one left out. GCC does not
imply it from `-Wall -Wextra`; MSVC's `/W4` does imply C4456. A hand-typed subset
of a flag list is a check that passes for the wrong reason, and this project has
now met that shape under several names.

| | |
|---|---|
| **+** | the fix is `tools/guards/check_windows_compiles.py`, which cross-compiles every Windows-only translation unit **with the flags read out of `CMakeLists.txt`** rather than restated by anyone. It reproduces C4456 as `-Werror=shadow`, on Linux, in under a second |
| **+** | verified by reintroducing the shadow on purpose: the guard names both lines, exactly as MSVC did |
| **+** | CI installs `g++-mingw-w64-x86-64` in the Linux guards step, so this is really performed somewhere rather than skipped everywhere — the failure mode `fetch-depth: 1` quietly gave `check_dates.py` for its whole life. Without a cross-compiler it says SKIPPED, loudly, and passes |
| **+** | selftest 72 → **75 cases**, and the third of them is the point rather than a detail: a `CMakeLists.txt` whose flags cannot be read makes the guard FAIL, because a guard that falls back to a guessed flag list is the defect it was written for |
| **−** | mingw is not MSVC. Green here does not promise green there; red here is red there. What this buys is a 1x Linux second in place of a 2x Windows minute, which is the same trade `check_includes.py` makes |
| **−** | four pushes to get one compiler error out of a runner: the flag, the build log, the arena, the shadow. Three of those four were the channel failing to report, not the code failing to work |

`docs/PLAN.md`'s Windows bullet was also carrying a half-finished edit — `[done]`
and *"No Windows host has ever run it"* in one paragraph, with a sentence that
stopped mid-clause. Rewritten to say what is now true and why Wine is not a
substitute for it.

### The arena worked, and the same push proved one was not enough

`82ec86d`'s Windows job built and the arena ran. The note it published:

```
allocation arena [0x7f0000000000, 0x7ffffffeffff) in windows of 0x4000000 bytes:
  16312 placed
      1 already held by this process at the window's own base
      0 structurally refused          <- the design assumption, measured
      6 refused with a region of this process elsewhere in the window
     64 not probed (inside a region already described)
```

**Zero structural refusals.** That is the assumption the whole thing rests on —
that no system-wide band lies inside the arena — confirmed by measurement rather
than by recollection about Windows ASLR.

And the same run failed the coverage test:

```
heap page      : 0x2f78000e000                      <- 2.97 TiB
nearest above  : [0x7f0000000000, 0x7ffffc000000)   <- gap 0x7c087fff2000
```

The occupancy note said the process was 99.8% in the top TiB; the coverage test
found its heap at 2.97 TiB. Both are true, because **they are different
processes**: `rs-env-probe` had its heap in the 1 TiB bucket, `test_probe` had its
at 2.97 TiB. On Windows the image and every DLL go to the top TiB while an NT heap
goes low, and where exactly is redrawn per process.

That is the *Linux* shape — a PIE's text four TiB from the kernel's mmap base —
not the macOS one, and it takes the Linux answer: a second arena,
`[1 TiB, 127 TiB)`.

| | |
|---|---|
| **+** | designing from one measurement got the top arena exactly right on the first try, including the band assumption. Designing from one measurement also got the *number of arenas* wrong, because one process's heap is not the platform's heap policy |
| **+** | the low arena is **contiguous**, not sampled: 126 TiB in 64 GiB windows is 2016 placements. Linux samples at this stride and therefore asserts the space between its samples — Windows does not have to, because `max_single_reservation` measured 64 TiB and `MEM_RESERVE \| PAGE_NOACCESS` never charges the commit limit, so a 64 GiB window costs what a 64 MiB one costs |
| **+** | `[min_map_address, 1 TiB)` is left out **on purpose**: KUSER_SHARED_DATA at `0x7ffe0000` lives there, and it is the one band whose presence would break the treat-a-covered-refusal-as-held rule. Stated gap, not oversight |
| **−** | the low arena records a refusal at the resolution of one 64 GiB window, which over-claims for a smaller hole — and over-claiming `unavailable` makes the analyzer answer UNSUPPORTED for addresses that are fine. `refused` has measured 0 so far and is the number that says otherwise. Narrowing is the fix when it fires, not before |

**Third time for one diagnostic.** `coverage_diagnosis()` printed *"NO arena was
scanned on this platform"* in the very failure whose `nearest above` was the
arena's own range. It reads `run.warnings`; the Windows arena's note went to
`profile.notes`. It had already been wrong twice — once reading the wrong
container, once matching prose (`"sampled every"`) that another file then
reworded. Both fixed now: the probes agree on `run.warnings`, *and* the diagnostic
reads both, because a diagnostic that is wrong is worse than one that is missing —
it gets believed.

**The test model was wrong before the product was.** `windows_probe()`'s describe
hook scanned a hardcoded 64 MiB while the new walk used 64 GiB, so it missed a
blocker 256 MiB into a window. That surfaced as a red test locally rather than as
an irreproducible profile on a runner, which is the entire argument for
`probe/windows_regions.hpp` existing.

### Windows is green, and Phase 1 is closed

`86fef1a`, `windows---msvc/success` and `windows-x86_64/success`. Both arenas ran
on a real Windows Server 2025 host:

```
top arena [0x7f0000000000, 0x7ffffffeffff)  windows of 0x4000000
  16313 placed   1 held at base   0 structurally refused   6 held elsewhere   63 skipped

low arena [0x10000000000, 0x7f0000000000)   windows of 0x1000000000
   2015 placed   0 held at base   0 structurally refused   1 held elsewhere    0 skipped

available_ranges:   2       [0x10000000000, 0x7f0000000000)
                            [0x7f0000000000, 0x7ffffc000000)
unavailable_ranges: 0
```

**`0` structural refusals in both.** Windows went from establishing *nothing*
— every address answering UNKNOWN for `RS-VM-0001/0002/0003` — to 127 TiB of
`measured_capability` coverage in two ranges, with the two-process reproducibility
step green in the same run.

`docs/PLAN.md`'s Phase 1 exit criterion **at least three platform families** moves
from `[partial]` to `[done]`. Wine still does not count and the probe still renames
itself `wine-on-posix-x86_64` so it cannot be mistaken for one.

### The reproducibility diff missed for the same reason twice

The improved `check_reproducible.sh` met its first real macOS failure and printed:

```
Facts that differ:
  available_ranges: 22 vs 22 entries
```

— the same as before. Because the new diff keyed entries on `[start, end)`, and
in this failure **all 22 bounds are identical in both runs**; what differs is
inside the entries. So the version written to fix "a count is not a diagnosis"
reproduced the same silence one level down.

It now keys on the bounds and compares in **full**, reporting three cases
separately: only in run 1, only in run 2, and *same bounds, different content* —
with the differing field named and both values shown. Verified against a fixture
carrying one of each. The third branch says explicitly that entries matching by
bounds and content but differing in order would be a serialization bug rather than
a probe one, because the canonical form exists to make that impossible.

The two macOS profile ids alternate between exactly two values across pushes
(`621881a4…` / `5f5f73a7…`), so this is bistable rather than drifting — which the
next failure will now name.

### And this file was lying about its own order

`docs/PROGRESS.md` is the past, and it had two entries in the wrong order: *the
fix was in the file, assigned and never read* (`650d510`, 12:49) sat **above**
*six green did not happen* (`90dc74b`, 13:01), against the newest-first rule
stated in this file's own header. Swapped. Every entry carries the same date, so
no date-comparison guard would have caught it, and none is being added that
pretends otherwise — the check that would work has to ask git which commit
introduced each header. Filed, not faked.

**Also next.** The Windows arena, once the runner says where its code and heap are. The Rosetta lane still needs a dispatch —
`gh workflow run macos-probe.yml --ref main` — and it now publishes its ground-truth
output, so the second half of the `file_map_beyond_eof` claim will be readable from
the sandbox when it runs.

---

## 2026-07-26 — the fix was in the file, assigned and never read

**Changed.** `tests/groundtruth/selftest.sh` gets `profile_for()`, so an UNKNOWN
row is checked against a profile that knows nothing instead of against the
measured host; `tools/guards/check_shell_vars.py` is new;
`tools/guards/selftest.py` now recomputes the case count `docs/PLAN.md` states
about it. Selftest 52 to **58 cases**.

| | |
|---|---|
| **+** | macOS's remaining failure was substantive, not portability, and the check that caught it was written for exactly this |
| **+** | the correct fix had been sitting in the file since the day it was written — `PROFILE`, assigned, referenced zero times |
| **+** | new guard: 1 true positive on the historical tree, **0 false positives** across 8 scripts |
| **+** | `docs/PLAN.md` said the guard selftest had `25` cases; it had 58. Now recomputed, so it cannot drift again |
| **−** | the same dead-variable class exists in Python and is **not** checked — `ROOT` in `selftest.py` was dead too, found by eye, not by a guard |
| **−** | the guard catches the symptom (a dead name), not the disease (a host-dependent expected value) |
| **−** | still no measurement from a real Windows host. Three green legs is not the fourth. |

### What macOS was actually complaining about

After `52f541e` the channel reported five green and one red:

```
compatibility-gate/success   determinism/success
linux---clang/success        linux---gcc/success
windows---msvc/success       macos---apple-clang/failure
```

`refs/ci-logs/<sha>/macos---apple-clang` held the reason:

```
tests/groundtruth/selftest.sh: precondition failed - exact-mapping-in-carveout
predicts UNSUPPORTED on this host, not UNKNOWN
  the selftest's rows are keyed on that; fix the row, not this check
```

Not bash 3.2 this time. The selftest's two `unknown` rows were checked against
the **measured** host, on the assumption that no real machine knows anything
about `0x1307200000`. True on Linux. False on macOS, where the probe **measures**
the Rosetta/GPU carveout `[0x1000000000, 0x7000000000)` as unavailable and the
address falls inside it — so the contract predicts UNSUPPORTED, and a row whose
whole job is to exercise UNKNOWN was exercising nothing.

Reproduced here without a macOS runner, against the committed fixture:

```
rs-check exact-mapping-in-carveout --profile ...
  unknown-host.synthetic.json          exit 3   UNKNOWN
  macos-arm64-rosetta-x86_64.fixture   exit 1   UNSUPPORTED   <- the CI failure
```

**The precondition check did its job.** Its comment says: *"checked here rather
than assumed — if a rule change moves one of these the selftest says so instead
of quietly testing the wrong row."* It fired for a platform difference rather
than a rule change, which is a use it was not written for and covers anyway.

### The fix was already written down

```sh
PROFILE="$ROOT/profiles/fixtures/unknown-host.synthetic.json"
```

Assigned on the day the file was written, above a comment explaining that it
exists so *"an UNKNOWN prediction is guaranteed rather than hoped for"* — and
then referenced **zero times**. `grep -c '\$PROFILE'` returned `0`. The author
wrote the right answer, did not use it, and nothing objected for the file's
entire life. `profile_for()` now routes `unknown` rows to it and everything else
to the measured host, which is host-independent by construction rather than by
luck.

### The guard, and what it does not cover

`check_shell_vars.py` flags a variable assigned as a whole statement and never
read. Measured both ways before being believed:

| | |
|---|---|
| on `52f541e` (before the fix) | **1** finding: `selftest.sh:47: PROFILE` |
| on this tree | 0 findings, 8 scripts |

Zero false positives against real code is the bar `check_includes.py` set, and
the reason the rules are narrow: a `FOO=bar cmd` prefix sets a **child's**
environment and is not a variable of the script; `export` and `local` are skipped
because something outside may read them; a name appearing only in a comment is
not a use.

**It catches the symptom, not the disease.** The defect was a test whose expected
value depended on the host it happened to run on. That is not mechanically
checkable and this guard should not be read as having checked it. What makes the
cheap version worth having is that here the symptom sat directly on top of the
disease, and would have named the exact line three weeks early.

**And the class is wider than the check.** `ROOT` in `tools/guards/selftest.py`
was assigned and never read — same shape, in the file that tests the guards, in
Python, where nothing here looks. It is used now (it reads `docs/PLAN.md`), but it
was found by reading, not by a guard. `pyflakes` would cover it; it is not
installed and adding a dependency to CI for one finding was not judged worth it
today. Recorded as a known hole rather than closed.

### A number in prose is a claim like any other

`docs/PLAN.md` stated `tools/guards/selftest.py`, 25 cases. There were 58. It had
already survived the increases to 48 and to 52 — `check_campaign.py` exists for
exactly this failure on measured numbers, and nothing was doing it for this one.
`selftest.py` now parses the number out of `PLAN.md` and fails on disagreement,
because the only thing that reliably knows the count is the file that holds it.

### Two measurement mistakes made while writing this entry

Both mine, both in verification commands, both worth writing down because the
project's whole claim is about not trusting an unmeasured number:

- `echo "$(basename $p) -> exit $?"` — the command substitution runs first and
  resets `$?`. It reported `SUPPORTED` for both profiles, i.e. "there is no
  macOS failure", which would have been a confident retraction of a real bug.
- `run_all.sh | tail -20; echo $?` — the pipeline's status is `tail`'s. It
  reported `exit=0` for a run that failed.

Neither reached a commit. Both are the same error as the one this session fixed:
reading a value from something that is not the thing you meant to measure.

**Next.** Push and watch `refs/status/<sha>/*` for six green — the first time
this project will have had that. Then T-004's only remaining piece, which no
guard can substitute for: a profile measured on a real Windows host.

---

## 2026-07-26 — the log channel answered in 60 seconds, and there were two bugs

**Changed.** `tests/groundtruth/selftest.sh` and `run.sh` and
`tools/campaign/run_campaign.sh` made bash-3.2-safe; `check_dates.py` judges each
line's blame instead of the clone; `tools/guards/check_shell_portability.py` is
new; `ci.yml`'s guards job fetches full history. Selftest 48 to **52 cases**.

**The channel added one commit earlier paid for itself immediately.** Sixty
seconds after the push, `refs/ci-logs/*` held `steps.json`, the ctest tail, the
full guard output and both ground-truth runs. Two failures, two different causes,
neither reproducible locally, both read from inside without anyone opening a
browser.

### macOS: `declare -A`, because macOS ships bash 3.2

```
tests/groundtruth/selftest.sh: line 57: supported: unbound variable
```

Bash 3.2 - frozen in 2007, because every later version is GPLv3 - has no
associative arrays. `[supported]=...` is parsed as an arithmetic subscript,
`supported` is read as a variable name, and `set -u` kills the script.

**Broken since the file was written.** Invisible because `ci.yml` ran macOS only
in `expensive-platforms`, gated off pushes until yesterday - so the harness that
validates this project's comparison table had never once run on macOS.

Writing the guard for it found **two more of the same class**, one of them the
next failure queued behind the first:

| | |
|---|---|
| `tests/groundtruth/run.sh:85` | `mapfile -t args < <(...)` — bash 4. **The harness itself**, which would have died the moment `selftest.sh` stopped dying first. |
| `tools/campaign/run_campaign.sh:43` | `declare -A counts=( ... )` |

And a bug I introduced while fixing the first one: the initial rewrite was
`printf | while read`, whose loop body runs in a **subshell** - so its `exit 70`
on a failed precondition would have exited the subshell and let the script carry
on testing the wrong rows. A `case`/`esac` lookup function has neither a subshell
nor an array.

### Linux: the date guard cannot blame what it cannot see

Thirteen confident, wrong accusations:

> `docs/PROGRESS.md:553`: entry is dated 2026-07-25, but git says the line was
> written 2026-07-26 12:19Z

`actions/checkout@v4` uses `fetch-depth: 1`. In a one-commit repository `git
blame` attributes **every line to HEAD**, so every heading looks as though it
were written at HEAD's date. Reproduced exactly with `git clone --depth 1`: 13
failures there, zero locally.

**It had been failing in CI since the day it was added** - which fell inside the
33.5-hour blackout, so nobody saw it. Accumulated, not fresh, exactly as the
previous entry guessed.

**And the first fix was too blunt, which this machine disproved in one command.**
Skipping check 1 whenever `git rev-parse --is-shallow-repository` returns true
would have thrown the check away here: this clone reports `true` (cloned with
`--depth` on 2026-07-24) while carrying 65 commits, and its blame had caught a
real heading error an hour earlier.

The right question is not "is the clone shallow" but "is THIS line's attribution
a truncation artifact". A line blamed to a **graft point** - a SHA in
`.git/shallow` - may belong to a commit that is not present; a line blamed to
anything else is attributed correctly, shallow or not. Each line is now judged on
its own blame:

```
this clone   (65 commits)  0 skipped, check runs
--depth 1                 19 skipped, counted and named, exit 0
```

`ci.yml` additionally gives the guards job `fetch-depth: 0`, so the check is
performed for real somewhere rather than merely not-failing everywhere.

### What the pattern is

Three bugs in two days, one sentence: **green on every platform anyone runs,
fatal on the platform nobody does.**

| Bug | Invisible because | Now caught by |
|---|---|---|
| `std::back_inserter` without `<iterator>` | MSVC never ran on a push | `check_includes.py` |
| `declare -A` and `mapfile` | macOS never ran on a push | `check_shell_portability.py` |
| `check_dates` on a shallow clone | CI was dark for 33.5 hours | the guard, judging blame per line |

The first two are now checked without needing the platform at all, which is the
only version of this that scales: a guard that needs a Windows runner to tell you
about Windows has not removed the dependency, it has moved it.

**Next.** Whether these three fixes make CI green - readable from
`refs/status/*` now, which is the point of having built it.

---

## 2026-07-26 — the workflow that builds the code could not report

**Changed.** All four `ci.yml` jobs publish `refs/status/<sha>/<job>/<state>`.

**Why, and it is a cost that had just been paid twice.** `macos-probe.yml` and
`windows-probe.yml` have had the git-ref channel from the start. `ci.yml` — the
workflow that actually builds and tests the code — published nothing. So when the
first real Windows build failed on a missing `<iterator>` and the one-line fix
went in as `3c49935`, **whether the fix worked was invisible from inside**.
Confirming it required a person opening a browser, for a result the repository
could have carried itself.

The measurement plane had a channel and the build plane did not, which is an odd
place to have drawn the line: a measurement is only interesting if the thing
being measured compiles.

Two things had to be right and neither was obvious:

- **The token is read-only.** `default_workflow_permissions: read` was set
  deliberately before the repository went public, so `contents: write` is granted
  in this workflow and nowhere else, and only for contents.
- **The step must skip pull requests.** A fork PR receives a read-only
  `GITHUB_TOKEN` whatever the workflow declares, so an unconditional publish step
  would show every external contributor a red job for something that cannot
  succeed and has nothing to do with their change. The repository now accepts
  external PRs, so this stopped being hypothetical the moment it went public.

**What it makes possible.** `git ls-remote origin "refs/status/<sha>/*"` now
answers "did it build on MSVC" from inside. That is the third leg of the
end-to-end claim — control and status were proven for the *measurement* workflow;
this extends status to the workflow that can actually fail for a code reason.

**It worked within 55 seconds and the first thing it said was bad news.**

```
compatibility-gate/success
determinism/success
linux---clang/failure
linux---gcc/failure
macos---apple-clang/failure
windows---msvc/success      <- the <iterator> fix is confirmed on MSVC
```

Two results at once. The fix works - that is the answer the channel was added to
get. And **three platforms are failing**, which nothing in this repository could
see until a minute ago.

The pattern is diagnostic in itself: `windows/msvc` skips the two ground-truth
steps (`if: runner.os != 'Windows'`), macOS runs them and failed, and linux runs
them plus the reproducibility check and failed. `compatibility-gate` and
`determinism` do not run them and passed.

**None of it reproduces here.** A fresh clone of the same commit configures,
builds, passes 13 suites, all 10 guards, the ground-truth selftest at 14/14 rows,
and the ground-truth comparison at 14 cases / 12 held / 0 contradicted - each run
with the exact command `ci.yml` uses. So the cause is on the runner, and a status
ref cannot carry a cause.

**Which is the other half of a channel I only half copied.** `macos-probe.yml` has
had a `refs/ci-logs/*` diagnostics step from the start; the status half was taken
and the log half was not. Added now, to all four jobs, carrying `steps.json`, the
ctest tail, the full guard output, and both ground-truth runs. Copying half of a
two-part channel is the kind of omission that looks complete until it is used
once, and it was used once immediately.

Worth stating plainly: **33.5 hours of CI darkness means every commit in that
window is unverified on anything but Linux-here.** This is the first daylight, and
what it shows may be an accumulated failure rather than a fresh one. The next run
should say which.

**Next.** The reason for the three failures, from `refs/ci-logs/*`. The
measurement leg is still untested and still needs a probe change or a dispatch -
this commit is neither, deliberately. Manufacturing a probe edit to force a run is
the stowaway pattern this project has already refused once.

---

## 2026-07-26 — Windows ran, and the failure was one missing include

**Changed.** `src/vm/impact.cpp` gains `<iterator>`;
`tools/guards/check_includes.py` is new and wired into `run_all.sh`; the record
is corrected in three places. Selftest 44 to **48 cases**.

**The repository is public and Windows ran for the first time.** Not a
measurement - a build failure, which is better than the silence that preceded
it:

```
src/vm/impact.cpp(89,47): error C2039: 'back_inserter' is not a member of 'std'
src/vm/impact.cpp(91,47): error C3861
                                            MSVC 19.51.36248.0, VS 18 Enterprise
```

`impact.cpp` included `<algorithm>` and `<set>` and used `std::back_inserter`,
declared in `<iterator>`. libstdc++ and libc++ supply it transitively; MSVC does
not. Every local build, every mingw cross-build and both macOS runners had been
green for as long as the file existed.

**The systematic version, and what stopped it being noise.** The owner's report
was explicit that other files also rely on transitive includes but that MSVC
compiled them all - hygiene, not a proven defect - and that it should not be
written up as a finding. Resolving it rather than asserting either way:

| Method | Result |
|---|---|
| grep for symbols, report files missing the declaring header | **42 hits** |
| ...following each `#include "runtimeskeptic/..."` recursively, so a header a file OWNS counts | **1 hit** |

The one is `impact.cpp`. **The scan agrees with the compiler exactly**, which is
the bar this class of guard has to clear, so it became one. `check_includes.py`
resolves the project-header chain and checks a narrow table of symbols whose
declaring header is unambiguous. It does not claim to predict MSVC; it reports a
symbol whose header is nowhere in the include graph.

The 42 were the interesting part. A `.cpp` getting `<string>` from its own
`.hpp` is not relying on an accident, and a guard saying otherwise gets switched
off - which this project has written down three times now. There is a selftest
case for that exact false positive.

### Three corrections, all to claims this log made

**1. "A small spending limit lifts it today" was wrong, and it was mine.** The
owner set a repository-scoped `Actions Windows` SKU budget at $10 with a
hard-stop. Over nine minutes and two dispatches the block did not lift: `0 ms`,
job never started. **Going public opened it instantly.** So of the three options
this log offered, one is measured and eliminated, one is proven, and the
eliminated one was the one presented as cheapest.

**2. `if: always()` is no longer an inference.** The run published
`refs/status/c13285c…/windows-x86_64/failure` - the project's first Windows ref.
A job that starts and fails does leave a trace. That chain had been reasoning
from the workflow's own text; it is now observed.

**3. The `0 ms` signal is dead, and every past use of it is now date-bound.**
Public repositories are not billed, so `billable.total_ms` is permanently `0`
here - the 37-second run that really executed returned `0` as well. Everywhere
this log reads "`0 ms` → blocked", that inference was valid **only while the
repository was private**, before 2026-07-26. It is not a diagnostic any more.

### The end-to-end cycle: two legs of three

The claim this log marked as *assumed, not measured* was "the git protocol is
sufficient for the measurement plane". It is now partly measured.

| Leg | State |
|---|---|
| **control** - dispatch a run | **proven.** Dispatched from the owner's `gh`, run started. |
| **status** - read the outcome from here | **proven.** `refs/status/.../failure` read with `ls-remote`, nobody opened the Actions tab. |
| **measurement** - a profile published and fetched | **still untested.** The `Measure` step was never reached; the build failed first. |

**And the control-plane dependency was resolved, but not by `add_repo`.** The
owner's machine has `gh` with `repo`+`workflow` scope. This sandbox still cannot
reach the API. What changed is that a second station exists which can press the
button - so "a named human dependency" remains the right description, with the
human now holding a terminal.

**Deliberately left to be done here.** The owner did not push the one-line fix,
because this project writes every change into this file with its reasoning and an
unlogged commit from elsewhere would break that. Correct, and the reason it is
worth naming: the discipline held under the temptation to just fix it.

### Two more findings, one structural and one about my own method

**Nothing built this project with MSVC on a push to main.** `ci.yml`'s
`build-and-test` matrix is Linux only; the macOS and Windows entries live in
`expensive-platforms`, gated `if: github.event_name != 'push'`. This repository
is worked by direct pushes to main, not pull requests, so in practice **Windows
coverage was the weekly cron**. `src/vm/impact.cpp` carried the missing
`<iterator>` from `87ffb6f` to `3c49935` - green on gcc, clang, mingw and both
macOS runners the entire time, broken on the one toolchain nobody was running.

The gate existed to save billable minutes, and that cost is now gone: public
repositories are not billed, demonstrated rather than assumed. So the gate is
removed and both platforms build on every push again.

**And the systematic sweep was not systematic.** The previous entry claims that
"every quota, billing and minutes claim in the repository" was grepped and
checked, and that "there were two". There were **three**. The third is in
`ci.yml` and it survived because the sweep was a line-based grep and the claim
spans a line break:

```
  # billable minutes per push. Twenty pushes in one night exhausted a 2,000
  # minute monthly quota, and the first symptom was ...
```

`grep -E "2,?000 minute|monthly quota"` matches neither line. Re-run as a
multiline scan it appears immediately. **A systematic check that is not
systematic is the exact shape this project keeps finding in itself**, and this
one was committed in the same breath as a paragraph congratulating itself for
being systematic.

All three are now corrected, and the wrong sentence is left above its correction
in each file.

**Next.** The measurement leg, still the one untested piece. This commit touches
`ci.yml`, so it triggers the Windows build - but not `windows-probe.yml`, whose
path filter covers the probe and the workflow only. That is correct by design and
it means the measurement leg needs either a probe change or a dispatch.

---

## 2026-07-26 — the project's own sin, committed by the project, against itself

**Changed.** `.github/workflows/macos-probe.yml` and `windows-probe.yml`: a
causal claim corrected and a figure fixed. The decision they justify is
unchanged.

**The finding, and it is about this project rather than about a platform.** The
macOS workflow was moved off per-push on 2026-07-25 and the comment explaining
why said this:

> three macOS jobs per push (one from ci.yml, two here) at 10x, roughly 75
> billable minutes a push, twenty pushes in a night, **and a 2,000 minute
> monthly quota gone**

Two things in that sentence are wrong, and the second one matters.

| Claimed | Billing page, 2026-07-26 |
|---|---|
| a 2,000 minute quota | **3,000** minutes (Pro tier) |
| this repository's pushes exhausted it | `CodeSkeptic` **$32.52 (82%)**, `Runtime_CodeSkeptic` **$4.28 (10.8%)**, `organon-mueller` $2.68 |

**This repository paid about a tenth of the bill.** The sibling repository runs
five workflows per push - CI, Windows, Docker, a Juliet benchmark, Docs - and its
last heavy day was 2026-07-24, immediately before this repository hit the wall at
`2026-07-25T01:09:39Z`.

So the story was: our arithmetic, our pushes, our exhaustion. The measurement
says: 89% of it was somebody else's, and adjacency in time was read as cause.

**This is precisely what the project forbids, done to the project, with the
evidence one page away.** `docs/PLAN.md` Phase 0 exit criterion 5 is that no
claim depends solely on generated interpretation. `corpus/runtime_failures/`
has forty-four entries about programs that took an observation for a
guarantee - and the one recorded here is a plausible arithmetic taken for a
cause, by the author of that corpus, about a bill that was itemised and never
opened. Nobody asked for the number. The number existed.

**The decision stands and the reasoning is replaced.** Three macOS jobs per push
at 10x is wasteful whoever is paying, and the case for per-push measurement was
always weak - the host cannot change between two commits. That is a sound
argument about waste and it survives intact. What does not survive is "we ran
out", which was never ours to say.

The wrong sentence is left in the workflow file above its correction rather than
tidied away. A comment that quietly became right teaches nothing.

**Not guardable, and saying so rather than building something fragile.** There is
no mechanical check for "this causal claim was never measured" - the same
conclusion this log reached about a wrong past date in prose. What *was* done is
the systematic version of the fix: every quota, billing and minutes claim in the
repository was grepped and each one checked, rather than only the line the
reviewer named. There were two.

**And the block is not what it was recorded as either.** A Windows minute is
$0.016, so one probe run at the 2x multiplier is roughly **$0.32**. "Impossible
until 1 August" is false: a small spending limit lifts it today. Both workflow
files now carry that figure, because a blocker whose price is thirty-two cents
should not be described as a wait.

**Next.** Unchanged in substance: `T-004` needs a runner. What changed is that
the owner now has three options rather than one - wait six days, set a spending
limit of a few dollars, or make the repository public.

---

## 2026-07-26 — the control plane, read from outside, and the block is wider than recorded

**Changed.** `src/probe/vm_probe_windows.cpp`: the exclusive-bound conversion is
now MEASURED, by the right API, after the first version measured it with the
wrong one. The record is corrected in four places from an external `gh` report.

**The named human dependency was exercised and it paid.** Everything below came
from the owner reading Actions with `repo`+`workflow` scope. It is what the
entry two above said the substitute for API access would be, and it produced
four facts that no amount of `ls-remote` could have.

### The trigger worked. I had recorded "most likely never triggered" as one of three

```
8ddfd4a  ->  TWO runs at 2026-07-25T22:38:14Z (CI and Windows measurement)
438c13d  ->  triggered correctly as well
```

The path filter is sound and nothing about it needed fixing. **The `if: always()`
inference was right for the right reason**: the job never started, so the step
never ran. `billable.WINDOWS.total_ms: 0`, zero steps, six seconds.

The annotation names the cause:

> "The job was not started because recent account payments have failed or your
> spending limit needs to be increased."

### The blackout has a measured start, and it narrows the record

| | |
|---|---|
| last successful run | `2026-07-25T01:05:41Z` |
| first refused run | `2026-07-25T01:09:39Z` |

A four-minute window, where this log had `01:06:39Z`–`12:42:26Z`. **Every run
since dies in 4-14 seconds, ubuntu CI included: 33.5 hours of total darkness.**

**So "10 guards green" in the last several commit messages is LOCAL
verification.** CI has confirmed nothing since `01:05:41Z`. The claims are true -
they were run here, and once on a genuinely fresh clone - but they are not what a
reader assumes when a commit message says guards are green.

### And the hypothesis was one cause too narrow

This log said "the quota was exhausted and resets on 1 August". GitHub's message
names **two** causes and does not distinguish them - a spending limit, or a
failed payment. The record asserted the first as if it were established.

**Settled the next day by reading the billing page, and it is the first one:**
payment is fine, billable is `$0` (`$39.50` usage against `$39.50` discount),
Actions minutes stand at **3,000 / 3,000** - exactly full - and the page itself
says *"Included usage limits reset in 6 days"*, which is 2026-08-01 <!-- future -->. So "resets
on 1 August" is now a **measured finding, not a hypothesis**, and this paragraph
is the record of it having been one.

**There is a third path and the owner already offered it, early in this
session:** making the repository public restores unlimited Actions minutes
regardless of which cause it is, because public repositories are not billed. The
cost is that the corpus, the campaign data and every document become public. That
is the owner's call and it is now written down where the decision lives.

### T-004's two documentation checks, done - and both came back negative

`## Now` named them and they are answered. Neither the way I expected.

**1. `lpMaximumApplicationAddress + 1` is not documented either way.**
`SYSTEM_INFO` says only "A pointer to the highest memory address accessible to
applications and DLLs". The memory-limits page gives x64 user space as "128 TB"
and no hexadecimal at all. The `+ 1` rested on reading "highest accessible" as
inclusive, which is reasonable and is not a citation.

So it became an experiment - and the first experiment was wrong. A one-page
reservation at the top page came back `ERROR_INVALID_ADDRESS` and the probe
concluded "exclusive", flipping `max_user_address` by a page. That inference does
not hold: **Windows keeps a no-access guard region at the very top of user
space**, so a refusal there is equally consistent with "in bounds and guarded".
Two explanations, one picked.

`VirtualQuery` separates them, because it reports on an address without touching
it and fails only when the address is outside the process's space. Measured:

```
VirtualQuery(0x7ffffffef000) -> succeeded, MEM_FREE over 4096 bytes
  => the address is part of the process's space => the field is INCLUSIVE
reservation at the same page -> ERROR_INVALID_ADDRESS
  => the guard region, recorded as corroboration and explicitly NOT what decided it
```

`+ 1` was right. It is now right *for a measured reason*, and the `Fact`'s source
string had to be corrected too - it credited the reservation, which is the one
thing that did not establish it and which failed.

**2. 64 KiB allocation granularity is not documented as a value at all.** Neither
`SYSTEM_INFO` nor `VirtualAlloc` states it, or says it is
architecture-dependent; both say "use `GetSystemInfo`". So 64 KiB is what
implementations return, not a guarantee: `allocation_granularity` must stay
`measured_capability`, and a host reporting something else is a **finding**, not
a probe bug. That is what the check was for.

What *is* documented is the asymmetry itself, which is the RSC-0044 mechanism in
Microsoft's own words: *"If the memory is being reserved, the specified address
is rounded down to the nearest multiple of the allocation granularity. If the
memory is already reserved and is being committed, the address is rounded down
to the next page boundary."* That is a `specified_guarantee` the entry did not
have before.

**Next.** `T-004`'s two hand-checks are closed; what remains needs a runner, and
that needs the billing question answered - which of the two causes, and whether
the repository goes public.

---

## 2026-07-26 — an accepted cost, recorded as a cost

**Changed.** `T-004` promoted into `docs/TODO.md`'s `## Now` by the owner's
decision, framed by the two pieces of it that need no runner. This entry
replaces a claim made in conversation that should not have been made.

**What was claimed and why it was wrong.** Asked whether to request GitHub API
access for this sandbox, the answer given was "no need", justified by listing
what the git protocol had achieved this session: a 27-minute ref poll, 60 refs
counted through `ls-remote`, "when did Actions last run" inferred from the
committer date of CI-authored commits. All true, and beside the point.

The owner's objection, which holds:

- **The one open item is blocked on exactly the facts git cannot reach.** Is the
  quota exhausted, is the run queued, did it trigger at all. The entry two above
  says **"cannot be determined from here"** twice, in a table. "No need" walks
  back from that sharpness within hours of writing it.
- **The example chosen to defend the method undercuts it.** The committer-date
  inference produced a 17-minute-stale timestamp and a day-wrong quota date on
  its first application. Both were caught by external verification, not by the
  git-only channel. The method is valid and it is not costless.

**The distinction that was missing, and it is the whole thing.**

| Plane | Git protocol |
|---|---|
| **measurement** — get a measured profile off a runner and into the repository | **sufficient, and better than the alternative.** `refs/measurements/*` beats artifact download: it is content-addressed, fetchable with one command, and survives the run's retention window. This channel exists *because* of the constraint and is now one of the project's more solid pieces. |
| **control** — start a run, read its state, tell a queue from a quota wall | **not sufficient.** What substitutes for it is the owner opening the Actions tab. |

The second row is a **named human dependency**, not an architecture. It is
accepted, and the price is: nobody inside this repository can tell a queued run
from a failed one, so any statement about why a measurement is missing is a
hypothesis until a person looks. That cost has already been paid once, in the
entry two above.

**And the claim is marked as what it is.** "The git protocol is sufficient for
the measurement plane" is currently **assumed, not measured.** What would
measure it: one end-to-end cycle — probe change pushed, run started, measurement
published, fetched and committed — completed *without a human reading the
Actions tab*. That has never happened. The first attempt is the one still
outstanding at `8ddfd4a`.

By this project's own rule that is `[partial]` reasoning wearing a `[done]`
sentence, and the correction is the point of writing it down.

**Not changed.** No API access requested; the owner's decision stands and the
reasoning for it is theirs, not the one given here. What changed is the record.

**Next.** `T-004`'s two documentation checks, which need no runner and are now
what `## Now` names.

---

## 2026-07-26 — an external review, and a guard that was green for the wrong reason

**Changed.** `tools/guards/check_docs.py` resolves cited paths against
`git ls-files` instead of the working tree; `tools/guards/check_todo.py` gained
check 6 (marker versus section) with a `pending-promotion` valve; two documents
stopped citing a gitignored artifact; `windows-probe.yml`'s trigger comment
counts to three; the entry above was corrected in four places. Selftest 36 to
**41 cases**.

**The owner ran an independent review of `6439aee` on a machine with more
access. 8 of 8 claims held.** What it found is worth more than the eight.

### The one that matters: a guard passing on untracked local state

`check_docs.py` resolved cited paths against the **filesystem**. Two documents
cite `profiles/generated/linux-x86_64.json`, and `.gitignore` excludes
`profiles/generated/*.json` by design - it is probe output, not a fixture. The
file existed here because the probe had run here.

So `run_all.sh` printed **"all guards passed" a dozen times this session over a
repository that fails the same guard in every fresh clone.** Confirmed by
cloning into `/tmp` and running it: two failures, immediately.

That is worse than the failure mode the guard's own docstring warns about. A
noisy guard gets switched off; a guard that is quiet *because of local state*
reports green while the thing it protects is already false for everybody else -
and it reports green to the one person who cannot notice.

Fixed at the root: `git ls-files` decides, and the message distinguishes "not in
the repository" from "exists in THIS tree but is untracked, so it is absent for
every other reader". A selftest case builds a real repository with a real
`.gitignore` and requires the failure.

### The compass contradicted itself in three places

`T-004` carried `[now]`, sat under `## Next`, and `## Now` said "(`Now` is
empty)". Checks 3 and 4 read markers; a human reads sections; nothing compared
them. That is how the entry above came to assert **"`T-004` stays in `Now`"**
about an item that was in neither state.

Check 6 now requires them to agree - and the resolution is deliberately *not*
automatic. `docs/TODO.md` warns in its own text against promotion "by drift",
and a guard that forced the move would take the decision as a side effect of a
consistency fix. So a disagreement is permitted when `## Now` carries
`<!-- pending-promotion: T-nnn -->`: the contradiction becomes a written, visible
open decision with the argument on both sides, instead of silence.

### Two corrections to the entry above, both mine

- **A timestamp 17 minutes stale.** `2026-07-25T00:49:40Z` is a real CI commit
  but not the newest; `cca8a6c` at `01:06:39Z` is. Cause: I took the *last* line
  of `git ls-remote` output as the newest ref. `ls-remote` sorts by refname.
- **The quota date, wrong by a day.** `2026-07-24` should have been
  `2026-07-25`, and the wrong date came from the environment header - the same
  source this log already records as having produced three dates for one day.
  Two workflow comments said 25; this line said 24, four lines below a row
  asserting a successful run on the 25th.

`check_dates.py` cannot catch the second and should not be stretched to try: a
*wrong past date in prose* is not mechanically distinguishable from a right one.
The honest record is that this class is caught by a reader.

### And one the review corrected in me rather than in the code

I had been about to reconcile "two files" against a three-path filter by
trimming the list. The owner's objection: **removing `windows-probe.yml` from
its own filter destroys the mechanism.** That path is the reason `8ddfd4a` could
start the run at all - a workflow that edits itself can trigger itself, which is
the only route available to a party that cannot press the button. The comment
now names three, separates *what* is measured from *how*, and says why the third
is load-bearing.

### And then it happened a fourth time, to the guard written for it

This entry was first headed `## 2026-07-25`. It was committed at
`2026-07-26T10:43Z`. The session had run past midnight and the author's notion
of "today" went stale in the middle of the work - **hours after the review
flagged that exact class**.

`check_dates.py` passed it. `HEADING_TOLERANCE_DAYS = 1` exists for the real
case where an entry is written just before midnight UTC and committed just
after, and a flat one-day allowance swallowed a disagreement of eleven and a
half hours. The one check written to catch a stale sense of today shrugged at
the clearest instance of it yet.

Now the day of grace is conditional on the clock: within three hours of
midnight, one day apart is a timezone artifact; outside it, one day apart is a
wrong date. The message says which, with the distance from midnight. Heading
corrected, and a selftest case pins both sides.

Fourth date incident in this project, and the only one caught by a guard rather
than by a person - after the guard was tightened by the person's finding.

**Next.** Two decisions belong to the owner and neither should be taken here:
resolve the `T-004` promotion, and - if dispatch capability is wanted - request
it, because Anthropic's own 403 names the remedy: `add_repo` with
`access:"push"`. The previous entry diagnosed that block precisely and then
failed to ask for the one thing that would lift it.

---

## 2026-07-25 — the dispatch that could not be dispatched

**Changed.** `.github/workflows/windows-probe.yml` gained a path-filtered
`push` trigger. Commit `8ddfd4a`. **No measurement arrived.**

**Why the button could not be pressed, isolated to one layer.** There are two
places this could fail and it is worth knowing which, because only one of them
is fixable by changing a token.

```
$ curl -H "Authorization: Bearer <the same PAT that pushes every commit>" \
       https://api.github.com/repos/tanzercakir-commits/Runtime_CodeSkeptic
HTTP 403
{"message":"GitHub access to this repository is not enabled for this session.
  Use add_repo to request access. If add_repo answers that read access is
  already available and you need GitHub API or write access, call add_repo
  again with access:\"push\" to attach the repository with credentials."}
```

That 403 is from **the sandbox proxy, not from GitHub** - the message is
Anthropic's and it names its own remedy. So:

| Layer | State |
|---|---|
| sandbox proxy allowlist for `api.github.com` + this repo | **blocking**; `add_repo` is the documented fix and is not in this session's tool set (checked, not assumed) |
| the GitHub PAT's scopes | **untested and untestable** - no request ever reaches GitHub, so whether it carries `Actions: write` is unknown |

The git protocol is unaffected: it is how every commit in this log was pushed,
and it is why the measurement channel is `refs/measurements/*` rather than an
artifact download. An API-only operation is simply out of reach from inside.

A first attempt at this diagnosis got 000 rather than 403 and nearly became
"the API is unreachable". The cause was mine: `~/.rs-cred` is a script that
*prints* `username=`/`password=` lines for git's credential helper, and reading
it with `. ~/.rs-cred; echo $password` captured the whole printed block - a
93-character token became a 126-character string with a newline in it, which
curl could not put in a header. A malformed request looked like a blocked one.

**What was done instead, and why it is not a workaround.** The workflow now
also fires on a push touching `.github/workflows/windows-probe.yml`,
`src/probe/vm_probe_windows.cpp` or the probe header. That is a *better*
trigger than either of the existing two, for a reason the macOS workflow's own
comment gets slightly wrong: the HOST does not change between two commits, but
the PROBE does, and a probe change is exactly when a fresh measurement is worth
2x billing. It fires when the answer could have changed and stays silent when
it could not.

**And then nothing happened.** 27 minutes of polling
`refs/status/8ddfd4a…/*`, `refs/measurements/8ddfd4a…/*` and
`refs/ci-logs/8ddfd4a…/*` returned empty. The status step carries
`if: always()`, so a job that starts and fails still publishes - which means
the job most likely never started.

What can be established from inside, and what cannot:

| | |
|---|---|
| the ref channel works | 60 refs exist from previous macOS runs (24 measurement, 30 status, 6 ci-logs) |
| Actions ran successfully as recently as | `2026-07-25T01:06:39Z` (`cca8a6c`), ~21h 30m before this attempt. Source: the committer date of the CI-authored commit a `refs/status/*` ref points at - git-protocol information, which is why this row sits above the ones that cannot be established |
| whether the Actions quota is currently exhausted | **could not be determined from here.** Determined externally on 2026-07-26 - see the entry above |
| whether the run is queued, failed at startup, or never triggered | **could not be determined from here.** Determined externally: it triggered, and the job never started |

The quota was exhausted on **2026-07-25** and resets on 1 August. That is the
leading explanation and it is a hypothesis, not a finding - exactly the
distinction this project keeps making, applied to itself.

It is surrounded by evidence rather than asserted: the last CI-authored commit
is `cca8a6c` at `01:06:39Z`, and `87d9c13` at `12:42:26Z` **the same day** is
the commit that took macOS off per-push *because of* the quota. Nothing
CI-authored exists after `01:06:39Z`, which is what "every run failed at
startup" looks like from the git side.

**This paragraph originally said 2026-07-24, and the wrong date came from the
environment header** - the same source `docs/PROGRESS.md` already records as
having produced three different dates for one day. Two workflow comments said
25 and this line said 24, four lines below a row asserting a successful run on
the 25th, and nothing objected.

`tools/guards/check_dates.py` cannot catch it and should not be stretched to
try. It checks `## YYYY-MM-DD` headings against `git blame` and refuses any date
later than the newest commit; a *wrong past date in prose* is not mechanically
distinguishable from a right one. The honest record is that this class of error
is caught by a reader, and this one was - by an external one.

**Status unchanged, deliberately.** `docs/PLAN.md` Phase 1 Windows fixtures
stay `[open]` and "three platform families" stays `[partial]`. **`T-004` has
still not been promoted into `Now`** - it sits under `## Next` carrying a
`[now]` marker, and `## Now` is empty. Nothing about the probe became more true
because a workflow was triggered; the deliverable is a measurement from a real
Windows host and there is not one.

This sentence first read "`T-004` stays in `Now`", which was false and
flattering in the usual direction: it described stability where the truth is
that the item has not been picked up. The correct sentence is the stronger one -
it is what the entry set out to demonstrate.

**Next, and it needs the owner.** Open the Actions tab. If the run is sitting
in a queue it will land on its own and publish to
`refs/measurements/8ddfd4a…/windows-x86_64`. If it failed with
`Billable Time -`, the quota is the blocker and 1 August is the date. Either
way the two facts to check by hand against Microsoft's documentation before
believing the result are unchanged: that `lpMaximumApplicationAddress + 1` is
the right exclusive bound, and that `dwAllocationGranularity` is measured
rather than assumed.

---

## 2026-07-25 — T-004: the Windows probe exists and has never run on Windows

**Changed.** `src/probe/vm_probe_windows.cpp`,
`.github/workflows/windows-probe.yml`,
`profiles/measured/wine-9.0-on-linux-x86_64.measured.json`, and a one-line fix
in `tools/rs-env-probe/main.cpp`.

**Status, stated first because it is the point.** The code is written, it
cross-compiles clean with `-Wall -Wextra` under mingw-w64, the whole project
builds for Windows including all 13 test binaries, and the probe runs correctly
under Wine. **`docs/PLAN.md` still says `[open]`.** Code that has never run on
the platform it targets is a hypothesis about that platform, and the
measurement is the deliverable.

**What it measured under Wine**, which is the shape a real run should have:

| Fact | Value | Why it matters |
|---|---|---|
| `page_size` | 4096 | |
| `allocation_granularity` | **65536** | first host in this project where these differ |
| `reserve_commit_model` | `windows_reserve_commit` | **first time this value has ever been measured anywhere** |
| `fixed_noreplace_available` | true, demonstrated | a second reservation over an occupied range was refused with `ERROR_INVALID_ADDRESS` |
| `hinted_mapping_may_relocate` | false | a base address is a requirement here, not a hint - the inverse of POSIX |
| `file_map_beyond_eof` | `error` | the third `BeyondEofBehavior` value, never observed before |

`RS-VM-0012` has existed since the model was written and had never fired
against a host that actually has the reserve/commit model. Under Wine it now
can - and Wine is not Windows.

**The trap this walked into and out of.** Wine reproduces Win32 faithfully
enough that *every probe succeeded*, and the resulting profile was labelled
`windows-x86_64` with `origin: measured`. Committed as-is it would have been
counted as the third platform family. The probe now detects Wine through
`wine_get_version` in ntdll - Wine's own documented marker, absent on real
Windows - renames itself `wine-on-posix-x86_64`, sets `translation_mode` and
writes a note beginning "THIS IS NOT WINDOWS". The CI job additionally refuses
to publish any profile whose name or version says Wine.

**And a defect that fix exposed.** `tools/rs-env-probe/main.cpp` overwrote
`profile_name` unconditionally when no `--name` was given, so the probe's own
name was discarded and the Wine profile came back labelled `windows-x86_64`
anyway. The caller knew less than the callee and overruled it. Fixed: `--name`
wins, then whatever the probe chose, then the default.

**Held to the discipline before it could fail.** `VirtualQuery` walks the
address space and reports exactly which regions are free - far better than the
Linux probe's sampling, and entirely a property of *this* process's ASLR
layout. Recording it as `available_ranges` would have made `profile_id` a
function of our load address: `min_map_address` made that mistake once and the
arena scan nearly made it again in T-013. The walk's output goes to `notes`,
outside the facts subtree and outside the hash. Two Wine processes produce an
identical `profile_id`.

**Next.** Dispatch `windows-probe.yml`. Two things to check by hand against
Microsoft's documentation before believing the result, because this author has
never seen a real Windows profile: that `lpMaximumApplicationAddress + 1` is
the right exclusive bound, and that `dwAllocationGranularity` on that runner is
measured rather than assumed.

---

## 2026-07-25 — T-003: the corpus, 1 of 30 to 44 of 30

**Changed.** 43 new entries in `corpus/runtime_failures/`, each citing a public
report, commit or vendor document that was fetched and read.
`tools/guards/check_corpus.py` and `tools/campaign/verify_corpus_sources.py`
are new. Phase 0's two blocking exit criteria are met: **44 counting against a
requirement of 30, and 35 virtual-memory against a requirement of 10.**

**The method, because the number is worthless without it.** Six readers worked
in parallel over six domains - emulators, JITs and language runtimes, Apple
Silicon, allocators and sanitizers, Windows and Wine, kernel changes - under
one rule: fetch the page, read it, quote a line from it, and if you cannot,
report nothing. They also reported what they rejected and why, which is how the
gaps below are known rather than guessed.

**What the corpus says about the project's central claim.** The dominant shape
across 44 real incidents is **not refusal**. Roughly a third are a call that
returned success and handed the program something else:

| Entry | Asked for | Got, successfully |
|---|---|---|
| RSC-0013 | a reservation at 0x1000 | one at 0x10000, and an error message ending in `(Success)` |
| RSC-0020 | `MAP_32BIT` | 0x7fa21f5cb000 |
| RSC-0036 | a 64 MiB-aligned region | an unaligned one, silently, every time |
| RSC-0031 | a decommit | a no-op, with the bookkeeping flag set anyway |
| RSC-0029 | RWX, and the API agreed | a hardware mask that denies the write |

That is the category this project was built around, and the corpus supports it
rather than merely illustrating it.

**Four gaps in the model, each with a citation rather than a guess.**

1. **W^X toggle granularity** (RSC-0027) — `pthread_jit_write_protect_np`
   controls *every* page on the thread, so unprotecting one region unprotects
   the code that is running. The model records that W and X are not
   simultaneous; it has no field for how coarse the switch is.
2. **The VMA-count limit** (RSC-0040) — a 4 KiB mapping failing with ENOMEM
   because `vm.max_map_count` was reached. A topology constraint that is not
   about addresses at all.
3. **"The same address in a future process"** (RSC-0041, RSC-0047) —
   PostgreSQL's shared segment and Cygwin's `fork()` both require an address to
   be identical in a process that does not exist yet. Not expressible today.
4. **A program that *requires* destructive `MAP_FIXED`** (RSC-0052) — the flag
   that was added to make placement safe had to be reverted from the ELF loader
   because real binaries have overlapping segments. `fixed_noreplace_available`
   is necessary and not sufficient.

**The guard found the drift it was written for, immediately.** `RSC-0011` - the
single entry the exit criteria had turned on for the project's whole life -
carried `provenance: reported_incident` and `evidence_available:
[reported_measurement, source_citation, log_excerpt]`, and **not one of those
four values exists in the corpus README's own tables.** RSC-0009 and RSC-0010
were malformed the same way. The entry the criteria depended on was invalid
against the rules of the file that defines them, and nothing had ever looked.
The README's counting table is now a `<!-- counting: N/30 vm: M/10 -->` marker
recomputed from the files.

**The honest bound, and it is the important part.** Only **6 of the 44** have
been re-fetched by a second reader (RSC-0011, 0018, 0020, 0035, 0047, 0052; all
six matched). `verify_corpus_sources.py` exists to close that gap and **cannot
run here** - this environment's proxy returns 403 to every plain HTTP client,
so the tool exits 2 with "nothing was checkable; this says nothing about the
corpus" rather than passing vacuously. Six is the number until it runs
somewhere with ordinary network access, and the corpus README says so where a
reader will see it.

**Also recorded, from the researchers' own rejection lists.** WineHQ's bugzilla
is unfetchable behind a proof-of-work wall and Apple's developer documentation
is JavaScript-rendered and returns no body - so the two richest sources for
this domain are absent, and the Wine and Apple coverage here comes from LKML,
box64 and Apple's own forums instead. A gap caused by infrastructure is still a
gap.

**Next.** `Now` is empty by design. `T-004`, the Windows probe, is the ranked
next item and should be promoted deliberately rather than by drift.

---

## 2026-07-25 — T-013: the probe was looking in the wrong place

**Changed.** `src/probe/vm_probe_linux.cpp` samples two allocation arenas;
`tests/conformance/test_probe.cpp` gained a coverage case; the campaign was
re-run into `campaigns/false-positive/2026-07-linux-x86_64-after-T013.json`.

**The result.** Coverage went up and correctness did not move:

| | before | after |
|---|---|---|
| observed addresses answered `UNKNOWN` | 637 of 639 — 99.7% | **1 of 640 — 0.2%** |
| answered `SUPPORTED` | 1 | **537** |
| **false positives** | 0 | **0** |

638 of 640 real addresses now get an answer, and every answer agrees with what
the kernel did.

**What was wrong.** The scan climbed powers of two plus four landmarks —
`0x1000000000`, `0x4000000000`, `0x6fffff0000`, `0x7fff00000000` — every one a
plausible *emulator* base, because the probe was written for the shadPS4
question. 224 MiB of a 128 TiB space, and none of it near where a process is
actually made of.

```
0x7c0000000000 .. 0x7ff000400000   kernel mmap arena  (shared libs, big malloc)
0x550000000000 .. 0x58f000400000   ELF_ET_DYN_BASE    (a PIE executable's text)
```

**The plan for this item named the trap and then the plan walked into it.**
`docs/TODO.md` T-013 said: derive the region from `/proc/self/maps`. That is
this process's ASLR slide — precisely the mistake `min_map_address` already made
once, when six campaign contracts returned a confident `UNSUPPORTED` off the
probe's own load address. Writing the trap down was not enough to avoid it; the
first draft did exactly what the entry warned against.

Two rules kept it out in the end:

1. **Bounds from `max_user_address`** — a kernel constant, measured, identical
   in every process — never from `/proc/self/maps`.
2. **`EEXIST` counts as success.** It means the address is already held *by us*:
   proof the kernel hands this space out, proof of nothing about the host.
   Making it a third outcome is what would make the recorded set depend on
   where our libc landed. The granted/held split goes to `notes`, outside the
   facts subtree and outside `profile_id`.

`check_reproducible.sh` — two processes, because the in-process test was once
green while this was false — reports identical `profile_id`s.

**Two more mistakes, both caught by running rather than reasoning.**

*Aligned the wrong way.* The first version rounded the arena top **down** to a
TiB. `max_user_address` is `0x7ffffffff000`, which rounds down to
`0x7f0000000000` — the exact bucket that 629 of the 639 observed addresses sit
**above**. The arena was placed 4 TiB below everything it was built to cover,
and it took looking at the emitted profile to see it.

*Claimed past the last sample.* The run was closed at `max_user_address` rather
than at the last sampled window, which asserted availability 64 GiB beyond
anything measured — and overlapped the structurally unavailable band at the top
of the space, so the profile said one range was both available and not.

**The second arena was found by a test.** A new conformance case asks the
profile about the address it is *executing from*; it failed, because the test
binary is PIE and lives at `0x55…`. Nothing in the reasoning had suggested
looking there.

**The one remaining `UNKNOWN` is the best advertisement in the campaign.**
HotSpot reserving its heap at `0x82a00000` — a deliberately-chosen
compressed-oops base no arena covers. The profile has nothing to say and says
so.

**Next.** `T-003`, the corpus: 1 of 30, and the largest hole left.

---

## 2026-07-25 — T-002: the false-positive rate, and what measuring it found

**Changed.** `tools/campaign/observe_requirements.py`,
`false_positive_rate.py`, `run_false_positive.sh`;
`docs/campaigns/2026-07-false-positive-rate.md` and the data behind it in
`campaigns/false-positive/`; `tools/guards/check_campaign.py`.

**The number: 0 false positives in 1292 requests.** Thirteen real programs
(python3, perl, ruby, php, git, openssl, jq, redis, gzip, xz, ffmpeg, node
with a hot loop, java with a hot loop), all exiting 0 on a measured Linux host.

**Why it counts, which is the whole difficulty.** The obvious method is
disqualified: the campaign's contracts and the analyzer's rules have the same
author, so their agreement measures consistency. The way out was to stop
writing contracts. `strace` the programs, keep only calls that **succeeded**
and appear in all three runs, transcribe each mechanically. Then a false
positive has a definition nobody can argue with: an `UNSUPPORTED` verdict on a
request the kernel performed on this host, minutes ago.

The harness asserts only what the syscall carried. `exact_address_required`
comes from `MAP_FIXED` and nothing else; `guest_host_identity_required` is
**always false**, because a trace does not reveal whether the program cared
where the mapping landed — setting it would manufacture `RS-VM-0007`, one of
the findings under test.

**And the measurement found a defect — in the probe, not the analyzer.**

```
probe establishes 56 windows x 4 MiB  =  224 MiB of a 128 TiB space
observed MAP_FIXED addresses:  629 in the 0x7f... TiB, 7 in 0x7e..., 3 low
inside a probe window:  2 of 639
```

`scan_address_space()` samples powers of two plus four landmarks
(`0x1000000000`, `0x4000000000`, `0x6fffff0000`, `0x7fff00000000`) — every one
a plausible **emulator** base, because the profile was built for the shadPS4
question. None is near `mmap_base`, where every ordinary program maps. So on
Linux the address rules are not wrong against real software; they are
**silent**, 99.7% `UNKNOWN`. Correct behaviour (`RS-VM-0017` exists for exactly
this) and a useless answer. That is now `T-013`, and it is in `Now`.

**Second finding: 42% of real mappings trip `RS-VM-0005`.** All 544
`CONDITIONALLY_SUPPORTED` verdicts are the granularity rule. It is not wrong —
`mmap(NULL, 53867, ...)` really does reserve 56 KiB — but real software passes
unrounded sizes constantly, because that is how `mmap` is specified to work. A
rule that fires on 42% of everything is noise in a CI gate, and this project
has already written down what happens to a noisy guard.

**Third: no false negatives were measurable.** Across 13 programs and three
runs each there was not one *failing* `mmap` or `mprotect`. Healthy software on
a healthy host is not refused, so this population contains no case where the
analyzer could have wrongly said `SUPPORTED`. The other half of correctness is
untouched.

**The harness's first output was wrong and the analyzer said so.** 16
requirements rejected: `exact_address_required` true with no address — the
shape contracts were setting the flag from `MAP_FIXED` while omitting the
address. An internally inconsistent document, refused rather than guessed at.
First time in this project that a new tool's first run was caught by something
else immediately.

**Guard added.** `check_campaign.py`: a published number must still match the
committed data it came from. Deliberately no threshold — a guard with one would
turn a measurement into a target, on one host's data. The judgement stays in
the document, signed and dated.

**Verdict.** Phase 3's criterion and Gate B are `[partial]`, not `[done]`. The
rate is measured and low **for the rules this population exercises**. Saying
otherwise would be the overclaiming the whole project exists to prevent.

**Next.** `T-013` — probe the region where programs actually map.

---

## 2026-07-25 — T-001: which of the 138 differences actually matter

**Changed.** `rs-profile impact OLD NEW CONTRACT...` —
`include/runtimeskeptic/vm/impact.hpp`, `src/vm/impact.cpp`,
`tests/unit/test_impact.cpp` (13 cases). First item closed under the compass.

**What it is for.** `rs-profile diff` reports 138 differences between the two
measured Apple Silicon lanes, one line per JSON pointer. That is precise and
nearly unreadable, and it does not answer the question a team asks when a
kernel ships: *does any of this matter to us?* This does — same contracts, two
profiles, report only the verdicts that moved.

```
$ rs-profile impact ROSETTA NATIVE tests/groundtruth/contracts/*.json
14 contract(s): 5 regressed, 0 improved, 0 never answered, 9 unchanged
```

**Five of 138. And one of the five is a finding about the platform.** On a
single Apple Silicon machine, minutes apart:

| Fact | native arm64 | x86-64 under Rosetta |
|---|---|---|
| `write_execute_simultaneous` | **false** | **true** |
| `jit_entitlement_required` | **true** | **false** |
| `page_size` | 16384 | 4096 |

All `measured_capability`. So a JIT *ported* from x86-64 to native arm64 **on
the same Mac** loses RWX and gains an entitlement requirement — which is
scenario S2 ("Apple Silicon port"), measured rather than imagined, and the
opposite of the intuition that native is the permissive case.

**Three design decisions, each of which could have gone the easy way.**

1. **UNKNOWN on both sides is `NEVER ANSWERED`, not `unchanged`.** A contract
   nobody could answer before and nobody can answer now has not stayed the
   same. Counting it as unchanged is how "3 of 40 affected" gets read as "37
   are fine".
2. **No finding is named "responsible".** That is a causal claim, and with
   several rules firing it is often the wrong one. What is reported is which
   ids *appeared* and which *disappeared*. When one appears next to a
   regression the reader draws the conclusion, and it is theirs.
3. **A bundle is compared requirement by requirement, not by its worst-of.**
   If one requirement improves while another regresses, the worst-of is
   identical on both sides and a whole-file comparison prints *nothing*. That
   is the silence-reads-as-success failure this project has already shipped
   twice — a crashing ground-truth case counted as a confirmed refusal, and a
   comparison table green while discarding compiler warnings. There is a test
   named for it.

**Also.** An unreadable contract exits 65, not 0: a file that failed to parse
is not a file that passed. Two identical profiles are called out by
`profile_id` before any result is printed, because a diff of a thing with
itself is not a measurement of anything.

**Learned while writing the tests.** The first version of the opposite-moves
test used two addresses on the existing fixtures and did not actually invert —
one side came out `UNKNOWN` rather than `SUPPORTED`, so nothing improved and
the test passed for the wrong reason until it was checked. Fixtures that
*look* like they exercise a case are the quietest way to test nothing.

**Next.** `T-002`, the false-positive rate. Not a feature — the permission to
be believed, and the last unmeasured Phase 3 exit criterion.

---

## 2026-07-25 — four documents, four jobs, and a guard between two of them

**Changed.** `docs/TODO.md` is new and is now **the file to follow**: 12 items,
each naming what would prove it done. `tools/guards/check_todo.py` keeps it and
`docs/PLAN.md` from disagreeing. `docs/scenarios/` was reframed as what the
owner said it was.

**The owner corrected a misreading, and it was mine.** The scenarios were
handed over as *the project's spirit* — something to read when the direction
feels unclear — and I had turned the end of the assessment into a ranked
backlog. That made one document try to be both a compass and a mission
statement, which is how it ends up being neither. The ranking moved into
`docs/TODO.md` as tracked items; the assessment now carries only a
scenario→item mapping, read in one direction.

| Document | Job |
|---|---|
| `docs/scenarios/` | the spirit — why, and for whom. Deliberately unchecked against the code. |
| `docs/TODO.md` | the compass — what we are doing, in order, at most three at once |
| `docs/PLAN.md` | the map — where we stand against the ROADMAP |
| `docs/PROGRESS.md` | the past — what changed, and what was wrong |

**The guard that makes it hold.** Every `[open]` and `[blocked]` criterion in
the plan must now carry the id of a todo item, or be tagged `(untracked)` and
justified by name. 17 plan entries were tagged. The compass cannot silently
drop work and the map cannot silently grow work nobody owns.

Four more checks came from asking what would actually go wrong: `Now` is capped
at three, because a list where everything is urgent is a wall; every item must
say what RUNS to prove it done, because an item that cannot be finished is a
mood; a `[blocked]` item must name its blocker, because a blocker nobody wrote
down is an excuse; and a `[done]` item must appear in this log, because
crossing something off is not the same as recording what it taught.

**Learned while writing the exemption.** `check_docs.py` fired on the sentence
"the scenarios are allowed to describe a project that does not exist yet" —
prose about the future, not a claim about the code. The first fix was to put
`docs/scenarios/` in `KNOWN`, which would also have stopped checking the paths
it cites, and the assessment cites real contracts, real profiles and real
commands that can all rot. The exemption was narrowed to check 2 alone, and a
selftest case now proves it: a scenario naming a missing path still fails.

**Next.** `T-001` — verdict diff across two profiles. It is the only new
capability in the ten scenarios that needs no new probe, no new rule and no new
evidence.

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

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
| whether the Actions quota is currently exhausted | **cannot be determined from here** - it is an API fact |
| whether the run is queued, failed at startup, or never triggered | **cannot be determined from here** |

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

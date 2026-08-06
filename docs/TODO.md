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

When an item is finished it **leaves the compass**: its account goes into
`docs/PROGRESS.md` (the past, newest first) in the same commit, and its line is
removed from here. The compass carries only the working method and the work
still ahead; the wake lives in the log, where `docs/PROGRESS.md` already keeps
what each finished item changed and taught. Work that completes without a
PROGRESS entry is work that will be redone.

---

## Cold start — read this before anything else

For the session that begins after any amount of forgetting, on any model.
Everything here is measured or enforced, not remembered.

**Where the project stands (2026-08-06):**

```
Phase 0-4   DONE. Gate B, all seven demonstrations, T-012 bounded pressure,
            strict rule-execution coverage and the runtime SDK matrix are green.
Phase 5     N/A. ADR-0001 fixes RuntimeSkeptic as a standalone product.
Phase 6-10  N/A for the v0.2 product line; reopening requires an accepted
            Plan v2 with new evidence gates and queue items.
Gate B      false-positive rate measured 0 on THREE operating systems:
              Linux x86-64       1292 requirements  (strace)
              macOS 14 arm64       37 requirements  (dtrace, mach traps)
              Windows 10.0.26100  247 requirements  (ETW)
            174 RS-VM-0005 notes on Windows, all SUPPORTED.
```

**What "finish this project" now means.** The standalone v0.2 product closes
at Phase 4 under `docs/decisions/0001-standalone-product-boundary.md`. Phases
5-10 are not incomplete work in this line; they require a separately accepted
Plan v2. Every completed or retired item is consumed into `docs/PROGRESS.md`.

**How to verify the tree before believing anything, including this file:**

```
bash tools/guards/run_all.sh          every guard must run and pass
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

**A rule for maintaining THIS file:** `plan.md` and `ROADMAP.md` are frozen by
hash guards; `docs/PLAN.md` updates status and current evidence; this file
and `docs/PROGRESS.md` carry the changing queue and immutable history. A
weekly scheduled session (Mondays 06:00 UTC) reads the CI channels and
reports drift; it cannot push.

---

## Now

No release-blocking executable item remains in the accepted v0.2 scope.

---

## Next

Any new product work starts with an owner-accepted Plan v2. It must define its
own scope, evidence gates and consumable tasks before implementation begins.

---

## Later

### T-008 — Fleet aggregation `[later]`

**Serves:** S8 (500 applications, one policy question)
**Plan:** no criterion; a scenario capability
**Done when:** a run over a manifest of applications aggregates by finding id
and answers "how many are affected, and which".

Straightforward, and worth nothing before T-002. Answering "31 applications
affected" with an unmeasured false-positive rate is worse than not answering.

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

None. T-011 was consumed by the standalone product decision; Phases 5-10 are
outside the current queue rather than blocked work.

---

## Deliberately not tracked

Each needs a reason, so this cannot quietly become a way to empty the list.

- **Documentation accuracy** (`docs/PLAN.md`, `[partial]`) — permanently
  partial by nature. Only paths and a fixed list of phrases are mechanical;
  prose is not compiled and never will be. `tools/guards/check_docs.py` covers
  what can be covered, and there is no end state to reach.

---

## Done

Finished items leave this compass when they are complete. Their accounts move
to docs/PROGRESS.md, newest first, in the same change. The false-positive
campaign, Gate B, evidence bundles, platform probes, and every review-hardening
round are recorded there. Read the log for what each changed and taught; counts
are derived by tools and are never restated here.


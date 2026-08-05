# Runtime CodeSkeptic - Immutable Plan

This file is the stable project plan. It contains no task status, campaign
count, release number, or other value that changes as implementation proceeds.
Its content is pinned by `tools/guards/plan.sha256` and must not be edited.

## Product outcome

Runtime CodeSkeptic decides whether a program's declared virtual-memory
requirements are compatible with a measured host profile. Every verdict must
be reproducible, attributable to evidence, and safe to consume in automation.

## Completion contract

The project is complete only when all of these remain true:

1. Published requirements, profiles, results, and replay bundles are validated
   before analysis; malformed or unverifiable input fails closed.
2. Identical normalized inputs produce deterministic verdicts, finding IDs,
   reports, and replay evidence.
3. Supported operating systems are measured by native probes and portability
   is demonstrated by warning-clean builds and tests on their toolchains.
4. A release contains everything needed to run its documented demo without a
   compiler, source checkout, download, or network connection.
5. Public claims, schemas, campaign numbers, guard behavior, and release
   contents are mechanically checked rather than trusted as prose.

The detailed frozen product specification is `ROADMAP.md`; normative exclusions
are in `docs/non_goals.md`.

## Working records

- `docs/TODO.md` is the consumable queue. Completed work leaves the queue.
- `docs/PROGRESS.md` is the newest-first, append-only evidence journal. Existing
  entries are history and may not be rewritten.
- `docs/PLAN.md` is a mutable criteria/evidence ledger retained for the current
  guard and review workflow. Despite its historical filename, it is not this
  immutable plan.

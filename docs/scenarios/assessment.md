# Scenario assessment — what the tool does today, for each of the ten

Every status here was produced by running the tool, on 2026-07-25, against a
**measured** profile — never by reading the code and forming an opinion. The
command is printed with each one so the row can be re-derived rather than
believed. Markers are the same vocabulary as `docs/PLAN.md` and are checked by
`tools/guards/check_plan.py`:

| Marker | Means |
|---|---|
| `[done]` | the scenario works end to end, and something that runs proves it |
| `[partial]` | part of it works, with the missing part named on the same line |
| `[open]` | not started, or started and never executed |
| `[blocked]` | not possible, with the blocker named |

Profiles used:

```
LINUX   probed on this machine by rs-env-probe
MAC     profiles/measured/macos-14-arm64-native.measured.json          16 KiB pages
ROSETTA profiles/measured/macos-14-arm64-rosetta-x86_64.measured.json   4 KiB pages
```

---

## S1 — shadPS4: `mmap(0x1000000000, ...)` fails five seconds in

`[done]` — the motivating incident, diagnosed against a measured host, both
Apple Silicon lanes: `tests/groundtruth/contracts/exact-mapping-in-carveout.json`
and `RS-VM-0001`

```
$ rs-check tests/groundtruth/contracts/exact-mapping-in-carveout.json --profile ROSETTA
UNSUPPORTED
  RS-VM-0001  PROVEN  UNSUPPORTED
  requested range intersects unavailable range [0x1000000000, 0x7000000000)
  (mach_vm_allocate(VM_FLAGS_FIXED) refused with KERN_NO_SPACE ...)
```

This is the scenario the owner called the strongest, and it is the one that
holds up best. Two things are worth adding to the account in the source
document.

**The carveout is not a Rosetta artefact.** `[0xfc0000000, 0x1000000000)` and
`[0x1000000000, 0x7000000000)` were measured on **native arm64 as well**, which
makes shadPS4's own `ARCH_X86_64` guard narrower than the condition it guards
against. That is a finding about shadPS4, produced by this tool, and it is the
first thing the project has said that its subject did not already know.

**`PROVEN` here is earned by the contract, not by the address.** See
`README.md`: the same address extracted from source rather than stated as a
guarantee returns `COUNTEREXAMPLE`. The verdict and the remediation do not
change; the label on the evidence does.

---

## S2 — Apple Silicon port: 16 KiB pages under a program compiled for 4 KiB

`[partial]` — the verdict is produced and is `PROVEN`; the *consequence list*
the scenario asks for (allocator, JIT, guard page, signal layout) is not

```
$ rs-check tests/groundtruth/contracts/page-size-at-most-4kib.json --profile MAC
UNSUPPORTED
  RS-VM-0006  PROVEN  UNSUPPORTED   host page size differs from the compiled-in size
  RS-VM-0005  PROVEN  UNSUPPORTED   size is not a multiple of the allocation granularity
```

Two findings rather than one, which is more useful than the scenario predicts:
the second says the mapping *succeeds* while covering more address space than
asked for, and that is the failure mode that survives testing and reaches a
customer.

What is missing is the second half of the scenario. RuntimeSkeptic says *the
page size is wrong*; it does not say *and therefore your allocator, your JIT,
your guard pages and your signal stack layout are each at risk*. Those are four
distinct downstream claims and the analyzer has no model of any of them. Writing
them as prose in a report would be exactly the "AI-generated interpretation"
that `docs/PLAN.md` Phase 0 forbids. Doing it honestly needs a fact in the
requirement document — the program declaring which of its subsystems are
page-size-dependent — and that field does not exist yet. <!-- checked: 2026-07-25 -->

---

## S3 — Wine: reserve/commit semantics

`[done]` — `RS-VM-0012` is confirmed at both platform failure points.

The scenario is a semantic comparison, not an exhaustion stunt. A measured
Windows profile establishes `windows_reserve_commit`; the Windows CTest
`test_windows_reserve_commit_job` then proves a 256 MiB reservation succeeds
while a 128 MiB `MEM_COMMIT` fails synchronously with native error 1455
inside a worker-only Job Object. On Linux, the cgroup-v2 ground-truth lane
proves reservation and `mprotect` succeed before first touch is SIGKILLed,
with the leaf-local `oom_kill` counter incrementing. Wine's measured
POSIX-lazy profile therefore differs from the Windows contract in the exact
way the rule reports, without exposing the shared runner to an OOM kill.

CI run 31046034092 passed both bounded controls and the case-specific
execution oracle. The earlier claim that this could not be provoked safely
was true only before worker-only Job Object and cgroup containment existed.
---

## S4 — LuaJIT: machine code must land within ±2 GB

`[partial]` — the constraint is carried, and the analyzer declares out loud that
it cannot evaluate it: `RS-VM-0024`, `contracts/campaign/luajit-mcode-jumprange-x64.json`

```
$ rs-check contracts/campaign/luajit-mcode-jumprange-x64.json --profile MAC
UNKNOWN
  RS-VM-0011  COUNTEREXAMPLE  the capability exists but is gated by process policy
  RS-VM-0024  HYPOTHESIS      "This constraint is carried but NOT evaluated in v0.1.
                               The rest of this verdict says nothing about it."
```

The scenario expects `Relative jump constraint cannot be satisfied`. The tool
will not say that, and should not: a host profile records what the address space
looks like, not where two regions of a *future* process will land relative to
each other. Deciding it needs a runtime observation of both regions, or an
allocation-order model, and v0.1 has neither.

What it does instead is the reason this row is `[partial]` and not `[open]`. The
overall verdict drops to `UNKNOWN` — which, by the aggregation order, outranks
`CONDITIONALLY_SUPPORTED` — so the unanswered question cannot be mistaken for a
clean bill of health. Without `RS-VM-0024` this contract reads as "64 KiB
anonymous RW, later RX", which is trivially supportable and completely beside
the point.

---

## S5 — mimalloc: large-page alignment assumption

`[partial]` — the rules exist and execute (`RS-VM-0004`, `RS-VM-0005`,
`RS-VM-0022`); no real allocator has ever been run through them

`RS-VM-0022` exists because of exactly this scenario's shape: `required_alignment`
was only consulted when an address was *also* given, so "give me 4 GiB aligned to
4 GiB, I do not care where" — the request every allocator and every pointer-cage
makes — fell straight through. Deleting the field from a V8 cage contract
produced a byte-identical verdict, which is the definition of dead code.

The gap is corpus, not code. `docs/PLAN.md` Phase 0 wants 30 classified real
incidents and has 1. "Customer says it broke on Mac" is a real incident, and
mimalloc, jemalloc and V8 are three projects whose shipped configurations are
public. This scenario is the best available argument for closing that hole.

---

## S6 — CI pipeline: reject the PR before the customer sees it

`[done]` — `.github/workflows/ci.yml` is wired and evidence-qualified.

`rs-check` returns stable CI codes for all four verdicts, and the
repository evaluates shipped contracts on every pull request without
launching the target application. Gate B is measured rather than assumed:
0 false positives across Linux (1292 observed requirements), macOS (37),
and Windows (247). The separate strict ground-truth aggregate rejects
false-negative evidence gaps and contradicted pairings. A pipeline may
therefore block on UNSUPPORTED while treating CONDITIONAL and UNKNOWN as
explicit policy choices, not as accidental success.

CI run 31046034092 passed compatibility, determinism, all three native
platform jobs, the bounded pressure controls, and strict execution coverage.
---

## S7 — Game studio: five platforms, one report

`[partial]` — all named platform families except Steam Deck have measured
profiles, but no command produces the requested per-platform matrix.

The tool answers one contract against one host. Linux, macOS Apple
Silicon, Rosetta x86-64, native Windows x64 and Wine-on-Linux profiles
are available or measured by CI, so the evidence side is no longer a
stub. The missing capability is the report join: evaluate one contract
against a selected profile set and render verdict changes as one table.

Steam Deck remains deliberately unclaimed. It is a specific kernel and
userspace configuration, not a synonym for generic Linux; a measured
Steam Deck profile is required before it appears in such a matrix.
---

## S8 — Security team: 500 applications, RWX now forbidden

`[open]` — one bundle of many requirements works; a rollup across many
applications does not

`rs-check` accepts `runtime-skeptic.application-requirements-bundle.v1` and
evaluates every requirement in it, taking the worst verdict as the run's. So
"one application, many requirements" is solved.

The scenario is the other axis: *many applications, one policy question*, with
the answer "31 affected" and a list. That needs a fleet-level artifact — a
manifest of applications, a run over all of them, and an aggregation keyed by
the finding id rather than by the application. None of it exists.

Worth noting that the policy question itself is already expressible: `RS-VM-0009`
(simultaneous write+execute restricted), `RS-VM-0010` (anonymous executable
mapping unavailable) and `RS-VM-0011` (executable memory needs an entitlement)
are three different ways an RWX ban bites, and a report that collapsed them into
one number would be less useful than the three.

---

## S9 — New kernel: does our emulator care?

`[partial]` — `rs-profile diff` works and is precise; mapping a diff back to
*which contracts are affected* does not exist

```
$ rs-profile diff profiles/measured/macos-14-arm64-native.measured.json \
                  profiles/measured/macos-14-arm64-rosetta-x86_64.measured.json
138 difference(s):
  /platform/process_arch: "aarch64" -> "x86_64"
  /virtual_memory/allocation_granularity/value: 16384 -> 4096
  ...
```

That is the first half of the scenario and it is solid — including the honest
detail that two runs on *the same machine* differ in 138 places, because the
lanes genuinely differ.

The second half — "Potential impact: shadPS4, Box64, QEMU" — is a join the tool
cannot perform. It would need to re-evaluate a set of known contracts against
both profiles and report the ones whose verdict *changed*. Every part is
present: contracts are files, profiles are files, the analyzer is deterministic.
Nothing wires them together.

This is the cheapest genuinely new capability in the whole document, and unlike
S7 and S8 it needs no new evidence, no new probe and no new rule — only a loop
and a comparison.

---

## S10 — Code review: a PR comment before the code is merged

`[blocked]` — by the owner's standing instruction that CodeSkeptic is not to be
modified; ROADMAP Phase 5, `docs/PLAN.md`

An extractor was built inside this repository on 2026-07-25 and it *worked* — it
recovered the shadPS4 issue #4157 pattern from source text and reached the same
`RS-VM-0001 / UNSUPPORTED` verdict as the hand-written contract. It also broke
`docs/non_goals.md` section 18, and the owner chose to remove it rather than
grant an exception. What it learned is preserved in `docs/PROGRESS.md`.

So this scenario is blocked by a decision, not by difficulty, and the decision
looks right: the alternative was two extractors drifting apart in two
repositories.

The one thing this row should carry forward is the ceiling in `README.md`. When
the pipeline is eventually built, the verdicts it produces will be
`COUNTEREXAMPLE`, not `PROVEN`, and any report template written in advance
should say so.

---

## What the gaps become

**They become items in `docs/TODO.md`, and this document does not track them.**
That separation is deliberate and it is the owner's: the scenarios are the
project's spirit, kept to be read when the direction feels unclear, not a
backlog to be worked through. A document that is both a compass and a mission
statement ends up serving neither.

The mapping, for reading in one direction only — from a scenario to the work
that would serve it:

| Scenario | Item in `docs/TODO.md` |
|---|---|
| S9, S7 | `T-001` verdict diff across two profiles |
| S6 | `T-002` measure the false-positive rate |
| S5, S2 | `T-003` corpus: 30 real incidents |
| S3, S7 | `T-004` Windows probe |
| S8 | `T-008` fleet aggregation |
| S2 (second half) | `T-010` downstream-consequence modelling |
| S10 | `T-011` CodeSkeptic integration — blocked |
| S3 | `T-012` completed; bounded Windows and Linux controls are in PROGRESS |
| S4 | `T-009` runtime wrapper; a displacement constraint needs an observation |

`tools/guards/check_todo.py` keeps that list and `docs/PLAN.md` from drifting
apart. Nothing keeps *this* document in step with either, on purpose — the
scenarios are allowed to describe a project that does not exist yet. That is
what they are for.

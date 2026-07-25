# Scenarios — the spirit of the project

**This is not a work list, and it must never become one.** The owner's words
when handing it over:

> Senaryoyu görev olsun diye vermedim. Projenin ruhu olsun, yol gösterici olsun
> istedim. Kaybolacak olursak bize ruhu hatırlatsın.
>
> *(I did not give the scenarios as a task. I meant them as the project's
> spirit, as a guide. If we get lost, let them remind us of the spirit.)*

So this is what to read when the direction feels unclear — not to find the next
task. The next task is in `docs/TODO.md`, which is the compass, and where the
gaps named here have already been turned into tracked items.

| Document | Job |
|---|---|
| `docs/scenarios/` | **the spirit** — why, and for whom |
| `docs/TODO.md` | **the compass** — what we are doing, in order |
| `docs/PLAN.md` | **the map** — where we stand against the ROADMAP |
| `docs/PROGRESS.md` | **the past** — what changed, and what was wrong |

**Status: official while this repository is private, and provisional by
decision.** The owner's instruction on 2026-07-25 was to adopt these as project
documents now and to replace or withdraw them later. They are not normative:
`docs/non_goals.md` is a set of commitments, this is a set of *targets*, and a
target the tool cannot hit is recorded here as one rather than quietly dropped.
Nothing checks these documents against the code, deliberately — the scenarios
are allowed to describe a project that does not exist yet.

## The three files

| File | What it is |
|---|---|
| `owner-scenarios-2026-07-25.txt` | the owner's text, **byte-for-byte**, sha256 `649abfa4c366e0e8922e3cc7d17b3a541c1bf726813d345d492b62166a6ccdaf` |
| `assessment.md` | what the tool actually does for each of the ten, each status backed by a command that was run |
| this file | how to read them, and the one finding that changes the plan |

The source is kept in the language it was written in and is never edited. A
source document is quoted, not paraphrased: the moment it is tidied up, nobody
can tell which parts were the owner's and which were the tool's. The assessment
is in English to match the rest of the repository.

## What the scenarios are actually saying

The owner's own summary, and it is the sharpest sentence written about this
project so far:

> RuntimeSkeptic'in sattığı şey aslında "VM analizi" değil. Sattığı şey şu:
> **"Kodunuzun platform hakkında sessizce yaptığı varsayımları, kullanıcıya
> ulaşmadan önce görünür hale getirmek."**
>
> *(What RuntimeSkeptic sells is not "VM analysis". It sells this: making the
> assumptions your code silently makes about the platform visible before they
> reach a user.)*

That framing survives contact with the code. The ten scenarios are ten places
where a silent assumption meets a platform that does not honour it.

## The finding that changes the plan

The owner's Scenario 1 ends with `Confidence: PROVEN`, and the closing vision is
a pipeline where CodeSkeptic extracts a requirement from source, RuntimeSkeptic
judges it, and an AI writes the report. **Both are right, but they cannot be
right at the same time**, and the reason is a rule this project already
enforces.

`confidence_ceiling()` in `include/runtimeskeptic/core/evidence.hpp` caps a
verdict by the *weakest* evidence in its chain. `statically_inferred` — which is
what any extraction from source text is — ceilings at `COUNTEREXAMPLE`. Only
`specified_guarantee` and `measured_capability` permit `PROVEN`.

Measured, not argued. The same address, the same host, two contracts:

```
$ rs-check tests/groundtruth/contracts/exact-mapping-in-carveout.json \
    --profile profiles/measured/macos-14-arm64-rosetta-x86_64.measured.json
  UNSUPPORTED   RS-VM-0001   PROVEN           application evidence: specified_guarantee

$ rs-check contracts/gtav-rage-direct-memory-mapping.json \
    --profile profiles/measured/macos-14-arm64-rosetta-x86_64.measured.json
  UNSUPPORTED   RS-VM-0001   COUNTEREXAMPLE   application evidence: statically_inferred
```

So: **an automatically extracted requirement can never produce a PROVEN
verdict.** The verdict is the same — `UNSUPPORTED`, same rule, same host fact,
same remediation — but it is honestly labelled as resting on a reading of source
rather than on a stated guarantee.

This is not a defect to fix. It is the project's central claim working: a text
match must not be able to manufacture a proof. But it does mean two things for
the roadmap the scenarios describe:

1. Scenario 1's `Confidence: PROVEN` is reachable **only** for a hand-authored
   contract. The doc's own Scenario 10 pipeline caps one level lower.
2. A report that says "Confidence: 94%" would be a *third* thing — neither the
   evidence class nor the confidence level, but a number with no definition in
   this project. `docs/evidence_model.md` has no percentages in it, deliberately,
   and adding one would be the single easiest way to undo the whole model.

The honest version of the closing vision is therefore:

```
C/C++ source
    -> CodeSkeptic            (statically_inferred)
    -> requirement bundle
    -> RuntimeSkeptic         + measured host profile
    -> UNSUPPORTED, RS-VM-0001, COUNTEREXAMPLE
       "this rests on reading your source, not on a guarantee you stated;
        here is the host measurement that contradicts it, and here is the
        one-line change to the contract that would raise it to PROVEN"
```

Which is a *better* product pitch than a percentage, because it tells the
developer exactly what to do to get a stronger answer.

## Scorecard

Full detail, with the command that establishes each row, in `assessment.md`.

```
S1  shadPS4 exact mapping        [done]      the motivating case, measured, both lanes
S2  Apple Silicon page size      [partial]   verdict yes; the consequence list no
S3  Wine reserve/commit          [open]      rule exists, never executed, no Windows profile
S4  LuaJIT +/-2 GB               [partial]   carried and declared unevaluable, out loud
S5  mimalloc alignment           [partial]   rules exist and run; no allocator ever tested
S6  CI pipeline gate             [partial]   mechanically works; Gate B unmeasured
S7  studio, five platforms       [open]      two families measured, no multi-host report
S8  security fleet, 500 apps     [open]      one bundle yes, a fleet rollup no
S9  new kernel, profile diff     [partial]   diff works; diff -> affected contracts does not
S10 PR review via CodeSkeptic    [blocked]   owner's instruction, Phase 5
```

One of ten works end to end, five do part of what they describe, three are not
started, and one is blocked by an instruction rather than by effort. That ratio
is the reason for writing this down instead of keeping the scenarios as a pitch.

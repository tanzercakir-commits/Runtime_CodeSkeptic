# Ground truth

Every other test in this repository checks that RuntimeSkeptic is *coherent*.
These check whether it is *right*.

A unit test proves the code does what the code says. A conformance test proves
the probe is self-consistent. The campaign compares verdicts against
expectations a human wrote. None of them runs the program and looks.

Each case here is a small program that performs the exact request a contract
describes, on the real host, and reports what actually happened. The runner
then compares that against what `rs-check` predicted:

```
prediction              requirement on reality
----------------------  ------------------------------------------
SUPPORTED               the operation must actually succeed
UNSUPPORTED             the operation must actually NOT succeed
CONDITIONALLY_SUPPORTED recorded, not asserted - see below
UNKNOWN                 no claim was made, so nothing is asserted
```

A disagreement is the point of the exercise. It means either the analyzer is
wrong about the platform or the contract misdescribes the program, and both
are findings worth more than a green tick.

## The independence rule

**A case program may not include or link any RuntimeSkeptic code.** Not the
probe, not the profile reader, not a helper. Plain POSIX and platform APIs
only.

If a case called into the probe to decide whether a mapping succeeded, this
directory would be the tool agreeing with itself in a more expensive way. The
syscalls are necessarily the same ones the probe makes - that is the thing
being measured - but the *judgement* about what happened has to be
independent, which means it is written out longhand in each case.

## Outcomes

A case prints one JSON object to stdout:

```json
{"case": "rwx-anonymous", "outcome": "refused",
 "detail": "mmap(PROT_READ|PROT_WRITE|PROT_EXEC) failed: errno=1 EPERM"}
```

| outcome | meaning |
|---|---|
| `satisfied` | the request succeeded *as specified*, including any exactness the contract requires |
| `satisfied-relocated` | the mapping succeeded at another address and this caller explicitly accepts relocation |
| `tail-addressable` | bytes past the requested size were accessible inside the host-rounded mapping |
| `misaligned` | a successful mapping lacked the stronger alignment the caller requires |
| `misaligned-einval` | an aligned-neighbour control succeeded, then the otherwise-identical misaligned exact request failed with EINVAL |
| `repeated-permanent-refusal` | every executed attempt failed with the same structural EINVAL refusal |
| `refused` | the call failed |
| `below-bound-unavailable` | an aligned control at the exclusive bound succeeded, while every page-aligned start below it was policy-denied |
| `relocated` | the call succeeded but not where it was asked to |
| `faulted` | the process took a signal performing the operation |
| `skipped` | the case does not apply to this host, with a reason |

`relocated` and `faulted` are distinct from `refused` on purpose. A program
that demands an exact address is not helped by a successful mapping somewhere
else, and the whole reason `MAP_FIXED` is dangerous is that it succeeds.

Those two are also the outcomes a well-behaved host rarely produces, which
makes them the ones most likely to be mishandled by the runner and never
noticed. `faulted` arrives for real from `file_map_beyond_eof` on a sigbus
host; `relocated` arrives from `hinted_relocation`. Both are additionally
exercised end-to-end by `selftest.sh`, because a comparison table that is
silently wrong makes every green run here meaningless.

Anything that can fault runs in a forked child, so a case that takes SIGBUS
reports it instead of taking the harness down with it.

## Why `CONDITIONALLY_SUPPORTED` is recorded and not asserted

That verdict means "succeeds if a condition outside the mapping call holds" -
a JIT entitlement, a signed binary, a kernel toggle. One execution can show
which side of the condition *this* process fell on, and cannot show the
condition is the reason. Asserting either way would be reading more out of a
single run than it contains. The outcome is printed and carried into the
report; it just does not pass or fail anything.

## Coverage

Case count and finding emission are not coverage. The harness optionally writes
a machine-readable execution ledger containing the measured profile id, actual
kernel outcome, analyzer verdict, pairing state and exact finding ids:

```console
$ GT_LEDGER=run.jsonl tests/groundtruth/run.sh PROFILE.json
$ tools/campaign/groundtruth_execution_coverage.py run.jsonl [run.jsonl ...]
```

`coverage_targets.json` supplies the rule-specific oracle. A branch is counted
only when the analyzer emitted that rule **and** the real case reached one of
the registered outcomes. A `CONDITIONAL` whole-contract verdict remains
unasserted by the harness, while a concrete `relocated` or `faulted` outcome may
still confirm the narrower branch it directly observes.

The grader fails closed on malformed JSON, unknown rule ids, empty ledgers,
broken cases and contradicted pairings. Rules that cannot be graded by this
method carry an explicit reason; everything else remains in the synthetic-only
backlog. CI produces Linux, constrained-Linux and macOS ledgers, then grades
their aggregate instead of mistaking one runner's missing branch for global
absence.

`groundtruth_coverage.py` is retained only for historical comparison. It counts
emitted findings and therefore must not be used as proof of execution coverage.

## Running

```console
$ cmake --build build
$ tests/groundtruth/run.sh PROFILE.json
```

The profile must be one measured on the machine the cases are running on.
Comparing a prediction made from one host's facts against an execution on a
different host measures nothing.

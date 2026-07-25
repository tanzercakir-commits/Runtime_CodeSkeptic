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
| `refused` | the call failed |
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

Case count is not coverage. What matters is which rules an execution has ever
checked, and `tools/campaign/groundtruth_coverage.py` counts what the analyzer
actually emits rather than what a manifest claims:

```console
$ tools/campaign/groundtruth_coverage.py PROFILE.json [PROFILE.json ...]
rules exercised by an execution: 9/25
of the 20 rules an execution could check, 9 are checked (45%)
```

Five rules are **not checkable this way at all**, and the tool prints the reason
for each rather than leaving them as an unexplained gap. `RS-VM-0012` claims
failures *move* from a checked call to an unchecked access under memory
pressure, and provoking that means exhausting the runner. `RS-VM-0014` is an
internal contradiction in a requirement, so no host is consulted and there is
nothing to execute. "Not yet written" and "not checkable" are different facts
and only one of them is a backlog item.

That number was wrong the first time it was printed, in the flattering
direction: the check grepped the manifest for rule ids mentioned in prose and
believed a commit message that claimed `RS-VM-0012` was covered. It is not -
the contract sets `commit_is_checked_call: true`, which is the POSIX idiom, and
the rule correctly returns early. The case and the contract were consistent
with each other; only the coverage claim was false.

## Running

```console
$ cmake --build build
$ tests/groundtruth/run.sh PROFILE.json
```

The profile must be one measured on the machine the cases are running on.
Comparing a prediction made from one host's facts against an execution on a
different host measures nothing.

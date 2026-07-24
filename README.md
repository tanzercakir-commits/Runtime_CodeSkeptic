# RuntimeSkeptic

**Predict runtime failures before they become crashes.**

RuntimeSkeptic is a cross-layer runtime compatibility analyzer. It detects
failures by comparing what a program *assumes* about its execution environment
against what that environment *measurably provides*.

Its question is not "did this call return an error?" but:

> Does the environment in which the program will execute provide the exact
> semantics that the program assumes?

**Status: v0.1, Phase 1-3 vertical slice.** Virtual memory only. See
[ROADMAP.md](ROADMAP.md) for the full plan and
[docs/non_goals.md](docs/non_goals.md) for what this deliberately is not.

---

## The failure family: silent contract degradation

```text
Application requires semantic guarantee X.
Compatibility layer approximates X with behavior Y.
Operating system permits Y but cannot provide X.
Compatibility layer reports success.
Application assumes X was satisfied.
Application later crashes, corrupts state, retries forever, or is wrong.
```

The critical failure is rarely the original OS error. It is the loss of
semantics between layers - and it is invisible to sanitizers, debuggers and
crash reporters, because at the moment of the mistake *nothing has gone wrong
yet*.

---

## The vertical slice, end to end

```text
probe host  ->  describe application requirement  ->  evaluate  ->  evidence report
```

```console
$ rs-env-probe vm --name my-laptop --output profiles/generated/host.json
profile written to profiles/generated/host.json
profile_id: sha256:a9dca1fe880b1642294646846fdbbcea3435a0767fc4d50d9ff5866b6e8c17b0

$ rs-check contracts/emulator-highmem-guest-mapping.json --profile profiles/generated/host.json
```

```text
  UNSUPPORTED

RS-VM-0003  Requested range extends past the end of the user address space
  severity critical  |  confidence PROVEN  |  impact UNSUPPORTED

  Required
    mapping placed exactly at [0x800000000000, 0x800000010000); guest address
    must equal host address
  Host capability
    range extends past the end of the user address space (0x7ffffffff000)
  Conclusion
    No execution satisfying all constraints exists on this host profile.

  Evidence chain
    [application]         program requires an exact mapping at 0x800000000000
      specified_guarantee <- src/guest_memory.cpp:138
    [compatibility_layer] no guest-to-host translation layer exists
      specified_guarantee <- requirement.assumptions
    [operating_system]    range extends past the end of the user address space
      measured_capability <- my-laptop

  Will not work
    x Retry the mapping
      The obstacle is a property of this host's address space, not a transient
      resource shortage. Every attempt fails identically.
```

No frame was rendered. No emulator was started. The contradiction was proven
from one measurement and one declared requirement.

---

## What makes this different from "an AI that reads your logs"

Three rules, enforced by the type system and by tests rather than by
convention:

**1. Every fact knows how it came to be known.** A capability is a
`Fact<T>`, and `Fact<T>` has no valid state that carries data without an
evidence class. `sysconf(_SC_PAGESIZE)` returning 16384 is
`measured_capability`. A number transcribed from a blog post is
`heuristic_risk`. They are not interchangeable.

**2. A finding is never stronger than its weakest supporting fact.**

| Weakest fact in the chain | Strongest claim allowed |
|---|---|
| `specified_guarantee`, `measured_capability` | `PROVEN` |
| `observed_invariant` | `OBSERVED_INVARIANT` |
| `statically_inferred`, `bounded_counterexample` | `COUNTEREXAMPLE` |
| `heuristic_risk` | `PREDICTIVE` |
| `unknown` | `HYPOTHESIS` |

This is one function, `clamp_confidence`, applied to every finding at
emission. A rule cannot bypass it.

**3. "Not observed" is not "impossible."** A range the probe never tested does
not appear in `unavailable_ranges`. It is absent from `available_ranges` too,
and any request touching it answers `UNKNOWN`. In the overall verdict,
`UNKNOWN` outranks `CONDITIONALLY_SUPPORTED` - because calling a request
"conditional" would falsely imply the conditions are known.

The same requirement against three different profiles:

| Profile | Verdict | Confidence | Why |
|---|---|---|---|
| Measured Linux host, address available | `SUPPORTED` | - | probe mapped that exact range |
| Measured Linux host, address above user space | `UNSUPPORTED` | `PROVEN` | measured `max_user_address` |
| Hand-authored macOS fixture | `UNSUPPORTED` | `PREDICTIVE` | facts are `heuristic_risk`, so no proof is claimed |

The third row is the point. The contradiction looks identical, and the tool
still refuses to call it proven.

---

## Build

No external dependencies. A C++20 compiler and CMake 3.20 are enough - by
design, because canonical output must be byte-identical across toolchains and
CI must work without network access.

```console
$ cmake -S . -B build -G Ninja
$ cmake --build build
$ ctest --test-dir build --output-on-failure
```

Binaries land in `build/bin/`.

---

## Tools

| Tool | Purpose |
|---|---|
| `rs-env-probe vm` | Measure this host's virtual-memory behavior; emit a profile |
| `rs-profile verify` | Validate a profile and report how many facts it actually knows |
| `rs-profile diff` | Compare two profiles' facts - did the platform change? |
| `rs-check` | Evaluate a requirement (or a bundle) against a profile |
| `rs-mcp` | The same capabilities over the Model Context Protocol |

### With CodeSkeptic

[CodeSkeptic](https://github.com/tanzercakir-commits/CodeSkeptic) answers the
static half of the question. Its `--runtime-assumptions` mode extracts what a
program assumes about its machine; `rs-check` decides whether a measured host
can satisfy it.

```console
$ codeskeptic --source src/ --build-path build --runtime-assumptions assumptions.json
$ rs-env-probe vm --output host-profile.json
$ rs-check assumptions.json --profile host-profile.json
```

Given the shape real emulators have -

```c
void* p = mmap(guest_base, size, prot, MAP_FIXED, -1, 0);
if (p == MAP_FAILED) return error;
assert(p == guest_base);
```

\- the extractor records the identity requirement **from the assert, not from
the error check**. `mmap` returns success whether or not it honoured the
address, so no error code will ever report that violation. See
[docs/integrations.md](docs/integrations.md).

### Exit codes

`rs-check` is built for CI:

| Code | Meaning |
|---|---|
| 0 | `SUPPORTED` |
| 1 | `UNSUPPORTED` |
| 2 | `CONDITIONALLY_SUPPORTED` |
| 3 | `UNKNOWN` |
| 64 | usage error |
| 65 | input unreadable or schema-invalid |
| 70 | internal error |

A job that should fail on proven contradictions but tolerate unknowns treats
`1` as failure and `2`/`3` as warnings.

---

## Probe safety

The probe operates next to the kernel boundary, so its constraints are hard
rather than aspirational (ROADMAP section 18):

- **`MAP_FIXED` is never used.** It unmaps whatever is already at the address.
  Exact placement is probed only through `MAP_FIXED_NOREPLACE`.
- **The `MAP_FIXED_NOREPLACE` flag is verified before it is trusted.** Kernels
  older than 4.17 ignore the unknown bit and silently relocate, which would
  make every "exact placement works" answer wrong. If the flag cannot be
  confirmed, exact-placement probing is skipped entirely and those facts stay
  `unknown`.
- **Faulting tests run in a forked child** with an alarm timeout. The parent
  records the signal as the observation.
- **Ranges occupied by the probe's own process image are not recorded as host
  limitations.** That is a fact about one process layout, not about the host.
- Every mapping the probe creates is unmapped again; no elevated privilege is
  required.

---

## Repository layout

```text
include/runtimeskeptic/   public headers
  core/                   json, sha256, evidence model, Fact<T>
  vm/                     address ranges, profiles, requirements, rules, findings
  probe/                  environment measurement
  reports/                rendering
src/                      implementations
tools/                    rs-env-probe, rs-profile, rs-check
schemas/                  versioned JSON schemas
contracts/                example application requirements
profiles/fixtures/        hand-authored profiles, clearly marked as unverified
tests/                    unit, conformance, incident and mutation suites
corpus/runtime_failures/  incident corpus
docs/                     problem statement, taxonomy, evidence model, findings registry
```

---

## Documentation

| Document | Contents |
|---|---|
| [docs/problem_statement.md](docs/problem_statement.md) | The error family and why existing tools miss it |
| [docs/failure_taxonomy.md](docs/failure_taxonomy.md) | The ten categories, with examples |
| [docs/evidence_model.md](docs/evidence_model.md) | Evidence classes, confidence ceiling, verdict aggregation |
| [docs/findings/registry.md](docs/findings/registry.md) | Every `RS-VM-xxxx` id and its stability policy |
| [docs/architecture/determinism.md](docs/architecture/determinism.md) | Canonicalization and `profile_id` |
| [docs/non_goals.md](docs/non_goals.md) | What to use instead, per non-goal |

---

## Honest limitations of v0.1

- Only Linux is probed. macOS and Windows build and run, but their probe is a
  stub that reports every fact as `unknown` - deliberately, rather than
  guessing plausible defaults.
- The macOS/Rosetta fixture in `profiles/fixtures/` was **not measured**. Its
  facts are `heuristic_risk` and every finding derived from it is capped at
  `PREDICTIVE`.
- The incident corpus is a set of reconstructed *patterns*, not verified
  citations. Each entry says so. Phase 0's exit criteria are not met yet.
- Requirements are written by hand. Automatic extraction from source is
  Phase 5 (CodeSkeptic integration).
- `translation_mode` is not detected; it is reported as `unknown` rather than
  assumed to be `none`.

---

## License

Apache-2.0. See [LICENSE](LICENSE).

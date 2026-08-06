# Integrations

Status: v0.2.

**RuntimeSkeptic has no required integrations.** It builds, tests and runs
against nothing but a C++20 compiler and CMake, and the whole vertical slice
- probe, evaluate, report - works with hand-authored requirement documents.
Everything on this page is optional.

| | State |
|---|---|
| MCP server (`rs-mcp`) | implemented, 25 protocol tests |
| Evidence bundles (`rs-check --bundle`, `rs-replay`) | implemented, round-trip tested across a process boundary in CI, fixture checked in |
| External requirement artifacts | supported through the producer-neutral v1 schemas |
| Runtime monitor (`libruntimeskeptic`, trace replay) | implemented; multi-platform CI gated |
| SARIF, GitHub Action | later phases |

---

## Command-line tools

Seven executables each do one thing. This table is the canonical invocation
map; the five general CLI tools also expose their full flag list through
`--help`.

| Tool | Does | Key invocation |
|---|---|---|
| `rs-check` | evaluate a requirement, or a whole bundle, against a host profile | `rs-check REQUIREMENT.json --profile PROFILE.json [--format text\|json\|markdown] [--output FILE] [--bundle DIR] [--no-unknowns] [--quiet] [--color]` |
| `rs-env-probe` | measure this host's virtual-memory behaviour into a profile | `rs-env-probe vm [--name NAME] [--output FILE] [--no-scan] [--no-faulting-tests] [--canonical]` |
| `rs-profile` | inspect and compare profiles | `rs-profile verify\|id\|diff\|impact PROFILE.json …` |
| `rs-replay` | re-derive a verdict from an evidence bundle, or purely replay a sealed runtime trace | `rs-replay BUNDLE_DIR` or `rs-replay trace TRACE.jsonl` |
| `rs-mcp` | the same capabilities as a Model Context Protocol server | `rs-mcp --serve` |
| `rs-runtime-sample` | record one selected VM lifecycle through the C ABI | `rs-runtime-sample TRACE.jsonl` |
| `rs-runtime-benchmark` | measure native, disabled, buffered and flush overhead | `rs-runtime-benchmark --iterations N --output FILE` |

Exit codes are uniform where they can be: `rs-check`, `rs-profile impact` and
`rs-replay` all use `1` for a substantive negative (UNSUPPORTED / a regression /
a bundle that did not reproduce), `65` for input that could not be read or did
not satisfy its schema, and `64` for a usage error. `rs-replay` returns `0` only
when the bundle re-derives its own verdict and finding IDs from its own bytes.

### Runtime calls: `libruntimeskeptic`, then `rs-replay trace`

The C ABI wraps selected POSIX and Windows virtual-memory APIs, preserves the
native result and post-call error state, and records into a fixed-capacity POD
buffer. `rs_runtime_flush_trace_v1` seals canonical JSONL; `rs-replay trace`
checks its schema, sequence, completeness and digest before reducing its
lifecycle without issuing OS calls. See `docs/runtime-monitor.md` for the ABI,
compile/runtime disable modes, checked exact-address expectations and safety
contract.

### Evidence bundles: `rs-check --bundle`, then `rs-replay`

`rs-check --bundle DIR` writes a self-contained, hash-sealed record of one
analysis — the requirement, the profile, the findings, a rendered report, and a
`manifest.json` of SHA-256 hashes — and re-runs the analysis from the written
files to certify it reproduces *before* the bundle is trusted. `rs-replay DIR`
then reads only that directory and re-derives the verdict, so a result can be
checked on a machine that never saw the original inputs:

```console
$ rs-check contract.json --profile host.json --bundle ./evidence
rs-check: wrote evidence bundle to './evidence' (replay: reproduced)

$ rs-replay ./evidence          # exit 0 = reproduced
```

`rs-replay` refuses the bundle — exit 1 or 65, never a silent pass — if a stored
file was edited after sealing (its hash no longer matches), if the manifest's
recorded verdict disagrees with the re-derived one, or if the bundle is missing
a file or hash the schema requires. An incomplete bundle is rejected, not
accepted as vacuously verified.

---

## As an MCP server

`rs-mcp` speaks the Model Context Protocol so an agent can measure a host and
evaluate requirements without shelling out and parsing text.

```json
{
  "mcpServers": {
    "runtimeskeptic": { "command": "/path/to/rs-mcp", "args": ["--serve"] }
  }
}
```

Newline-delimited JSON-RPC 2.0 on stdio, protocol version `2024-11-05`.
RuntimeSkeptic owns this interface independently; no second server, project or
agent configuration is required.

| Tool | Answers |
|---|---|
| `probe_host` | What can this machine actually do? |
| `check_requirement` | Can this host satisfy this requirement? |
| `verify_profile` | Is this profile well-formed, and how much does it know? |
| `diff_profiles` | Did the platform's measured behaviour change? |
| `describe_findings` | What does `RS-VM-0001` mean? What does `PREDICTIVE` mean? |

Every document argument can be passed either as a path (`profile_path`) or
inline (`profile_json`), so an agent that has just composed a requirement
never has to touch the disk.

Two conventions worth knowing:

- **Tool payloads are a JSON string inside `content[0].text`**, not
  `structuredContent`. Parse it.
- **Every input schema property is `type: "string"`**, including booleans
  (`"true"` / `"false"`). This costs some elegance and buys compatibility
  with clients that flatten arguments.

`describe_findings` returns the confidence vocabulary alongside the registry,
because an agent reading a verdict needs to know that `PROVEN` and
`PREDICTIVE` are not interchangeable.

---

## External requirement artifacts

`rs-check` accepts either a single
`runtime-skeptic.application-requirements.v1` document or a producer-neutral
`runtime-skeptic.application-requirements-bundle.v1` bundle. The artifacts can
be written by a person or produced elsewhere; RuntimeSkeptic validates the
schema and does not trust a producer identity.

Evidence class remains load-bearing. A declared `specified_guarantee` may
support `PROVEN`; `statically_inferred` is capped at `COUNTEREXAMPLE`.
RuntimeSkeptic never raises that ceiling because a producer is familiar.

The standalone boundary is explicit: this repository does not copy, vendor,
submodule, fetch, link, import or invoke an external analyzer. It consumes only
bounded, schema-valid artifacts supplied by the caller. See
[ADR-0001](decisions/0001-standalone-product-boundary.md).

---

## In CI

`rs-check` exit codes are the contract:

| Code | Meaning |
|---|---|
| 0 | `SUPPORTED` |
| 1 | `UNSUPPORTED` |
| 2 | `CONDITIONALLY_SUPPORTED` |
| 3 | `UNKNOWN` |
| 64 / 65 / 70 | usage / input / internal error |

A job that should fail on proven contradictions but tolerate unknowns treats
`1` as failure and `2` and `3` as warnings.

```yaml
- name: Measure the runner
  run: rs-env-probe vm --name ci --output host-profile.json

- name: Fail only on proven contradictions
  run: |
    # Keep a reviewed, schema-valid requirement document in the repository.
    # Capture the code WITHOUT tripping bash -e, then decide.
    code=0
    rs-check contracts/my-app.json --profile host-profile.json \
      --format markdown --output report.md || code=$?
    cat report.md
    # Fail the job only on a PROVEN contradiction (1); tolerate UNKNOWN (3) and
    # CONDITIONALLY_SUPPORTED (2). Anything else (64/65/70) is a real error.
    if [ "$code" = "1" ] || [ "$code" -ge 64 ]; then exit 1; fi
```

Two things worth doing beyond the gate:

- **Archive the profile.** `rs-profile diff` against last week's answers "did
  this kernel upgrade change anything?" — the Risk 3 mitigation in the
  roadmap, and the only way to notice a platform change before a user does.
- **Do not treat `UNKNOWN` as green.** It means a fact the analysis needed was
  never established, usually because the probe does not cover that platform
  yet. Silencing it turns the tool into the thing it exists to catch.

---

## Outside v0.2

These are historical roadmap candidates, not promises in the current product
line. Each requires an accepted Plan v2 before work begins.

- SARIF output and a reusable GitHub Action
- automatic temporal-contract extraction
- signed profiles and a profile registry

# Independent review, 2026-08-02 — findings and their fate

An external reviewer tested v0.1 at commit `1853a83` from a clean GitHub clone
on Windows (VS2022/MSVC), made **no changes to the repo** (`git status` clean,
HEAD still `1853a83`), and produced the findings below. This file is the durable
record so nothing is lost to a session boundary; each finding carries its status
and, when fixed, the commit. Work items are tracked as **T-024** on the compass.

The reviewer's verdict: the core analysis engine is a strong v0.1/MVP — evidence
chains, the UNKNOWN discipline, remediation and "will not work" sections are
"unusually useful" — but **input validation and evidence-replay gaps mean it is
not yet trustworthy as a production CI gate.** That framing is correct and is
now the release posture.

## Resolution status — four rounds, and what each check could not see

**Round 4 (commit pending) — stop hand-checking, read the schema.** The fourth
re-test confirmed round 3 held (CI green 6/6 on `58f6851`) and then found the
class four rounds of hand-written type checks kept leaking: an independent
null/container matrix accepted **36/36** schema-invalid documents
(`assumptions:null`, a null `page_size` fact, `source_locations[].line:null`); a
replay matrix accepted **28/111** manifests with wrong or missing NESTED fields
(`host.os`, `schema_versions.*`, `analysis_options.report_unknowns`, `replay.*`);
two verdicts were wrong at the edges (`min_map_address >= max_user_address` and a
tampered `profile_id` both verified 0); and the matrix guard itself was
fail-OPEN (returned 0 when its binary or `jsonschema` was missing, `_bin` never
looked in `build/bin/Release`, exit 70 counted as accepted, CI install ended in
`|| true`). All closed under **T-030/T-031/T-032**. The decisive change: inputs
are no longer hand-checked field by field — a real C++ JSON Schema validator
(`src/core/schema.cpp`, the subset the schemas use, embedded at build) validates
every document against the published schema at the two domain entry points
(`Requirement::from_json`, `EnvironmentProfile::from_json`) and at the
`rs-replay` manifest, so rs-check, rs-profile, rs-mcp and rs-replay are all
gated. A new dev tool `rs-validate` lets the boundary matrix compare the C++
validator to Python's `jsonschema` for every mutation: **639 mutations, 0
divergences, 0 false-greens, 0 over-strict**, golden and verify-integrity 0. The
one cross-field rule the parser enforced (an exact-address request must carry an
address) is now IN the schema as `if/then`, so `jsonschema == validator == tool`.
The guard now fails CLOSED (`RS_MATRIX_REQUIRE=1` in CI). The lesson, a fifth
time and why this fix differs: rounds 1-4 were each systematic about a DIMENSION
and the schema always had one more field; a validator that READS the schema is
complete by construction, and the matrix proves it equals the oracle. What it
does not close — a bug in the schema itself — is why the semantic golden and
verify-integrity checks stay. Accounts in `docs/PROGRESS.md`.

**Round 3 (commit pending) — the matrix measured acceptance, not truth.** The
third re-test confirmed round 2 held and CI was green 6/6, then found the
round-2 matrix's blind spots: it asked only "schema accepted == parser
accepted?", never whether the VERDICT was right, never the nested/container
fields, never the bundle's file integrity. Real holes lived there:
`file_offset+size` overflow came out SUPPORTED where offset 0 was UNSUPPORTED; a
`page_size`/`allocation_granularity` of 0 masked a guard-page contract to
SUPPORTED; 22 nested-field type disagreements; and — the serious one — the
bundle hash was verified on the file NAMED IN THE MANIFEST while the analysis
read the fixed `application_requirements.json`, so a tampered requirement passed
when the manifest pointed the hash at a pristine copy, and `../outside_req.json`
escaped the bundle. All closed under **T-027/T-028/T-029**: the harness now
checks GOLDEN VERDICTS and the nested fields (0 of 310 type cases + 0 verdict
mismatches); the file_offset overflow reaches the finding; zero page/granularity
is refused; `rs-replay` hashes the SAME fixed files it analyses, refuses a
non-canonical `file` (closing indirection AND traversal), and enforces the full
manifest by type. Accounts in `docs/PROGRESS.md`. The lesson, a third time: a
check is only as good as the dimension it measures.

**Round 1 (commits `861f6af`…`82d5d58`) was marked "all FIXED" and it was not.**
A second independent re-test on 2026-08-02 ran a **300-case boundary matrix** and
found the false-green blockers A1/A2/A5 had been patched only at their *named
examples*: the input readers still read a wrong TYPE as an absent field, so
`required_page_size: "16384"` (a string) still meant "no page size required" and
passed. The bundle blockers A3/A4 still let an incomplete run be written and a
gutted manifest replay clean. Marking them FIXED off a handful of hand cases,
with no systematic check, was the mistake — the same "a check that is not
systematic is the shape of error this project keeps finding in itself" the CI
comments already warn about.

**Round 2 closes them at the root and, this time, against a matrix that runs as a
guard.** `tools/audit/boundary_matrix.py` reports **0 disagreements across 249
cases** (was 89), and status now follows it, not a feeling:

- **A1/A2/A5 — FIXED (T-024, commit `f72e834`).** Every reader rejects a wrong
  type instead of dropping it; the schemas were tightened (min/max, address
  pattern, no `operation:"unknown"`, no `unknown` range-evidence, typed fact
  values) so the tool and schema agree in both directions; `address+size`
  overflow is now a PROVEN UNSUPPORTED verdict, not a limitation.
- **A3/A4 — FIXED (T-025, commit `ed34812`).** `rs-check` writes no bundle when
  any requirement was rejected; `rs-replay` requires every field
  `analysis-bundle.v1` lists, so a stripped manifest is refused.
- **B1, B2, B3, B4a, B4b, B4c, B4d, D1 — FIXED** in round 1 and re-checked; the
  re-test confirmed the profile-id and MCP fixes hold, and flagged remaining doc
  drift (README timing, "Only Linux" help text, the `set -e` CI snippet, and
  this document's own premature "all FIXED"), fixed under T-026.

**CI is now green on the fixed SHA** (`681b048`, CI run #137): all six jobs —
`linux-gcc`, `linux-clang`, `macos-apple-clang`, `windows-msvc`,
`compatibility-gate`, `determinism` — succeeded, and the guards step that runs
the boundary matrix passed with it. The branch was added to `on.push`
temporarily to get there (`refs/status/681b048…/*` all `/success`); it is to be
removed when the branch lands and it never merges anything. Nothing is merged to
`main`.

## Confirmed working (recorded so the fixes do not regress them)

- Clean clone + default VS2022/MSVC build succeeds; 15/15 CTest.
- 39 contracts × 4 measured profiles = 156 evaluations: 46 SUPPORTED, 42
  UNSUPPORTED, 21 CONDITIONALLY_SUPPORTED, 47 UNKNOWN. No crash, hang, or
  internal error.
- README Redis and local-host scenarios produce the right finding + exit code
  once paths are corrected (see B1).
- Normal `rs-check --bundle` → `rs-replay` round-trip works; a tampered file
  inside a real bundle is detected.
- Two Windows probe runs produced the same `profile_id` and the same facts.
- GitHub CI green on the same commit across GCC, Clang, Apple Clang, MSVC 19.51.

## A — false-green / trust (production blockers)

### A1 — schema-invalid input analyzed instead of rejected  ·  FIXED
`name: 123` (schema types it `string`, line 17) and `protection: "rwx"` (schema
types it `object`, line 89) were **silently dropped**; analysis proceeded with
the field gone and still returned a verdict. A program needing RWX, written with
`protection: "rwx"`, read as needing no protection.
- Root cause: `Requirement::from_json` called `as_string()`/`read_flag` without a
  type check, so a wrong type coerced to empty/default.
- Fix (`src/vm/requirement.cpp`): `name`/`component` must be strings, `protection`
  must be an object, or the document is rejected with exit **65** (the code the
  CLI help already documents as "did not satisfy its schema"). All 54 valid
  contracts still parse; the two malformed inputs now exit 65.
- NOT a bug, recorded to prevent an over-correction: an **unknown extra field**
  is schema-COMPLIANT — the published schema sets `additionalProperties: true`
  (line 242) and its description says unknown fields are *reported as a
  limitation, not dropped*. That behaviour is correct and was kept.

### A5 — negative numbers coerced into fabricated PROVEN facts  ·  FIXED
`required_page_size: -1` produced "page size <= 0" and `address: -1` produced
"0x0" — a value the document never gave, presented as a PROVEN fact, because
`as_uint()` turned a negative `Int` into 0 or a giant unsigned.
- Fix (`src/vm/requirement.cpp`): `reject_negative()` guards `size`, `address`,
  `required_alignment`, `required_page_size`; a negative is exit 65 with the
  field named. The published schema should also gain `minimum` on these (B-note).

### A2 — profile schema not enforced, both directions  ·  FIXED
`rs-profile verify` accepted a profile with wrong types and extra fields, and
**rejected some documents valid under `environment-profile.v1`**. This is the
real false-green: CI can skip a requirement it believes it checked. Reproduced
all six divergences against the built binary before touching anything.
- Over-strict half (schema-valid docs were rejected), now accepted:
  - `virtual_memory` is not a required field; a platform-only profile is valid
    and answers UNKNOWN to every memory question ("absent = unknown"). The
    parser treated it as required (`src/vm/profile.cpp`); an absent block is now
    read as an empty one, so every fact takes its unknown default.
  - `os`/`host_arch`/`process_arch` are typed `string` in the schema, not
    `enum`. A host the tool does not model (`freebsd`, `riscv64`) is a real
    measurement and now maps to `other` (a known, non-absent value) instead of
    being refused. A non-string is still rejected, because the schema types the
    field as a string.
- Under-strict half (schema-invalid docs were accepted), now rejected with
  exit 65:
  - `profile_name` must be a string (was coerced to `(unnamed)`).
  - a fact and a range are `additionalProperties:false`: an extra key is a
    misspelling whose data is then dropped in silence (`page_size.valeu`), so it
    is a violation (`core/fact.hpp`, `vm/address_range.cpp`).
  - a fact's / range's `source` and `note` must be strings (were coerced).
- Deliberate asymmetry, and it is schema-backed: `origin` and `translation_mode`
  stay CLOSED enums (the schema types them `enum`); the requirement parser
  REPORTS unknown fields rather than rejecting them because that schema is
  `additionalProperties:true` with an `x_` extension namespace, whereas a
  profile fact is `additionalProperties:false` and rejects them. The parser now
  matches whichever the schema says, field by field.
- Verified: all six repro cases flip to the correct behaviour; all five shipped
  profiles still verify with a stable canonical form; 15/15 CTest; 18/18 guards.
  Regressions pinned in `tests/unit/test_profile.cpp` (both directions).

### A4 — a bundle is not all-or-nothing  ·  FIXED
A two-requirement bundle whose second entry was broken: the tool wrote "skipped"
to stderr and recorded `rejected_requirements` in JSON, but the overall verdict
was SUPPORTED / exit 0 because the first entry was supported. CI watching only
the exit code turns green believing every requirement ran — contradicting the
help text "Every requirement in a bundle is evaluated."
Fix (`tools/rs-check/main.cpp`): a non-empty `bundle->rejected` now returns exit
65 with a message, whatever the parsed entries came to. Verified: the mixed
bundle exits 65 (was 0); a clean single-entry bundle still exits 0. The parsing
half (rejected list populated) is unit-tested by
`one_malformed_entry_does_not_discard_the_rest` in `tests/unit/test_bundle.cpp`.

### A3 — rs-replay accepts an incomplete bundle as verified  ·  FIXED
A bundle with only `application_requirements.json`, `environment_profile.json`,
and a `manifest.json` whose `finding_ids` was empty — no `hashes.txt`,
`findings.json`, `report.md`, input/output hashes, or replay record — was
reported "reproduced ... every finding ID", exit 0. All the missing files are
**required** by `analysis-bundle.v1` (line 8). The tamper check works when
hashes exist; the hole is that verification passes vacuously when the hash
fields are absent entirely.
Fix (`src/reports/bundle.cpp`, commit `b212184`): `replay_bundle` now requires
the manifest to carry `inputs.requirement`, `inputs.profile`, `outputs.findings`
and `outputs.report`, each with its file and sha256, before the tamper check
runs; a bundle missing any of them is rejected as "incomplete" rather than
certified. Regression: `an_incomplete_manifest_is_rejected_not_replayed_vacuously`
in `tests/unit/test_evidence_bundle.cpp`.

## B — documentation, provenance, build, MCP (real, not false-green)

- **B1 — Windows quickstart** · FIXED. `&&` and the `\` continuation are POSIX,
  not PowerShell 5.1, and the VS generator writes to a per-config subdir. README
  now carries a PowerShell block: one command per line, a named `--config`, and
  the `build\bin\Release\` (or `Debug`) path. Commit `944a907`.
- **B2 — problem_statement.md drift** · FIXED. <!-- checked: 2026-08-02 --> The
  "Full pitch" still described a state with no probe, no CLI and an unwired
  build. Corrected: Status is now "v0.1 built and shipping"; the out-of-scope
  `rs-env-probe` row and the false "the build is not wired up" gap are gone; the
  stale "8-entry" corpus claim now defers to the guard-computed figure. Commit
  `3c38ecc`.
- **B3 — profile provenance** · FIXED. README no longer lists Linux as a shipped
  measured profile (it is CI-measured to `refs/measurements/*`, not checked in),
  and `profiles/measured/README.md` gained the Windows and Wine provenance rows.
  Commit `944a907`.
- **B4a — integrations.md** · FIXED. Added a Command-line tools reference (all
  five executables and their flags) and an Evidence-bundles section documenting
  `rs-replay` and exactly when it refuses a bundle. Commit `82d5d58`.
- **B4b — empty provenance fields** · FIXED. `determinism.md` and the profiles
  README now state the truth instead of overclaiming: `run_id` on the macOS
  lanes only, `probe_binary_hash` empty everywhere (the probe does not hash its
  own image yet), none of it part of `profile_id`. Commit `944a907`. See also D1.
- **B4c — MSVC minimum** · FIXED. Rather than pin a minimum, the actual C4127
  false positive is suppressed at the `RS_CHECK`/`RS_CHECK_MESSAGE` macros
  (`tests/test_support.hpp`), so the `/WX` build is portable across MSVC versions
  and the whole class is covered, not just line 639. `_MSC_VER`-guarded, a no-op
  on GCC/Clang. Commit `4fae159`. Follow-up worth doing once a Windows runner can
  confirm it: add a pinned `windows-2022` lane so CI exercises 19.44 directly —
  left out here because it cannot be verified from this environment.
- **B4d — MCP over-tolerant** · FIXED. Invalid JSON-RPC versions / id types and
  bad tool arguments were accepted; `report_unknowns: "treu"` silently became
  false and suppressed the findings an agent asked to see. Fixed in
  `src/server/mcp_server.cpp` on three fronts: (1) every request must carry
  `"jsonrpc": "2.0"` or it is rejected -32600 (a notification still gets no
  reply); (2) an `id`, when present, must be a string, number, or null, else
  -32600 with a null id echoed back; (3) a boolean argument must be a recognized
  spelling - a typo like `"treu"` is now -32602 naming the argument, not a silent
  `false` - and an unknown `format` is rejected rather than defaulted. Unknown
  argument KEYS are still tolerated on the MCP surface (unlike profile facts,
  whose schema is `additionalProperties:false`): MCP clients legitimately attach
  metadata, so the fix validates argument VALUES, not the presence of extra keys.
  Six regressions in `tests/unit/test_mcp.cpp` cover both directions.

## Discovered while fixing (not in the original review)

### D1 — the tracked Wine profile carries a stale `profile_id`  ·  TRACKED
While checking that the A2 parser changes did not move any profile's identity, a
recompute found `profiles/measured/wine-9.0-on-linux-x86_64.measured.json` stores
a `profile_id` that no longer matches the canonical hash of its own facts
(`sha256:b6bd1d2e…` stored vs `sha256:ce95c1d5…` recomputed). Proven
pre-existing: the same drift reproduces on the pre-A2 binary, so the fixes did
not cause it. Cause, pinned exactly by diffing the subtrees: the file predates
the serializer gaining `max_single_reservation` and
`max_single_reservation_hinted`, which `to_json` now emits as explicit-unknown
facts — those two keys are the ONLY difference, nothing in the stored file is
dropped or changed. The generated Linux profile under `profiles/generated/`
shows the same lag, but that file is git-ignored and rebuilt on every run, so it
is not a committed defect. Not a functional false-green either — the tool always
RECOMPUTES `profile_id` from the facts and never reads the stored field — but
the stored value is misleading, and `rs-profile verify` does not flag the
disagreement (it prints the recomputed id and reports "canonical form: stable"
regardless). <!-- checked: 2026-08-02 --> Two follow-ups worth their own change,
off the review branch: regenerate the stale id from its facts, and have `verify`
warn when a stored `profile_id` disagrees with the recompute. Adjacent to B4b
(provenance-field integrity).

## Fix order (the reviewer's, and it is right)

1. Enforce the published JSON Schemas on ALL CLI and MCP inputs — A1 (requirement
   parser), A2 (profile parser) and the MCP surface (B4d) all DONE.
2. Bundle all-or-nothing; one rejected entry ⇒ non-clean exit (A4). DONE.
3. `rs-replay` rejects missing manifest/hash/output files (A3). DONE.
4. Then Windows quickstart, stale docs, profile provenance (B group).

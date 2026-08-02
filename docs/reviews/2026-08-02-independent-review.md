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

### A2 — profile schema not enforced, both directions  ·  TRACKED (T-024)
`rs-profile verify` accepted a profile with wrong types and extra fields, and
**rejected some documents valid under `environment-profile.v1`**. This is the
real false-green: CI can skip a requirement it believes it checked. Fix: apply
the same type-enforcement to the profile parser (`src/vm/profile.cpp`) and
reconcile it with the published profile schema in both directions.

### A4 — a bundle is not all-or-nothing  ·  TRACKED (T-024)
A two-requirement bundle whose second entry was broken: the tool wrote "skipped"
to stderr and recorded `rejected_requirements` in JSON, but the overall verdict
was SUPPORTED / exit 0 because the first entry was supported. CI watching only
the exit code turns green believing every requirement ran — contradicting the
help text "Every requirement in a bundle is evaluated." Fix: any rejected/broken
entry makes the overall exit non-clean.

### A3 — rs-replay accepts an incomplete bundle as verified  ·  TRACKED (T-024)
A bundle with only `application_requirements.json`, `environment_profile.json`,
and a `manifest.json` whose `finding_ids` was empty — no `hashes.txt`,
`findings.json`, `report.md`, input/output hashes, or replay record — was
reported "reproduced ... every finding ID", exit 0. All the missing files are
**required** by `analysis-bundle.v1` (line 8). The tamper check works when
hashes exist; the hole is that verification passes vacuously when the hash
fields are absent entirely. Fix: `rs-replay` must reject a bundle missing any
required file or hash.

## B — documentation, provenance, build, MCP (real, not false-green)

- **B1 — Windows quickstart** · TRACKED. README's `&&` and `\` line
  continuation are not PowerShell 5.1; the default VS generator writes to
  `build/bin/Debug/`, not `build/bin/`. Give a PowerShell-correct path and note
  the Debug/Release subdir.
- **B2 — problem_statement.md drift** · TRACKED. README links it as the "Full
  pitch"; it still says there is no probe/CLI/build and that Windows is a stub —
  the opposite of the current product. Serious drift; rewrite or relink.
- **B3 — profile provenance** · TRACKED. README lists five *measured*
  environments including Linux, but `profiles/measured/` has **no Linux profile**
  (Linux lives under `profiles/generated/`); the provenance table in
  `profiles/measured/README.md` omits the Windows and Wine files. Correct the
  README claim (measured vs generated) and complete the table.
- **B4a — integrations.md** · TRACKED. Linked as "All tools and flags" but does
  not document `rs-replay` or many CLI options.
- **B4b — empty provenance fields** · TRACKED. `probe_binary_hash` is empty on
  new and shipped profiles; `run_id` is empty on Windows/Wine, though
  `docs/architecture/determinism.md` (line 283) says the binary hash records
  which probe took the measurement.
- **B4c — MSVC minimum** · TRACKED. CI passes on floating VS2026/MSVC 19.51; the
  same `/WX` config fails to compile on VS2022/MSVC 19.44 at
  `tests/unit/test_arena_walk.cpp:639` (C4127). State a minimum MSVC or add
  VS2022 to the matrix.
- **B4d — MCP over-tolerant** · TRACKED. Invalid JSON-RPC versions / id types and
  bad tool arguments are accepted; e.g. `report_unknowns: "treu"` silently hides
  findings. Does not meet JSON-RPC 2.0 / MCP lifecycle. Apply the same input
  strictness to the MCP surface.

## Fix order (the reviewer's, and it is right)

1. Enforce the published JSON Schemas on ALL CLI and MCP inputs (A1 done for the
   requirement parser; A2 and MCP remain).
2. Bundle all-or-nothing; one rejected entry ⇒ non-clean exit (A4).
3. `rs-replay` rejects missing manifest/hash/output files (A3).
4. Then Windows quickstart, stale docs, profile provenance (B group).

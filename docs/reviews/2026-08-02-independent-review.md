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

- **B1 — Windows quickstart** · TRACKED. README's `&&` and `\` line
  continuation are not PowerShell 5.1; the default VS generator writes to
  `build/bin/Debug/`, not `build/bin/`. Give a PowerShell-correct path and note
  the Debug/Release subdir.
- **B2 — problem_statement.md drift** · TRACKED. <!-- checked: 2026-08-02 -->
  README links it as the "Full pitch"; that document still describes a state
  with no probe, no CLI and a stub Windows build — the opposite of the current
  product. Serious drift; rewrite or relink.
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
   parser) and A2 (profile parser) DONE; the MCP surface (B4d) remains.
2. Bundle all-or-nothing; one rejected entry ⇒ non-clean exit (A4). DONE.
3. `rs-replay` rejects missing manifest/hash/output files (A3). DONE.
4. Then Windows quickstart, stale docs, profile provenance (B group).

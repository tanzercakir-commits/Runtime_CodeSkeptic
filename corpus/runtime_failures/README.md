# Runtime Failure Corpus

The incident corpus: one Markdown file per cross-layer runtime failure, classified against `docs/failure_taxonomy.md` and linked to the finding IDs that would diagnose it.

**Status:** ROADMAP Phase 0 deliverable (`corpus/runtime_failures/`). **Incomplete, and none of its current contents count toward the Phase 0 exit criteria.**

> ### Current state, stated plainly
>
> | | Required by Phase 0 | Present | Counting |
> | --- | --- | --- | --- |
> | Classified real incidents | ≥ 30 | 8 files | **0** |
> | In the virtual-memory category | ≥ 10 | 8 files | **0** |
>
> All 8 entries carry `provenance: pattern_reconstruction`. They describe failure *patterns* reconstructed from general systems knowledge. **They are not citations of specific reported incidents.** No bug tracker number, CVE, commit hash, project name or URL in this corpus has been verified, because none is claimed. Every entry must be replaced by, or upgraded to, a verified incident with a source link before it counts toward anything.
>
> The exit criteria are therefore **0 / 30** and **0 / 10**, not 8 / 30.

---

## 1. Why the corpus exists

ROADMAP Phase 0 requires collecting 30–50 real cross-layer runtime failures, classifying them, identifying repeated semantic patterns, and distinguishing conventional memory bugs from environment contradictions. The corpus is the artifact that work produces, and it has three jobs:

1. **It bounds the problem.** A taxonomy invented without incidents describes what its author imagined. A taxonomy derived from incidents describes what actually breaks. Categories with no entries are visible in the corpus and are treated as unvalidated.
2. **It justifies finding IDs.** Every `RS-VM-*` ID should trace to at least one incident. An ID with no corpus entry is a hypothesis about what users need.
3. **It becomes the regression suite.** Entries that reach `reproduced` status get a requirement document and a profile fixture under `tests/incidents/`, so a diagnosis that once worked keeps working.

It is also the gate for every future domain. ROADMAP Gate D requires ten real incident examples before a new domain opens. Recording loader/ABI, filesystem and time incidents now is how that gate is satisfied later, even though no analyzer models them.

---

## 2. File layout and naming

```text
corpus/runtime_failures/
├── README.md                                       (this file)
├── RSC-0001-exact-guest-mapping-relocated.md
├── RSC-0002-hinted-mapping-relocation-breaks-identity.md
└── RSC-NNNN-short-kebab-case-slug.md
```

- One incident per file. No index file: the front matter is the index, and a generated index would drift.
- `RSC-NNNN` = **R**untime**S**keptic **C**orpus, zero-padded, allocated in registration order.
- The slug describes the *contradiction*, not the software. "exact-guest-mapping-relocated", not "emulator-bug".
- IDs are permanent. A rejected or withdrawn entry keeps its number and its file, with `status: rejected` — see section 7.

---

## 3. Required front matter

Every entry begins with a YAML front-matter block delimited by `---`. All eight fields below are **required**; an entry missing any of them is malformed.

```yaml
---
id: RSC-0001
title: Fixed guest mapping relocated and reported as success
category: semantic success violation
layers_involved:
  - application
  - compatibility_layer
  - syscall_interface
  - operating_system
  - kernel
evidence_available:
  - specified_guarantee
  - unknown
finding_ids:
  - RS-VM-0001
  - RS-VM-0008
  - RS-VM-0014
status: draft
provenance: pattern_reconstruction
---
```

### 3.1 Field reference

| Field | Type | Rule |
| --- | --- | --- |
| `id` | string | `RSC-NNNN`. Must match the filename prefix. Permanent. |
| `title` | string | One line naming the contradiction. Not a bug title; no product names unless verified. |
| `category` | string | **Exactly one** value from `docs/failure_taxonomy.md`, spelled verbatim. Secondary categories go in the body. |
| `layers_involved` | list | Values from `rs::Layer` (section 3.3). Ordered top-down, from the layer that stated the requirement to the layer that could not satisfy it. |
| `evidence_available` | list | Values from `rs::EvidenceClass` (section 3.4). What evidence exists *for this entry*, not what a future analysis could hope for. |
| `finding_ids` | list | `RS-VM-*` IDs that would diagnose this incident, or `[]` if none exists yet. An empty list is a signal, not a defect. |
| `status` | string | Lifecycle state (section 3.5). |
| `provenance` | string | Where the entry's content came from (section 3.6). |

Optional fields, added when they apply and omitted otherwise: `source` (a URL or citation — required once `status` is `sourced` or better), `platforms`, `apis`, `related` (other `RSC-*` ids), `superseded_by`.

### 3.2 Category values

Exactly one of the ten, spelled as in `docs/failure_taxonomy.md`:

```text
semantic success violation
unsupported exact capability
invalid fallback
error-code laundering
permanent error treated as retryable
capability present but required property absent
resource-topology contradiction
temporal contract violation
loader/ABI mismatch
platform-observed behavior treated as guaranteed behavior
```

### 3.3 Layer values

From `rs::Layer` (`src/core/evidence.cpp`), spelled exactly:

```text
application  library  language_runtime  compatibility_layer
syscall_interface  operating_system  kernel  hardware  analyzer
```

`analyzer` appears in a corpus entry only when RuntimeSkeptic's own reasoning is part of the incident — normally it does not.

### 3.4 Evidence values

From `rs::EvidenceClass` (`src/core/evidence.cpp`):

```text
specified_guarantee  measured_capability  observed_invariant
statically_inferred  bounded_counterexample  heuristic_risk  unknown
```

Be conservative and be honest. For a `pattern_reconstruction` entry the correct list is usually `[specified_guarantee, unknown]`: the platform behavior may be documented and stable, while everything about the affected program is unestablished. Listing `measured_capability` requires a probe run you actually performed; listing `observed_invariant` requires a trace set you actually have.

### 3.5 Status values

| Status | Meaning | Counts toward exit criteria? |
| --- | --- | --- |
| `draft` | written, not tied to a verifiable incident | **no** |
| `sourced` | a verifiable public incident with a `source:` link | **yes** |
| `reproduced` | reproduced locally; a requirement document and a profile exist | yes |
| `regression` | has an automated test under `tests/incidents/` | yes |
| `rejected` | examined and found not to be an environment contradiction | no — kept as a negative example |

Only `sourced` and above count. All 8 current entries are `draft`.

### 3.6 Provenance values

| Provenance | Meaning |
| --- | --- |
| `pattern_reconstruction` | written from general systems knowledge; **not** a citation of a specific reported incident |
| `public_report` | a public bug report, mailing-list thread, commit, CVE or post-mortem, cited in `source:` |
| `first_party` | observed by a contributor on a system they operate, with detail they are permitted to publish |
| `vendor_documentation` | derived from a platform vendor's own documentation of a behavior change |

`pattern_reconstruction` and `status: draft` always occur together. Moving to `sourced` requires changing both fields and adding `source:`.

---

## 4. Body structure

After the front matter, use these sections. Keep them in this order; omit one only when it is genuinely empty.

```markdown
# RSC-NNNN — Title

**Provenance notice** (mandatory for pattern_reconstruction — exact wording in section 5)

## Summary
Two or three sentences. What was required, what the environment provided, what broke.

## Failure chain
The layer-by-layer table: requirement → approximation → refusal → laundering → sink.

## What the program required
The postcondition, in the program's own terms. Code where it clarifies.

## What the environment provided
The platform behavior, with its evidence class. Distinguish documented from observed.

## Why the mismatch is not detected at the call site
Why no error is returned, or why the returned error is misleading.

## Manifestation
What an engineer actually sees, and why it is hard to attribute.

## Classification
Primary category and why; secondary categories; finding IDs and why each applies.

## Why this is not an ordinary memory bug
The universal discriminator from docs/failure_taxonomy.md, applied to this entry.

## What RuntimeSkeptic would need
The requirement fields and profile facts that make this decidable, with the
confidence the ceiling rule would permit.

## Remediation classes
From rs::vm::RemediationClass, with the rejected fixes and their reasons.

## Open questions
What is unverified. For a pattern_reconstruction entry this is never empty.
```

---

## 5. The honesty requirement

This is the corpus's central rule, and it is not negotiable.

**Do not invent incidents.** Do not write a bug tracker number, a CVE identifier, a commit hash, a project name, a version number, a date or a URL that you have not verified. A fabricated citation is worse than no citation: it is unfalsifiable-looking, it propagates, and a single one discredits the whole corpus and the tool built on it.

**Patterns are allowed; attributions are not.** Documenting a widely known *class* of platform behavior from general systems knowledge is legitimate and useful. Attaching it to a specific reported incident you cannot verify is not. You may write "hosts that enforce write-xor-execute reject a mapping requested with write and execute permissions simultaneously" as a described behavior. You may not write "this is the bug reported in <project> issue #1234" unless you have read issue #1234.

**Mark it.** Every entry written this way carries `provenance: pattern_reconstruction` in the front matter **and** this paragraph verbatim in the body, immediately after the title:

> This entry describes a failure pattern reconstructed from general systems knowledge. It is not a citation of a specific reported incident. Replace with a verified incident (with source link) before counting it toward the Phase 0 exit criteria.

**Do not count it.** A `pattern_reconstruction` entry is `status: draft` and contributes zero to the exit criteria. The table at the top of this file reports the counting number, not the file count.

This is the corpus-level expression of the same rule the code enforces on facts: `EvidenceClass::Unknown` is the default and cannot be overridden by supplying a value (`Fact<T>::known()` rejects it). An unverified corpus entry is a fact with unknown evidence. It may inform design; it may not support a claim.

---

## 6. Phase 0 exit criteria

From ROADMAP section 13:

| # | Criterion | State |
| --- | --- | --- |
| 1 | at least 30 classified real incidents | **not met** — 0 of 30 counting |
| 2 | at least 10 incidents in the virtual-memory category | **not met** — 0 of 10 counting |
| 3 | each category has a concrete evidence model | **met** — `docs/failure_taxonomy.md` states an establishing evidence class per category; `docs/evidence_model.md` defines the ladders and the ceiling |
| 4 | initial terminology is stable enough for public documentation | **met** — the vocabulary is implemented in `src/core/evidence.cpp` and used verbatim across the docs |
| 5 | no claim depends solely on AI-generated interpretation | **met, conditionally** — every current entry is marked `pattern_reconstruction` and counts toward nothing, which is what satisfies this criterion; it would be violated the moment such an entry were counted |

Criteria 1 and 2 are the blocking ones and they are blocked on human research, not on code.

### Current inventory

| Entry | Category | Finding IDs | Status | Provenance |
| --- | --- | --- | --- | --- |
| RSC-0001 | semantic success violation | RS-VM-0001, RS-VM-0008, RS-VM-0014 | `draft` | `pattern_reconstruction` |
| RSC-0002 | platform-observed behavior treated as guaranteed behavior | RS-VM-0007, RS-VM-0008 | `draft` | `pattern_reconstruction` |
| RSC-0003 | resource-topology contradiction | RS-VM-0006, RS-VM-0004 | `draft` | `pattern_reconstruction` |
| RSC-0004 | capability present but required property absent | RS-VM-0009, RS-VM-0011, RS-VM-0010 | `draft` | `pattern_reconstruction` |
| RSC-0005 | invalid fallback | RS-VM-0004, RS-VM-0005, RS-VM-0014 | `draft` | `pattern_reconstruction` |
| RSC-0006 | temporal contract violation | RS-VM-0012 | `draft` | `pattern_reconstruction` |
| RSC-0007 | permanent error treated as retryable | RS-VM-0015, RS-VM-0001 | `draft` | `pattern_reconstruction` |
| RSC-0008 | unsupported exact capability | RS-VM-0018 | `draft` | `pattern_reconstruction` |

### Category coverage

| Category | Entries |
| --- | --- |
| semantic success violation | RSC-0001 |
| unsupported exact capability | RSC-0008 |
| invalid fallback | RSC-0005 |
| **error-code laundering** | **none** |
| permanent error treated as retryable | RSC-0007 |
| capability present but required property absent | RSC-0004 |
| resource-topology contradiction | RSC-0003 |
| temporal contract violation | RSC-0006 |
| **loader/ABI mismatch** | **none** |
| platform-observed behavior treated as guaranteed behavior | RSC-0002 |

Two categories have no entry at all. Both also lack a primary finding ID (`docs/failure_taxonomy.md` sections 4 and 9), and both are the categories whose detection is deferred to later phases — Phase 4 for error-code laundering, Phase 8 for loader/ABI. That correlation is expected but should not be used as an excuse: incidents in both categories are common and can be recorded now.

### What "done" looks like

30 entries at `status: sourced` or better, each with a working `source:` link, at least 10 with `category` in the virtual-memory-relevant set, at least one per taxonomy category, and every `RS-VM-*` ID referenced by at least one entry. At that point this file's counting table reports 30 / 30 and 10 / 10, and Phase 0 closes.

---

## 7. Adding an entry

1. Take the next `RSC-NNNN`. Never reuse a number, including a `rejected` one.
2. Create `RSC-NNNN-short-kebab-case-slug.md`.
3. Write the front matter. All eight required fields. Assign exactly one category.
4. If you cannot cite a verifiable source, set `provenance: pattern_reconstruction`, `status: draft`, and include the provenance notice from section 5 verbatim.
5. Write the body in the section order from section 4.
6. List the `RS-VM-*` IDs that would diagnose it. If none exists, use `[]` and say what the missing ID would need to assert. This is how the registry grows.
7. Update the inventory and category-coverage tables in section 6, and the counting table at the top of this file.

### Upgrading a draft to sourced

1. Find the incident: a public bug report, mailing-list thread, commit message, CVE, vendor release note or post-mortem.
2. Verify it says what you think it says. Read it.
3. Add `source:` with the URL or full citation.
4. Change `provenance` to `public_report` (or `vendor_documentation` / `first_party`) and `status` to `sourced`.
5. Remove the provenance notice from the body.
6. Rewrite the body against what the source actually reports. Do not keep reconstructed detail that the source does not support — that is the failure mode this process exists to prevent.
7. Update the tables in section 6 and the counting table at the top.

### Rejecting an entry

If an entry turns out to be an ordinary memory bug, a language-level defect, or a plain logic error, set `status: rejected`, keep the file, and add a section explaining which discriminator it failed. Negative examples are how the taxonomy's boundary stays sharp, and knowing what was examined and excluded is worth as much as knowing what was included.

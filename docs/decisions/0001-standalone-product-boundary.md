# ADR-0001 — RuntimeSkeptic remains a standalone product

<!-- STANDALONE-BOUNDARY: accepted 2026-08-06 -->

**Status:** Accepted
**Decision date:** 2026-08-06
**Owner:** Tanzer Cakir

## Context

The frozen roadmap contains a Phase 5 CodeSkeptic integration and later
research phases. RuntimeSkeptic v0.2 already provides a complete useful slice
without them: measured environment profiles, deterministic compatibility
analysis, evidence bundles, a runtime monitor, canonical trace replay, an
installed SDK and verified native packages.

Copying or linking CodeSkeptic into this repository would turn two independent
projects into a drifting fork, blur provenance, enlarge the release and
security boundary, and make RuntimeSkeptic harder to install and explain.

## Decision

RuntimeSkeptic remains standalone. The v0.2 product line closes at the
completed Phase 4 boundary.

- Phase 5 CodeSkeptic integration and its differential test are **not
  applicable** to this product line.
- CodeSkeptic source is not copied, vendored, added as a submodule, fetched by
  the build, linked, imported or invoked by RuntimeSkeptic.
- Requirement documents are hand-authored or supplied as independently
  produced, schema-valid artifacts. RuntimeSkeptic records their provenance and
  applies the existing evidence-confidence ceiling; it does not know or require
  their producer.
- Frozen `plan.md` and `ROADMAP.md` remain historical contracts. Phases 5–10
  are not release obligations for v0.2 and are marked `n/a` only in the mutable
  status plan. Reopening any of them requires an explicit, superseding Plan v2
  with its own scope and gates.

## Optional future bridge

If a real user need appears, an adapter may be proposed in a separate
repository or separately versioned package. It must be optional and disabled by
default; communicate only through a versioned artifact boundary; pin producer
and schema versions in end-to-end tests; enforce time, memory and output bounds;
fail closed on malformed or incomplete artifacts; and leave both source trees
unchanged. That proposal is future scope, not an integration hidden inside
RuntimeSkeptic.

## Consequences

- Users can clone, build and run RuntimeSkeptic without CodeSkeptic, network
  access at runtime, or an integration setup step.
- The public story is one product and one responsibility: determine whether a
  stated runtime assumption is satisfiable on a measured host.
- RuntimeSkeptic makes no claim that it extracts assumptions automatically.
- Later research ideas remain available to a future plan, but do not keep the
  current product perpetually “unfinished.”

## Rejected alternatives

1. **Copy CodeSkeptic into this repository.** Rejected because source,
   provenance, fixes and security boundaries would drift.
2. **Make CodeSkeptic a required submodule or build dependency.** Rejected
   because installation and release reproducibility would inherit another
   project's state.
3. **Add direct CLI orchestration in the core repository.** Rejected because it
   creates a de facto integration and couples independent release schedules.

## Enforcement

`tools/guards/check_standalone_boundary.py` rejects vendored CodeSkeptic paths,
submodules, build fetch/link wiring and source includes. Its adversarial cases
run first in `tools/guards/selftest.py`; `tools/guards/check_non_goals.py`
continues to forbid a replacement static extractor.

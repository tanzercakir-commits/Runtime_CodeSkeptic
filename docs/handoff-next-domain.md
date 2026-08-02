# Handoff: opening the second runtime domain

This is a cold-start work order for extending RuntimeSkeptic past virtual memory
to a **second domain** — the "other layers" the project has always named
(filesystem, loader, time, networking). It is written so an agent with no memory
of this session can pick it up, and it deliberately makes the project's own
discipline the first constraint, because that discipline is what the tool's
credibility rests on.

Read [docs/TODO.md](TODO.md) (the compass) and [docs/PLAN.md](PLAN.md) (the map)
before starting. This file does not replace them; it proposes the next block of
work for them to absorb once a domain is chosen.

---

## Where you are starting from (honest)

- **Virtual memory is the only live domain.** Phases 1–3 shipped: probes,
  contracts, the analysis engine, evidence reports. Gate B passed — 0 false
  positives across 1,576 kernel-observed mapping requests on three OSes.
- **`rs-replay` already exists** (`tools/rs-replay`, `build/bin/rs-replay`) and
  seals/verifies evidence bundles. Do not rebuild it.
- **A second domain has never been opened.** Phase 8 (ROADMAP §18) is `[open]`
  and is where this work lives.

## The guardrails — non-negotiable, read before writing any code

1. **Gate D (ROADMAP §20) gates every new domain.** A domain may not ship until
   it demonstrates *all four*:
   - at least **ten real incident examples** (documented, reproducible — not
     invented);
   - a **bounded operation model** (a small, closed set of operations);
   - **measurable or specified** environment behavior (a probe or a cited spec);
   - **actionable outputs** (a diagnosis a maintainer can act on).
   Corpus first. Code that precedes the corpus is the project's named Risk 1
   (*excessive scope*) materialising.
2. **One domain at a time** (ROADMAP §18). Do not open two. Do not touch VM's
   rules while adding a domain.
3. **Never add an extractor, and never modify CodeSkeptic.** `docs/non_goals.md`
   §18 is normative; `tools/guards/check_non_goals.py` fails the build if an
   extractor reappears. Requirements stay hand-written for now.
4. **The false-positive rate is the north star.** VM earned trust with 0 FP on
   real traces. A new domain's first campaign must measure the same way
   (`tools/campaign/`), and a rule that fires on a request that actually
   succeeds is a bug, not a finding.
5. **The four documents move together.** Every task updates the compass
   (TODO.md) and the map (PLAN.md); every finished task appends to
   PROGRESS.md (newest first). `tools/guards/run_all.sh` and
   `tools/guards/selftest.py` must stay green on every commit — they are the
   contract, not a suggestion.

## Which domain first — recommendation, not a mandate

ROADMAP §18 lists five candidates (A Filesystem, B Dynamic Loader/ABI, C Time,
D Network, E Threading). **Start with Filesystem Semantics (Candidate A).**
Rationale:

- It is **independently probeable** the way VM was — you can measure the real
  behavior of a mounted filesystem (case-sensitivity, atomic-rename guarantees,
  path normalization) rather than cite a spec and hope.
- It has an **abundant, well-documented incident record** — case-collision bugs
  on case-insensitive macOS/Windows, non-atomic rename across filesystems,
  APFS/NTFS/ext4 durability differences. Ten real examples is easy and honest.
- Its failure family is **the same shape as VM's**: the program assumes a
  guarantee (atomic rename, case-sensitivity, `fsync` durability) that the
  environment measurably does not provide.

Dynamic Loader/ABI (B) is the strong second choice and the most VM-adjacent, but
its behavior is harder to *measure* and leans more on cited ABI specs. Pick B
only if you have a concrete loader-incident corpus in hand.

---

## The work order — VM's proven playbook, applied to the new domain

Each task names its deliverable and its done-check. Do them in order; the corpus
gates everything after it.

### D-1 · Build the incident corpus (Gate D, criterion 1)
- **Do:** collect ≥10 real, documented incidents for the chosen domain into
  `corpus/runtime_failures/<domain>/`, one file each, in the same shape as the
  existing VM corpus entries. Each states the source, the assumed guarantee, the
  environment that broke it, and the observed failure.
- **Done when:** ten entries exist, each with a real citation, and a reader can
  see the failure family without domain expertise.
- **Do not:** invent incidents or pad with variations of one bug.

### D-2 · Define the bounded operation model (Gate D, criterion 2)
- **Do:** write `docs/domains/<domain>/operation-model.md` — the small closed
  set of operations the domain covers (e.g. filesystem: open, rename, fsync,
  stat, lock — and explicitly *nothing else* in v1). Mirror how VM bounded
  itself to mapping operations.
- **Done when:** the operation set is closed and every corpus incident maps to
  one operation in it. Anything that does not map is out of scope, stated so.

### D-3 · Build the environment probe (Gate D, criterion 3)
- **Do:** add a probe under `src/probe/` and a `rs-env-probe <domain>`
  subcommand that *measures* the host's real behavior for the bounded
  operations, emitting facts with evidence classes — exactly as the VM probe
  does (`measured_capability`, never a guessed default). Reuse `Fact<T>` and the
  evidence model unchanged.
- **Done when:** `rs-env-probe <domain>` writes a profile whose facts each carry
  a real measurement source, and `rs-profile verify` reports how many it knows.
- **Probe safety** (ROADMAP §21 discipline) transfers: measure, do not mutate;
  clean up after yourself; an unmeasured fact stays `unknown`, never assumed.

### D-4 · Extend the contract schema and add the rules
- **Do:** extend the requirement schema for the domain's operations (new
  `operation` values, new postconditions), version it under `schemas/`, and add
  the rules under `src/<domain>/` with `RS-<DOMAIN>-xxxx` ids in
  `docs/findings/registry.md`. Every finding runs through `clamp_confidence` —
  no rule may claim more than its weakest fact allows.
- **Done when:** hand-written contracts for the corpus incidents evaluate to the
  known-correct verdict against a measured profile, and `ctest` is green.

### D-5 · Run the false-positive campaign (Gate D, criterion 4 + the north star)
- **Do:** add a campaign under `tools/campaign/` that observes real programs
  exercising the domain on a permissive host and checks that no rule fires on a
  request that actually succeeded. Record the rate in a `docs/campaigns/` note,
  the same way `2026-07-false-positive-rate.md` did for VM.
- **Done when:** the campaign reports its false-positive count (target 0) on a
  named population, honestly, with the population size stated.

### D-6 · Write the diagnosis cards and close Gate D
- **Do:** add 2–3 reproduce-exact diagnosis cards to `docs/diagnosis-cards.md`
  (one `rs-check` command each), and write the Gate D sign-off note in
  PROGRESS.md showing all four criteria met.
- **Done when:** a visitor can reproduce a real cross-environment verdict for the
  new domain from a clean clone with one command, and PLAN.md's Phase 8 line
  moves from `[open]` to `[partial]` with the domain named.

---

## Verify continuously

After every task: `bash tools/guards/run_all.sh` and
`python3 tools/guards/selftest.py` must both pass. If you found a drift while
working, encode it as a new guard + selftest case before moving on — that is how
this repository has stayed honest, and every guard here started as exactly that.

## What "done" is NOT

- Not "the code compiles." Done is *a real incident, predicted correctly from a
  measured profile, with zero false positives on real traces.*
- Not "all five domains." One domain, fully through Gate D, beats five half-built.
- Not touching CodeSkeptic, and not adding an extractor. If you feel the pull to
  extract requirements from source, that is Phase 5's job and it is blocked by
  the owner — hand-write the contracts.

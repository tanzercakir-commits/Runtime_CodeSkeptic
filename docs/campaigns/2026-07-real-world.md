# Real-world campaign, July 2026

Status: Phase 3 evidence. Standalone — no static extractor was used; every
requirement was derived by reading source and written by hand.

Twenty-six requirement documents were derived from six real, shipping C/C++
projects and evaluated against a profile measured on an x86-64 Linux host
(kernel 6.18.5, 4 KiB pages, `max_user_address` `0x7ffffffff000`).

**The campaign found eight defects in RuntimeSkeptic.** That was its purpose.
A campaign that produces only green ticks has measured nothing.

---

## Subjects

| Project | Commit | Why it is interesting |
|---|---|---|
| QEMU | `300438f` | user-mode `guest_base` / `reserved_va`, guest-host identity |
| Box64 | `a2ae050` | x86-64 on ARM: page size, 32-bit address bands, dynarec W^X |
| LuaJIT | `346ab58` | the low-2 GB heap constraint, ±2 GB machine-code window |
| mimalloc | `a3ca0e5` | 32 MiB segment alignment, huge-page reservation |
| Redis / jemalloc | `b53f65d` | compiled page size, fork/copy-on-write, hinted mappings |
| V8 | `2dc7d00` | 4 GiB pointer-compression cage aligned to 4 GiB |

Each contract carries an `x_campaign` block with the exact `file:line`, the
quoted source, and an **expected verdict written before the tool was run**.
Disagreement between prediction and output is the signal; agreement is not
evidence of much, since the predictions were made by people who had read the
rule list.

---

## What the campaign found

### 1. A four-petabyte reservation was reported as `SUPPORTED`

QEMU's aarch64 user mode reserves `MAX_RESERVED_VA = (1ul << 52) - 1`
(`linux-user/main.c`). The analyzer returned exit code 0 with no findings.

The mechanism: every placement rule opened with `if (!request.address)
return;`, and no rule ever compared `request.size` against anything at all. A
request for eight exabytes and a request for one page were indistinguishable.

Address-less-but-enormous is not an exotic shape — it is how emulators,
allocators and JITs make their *largest* requests. Fixed by `RS-VM-0021`.

### 2. Alignment of a reservation was dead code

`required_alignment` was consulted only when an `address` was *also* supplied.
So the constraint that matters most to allocators and runtimes — "give me
4 GiB aligned to 4 GiB, I do not care where" — fell straight through.

This was demonstrated by deleting the field from V8's cage contract and
getting a byte-identical verdict. That is the definition of dead code.

The affected constraints are real and load-bearing:

| Project | Constraint | Enforcement in the real code |
|---|---|---|
| V8 | `kPtrComprCageBaseAlignment = 1 << 32` | release-mode `CHECK` |
| V8 | TrustedRange `4 * GB` alignment | release-mode `CHECK` |
| mimalloc | `MI_SEGMENT_ALIGN` = 32 MiB | `mi_assert_internal`, **compiled out in release builds** |

mimalloc's case is the worst of the three: misalignment breaks the pointer
mask in `_mi_ptr_segment` and there is no runtime check to catch it. Fixed by
`RS-VM-0022`.

### 3. One rule was unreachable for exactly the input it was written for

`RS-VM-0015` ("retry loop targets a permanently impossible operation") tested
`confidence == Proven` to decide whether anything had been proven impossible.

But confidence is *clamped by evidence*, and a statically inferred requirement
can never reach `PROVEN`. So the rule was dead on every document a static
extractor produces — the entire class of input it exists to serve.

Confirmed by flipping one field:

```text
assumption_evidence = statically_inferred   -> [RS-VM-0013/COUNTEREXAMPLE]
assumption_evidence = specified_guarantee   -> [RS-VM-0013/PROVEN, RS-VM-0015/PROVEN]
```

Fixed by adding `Finding::structural_impossibility` — a property of the
*reasoning* ("no execution can succeed") rather than of how well-attested the
facts are. The two were conflated because for hand-written contracts they
coincide.

### 4. A critical false positive on page size, with actively harmful advice

jemalloc built with `--with-lg-page=16` checks `if (os_page > PAGE)`
(`pages.c:760`). It demands the host page be **at most** 64 KiB and runs
perfectly on a 4 KiB kernel.

The rule tested equality, so it reported a critical impossibility and advised
`select_different_host_configuration`, detail: *"a host whose page size is
65536"*. That host does not exist on x86-64 Linux. The tool was confidently
wrong and its remediation would have wasted a real engineer's day.

Fixed by `required_page_size_relation` (`equal` / `at_most` / `at_least`).
After rewriting the contract to the relation the code actually uses, the
verdict is `SUPPORTED` with no findings.

### 5. The wrong failure mechanism, even when the verdict was right

LuaJIT on x64 without GC64 needs its heap below 2 GB (`LJ_ALLOC_MBITS 31`).
The tool said `UNSUPPORTED`, explaining that "the high bits are discarded on
assignment and the truncated pointer refers to unrelated memory."

**That cannot happen in LuaJIT.** It tests `(addr >> LJ_ALLOC_MBITS) == 0` at
`lj_alloc.c:242` and `munmap`s anything that fails, thirty times. It never
stores an un-vetted address. The reachable failure is retry exhaustion.

Box64 got the *expected* verdict through the same wrong story — which is worse
than an outright disagreement, because nothing in the output flagged it.
`rule_pointer_truncation` never consulted `retries_on_failure` or
`permitted_fallbacks`. Fixed via `validates_returned_address`; such programs
now get `CONDITIONALLY_SUPPORTED` and an explanation about degrading as the
address space fragments.

### 6. The probe looked in the wrong places, systematically

Two contracts came back `UNKNOWN` for no better reason than where the probe
happened to sample. Its ladder climbed powers of two — and emulators place
guard pages, commpages and rollover barriers *just below* a boundary, never on
it. QEMU's ARM commpage at `0xffff0f00` and Box64's 4 GiB guard both sat in
the blind spot.

This is not bad luck, it is a systematic bias: the probe sampled `2^n`, the
subjects used `2^n − k`. Fixed by sampling both. `UNKNOWN` count across the
campaign went from several to **zero**.

### 7. A graceful fallback was reported like an abort

mimalloc's huge-page loop keeps whatever contiguous prefix it obtained and
warns (`os.c:684`). Declaring `failure_sink: none` left the finding at
`Critical`, because `adjust_severity` passed that case through unchanged.

### 8. Verdicts were derivable without consulting the host

One reviewer predicted all twelve of their verdicts correctly by reading
`analyzer.cpp` and never opening the profile. For those cases the tool added
no information beyond restating its own rule list. This is not a bug with a
patch; it is the honest measure of how much of the analysis is currently
platform-sensitive, and it is the number to move.

#### Measured, July 25 — `tools/campaign/host_sensitivity.sh`

The number could not be measured in July because measuring it needs a second
host and there was one. There are now three, so it has been.

**The test.** Run every contract twice: once against a measured profile, once
against `profiles/fixtures/unknown-host.synthetic.json`, in which 0 of 14
facts are known. Same verdict *and* same findings both times means the profile
was not consulted - the answer came out of the rule list. This is a
demonstration, not an inference from two hosts happening to agree.

| host | consulted the host | did not |
|---|---|---|
| `linux/x86_64+unknown` | 24 / 26 (92%) | 2 |
| `macos/x86_64+rosetta2` | 24 / 26 (92%) | 2 |
| `macos/aarch64+none` | 25 / 26 (96%) | 1 |

Both rows that answer without a host turn out to be entitled to:

- `redis-jemalloc-hinted-identity` → `UNSUPPORTED`, `RS-VM-0014`. The contract
  requires guest/host address identity *and* lists relocation as a permitted
  fallback. That contradiction is internal to the requirement and holds on
  every host - and the finding **says so in its own output**: "Host
  capability: not consulted: the contradiction is internal to the requirement
  and holds on every host."
- `luajit-mcode-jumprange-x64` → `UNKNOWN`, `RS-VM-0024`. A profile records
  what the address space looks like, not where a future process will place its
  interpreter. Correctly unevaluatable, correctly reported as unknown, and it
  *becomes* host-sensitive on native arm64.

**This is a different question from the one the reviewer answered, and the
result does not refute them.** A reviewer predicting a verdict from
`analyzer.cpp` shows the rules are simple enough to run in your head once you
also know the host - "page size must equal 4096" is easy to predict if you
know the host is 4 KiB. The script asks the stricter question: did the profile
change the answer? Both are real measurements of different things, and the
reviewer's version is the harder standard.

The first version of this script reported 100% and was wrong. `RS-VM-0017`
("a platform fact this request depends on was never established") fires on the
empty profile by construction, so every signature differed. `--no-unknowns`
suppresses it without changing verdicts. Worth recording because the failure
mode was a metric that flattered the tool, produced by the tool's own author,
on the metric measuring whether the tool is honest.

---

## Results after the fixes

```text
total: 26   supported: 10   unsupported: 7   conditional: 9   unknown: 0
```

Four rows still disagree with their recorded expectation:

| Contract | Verdict | Expected | Who is right |
|---|---|---|---|
| `mimalloc-segment-alignment` | CONDITIONAL | SUPPORTED | **the tool** — the expectation predates `RS-VM-0022`, and 32 MiB alignment genuinely is not guaranteed |
| `box64-4k-page-dirty-code-detection` | CONDITIONAL | SUPPORTED | draw — Box64 does map guests at host addresses, so `RS-VM-0008` is defensible |
| `qemu-reserved-va-32bit-guest`, `qemu-i386-etexec-fixed-noreplace` | CONDITIONAL | SUPPORTED | **the reviewer** — see the open defect below |
| `box64-guest-map32bit-emulation` | UNSUPPORTED | SUPPORTED | neither — the contradiction was manufactured by a missing field, see below |

---

## Open defects, not yet fixed

**`RS-VM-0012` conflates two different reserve/commit models.** It fires off a
single boolean and cannot tell "Windows `MEM_RESERVE`/`MEM_COMMIT`, expecting
a commit-time error" from "`mmap(PROT_NONE)` then `mmap(MAP_FIXED)` over it,
and I check *that* call". QEMU is the second kind: its commit *is* a checked
call site, so the conclusion "failures move from a checked call site to an
unchecked memory access" is simply false for it. This single rule downgraded
four contracts. Every POSIX two-step reserver will trip it.

**`RS-VM-0014` misreads try-exact-then-relax as self-contradiction.** jemalloc
unmaps a relocated result and retries with over-allocate-and-trim
(`pages.c:155`). There is one `permitted_fallbacks` list per request, so
expressing both the program-level truth (relocation is fine) and the
call-level truth (this attempt wants identity) trips a rule that declares the
requirement impossible "on every host". This is the normal shape of allocator
code.

**A satisfied assumption is invisible.** Box64's contract exists to capture the
4 KiB-versus-16 KiB problem. The host is 4 KiB, so the rule stayed silent and
the contract's entire point produced no output. A matched assumption and an
unexamined one look identical in the report. This is the contract that should
flip to `UNSUPPORTED` on Apple Silicon, and today nothing records that it was
even checked.

---

## Addendum 2: the schema gaps closed

Four fields were added after the campaign, each because more than one project
needed it and had nowhere to put it.

| Field | Closes | Effect |
|---|---|---|
| `address_min` / `address_max` | "below 2^31", "below 2^32", "above 2^32" - LuaJIT, Box64 box32, Box64 dynarec | `RS-VM-0023`, `RS-VM-0025` |
| `max_displacement_bytes` + `displacement_reference` | "within +/-2 GiB of another region" - every JIT with a rel32 branch | `RS-VM-0024` |
| `commit_is_checked_call` | POSIX map-over-PROT_NONE vs Windows two-phase | silences the `RS-VM-0012` false positive |
| `validates_returned_address` | test-and-reject vs silent truncation | already covered above |

The displacement field deserves a note. v0.1 still **cannot evaluate** it - a
host profile has no idea where the reference region will land - so
`RS-VM-0024` reports it as an acknowledged gap and moves the verdict to
`UNKNOWN`. That is the point. Before, LuaJIT's machine-code window came back
`SUPPORTED` with nothing said, because stripped of the displacement the
request is "64 KiB anon RW, later RX". The verdict was not wrong; it was
empty. An unanswered question and an unnoticed one now look different.

`RS-VM-0025` exists because closing a gap cost an insight. Removing the
(incorrect) pointer-truncation story from LuaJIT also removed the only
warning that the program depends on winning an address-space lottery. The
bound is satisfiable on this host - and it covers **one part in 65535** of the
space. That is ROADMAP section 11's PREDICTIVE class: not a defect today, not
a guarantee either.

### Campaign after the schema work

```text
total: 26   supported: 11   unsupported: 5   conditional: 9   unknown: 1
```

Four rows still disagree, and all four are the tool arguing for a subtlety the
prediction missed: an unevaluated displacement (`luajit-mcode-jumprange`), an
unguaranteed 32 MiB alignment (`mimalloc-segment-alignment`), a tight address
bound (`box64-guest-map32bit`), and a defensible identity reading
(`box64-4k-page-dirty-code-detection`).

Two expectations were revised, each recording its original value and why:
`redis-jemalloc-page-size-lg16` (the contract's page-size relation was
corrected from equality to at-most against `pages.c:760`) and
`box64-box32-guest-alloc-below-4gb` (the truncation story it rested on cannot
occur; Box64 tests and returns `ENOMEM`). No expectation was changed merely
because the output disagreed with it.

---

## What the schema cannot express

The most valuable output of the campaign. Six of twenty-six contracts have a
load-bearing constraint that survives only as free text in
`required_postconditions`, where no rule reads it.

| Constraint | Who needs it | Consequence today |
|---|---|---|
| **"within ±N bytes of another region"** - now *expressible*, still not *evaluable* | LuaJIT mcode (`±2 GB` of the exit handler), V8 code range (`kRadiusInMB` around the embedded blob) — and every JIT with a rel32 branch: SpiderMonkey, .NET | Stripped of it, LuaJIT's request is "64 KiB anon RW, later RX" — trivially `SUPPORTED`. The verdict is not wrong, it is **empty** |
| **"must be below 2^31 / 2^32"** | LuaJIT `LJ_ALLOC_MBITS`, Box64 box32, Box64 `MAP_32BIT` emulation — three sites in two unrelated projects | `pointer_storage_width_bits` is wrong twice over: it means 2^32 when the bound is 2^31, and it models truncation when both programs test-and-reject |
| **"must be at or above 2^32"** | Box64 box32 dynarec, V8 TrustedRange | Expressible only as a non-binding hint, so a correctness failure and a harmless relocation look the same |
| **"the reservation must be contiguous"** | QEMU `reserved_va`, mimalloc's 4×1 GiB huge-page claim | Only a size is carried |
| **"reserve everything except a region"** | Box64 `reverveHigMem32` — the guest address space is defined by what Box64 has *not* taken | No operation kind for a negative reservation |
| **fork / copy-on-write, entirely** | Redis THP check, `MADV_FREE` dirty-bit probe | Two contracts, zero findings each. On a THP=always host `sysconf(_SC_PAGESIZE)` still returns 4096 while the copy-on-write unit is 2 MiB, so **no page-size fact can ever detect the condition Redis is checking for** |

Two additions would have changed the verdict on five of the twenty-six rows:

- `address_bound { min, max }` on `MappingRequest` — covers rows 2 and 3, and
  would have caught the 4 PiB case independently.
- `relative_to { reference, max_displacement }` — covers row 1, the constraint
  shared by every JIT that emits a relative branch.

---

## Addendum: a reported incident, and a ninth defect

After the campaign, the full body of
[shadps4-emu/shadPS4 issue #4157](https://github.com/shadps4-emu/shadPS4/issues/4157)
became available. It reports GTA V (CUSA00419 v1.47) aborting during GPU
initialisation on an Apple M4 Max under Rosetta 2, and it contains something
the source comments did not: **a measurement**. The reporter compiled a test
program as x86-64, ran it under Rosetta 2, and observed `ENOMEM` for
`MAP_FIXED` at every address across the reserved band, `0x1307200000`
included. Without `MAP_FIXED`, hints in the band redirect to `0x7000000000`
and succeed.

That is a different evidence class from a code comment. It is recorded as
`observed_invariant` - a credible third-party observation we cannot reproduce
- rather than `measured_capability`, which is reserved for what `rs-env-probe`
saw on the host in front of us.

The same requirement, `contracts/gtav-rage-direct-memory-mapping.json`, across
four profiles of increasing evidence quality:

| Profile | Evidence of the band | Verdict | Confidence |
|---|---|---|---|
| ROADMAP transcription | `heuristic_risk` | UNSUPPORTED | `PREDICTIVE` |
| shadPS4 source comment | `heuristic_risk` | UNSUPPORTED | `PREDICTIVE` |
| Issue #4157 measurement | `observed_invariant` | UNSUPPORTED | `OBSERVED_INVARIANT` |
| This Linux host, probed | n/a - never sampled there | **UNKNOWN** | - |

The last row is the one to read twice. On a host where the address is almost
certainly mappable, the tool does not say `SUPPORTED`. The probe never sampled
`0x1307200000`, so it says `UNKNOWN`. "Not observed" cuts both ways.

### Defect 9: the evidence ladder was not monotonic

Building that profile exposed a flaw in the model itself. The ladder ranked
`observed_invariant` above `statically_inferred`, following the ROADMAP's
prose - but their ceilings are `OBSERVED_INVARIANT` and `COUNTEREXAMPLE`, and
`COUNTEREXAMPLE` is the *stronger* claim. So:

```text
evidence rank -> confidence ceiling (lower = stronger claim)
  2 ObservedInvariant     -> 2
  3 StaticallyInferred    -> 1   <- weaker evidence, stronger ceiling
```

`weakest()` would select `statically_inferred` as the limiting link and then
permit a stronger conclusion than the `observed_invariant` fact alone would
have allowed. **Adding better evidence to a chain could weaken its
conclusion.** The GTA V finding came out `COUNTEREXAMPLE` when it should have
been capped at `OBSERVED_INVARIANT`.

The ladder's only job is to determine the ceiling, so it is now ordered by the
ceiling, and `ceiling_is_monotonic_in_the_evidence_order` fails the build if a
future class is inserted in the wrong place. This is a deviation from ROADMAP
section 8.2's ordering, made deliberately and for this reason.

---

## Honest limitations of this campaign

- **One host, one architecture.** Everything was measured on x86-64 Linux.
  The most interesting subject — Apple Silicon under Rosetta, where the page
  size is 16 KiB and W^X is enforced — was not measured, because no macOS
  probe exists yet. Several contracts were written specifically to flip there
  and nothing here demonstrates that they do.
- **Requirements were hand-derived.** Someone read the source and decided what
  the constraint was. Automatic extraction is Phase 5. Hand-derivation makes
  the contracts more accurate than a v0 extractor would, and less
  representative of what a real user would get.
- **One verified field failure.** RSC-0011 / the GTA V contract cites a real
  reported incident with a log excerpt and a described measurement. Every
  other contract here quotes source at a pinned commit and makes no claim that
  any bug report corresponds to it. The issue body was supplied by hand; the
  GitHub API is not reachable from this environment, so nothing was
  independently re-fetched.
- **Predictions were not blind.** The reviewers had read the rule list, so
  agreement between prediction and output is weak evidence. Disagreement is
  the part worth reading.
- **26 contracts is small.** The Phase 0 exit criterion asks for 30 classified
  incidents with 10 in the virtual-memory category. This is not that; these
  are derived constraints, not incidents.

---

## Reproducing

```console
$ cmake -S . -B build -G Ninja && cmake --build build
$ ./build/bin/rs-env-probe vm --name campaign -o host.json
$ bash tools/campaign/run_campaign.sh host.json
```

The runner marks with `!` every row whose verdict disagrees with the
expectation recorded inside the contract. Read those first.

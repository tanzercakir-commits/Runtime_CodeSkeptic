# Domain Case Study — shadPS4

**Status:** ROADMAP **Phase 3** (Virtual Memory Compatibility Analyzer MVP), exercising the work item *"accept manually authored application requirements"* and the deliverable *"sample emulator integration"* against real third-party production code instead of a synthetic example. The requirements here were **hand-derived by reading source**; automatic extraction is **Phase 5** (CodeSkeptic Static Integration) and is not implemented. The environment probe used for the Linux profile is Phase 1 and **is** implemented; the macOS profile is a hand-authored fixture because no Apple Silicon host was available.

**Subject:** `shadps4-emu/shadPS4` at commit `d392abe` (`d392abed77dfa3cfe9a3775f89c6afbe89cc54cd`, *"externals: Update mesa-kosmickrisp to latest (#4606)"*), sparse checkout of `src/core` and `src/common`.

---

## 1. What shadPS4 is, and why it is the right first real subject

shadPS4 is an open-source PlayStation 4 emulator written in C++. It runs guest PS4 executables by reproducing the console's memory layout inside its own host process.

Four properties make it close to an ideal first validation subject, and one makes it unusually convenient.

**It has no address translation layer on its primary target.** On x86-64, guest pointers *are* host pointers. `user_base` is assigned the compile-time constant directly (`src/core/address_space.cpp:664`), and every derived guest pointer is dereferenced as a host pointer. Guest/host address identity is load-bearing, which is exactly the assumption `Assumptions::guest_host_identity_required` exists to model.

**Its address-space requirements are compile-time constants.** Twenty-one lines (`src/core/address_space.cpp:32-52`) fix the entire layout. There is no configuration file, no autotuning, no runtime negotiation on the POSIX path. A requirement document can be derived by reading, and checked by anyone else reading the same lines.

**It carries a shipped workaround for the ROADMAP's motivating incident.** ROADMAP section 3 is built around an emulator that requires a fixed mapping at `0x1000000000` and cannot get it on a translated x86-64 process. shadPS4 has that exact constant, that exact problem on that exact platform, and a conditional that moves the base to dodge it. The motivating example is not a hypothetical; it is `#if defined(__APPLE__) && defined(ARCH_X86_64)`.

**Both branches are in the tree simultaneously.** The pre-workaround base and the post-workaround base are eight lines apart. That permits a controlled comparison — same program, same analyzer, same profile, one constant different — which is what section 5 reports.

**And the convenient property:** shadPS4 computes its own sizes and, on Apple, also emits them as assembler `.zerofill` directives (`src/core/address_space.cpp:23-28`). Those literals independently confirm this document's arithmetic. See section 3.

### Where shadPS4 is *not* representative

It is a single project in a single domain, GPL-2.0-or-later, with an unusually rigid address-space model. Emulators are the easy case for this tool: their assumptions are explicit and hard-coded. A database or a JIT expresses the same class of assumption more diffusely. Nothing here demonstrates that the approach generalizes.

---

## 2. The real constants

Verbatim from `shadps4-emu/shadPS4@d392abe`, `src/core/address_space.cpp:32-52`:

```cpp
// Constants used for mapping address space.
constexpr VAddr SYSTEM_MANAGED_MIN = 0x400000ULL;                    // :33
constexpr VAddr SYSTEM_MANAGED_MAX = 0x7FFFFBFFFULL;                 // :34
constexpr VAddr SYSTEM_RESERVED_MIN = 0x7FFFFC000ULL;                // :35
#if defined(__APPLE__) && defined(ARCH_X86_64)                       // :36
// Commpage ranges from 0xFC0000000 - 0xFFFFFFFFF, so decrease the system reserved maximum.
constexpr VAddr SYSTEM_RESERVED_MAX = 0xFBFFFFFFFULL;                // :38
// GPU-reserved memory ranges from 0x1000000000 - 0x6FFFFFFFFF, so increase the user minimum.
constexpr VAddr USER_MIN = 0x7000000000ULL;                          // :40
#else                                                                 // :41
constexpr VAddr SYSTEM_RESERVED_MAX = 0xFFFFFFFFFULL;                // :42
constexpr VAddr USER_MIN = 0x1000000000ULL;                          // :43
#endif                                                                // :44
#if defined(__linux__)                                                // :45
// Linux maps the shadPS4 executable around here, so limit the user maximum
constexpr VAddr USER_MAX = 0x54FFFFFFFFFFULL;                        // :47
#elif defined(__FreeBSD__)                                            // :48
// FreeBSD address space is extremely volatile, keep this lower for safety.
constexpr VAddr USER_MAX = 0xFFFFFFFFFFFULL;                         // :50
#else                                                                 // :51
constexpr VAddr USER_MAX = 0x5FFFFFFFFFFFULL;                        // :52
#endif
```

Supporting sites, each verified by reading the file:

| Fact | Location |
| --- | --- |
| Size arithmetic `MAX - MIN + 1` (inclusive maxima) | `address_space.cpp:56-58` |
| `.zerofill` directives, Apple/x86-64 only | `address_space.cpp:23-28` |
| `map_flags = MAP_PRIVATE \| MAP_ANONYMOUS \| MAP_FIXED` | `address_space.cpp:639` |
| Apple three-part reservation (`SYSTEM_MANAGED`, `SYSTEM_RESERVED`, `USER`) | `address_space.cpp:644-655` |
| Non-Apple x86-64 **single combined** reservation | `address_space.cpp:657-664` |
| FreeBSD / non-x86-64 branch: `map_flags &= ~MAP_FIXED`, kernel picks base | `address_space.cpp:666-674` |
| Reservation failure check — `== MAP_FAILED` only | `address_space.cpp:677-678` |
| Reservation failure sink — `throw std::bad_alloc{}` | `address_space.cpp:680` |
| Per-mapping sink — `ASSERT_MSG(ret != MAP_FAILED, ...)` | `address_space.cpp:753` |
| Unmap / protect sinks | `address_space.cpp:775`, `:792` |
| Windows placeholder reservation + `ASSERT_MSG` sink | `address_space.cpp:175-181` |
| Windows runtime version clamp to `0x10000000000` | `address_space.cpp:127-144` |
| `DEFAULT_MAPPING_BASE = 0x200000000` | `memory.h:31` |
| `DEFAULT_MAPPING_BASE` used as a **search start** | `memory.cpp:563`, `:740` |
| `SearchFree` — relocating placement, returns `-1` on failure | `memory.cpp:1383-1443` |
| `max_gpu_address = 0x10000000000` (40-bit GPU limit) | `memory.h:196` |

### Inclusive maxima versus half-open ranges

shadPS4's constants are **inclusive** maxima and its own arithmetic is `MAX - MIN + 1` (`address_space.cpp:56-58`). RuntimeSkeptic's `AddressRange` is **half-open**, `[start, end)`. Every conversion in this work therefore adds 1 to the inclusive maximum to obtain the exclusive end. Getting this backwards produces an off-by-one at every boundary and silently changes whether a range intersects a neighbour.

---

## 3. Arithmetic, and its independent confirmation

| Quantity | Computation | Result | `.zerofill` literal | Match |
| --- | --- | --- | --- | --- |
| `SystemManagedSize` | `0x7FFFFBFFF - 0x400000 + 1` | `0x7FFBFC000` (≈32 GiB) | `:25` → `0x7FFBFC000` | yes |
| `SystemReservedSize` (Apple) | `0xFBFFFFFFF - 0x7FFFFC000 + 1` | `0x7C0004000` | `:26` → `0x7C0004000` | yes |
| `UserSize` (Apple) | `0x5FFFFFFFFFFF - 0x7000000000 + 1` | `0x5F9000000000` (95.56 TiB) | `:27` → `0x5F9000000000` | yes |
| `UserSize` (Linux) | `0x54FFFFFFFFFF - 0x1000000000 + 1` | `0x54F000000000` (84.94 TiB) | — (Apple-only directive) | n/a |

The first three rows are confirmed twice over: once by this document's arithmetic and once by literals the shadPS4 authors wrote independently at `address_space.cpp:25-27`. That is a genuine cross-check, not a restatement — the `.zerofill` sizes are hand-written constants in a different syntax in a different part of the file.

Half-open conversions used by the contracts:

| Region | Half-open range | End equals |
| --- | --- | --- |
| System managed | `[0x400000, 0x7FFFFC000)` | `SYSTEM_MANAGED_MAX + 1` |
| User area, Linux | `[0x1000000000, 0x550000000000)` | `USER_MAX + 1` |
| User area, Apple | `[0x7000000000, 0x600000000000)` | `USER_MAX + 1` |

Note that macOS takes the **`#else`** branch at line 52 (`0x5FFFFFFFFFFF`), because it is neither `__linux__` nor `__FreeBSD__`. The `.zerofill USER_AREA` literal `0x5F9000000000` at line 27 confirms this: it equals `0x5FFFFFFFFFFF - 0x7000000000 + 1` and no other combination of the constants.

### What the Apple workaround costs

From `memory.h:196` (`max_gpu_address = 0x10000000000`, the PS4 GPU's 40-bit limit), the GPU-addressable slice of the user area is `max_gpu_address - USER_MIN`:

| Configuration | GPU-addressable window | Size |
| --- | --- | --- |
| non-Apple (`USER_MIN = 0x1000000000`) | `0x10000000000 - 0x1000000000` | `0xF000000000` = 960 GiB |
| Apple/x86-64 (`USER_MIN = 0x7000000000`) | `0x10000000000 - 0x7000000000` | `0x9000000000` = 576 GiB |
| **Difference** | | `0x6000000000` = **384 GiB (40%)** |

The workaround is not free: it removes 40% of the GPU-addressable user window. This is arithmetic over three published constants, not an inference.

---

## 4. Verdict matrix — 8 combinations

Four contracts × two profiles. Command form:

```sh
./build/bin/rs-check contracts/<file>.json --profile <profile>.json --quiet
```

Profiles: `profiles/generated/linux-x86_64.json` (`origin: measured`, `profile_id sha256:a9dca1fe…`, kernel 6.18.5) and `profiles/fixtures/macos-arm64-rosetta-x86_64.shadps4-derived.fixture.json` (`origin: hand_authored_fixture`, `profile_id sha256:bf00edab…`).

| # | Contract | Profile | Verdict | Exit | Finding | Confidence |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | `shadps4-user-area-base` | linux (measured) | `UNKNOWN` | 3 | RS-VM-0017 | `HYPOTHESIS` |
| 2 | `shadps4-user-area-base` | macos (fixture) | **`UNSUPPORTED`** | **1** | **RS-VM-0001** | `PREDICTIVE` |
| 3 | `shadps4-system-managed-area` | linux (measured) | `UNKNOWN` | 3 | RS-VM-0017 | `HYPOTHESIS` |
| 4 | `shadps4-system-managed-area` | macos (fixture) | `UNKNOWN` | 3 | RS-VM-0017 | `HYPOTHESIS` |
| 5 | `shadps4-user-area-base-apple` | linux (measured) | `UNKNOWN` | 3 | RS-VM-0017 | `HYPOTHESIS` |
| 6 | `shadps4-user-area-base-apple` | macos (fixture) | `UNKNOWN` | 3 | RS-VM-0017 | `HYPOTHESIS` |
| 7 | `shadps4-default-mapping-base` | linux (measured) | `SUPPORTED` | 0 | none | — |
| 8 | `shadps4-default-mapping-base` | macos (fixture) | `SUPPORTED` | 0 | none | — |

Exit codes per `include/runtimeskeptic/reports/report.hpp:46-51`: `0` supported, `1` unsupported, `2` conditional, `3` unknown. All four contracts parsed on first submission; no schema errors occurred.

Requirement IDs, for reproducibility:

| Contract | `requirement_id` |
| --- | --- |
| `shadps4-user-area-base` | `sha256:b69e70ef…` |
| `shadps4-system-managed-area` | `sha256:49d542ef…` |
| `shadps4-user-area-base-apple` | `sha256:11fc399e…` |
| `shadps4-default-mapping-base` | `sha256:b8bdd435…` |

### Reading the `UNKNOWN` rows

Rows 1, 3, 5 and 6 are `UNKNOWN` for a reason that is the evidence model working, not failing.

On the **measured Linux profile**, `rs-env-probe` v0.1 recorded 35 `available_ranges`, each a window of a few MiB. `query_range()` requires an available range to **fully contain** the request (`src/vm/profile.cpp`, step 3); partial containment yields `UNKNOWN`. The smallest of these requests is ≈32 GiB. No small window can contain it, and the probe never attempted a reservation at that scale, so nothing is established. The host's hard bounds *are* satisfied — `0x550000000000 < 0x7FFFFFFFF000` — so nothing is refuted either.

On the **macOS fixture**, `available_ranges` is deliberately empty. The fixture asserts exactly one thing positively: that one band is unavailable. Everything outside that band resolves to `UNKNOWN`, because absence of a contradiction is not evidence of availability.

This is ROADMAP section 10.1 — *not observed is not impossible* — and its mirror, *not observed is not available*.

---

## 5. The key finding

### Before the workaround

Row 2 is the whole point. Contract `shadps4-user-area-base.json` encodes the `#else` branch: `USER_MIN = 0x1000000000`, `exact_address_required: true`, `guest_host_identity_required: true`, `translation_layer_available: false`, no permitted fallbacks. Checked against a macOS-shaped profile whose unavailable band is transcribed from shadPS4's own comment:

```
UNSUPPORTED   (exit 1)
RS-VM-0001    exact virtual-memory mapping cannot be satisfied
severity critical  |  confidence PREDICTIVE  |  impact UNSUPPORTED
```

Had this contract and this profile existed before the Apple conditional was written, RuntimeSkeptic would have reported, **at build time and without launching the emulator**, that the reservation cannot be placed — naming the conflicting range, the missing translation layer, and the `throw std::bad_alloc{}` at `address_space.cpp:680` that the failure would reach.

That is the Phase 3 exit criterion *"motivating exact-mapping incident is diagnosed correctly"*, met against real third-party code rather than a synthetic input.

### After the workaround

Row 6 is the same program with one constant changed. `USER_MIN = 0x7000000000` places the reservation immediately above the band, and RS-VM-0001 does not fire. The verdict improves from `UNSUPPORTED` (exit 1) to `UNKNOWN` (exit 3).

**The workaround is not reported as `SUPPORTED`, and it should not be.** The fixture never established that `[0x7000000000, 0x600000000000)` is available; it established only that a different band is not. Reporting `SUPPORTED` would convert an absence of evidence into an endorsement, which is precisely what `UNKNOWN` outranking `CONDITIONALLY_SUPPORTED` exists to prevent (`docs/evidence_model.md` §5).

So the honest summary of the contrast is: **the tool refutes the old base and declines to bless the new one.** That is a smaller claim than "we verified the fix", and it is the correct one given a fixture rather than a measurement.

### Why confidence is `PREDICTIVE` and cannot be higher

The band carries `evidence: heuristic_risk`, because it is transcribed from a third-party source comment rather than measured. The ceiling rule caps any finding resting on it at `PREDICTIVE` (`docs/evidence_model.md` §4). The analyzer asked for a stronger label and did not get one; `emit()` clamped it. Raising this to `PROVEN` requires an `rs-env-probe` run on Apple Silicon under Rosetta 2 producing a `measured_capability` fact — nothing else will do it, and no amount of confidence in shadPS4's maintainers substitutes.

### Two structural observations from reading the source

**No non-destructive fixed mapping anywhere.** A tree-wide search of `shadps4-emu/shadPS4@d392abe` finds no `MAP_FIXED_NOREPLACE`, `MAP_EXCL`, or any `NOREPLACE` token. Only destructive `MAP_FIXED` is used (`address_space.cpp:639`, `:751`, `:774`). Where a target range is occupied rather than excluded, the reservation succeeds by clobbering, and the `== MAP_FAILED` check at `:677-678` cannot see it. The source itself documents the hazard, at `address_space.cpp:666`: *"FreeBSD can't stand MAP_FIXED or it may overwrite mmap() itself!"*

**The same program is three different contracts.** The x86-64 POSIX branch (`:658-664`) requires identity and permits nothing. The FreeBSD / non-x86-64 branch (`:666-674`) drops `MAP_FIXED`, lets the kernel choose, and treats the bases as offsets — `translation_layer_available: true`, `relocate` permitted, because the JIT absorbs the delta. The Windows branch (`:107-212`) enumerates free regions with `VirtualQuery`, reserves them as `MEM_RESERVE_PLACEHOLDER` via `VirtualAlloc2`, and clamps `supported_user_max` at runtime based on `RtlGetVersion` and the title's SDK version (`:127-144`). Only the Windows path adapts to the host it finds. A requirement document must therefore name its build configuration; all four contracts here do so in `name` and `component`.

---

## 6. Two verdicts that needed investigating

Per the working rule that a suspicious verdict is investigated rather than tuned away, both of these are reported with their causes.

### 6.1 An authoring error the analyzer caught (fixed)

`shadps4-default-mapping-base.json` initially declared `guest_host_identity_required: true` **and** `permitted_fallbacks: ["relocate"]`. It came out `UNSUPPORTED` on both profiles with **RS-VM-0014** (*"permitted fallback contradicts a required postcondition"*, `Confidence::Proven`, host not consulted) and **RS-VM-0007**.

The analyzer was right and the contract was wrong. `rule_internal_fallback_contradiction` (`src/vm/analyzer.cpp:152-158`) fires when a requirement permits relocation while demanding identity — a host-independent self-contradiction.

Re-reading the source settled it. On this path the guest passes address `0`, shadPS4 substitutes `DEFAULT_MAPPING_BASE` (`memory.cpp:563`), then `SearchFree` walks past it to any free address (`memory.cpp:564`, `:1383-1443`) and reports the result back through `out_addr`. There is no caller-chosen address for the host address to be identical to. Correct value: `guest_host_identity_required: false`. Fixed, documented in the contract's `required_postconditions`, and rows 7-8 above are post-fix.

This was a contract defect, not an analyzer defect, and RS-VM-0014 did exactly its job.

### 6.2 An analyzer over-claim (reported, not worked around)

After the fix, rows 7 and 8 both report `SUPPORTED` with **exit code 0 and "No findings"** — including against the macOS fixture, whose `available_ranges` is **empty** and which establishes nothing whatsoever about `0x200000000`.

The cause is a deliberate early return in `rule_range_availability` (`src/vm/analyzer.cpp:367-374`):

```cpp
if (!req_.request.address) return;
if (!req_.request.exact_address_required &&
    !req_.assumptions.guest_host_identity_required) {
    // The program does not care where the mapping lands, so range
    // availability at this particular address does not decide anything.
    return;
}
```

`result_.overall` is initialised to `SupportLevel::Supported` (`src/vm/analyzer.cpp:1008`), so when no rule fires, the verdict stays `SUPPORTED`.

**This was confirmed to be address-independent.** Taking contract 4 and moving its address to `0x2000000000` — squarely inside the band the macOS fixture explicitly declares **unavailable** — still yields `SUPPORTED`, exit 0, "No findings".

The guard's reasoning is defensible: a caller with no placement constraint genuinely does not care about that address. But the *reported* verdict is not. Per `docs/evidence_model.md` §5, `SUPPORTED` means *"every relevant fact was established and every requirement is satisfiable"*, and here **no fact was established at all**. A profile that knows nothing produced a green CI exit code.

`CONDITIONALLY_SUPPORTED` ("satisfiable, and the conditions are known and stated" — the condition being that relocation will occur) or `UNKNOWN` would both be honest. `SUPPORTED` with exit 0 is not.

For shadPS4 specifically the outcome is substantively harmless: `SearchFree` really will find another address, and the sink is a graceful `ORBIS_KERNEL_ERROR_ENOMEM` (`memory.cpp:567`, `:744`) rather than an abort. The contract has been left as-is because it is now a faithful model of the program. **The finding is about the analyzer, not about shadPS4, and is recorded here rather than hidden by adjusting the contract.**

Suggested follow-up for the maintainers: when `rule_range_availability` returns early, a don't-care placement request should still contribute `CONDITIONALLY_SUPPORTED`, or emit an informational finding, so that "no constraint" is not silently reported as "verified".

---

## 7. Limitations

Stated plainly, because several results above are weaker than they look.

**The macOS band is unverified.** `[0x1000000000, 0x7000000000)` is transcribed from a source comment at `address_space.cpp:39-40`. **The claim that macOS reserves `0x1000000000-0x6FFFFFFFFF` for the GPU is shadPS4's comment, not a verified platform fact** — not an Apple statement, not a documented contract, and not measured by RuntimeSkeptic. It is classified `heuristic_risk` and caps every dependent finding at `PREDICTIVE`. The same applies to `max_user_address = 0x600000000000`, which is shadPS4's self-imposed `USER_MAX` (`:52`) reused as a host ceiling — an inference layered on a third-party constant.

**No incident report was confirmed.** No GitHub issue, CVE, bug number, mailing-list thread, user report or game title is cited in the contracts, the corpus entries, the fixture or this document, because none was verified. RuntimeSkeptic made no network query about this project; all work is from the local clone. Where the corpus entries suggest the workaround implies prior field failures, that is labelled **explicitly as inference** under its own heading in both RSC-0009 and RSC-0010, and it is inference from the existence of the code, nothing more.

**The requirements were hand-derived.** A human read `address_space.cpp` and `memory.cpp` and wrote JSON. Automatic extraction is Phase 5 (CodeSkeptic) and does not exist. Section 6.1 is direct evidence that hand-derivation is error-prone: the first draft of one contract was internally contradictory and was caught only because the analyzer has a rule for exactly that. Nothing here demonstrates that requirement extraction scales.

**This is a retrospective, not a prediction.** The Apple workaround was committed before this analysis began. RuntimeSkeptic did not warn anyone; it was pointed at a known answer and reproduced it. The honest claim is *"the tool would have flagged this"* — which is worth establishing, but is not the same as *"the tool found something new"*. No prediction was made and none has been tested.

**Only four requirements, one file, one domain.** These cover the address-space reservation path. shadPS4's protection handling, its JIT (`cpu_patches.cpp`), its file mappings and its Windows placeholder logic are untouched. Findings for page size, W^X, reserve/commit and pointer width were not exercised at all.

**The Linux profile under-determines these requests.** `rs-env-probe` v0.1 samples small windows and never attempts a multi-terabyte reservation, which is why four of eight rows are `UNKNOWN`. That is a probe limitation, correctly surfaced rather than papered over — but it does mean this case study establishes very little about Linux.

**One contract's size is an authoring choice, not a source fact.** `DEFAULT_MAPPING_BASE` is a search start, not a sized mapping; no size is derivable from the source. The 16 KiB used matches shadPS4's own default alignment (`memory.cpp:562`) and is flagged in that contract's `name`, in capitals, as not being a source fact.

---

## 8. Artifacts

| Path | Contents |
| --- | --- |
| `contracts/shadps4-user-area-base.json` | Linux user area, `USER_MIN = 0x1000000000`, pre-workaround base |
| `contracts/shadps4-system-managed-area.json` | System-managed area, `0x400000` … `0x7FFFFBFFF` |
| `contracts/shadps4-user-area-base-apple.json` | Apple/Rosetta user area, `USER_MIN = 0x7000000000`, workaround applied |
| `contracts/shadps4-default-mapping-base.json` | `DEFAULT_MAPPING_BASE = 0x200000000`, relocating placement path |
| `profiles/fixtures/macos-arm64-rosetta-x86_64.shadps4-derived.fixture.json` | macOS fixture with band and ceiling cited to `address_space.cpp:39-40` and `:52` |
| `corpus/runtime_failures/RSC-0009-shadps4-apple-gpu-reserved-band.md` | The Apple band; `provenance: source_citation` |
| `corpus/runtime_failures/RSC-0010-shadps4-linux-user-max-clamp.md` | The Linux ceiling; `provenance: source_citation` |

All four contracts declare `assumption_evidence: "statically_inferred"` — read out of shadPS4's source, not declared to us by its developers. Under the ceiling rule that caps dependent findings at `COUNTEREXAMPLE`; the macOS rows land lower still, at `PREDICTIVE`, because the profile fact is weaker than the requirement fact.

Both corpus entries use `provenance: source_citation`, **a new value not yet listed in `corpus/runtime_failures/README.md` §3.6**. It should be added there, defined as: *every claim about program behavior is a claim about a specific publicly readable source file at a specific commit, cited with `file:line`, and licenses no claim about incidents, reports or platform behavior.* Both remain `status: draft`, since §3.5 reserves `sourced` for a verifiable public **incident**, and what is verifiable here is source code.

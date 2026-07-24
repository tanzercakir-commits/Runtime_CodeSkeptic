# Failure Taxonomy

Ten categories of cross-layer runtime failure, each defined so that an incident can be assigned to exactly one of them and so that ordinary memory bugs can be excluded.

**Status:** ROADMAP Phase 0 (Research Boundary and Failure Taxonomy), initial taxonomy of ROADMAP section 13. The category names are fixed and are used verbatim in corpus front matter (`corpus/runtime_failures/`). Detection coverage is **partial**: 17 of the 18 registered virtual-memory finding IDs are emitted by `src/vm/analyzer.cpp`; two of the ten categories have no dedicated finding ID in v0.1 and are marked as such below.

---

## How to use this document

Each category below states:

1. **Definition** — the invariant that is broken.
2. **Concrete example** — C/C++ or systems level.
3. **How it manifests** — what an engineer actually sees.
4. **Typical establishing evidence class** — which `rs::EvidenceClass` normally carries the decisive fact (`docs/evidence_model.md`).
5. **Finding IDs** — which registered IDs are primary to the category, and which are cross-listed.
6. **Not an ordinary memory bug because...** — the discriminator.

A single incident often touches several categories. Corpus entries carry exactly one `category:` value — the category that names the *decisive* mismatch — and mention the others in the body.

### The universal discriminator

Every category shares one test. Before classifying an incident here, it must pass:

> **Would the same source code, unchanged, be correct on some legal execution environment, and incorrect on another, with no undefined behavior in either?**

If yes, it is an environment contradiction and belongs in this taxonomy.
If the code is wrong on *every* environment — out-of-bounds access, use-after-free, data race, signed overflow, strict-aliasing violation — it is an ordinary memory or concurrency bug. Use AddressSanitizer, Valgrind, ThreadSanitizer or UndefinedBehaviorSanitizer; RuntimeSkeptic has nothing to add and will not report it.

There is one deliberate exception: `RS-VM-0014` (invalid fallback contract) reports a requirement document that contradicts itself, which is wrong on every host. It is included because the contradiction is in the *contract*, not in the code, and no other tool reads contracts.

### Category-to-finding-ID map

| Category | Primary finding IDs | Cross-listed |
| --- | --- | --- |
| semantic success violation | RS-VM-0007, RS-VM-0016 | RS-VM-0001, RS-VM-0005, RS-VM-0018 |
| unsupported exact capability | RS-VM-0001, RS-VM-0010 | RS-VM-0002, RS-VM-0003, RS-VM-0009 |
| invalid fallback | RS-VM-0014 | RS-VM-0007, RS-VM-0016 |
| error-code laundering | *(none in v0.1)* | RS-VM-0015, RS-VM-0007 |
| permanent error treated as retryable | RS-VM-0015 | RS-VM-0001 |
| capability present but required property absent | RS-VM-0004, RS-VM-0005, RS-VM-0009, RS-VM-0011 | RS-VM-0010, RS-VM-0016 |
| resource-topology contradiction | RS-VM-0002, RS-VM-0003, RS-VM-0006, RS-VM-0013 | RS-VM-0004, RS-VM-0005 |
| temporal contract violation | RS-VM-0012, RS-VM-0018 | RS-VM-0016 |
| loader/ABI mismatch | *(none in v0.1)* | RS-VM-0013, RS-VM-0003 |
| platform-observed behavior treated as guaranteed behavior | RS-VM-0008, RS-VM-0017 | RS-VM-0007 |

Every registered ID appears at least once as a primary. Two categories have no primary ID in v0.1; both are explained in their sections.

---

## 1. Semantic success violation

### Definition

The operation returns a success status, and a postcondition the caller requires is nevertheless false. The API contract is satisfied; the caller's contract is not.

```text
API success  !=  caller postcondition satisfied
```

This is the archetype of the Silent Contract Degradation family and the reason ROADMAP section 8.1 is design principle number one.

### Concrete example

```c
void* requested = (void*)0x1000000000;
void* result = mmap(requested, size, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

if (result == MAP_FAILED)
    return -1;              // the only outcome the caller models

assert(result == requested); // the postcondition, declared nowhere
```

`mmap` without `MAP_FIXED` treats the address as a hint. Returning a different address is a **success**, fully within the specified contract. The caller's model has two outcomes; the platform has three.

### How it manifests

- an assertion fires far from the mapping call, often at a much later stage of startup;
- or, with assertions compiled out, pointers computed as `guest_base + guest_offset` resolve into unrelated memory and the process corrupts itself;
- reproduction is environment-dependent and frequently disappears under a debugger, because the debugger changes the address-space layout that made the hint succeed.

### Typical establishing evidence class

`measured_capability` or `specified_guarantee` for the platform half (a probe or documentation establishes that relocation is permitted), combined with `statically_inferred` or `specified_guarantee` for the caller's postcondition depending on whether it came from CodeSkeptic or from a hand-authored requirement.

When the platform fact is "relocation is *permitted*" rather than "relocation *will* occur", the decisive analyzer step is `bounded_counterexample` — a platform-legal outcome that reaches the failure sink — which ceilings the finding at `COUNTEREXAMPLE`. See `Analysis::rule_hinted_relocation()`.

### Finding IDs

Primary: `RS-VM-0007` (hinted mapping may relocate), `RS-VM-0016` (no non-destructive exact-mapping primitive: success does not imply the range was free).
Cross-listed: `RS-VM-0001`, `RS-VM-0005` (the mapping is larger than requested and the surplus does not fault), `RS-VM-0018`.

### Not an ordinary memory bug because...

Nothing in the program is undefined. Both outcomes of `mmap` are valid, both pointers are valid, and every access through them is in-bounds of a real mapping. A sanitizer sees a well-formed program. The defect is that the caller enumerated two outcomes where the platform contract permits three, so the missing branch was never written.

---

## 2. Unsupported exact capability

### Definition

The environment cannot provide the requested capability at all, for any parameters or any retry. There exists no legal outcome that satisfies the requirement on this host.

This is the only category that can reach `PROVEN` + `UNSUPPORTED` from a single platform fact.

### Concrete example

An emulator requires a mapping at exactly `0x1000000000` because it has no guest-to-host translation layer, and the host's user address space has that region permanently occupied (loader reservations, a runtime's heap arena, a security policy's shadow region). Or, more bluntly, a host whose `mmap` implementation does not honor `MAP_FIXED` semantics at all, so exact placement is not a capability the platform offers.

```json
{ "operation": "virtual_memory_map",
  "requested_address": "0x1000000000",
  "size": 16384,
  "exact_address_required": true,
  "guest_host_identity_required": true,
  "translation_layer_available": false }
```

### How it manifests

- a hard failure at startup on one host and clean operation on another, with an error message that names a resource shortage rather than a structural impossibility;
- or, worse, the layer below converts the impossibility into a relocation and the incident becomes a semantic success violation (category 1) instead.

### Typical establishing evidence class

`measured_capability` — a probe attempted the mapping and recorded the failure — or `specified_guarantee` where the platform documents a hard bound. An `unavailable_ranges` entry in a profile must never carry `unknown`; `ClassifiedRange` exists specifically to force an evidence class onto every range (`include/runtimeskeptic/vm/address_range.hpp`).

### Finding IDs

Primary: `RS-VM-0001` (exact virtual-memory mapping cannot be satisfied), `RS-VM-0010` (anonymous executable mapping is not available).
Cross-listed: `RS-VM-0002`, `RS-VM-0003`, `RS-VM-0009`.

### Not an ordinary memory bug because...

The program never executes an invalid operation; it executes a valid operation that this environment refuses. The same binary is correct on a host whose address space is laid out differently. The remediation classes are architectural (`add_address_translation_layer`, `relax_exact_address_requirement`, `select_different_host_configuration`), not corrective — there is no line of code to fix.

---

## 3. Invalid fallback

### Definition

A substitute behavior is accepted in place of the requested one, and the substitute does not preserve a guarantee the caller depends on. The fallback may be chosen by the adapter at runtime, or declared acceptable in the requirement document itself.

### Concrete example, adapter form

```c
void* p = mmap(hint, size, prot, flags | MAP_FIXED_NOREPLACE, -1, 0);
if (p == MAP_FAILED) {
    // "graceful degradation"
    p = mmap(NULL, size, prot, flags, -1, 0);   // anywhere is fine, right?
}
return p;   // caller still believes it got `hint`
```

The fallback is a legitimate operation. It is invalid *as a fallback* because the caller's reason for asking for `hint` was identity, and the fallback discards exactly that property.

### Concrete example, contract form

A requirement document that simultaneously states `exact_address_required: true` and lists `relocate` among `permitted_fallbacks`. Any relocated mapping satisfies the fallback while violating the postcondition. This is host-independent, so `Analysis::rule_internal_fallback_contradiction()` runs first and does not consult the profile at all.

### How it manifests

- the failure moves from the point where an error was available to a point where none is;
- error handling *appears* comprehensive in review — there is a fallback path, it is tested, it works — and the review never asks whether the fallback preserves the property the primary path was chosen for.

### Typical establishing evidence class

For the contract form: `specified_guarantee`, because the contradiction is analytic — the analyzer's own step is *"a relocated mapping cannot satisfy `returned_address == requested_address`"*, sourced to the definition of relocation.
For the adapter form: `statically_inferred` (Phase 5, CodeSkeptic identifies the fallback branch) or `observed_invariant` (Phase 4, the wrapper records that the fallback path executed).

### Finding IDs

Primary: `RS-VM-0014` (permitted fallback contradicts a required postcondition).
Cross-listed: `RS-VM-0007`, `RS-VM-0016`.

### Not an ordinary memory bug because...

Both the primary and the fallback operation are individually correct and individually well-defined. The defect is a relation between them: the fallback drops a property the primary was selected to obtain. No memory-safety tool models "why this call was made", so none can detect it.

---

## 4. Error-code laundering

### Definition

An error's meaning changes as it crosses a layer boundary. The layer preserves the *fact* of failure but discards its *classification*: permanent becomes transient, specific becomes generic, security-policy refusal becomes resource exhaustion, or failure becomes success.

### Concrete example

```c
// compatibility layer
int adapter_map(uint64_t guest_addr, size_t len, void** out) {
    void* p = mmap((void*)guest_addr, len, prot, MAP_FIXED_NOREPLACE|flags, -1, 0);
    if (p == MAP_FAILED) {
        // errno was EEXIST: the range is permanently taken in this process.
        return ADAPTER_EAGAIN;      // laundered into "try again"
    }
    *out = p;
    return ADAPTER_OK;
}
```

`EEXIST` and `EPERM` are permanent for the lifetime of the address-space layout; `ENOMEM` may be transient. Collapsing them into one generic code destroys the only information the caller needed to decide between retrying and reporting. The reverse direction is equally common: a `PROT_EXEC` request refused by a security policy is reported as "out of memory", sending the investigation toward memory limits rather than toward the policy.

### How it manifests

- retry loops that never terminate (see category 5);
- alerting and dashboards that attribute a policy failure to capacity, so the response is to add hardware;
- error messages that are true at the layer that emitted them and misleading at the layer that reads them.

### Typical establishing evidence class

`observed_invariant` in the Phase 4 runtime-wrapper mode, because detecting laundering requires seeing the *native* error before the layer transforms it and the *reported* error after. Static analysis can flag a suspicious mapping table (`statically_inferred`), but confirming a launder needs both sides of the boundary.

### Finding IDs

**No dedicated finding ID exists in v0.1.** The reason is structural, not an oversight: laundering is defined by a difference between two error values observed at two layers, and v0.1 has no mechanism to observe either — it compares two static documents. `libruntimeskeptic` (ROADMAP Phase 4) is required, and its exit criterion *"monitoring does not alter error codes"* exists precisely so the monitor can be trusted as the reference for what the native error was.

Cross-listed today: `RS-VM-0015` (the consequence of laundering permanent into transient) and `RS-VM-0007` (the extreme case, failure laundered into success).

### Not an ordinary memory bug because...

Every layer's error handling is locally correct. There is no leak, no invalid access, and often no crash at all. The defect lives in the translation table between two error vocabularies, which is data, not memory.

---

## 5. Permanent error treated as retryable

### Definition

The caller retries an operation whose failure is structural on this host. No number of attempts, no backoff schedule and no waiting period changes the outcome. A special case of category 4, promoted to its own category because it has a distinct, mechanically checkable signature and a distinct remediation.

### Concrete example

```c
for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
    void* p = mmap(kFixedGuestBase, kArenaSize, prot, MAP_FIXED_NOREPLACE|flags, -1, 0);
    if (p != MAP_FAILED)
        return p;
    usleep(backoff_us);
    backoff_us *= 2;
}
fatal("could not map guest arena after %d attempts", kMaxAttempts);
```

If `kFixedGuestBase` collides with a loader reservation, every iteration fails identically. The backoff is a pure cost. With no attempt limit, the process hangs at a point that looks like a slow startup rather than a failure.

### How it manifests

- startup that hangs, or takes exactly `kMaxAttempts * backoff` before failing;
- a failure report that names the *last* attempt, hiding that all attempts were identical;
- the reflexive fix — raise the retry count — is applied, appears to change the timing, and is shipped.

### Typical establishing evidence class

Derived rather than directly measured. `Analysis::rule_retry_of_permanent_error()` runs last and fires only when another rule already produced a `PROVEN` + `UNSUPPORTED` finding, i.e. when impossibility has been established. It therefore inherits the establishing evidence of that other rule, typically `measured_capability` or `specified_guarantee`.

*Implementation note:* the rule's own evidence step is currently labelled `measured_capability` with source `"analyzer: cross-rule aggregation"`. That is a defensible ceiling (it can only fire behind a `PROVEN` finding) but the label describes the upstream fact, not this step. A dedicated evidence class for analyzer-internal derivation would be more honest; none exists yet.

### Finding IDs

Primary: `RS-VM-0015` (retry loop targets a permanently impossible operation).
Cross-listed: `RS-VM-0001`.

### Not an ordinary memory bug because...

The loop is well-formed, terminates or not exactly as written, and touches no invalid memory. The defect is a false belief about the *nature* of an error code. `remediation_class: treat_error_as_permanent` is the fix; `"increase the retry count or add a backoff"` is emitted as a rejected fix with the reason *"the operation is impossible on this host, not contended"*.

---

## 6. Capability present but required property absent

### Definition

The environment supports the operation and the call succeeds, but a *property* the caller requires of the result is missing: an alignment, a permission combination, a granularity, a policy grant, or a guarantee about what lies outside the requested extent.

The distinction from category 2: there, the capability does not exist. Here it does, and the gap is one attribute of it.

### Concrete examples

**Write-xor-execute.** The host provides executable memory and writable memory, and refuses to provide both permissions on one mapping at one time. A JIT that writes an instruction stream through the same pointer it later calls requires the conjunction, not the two capabilities separately.

```c
void* code = mmap(NULL, size, PROT_READ|PROT_WRITE|PROT_EXEC,
                  MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
// Depending on host policy: MAP_FAILED, or a mapping whose PROT_EXEC
// was quietly dropped, or a mapping that faults on first execution.
```

**Entitlement gating.** Executable memory exists but is granted only to processes carrying a platform entitlement or policy opt-in. The capability is present in the kernel and absent for *this process*, which is a property of the process, not of the call.

**Allocation granularity.** A host that maps at 4 KiB page granularity but reserves address space in 64 KiB units. `VirtualAlloc`-style reservation succeeds for a base of `0x...1000`, having rounded the base down to `0x...0000`. Exact placement is a capability; exact placement *at 4 KiB resolution* is the absent property.

### How it manifests

- `SIGSEGV`/`SIGBUS`/`EXCEPTION_ACCESS_VIOLATION` on the first execution of generated code, at an address that is a valid mapping;
- a mapping call that succeeds and returns a base different from the one requested, with no error to check;
- behavior that differs between a signed/entitled build and a locally built binary of identical source.

### Typical establishing evidence class

`measured_capability` — this category is the strongest argument for an executable probe, because the difference between "PROT_EXEC accepted" and "PROT_EXEC accepted and the page is actually executable" is only visible by trying it. `specified_guarantee` where the platform documents the policy.

### Finding IDs

Primary: `RS-VM-0004` (address vs allocation granularity), `RS-VM-0005` (size vs allocation granularity), `RS-VM-0009` (simultaneous write and execute restricted), `RS-VM-0011` (executable memory requires a platform entitlement).
Cross-listed: `RS-VM-0010`, `RS-VM-0016`.

### Not an ordinary memory bug because...

The program's memory model is internally consistent and would be correct on a host offering the conjunction. Note the direction of remediation: `RS-VM-0009` emits `"disable the platform's W^X enforcement"` as a **rejected fix**, with the reason that RuntimeSkeptic does not treat a security control as a defect. Distinguishing "unsupported" from "refused by policy" is a stated safety requirement (ROADMAP section 18).

---

## 7. Resource-topology contradiction

### Definition

The request contradicts the *shape* of the resource space itself: its bounds, its unit size, or the width of the identifiers used to address it. No parameter choice satisfies the requirement, because the requirement is inconsistent with the geometry of the address space.

### Concrete examples

**Page size.** An emulator compiled with `#define GUEST_PAGE 4096` running on a host with a 16 KiB page. Protection changes and dirty tracking operate on host pages, so several guest pages share one protection domain. Rounding the allocation size does not repair the model.

```c
static_assert(GUEST_PAGE == 4096, "guest page size");
// sysconf(_SC_PAGESIZE) == 16384 on this host
mprotect(page_addr, GUEST_PAGE, PROT_READ);   // affects 4 guest pages
```

**Address-space bounds.** A mapping below the host's minimum mappable address (a policy floor such as Linux's `vm.mmap_min_addr`, which exists to make null-pointer dereferences fault rather than execute), or a range extending past the top of the user-mode address space on a host with fewer usable virtual-address bits than the program assumes.

**Pointer width.** A host address above `2^32` stored in a 32-bit slot. The high bits are discarded on assignment and the truncated pointer refers to unrelated memory.

### How it manifests

- a `static_assert` or an `assert` at startup, if the program checks;
- otherwise, protection changes affecting more memory than intended, "impossible" aliasing between logically distinct regions, or pointers that are silently wrong from the first store;
- a symptom that scales with address-space size, so it appears only on hosts with large or unusually laid-out address spaces.

### Typical establishing evidence class

`measured_capability` for page size, allocation granularity and address-space bounds — all four are direct probe outputs. `specified_guarantee` for pointer width derived from the process architecture: `EnvironmentProfile::process_pointer_width()` derives it from `platform.process_arch`, and `Analysis::rule_pointer_truncation()` labels that step `specified_guarantee` because it follows from the ABI rather than from an observation.

### Finding IDs

Primary: `RS-VM-0002` (address below minimum), `RS-VM-0003` (range above user space), `RS-VM-0006` (page size mismatch), `RS-VM-0013` (pointer truncation).
Cross-listed: `RS-VM-0004`, `RS-VM-0005`.

### Not an ordinary memory bug because...

The arithmetic is correct with respect to the constants the program was built with. `RS-VM-0006` is emitted with `"round the mapping size up to the host page size"` as a rejected fix: rounding fixes the allocation and leaves the program's page-granularity model wrong. This is the clearest case of a defect that lives in a *constant*, not in a statement.

---

## 8. Temporal contract violation

### Definition

The ordering or lifecycle of states differs from the program's model. Each individual operation succeeds; the sequence does not deliver the guarantee, or the point at which a failure can be detected moves to a place where the program does not look.

### Concrete examples

**Reserve/commit.** A program written against a two-phase model reserves a large region, then commits sub-regions as they are needed, and checks the commit call for failure. On a host that reserves lazily with no distinct commit step, the reservation succeeds for the whole size and the shortage surfaces at first *touch* — as a fault or an out-of-memory kill — rather than at a checkable call site. The check the program wrote is now guarding an operation that cannot fail.

**JIT lifecycle.** `Allocated -> Writable -> CodeWritten -> CacheFlushed -> Executable`. Omitting the instruction-cache flush on an architecture with non-coherent caches produces execution of stale bytes. Every call in the sequence returned success.

**File mapping beyond end of file.** A mapping longer than the file succeeds at map time. Accessing the tail is host-specific: fault, error, or zero fill. The file's length can also change between the map and the access, so the same code is correct or incorrect depending on an event in another process.

### How it manifests

- out-of-memory kills at a point unrelated to any allocation call;
- `SIGBUS` on a read from a mapping that was created successfully;
- execution of stale or partially written code, which usually presents as an impossible control-flow trace.

### Typical establishing evidence class

`specified_guarantee` for the host's reserve/commit model and beyond-EOF behavior, both of which are documented platform contracts and both of which are carried as facts in `VirtualMemoryModel` (`reserve_commit_model`, `file_map_beyond_eof`). Full detection of ordering violations requires the Phase 7 trace/state monitors and produces `observed_invariant` at best.

### Finding IDs

Primary: `RS-VM-0012` (reserve/commit semantic mismatch), `RS-VM-0018` (file-backed mapping extends beyond end of file).
Cross-listed: `RS-VM-0016`.

**Implementation gap:** `RS-VM-0018` is registered in `src/vm/finding.cpp` and the profile carries the `file_map_beyond_eof` fact, but no rule in `src/vm/analyzer.cpp` emits it. It is currently unreachable. The rule belongs to Phase 3 (a static size-vs-length comparison would catch the simple case) or Phase 7 (the general lifecycle case).

### Not an ordinary memory bug because...

No operation is invalid at the moment it is performed. The defect is where the *checkable failure point* sits: the program placed its error handling around a call that, on this host, cannot fail. Detecting that requires a model of the platform's state machine, which is precisely what ROADMAP Phase 7 is for.

---

## 9. Loader/ABI mismatch

### Definition

The process's own layout, calling convention, symbol resolution or translation mode contradicts an assumption the program makes about itself or about the code it loads. The environment here is not the kernel but the loader, the runtime linker and the process's ABI.

### Concrete examples

**Translated execution.** An x86-64 process running under a translation layer on an arm64 host. The *process* page size and address-space layout may follow the translated architecture, the *host* may enforce different protection policies, and a JIT that queries the architecture at compile time and the page size at run time can end up with an inconsistent pair. `PlatformInfo` carries `host_arch`, `process_arch` and `translation_mode` as three separate fields for exactly this reason.

**Address-space layout as an ABI.** A program hard-codes a base address chosen because it was free on the developer's machine. A loader change, a new dependency, a different allocator arena, or ASLR moves something into that range. Nothing about the program changed; the process's self-layout did.

**Plugin ABI drift.** A struct grows a field, both sides compile cleanly against their own headers, and the plugin reads past the caller's allocation. Well-formed in each translation unit, incorrect at the boundary.

### How it manifests

- failures that appear only in a translated or emulated process mode;
- failures that appear after an unrelated dependency upgrade changed the loader's placement decisions;
- a plugin that works when built in the same tree and fails when built separately.

### Typical establishing evidence class

`specified_guarantee` where an ABI document fixes the layout, `measured_capability` for translation mode and per-process address-space observations. Struct-layout and symbol-resolution facts are `statically_inferred` until a loader-domain probe exists.

### Finding IDs

**No dedicated finding ID exists in v0.1.** The virtual-memory domain touches this category only at its edge. The closest instances are `RS-VM-0013` (pointer truncation, which is an ABI property when it arises from `process_arch` rather than from a declared storage width) and `RS-VM-0003` (a range that fits one architecture's user address space and not another's).

The category is carried in the taxonomy because ROADMAP Phase 8 Candidate Domain B (Dynamic Loader and ABI) will populate it, and because ROADMAP Gate D requires ten real incidents before that domain opens. Recording loader/ABI incidents in the corpus now is how that gate gets satisfied later.

### Not an ordinary memory bug because...

Each translation unit is correct against the headers it was compiled with. The contradiction exists only in the combination, and only at run time. ASan can catch the *consequence* of a plugin ABI drift (a heap-buffer-overflow) but reports it as a memory error at the read site, with no indication that the cause is a version mismatch across a boundary.

---

## 10. Platform-observed behavior treated as guaranteed behavior

### Definition

The program depends on behavior that is *permitted but not promised*. It holds on the machines where the program was developed and tested, is not part of any contract, and can change with a kernel version, a security policy, a library update, an allocator change, or a different random ASLR draw.

This is the category that motivates the entire evidence model. It is also the category most likely to be dismissed in review, because the code demonstrably works.

### Concrete examples

**The hint that always worked.** `mmap` with a non-null address and no `MAP_FIXED` has returned the hinted address on every run for years, because the range happens to be free early in startup. Nothing forbids the kernel from returning something else. The program has no relocation path.

**Identity without a translation layer.** An emulator requires guest addresses to equal host addresses and has no translation layer. Identity currently holds by circumstance. `Analysis::rule_identity_without_translation()` emits `RS-VM-0008` with the conclusion *"The identity assumption currently holds but is not guaranteed by anything. It is a structural fragility rather than a present defect."*

**Unprobed ranges.** A profile contains no observation covering a requested range. Treating "we never tested it" as "it works" is the same error made by the tool instead of by the program. `EnvironmentProfile::query_range()` returns `UNKNOWN` with the reason *"no probe observation covers this range; absence of observation is not evidence of availability"*, and `RS-VM-0017` reports it.

### How it manifests

- the canonical "works on my machine", except the machine is an entire platform generation;
- a defect report that arrives with the next OS release, on hardware the team does not own, from a user whose configuration differs in a way nobody thought was load-bearing;
- an inability to reproduce, because reproducing requires recreating an address-space layout rather than an input.

### Typical establishing evidence class

`observed_invariant` when the belief comes from recorded traces — and this is the category where the ceiling rule bites hardest. `observed_invariant` ceilings a finding at `OBSERVED_INVARIANT`; it can never reach `PROVEN`, no matter how many runs agree. Where the evidence is a pattern rather than a trace set, `heuristic_risk` ceilings at `PREDICTIVE`. Where nothing was established at all, `unknown` ceilings at `HYPOTHESIS`.

### Finding IDs

Primary: `RS-VM-0008` (guest/host identity required without a translation layer), `RS-VM-0017` (availability of the requested range was never established).
Cross-listed: `RS-VM-0007`.

### Not an ordinary memory bug because...

There is, today, no bug at all. Every run is correct. The finding describes a *latent* portability failure: a legal environment behavior that the program does not survive. This is the class ROADMAP Phase 6 (counterfactual exploration) is built to enumerate — *"what other valid operating-system outcomes could have occurred?"* — and the reason `RS-VM-0007` emits `"it works on our machines, so ship it"` as a rejected fix with the reason *"absence of relocation in observed runs is not a guarantee about future runs."*

---

## Appendix A — Deciding between adjacent categories

| If you are torn between... | Ask |
| --- | --- |
| 1 (semantic success violation) and 2 (unsupported exact capability) | Did the call return success? Success → 1. Failure, or no legal success exists → 2. |
| 2 (unsupported) and 6 (property absent) | Does the operation succeed with a weaker result? Yes → 6. Nothing works at all → 2. |
| 3 (invalid fallback) and 1 (semantic success violation) | Is there a second, explicitly chosen code path? Yes → 3. One path that silently under-delivers → 1. |
| 4 (laundering) and 5 (permanent retried) | Is there a retry loop? Yes → 5. Any other misinterpretation of an error → 4. |
| 6 (property absent) and 7 (topology) | Is the missing thing an attribute of the result, or the geometry of the address space? Attribute → 6. Geometry → 7. |
| 8 (temporal) and 1 (semantic success violation) | Does the failure appear at a *later* operation than the one that degraded? Yes → 8. |
| 10 (observed-as-guaranteed) and anything | Is the program failing today on the profile under analysis? If it is passing and the concern is a permitted-but-unobserved outcome → 10. |

## Appendix B — Coverage status

| Category | Has primary finding ID | Emitted by v0.1 analyzer | Corpus entries |
| --- | --- | --- | --- |
| semantic success violation | yes | yes | RSC-0001 |
| unsupported exact capability | yes | yes | RSC-0008 |
| invalid fallback | yes | yes | RSC-0005 |
| error-code laundering | no (Phase 4) | no | none |
| permanent error treated as retryable | yes | yes | RSC-0007 |
| capability present but required property absent | yes | yes | RSC-0004 |
| resource-topology contradiction | yes | yes | RSC-0003 |
| temporal contract violation | yes | partially (`RS-VM-0018` unreachable) | RSC-0006 |
| loader/ABI mismatch | no (Phase 8) | no | none |
| platform-observed behavior treated as guaranteed behavior | yes | yes | RSC-0002 |

All corpus entries listed above are `provenance: pattern_reconstruction` and count toward none of the Phase 0 exit criteria. See `corpus/runtime_failures/README.md`.

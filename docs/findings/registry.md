# Finding ID Registry — Virtual Memory Domain

The 27 registered `RS-VM-*` finding IDs, their default severities, taxonomy categories and typical confidence, plus the policy that makes an ID permanent once published.

**Status:** ROADMAP Phase 0 deliverable ("initial finding-ID registry"), consumed by the Phase 3 analyzer. **Implemented and fully reachable.** All 25 IDs are declared in `include/runtimeskeptic/vm/finding.hpp` (`namespace rs::vm::ids`), defined in `src/vm/finding.cpp` (`registry_storage()`), and every one is emitted by a rule in `src/vm/analyzer.cpp`. `tools/guards/check_registry.py` fails CI if those three ever disagree — they did, for months, while this line said 18.

---

## 1. ID format

```text
RS-VM-0001
│  │  └── zero-padded sequence number, permanent
│  └───── domain: VM = virtual memory
└──────── project prefix
```

One domain is defined today. Future domains take their own prefixes (`RS-FS-*` filesystem, `RS-LD-*` loader/ABI, `RS-TM-*` time, `RS-NET-*` network) and their own independent number spaces, so a domain can be developed without coordinating numbers with any other.

Numbers are allocated in registration order, not in severity or category order. Do not infer anything from adjacency: `RS-VM-0009` and `RS-VM-0010` are related by accident of allocation, `RS-VM-0001` and `RS-VM-0016` are related by subject matter and are seven apart.

---

## 2. The registry

`Severity` is the **default** from `src/vm/finding.cpp`. The severity actually emitted is adjusted by the program's failure sink (`adjust_severity()` in `src/vm/analyzer.cpp`) — see section 3.2. `Confidence` is the level the emitting rule requests; the value that reaches the report is clamped by the evidence ceiling (`docs/evidence_model.md`).

| ID | Title | Default severity | Taxonomy category | Typical confidence | Support impact | Summary |
| --- | --- | --- | --- | --- | --- | --- |
| RS-VM-0001 | Exact virtual-memory mapping cannot be satisfied | `critical` | unsupported exact capability | `PROVEN` | `UNSUPPORTED` | The program requires a mapping at an exact address the host cannot provide, and no translation layer can repair the difference. |
| RS-VM-0002 | Requested address is below the lowest mappable address | `critical` | resource-topology contradiction | `PROVEN` | `UNSUPPORTED` | The host forbids mappings below a policy minimum (for example `vm.mmap_min_addr` on Linux). |
| RS-VM-0003 | Requested range extends past the end of the user address space | `critical` | resource-topology contradiction | `PROVEN` | `UNSUPPORTED` | The requested range does not fit inside the user-mode portion of this host's virtual address space. |
| RS-VM-0004 | Requested address does not satisfy host allocation granularity | `high` | capability present but required property absent | `PROVEN` | `UNSUPPORTED` | An exact mapping request must be aligned to the host allocation granularity; this one is not. |
| RS-VM-0005 | Requested size is not a multiple of the host allocation granularity | `medium` | capability present but required property absent | `PROVEN` | `CONDITIONALLY_SUPPORTED` | The host rounds the reservation up, so the mapping covers more address space than the program asked for. |
| RS-VM-0006 | Host page size differs from the required page size | `critical` | resource-topology contradiction | `PROVEN` | `UNSUPPORTED` | The program hard-codes a page size this host does not use; no mapping request can repair it. |
| RS-VM-0007 | Address hint may relocate while the caller assumes identity | `high` | semantic success violation | `COUNTEREXAMPLE` | `UNSUPPORTED` | The address is a hint, the host may return a different one, and the caller treats the returned address as if it equalled the request. |
| RS-VM-0008 | Guest/host address identity required without a translation layer | `high` | platform-observed behavior treated as guaranteed behavior | `HYPOTHESIS` | `CONDITIONALLY_SUPPORTED` | Identity holds by circumstance rather than by contract; any address-space change can break it. |
| RS-VM-0009 | Simultaneous write and execute permission is restricted | `critical` | capability present but required property absent | `PROVEN` | `UNSUPPORTED` | The host enforces write-xor-execute; the program requires one mapping that is writable and executable at once. |
| RS-VM-0010 | Anonymous executable mapping is not available | `critical` | unsupported exact capability | `PROVEN` | `UNSUPPORTED` | The host does not permit executable anonymous memory in this process configuration. |
| RS-VM-0011 | Executable memory requires a platform entitlement | `high` | capability present but required property absent | `PROVEN` | `CONDITIONALLY_SUPPORTED` | Executable memory is granted only to processes carrying an explicit entitlement or policy opt-in. |
| RS-VM-0012 | Reserve/commit semantics differ from the program's model | `medium` | temporal contract violation | `PROVEN` | `CONDITIONALLY_SUPPORTED` | The program models reservation and commitment as distinct states; this host provides no such distinction, so failures move to first touch. |
| RS-VM-0013 | Returned address does not fit the caller's pointer storage | `critical` | resource-topology contradiction | `PROVEN` | `UNSUPPORTED` | The address the host would return exceeds the width of the variable the program stores it in. |
| RS-VM-0014 | Permitted fallback contradicts a required postcondition | `high` | invalid fallback | `PROVEN` | `UNSUPPORTED` | The requirement permits a fallback that cannot satisfy one of its own postconditions. Host-independent. |
| RS-VM-0015 | Retry loop targets a permanently impossible operation | `high` | permanent error treated as retryable | `PROVEN` | `UNSUPPORTED` | The program retries after failure, but the failure is structural on this host: no number of attempts can succeed. |
| RS-VM-0016 | No non-destructive exact-mapping primitive is available | `high` | semantic success violation | `PROVEN` | `CONDITIONALLY_SUPPORTED` | The host has no way to request an exact address without either overwriting an existing mapping or silently relocating. |
| RS-VM-0017 | Availability of the requested range was never established | `info` | platform-observed behavior treated as guaranteed behavior | `HYPOTHESIS` | `UNKNOWN` | The profile contains no observation covering the requested range. Reported as `UNKNOWN` rather than as support. |
| RS-VM-0018 | File-backed mapping extends beyond end of file | `high` | temporal contract violation | *(not emitted)* | *(not emitted)* | Accessing the portion of a file mapping past end-of-file has host-specific behavior the program does not handle. |

### 2.1 Which rule emits which ID

| ID | Emitting rule in `src/vm/analyzer.cpp` | Deciding profile fact |
| --- | --- | --- |
| RS-VM-0001 | `rule_range_availability()`, `rule_exact_mapping_capability()` | `unavailable_ranges` / `available_ranges`; `exact_mapping` |
| RS-VM-0002 | `rule_range_availability()` | `min_map_address` |
| RS-VM-0003 | `rule_range_availability()` | `max_user_address` |
| RS-VM-0004 | `rule_address_alignment()` | `allocation_granularity` (or the requirement's own `required_alignment`) |
| RS-VM-0005 | `rule_size_granularity()` | `allocation_granularity` |
| RS-VM-0006 | `rule_page_size()` | `page_size` |
| RS-VM-0007 | `rule_hinted_relocation()` | `hinted_mapping_may_relocate` |
| RS-VM-0008 | `rule_identity_without_translation()` | none — requirement-only, structural |
| RS-VM-0009 | `rule_write_execute()` | `protection.write_execute_simultaneous`, `protection.write_then_execute_transition` |
| RS-VM-0010 | `rule_executable_mapping()` | `protection.anonymous_executable_mapping` |
| RS-VM-0011 | `rule_jit_entitlement()` | `protection.jit_entitlement_required` |
| RS-VM-0012 | `rule_reserve_commit()` | `reserve_commit_model` |
| RS-VM-0013 | `rule_pointer_truncation()` | `platform.process_arch` (or the requirement's `pointer_storage_width_bits`) |
| RS-VM-0014 | `rule_internal_fallback_contradiction()` | none — the profile is not consulted |
| RS-VM-0015 | `rule_retry_of_permanent_error()` | none — fires behind another `PROVEN` + `UNSUPPORTED` finding |
| RS-VM-0016 | `rule_non_destructive_exact_mapping()` | `fixed_noreplace_available` |
| RS-VM-0017 | `rule_range_availability()`, `rule_page_size()` | absence of any covering fact |
| RS-VM-0018 | *none* | `file_map_beyond_eof` (carried in the profile, never read by a rule) |

### 2.2 Findings that do not consult the profile

`RS-VM-0008`, `RS-VM-0014` and `RS-VM-0015` are decided without any host fact:

- `RS-VM-0014` is an internal contradiction in the requirement document and holds on every host. Its `host_capability` field reads *"not consulted: the contradiction is internal to the requirement and holds on every host."*
- `RS-VM-0015` fires only after another rule has already produced a `PROVEN` + `UNSUPPORTED` finding, so it inherits that rule's platform evidence.
- `RS-VM-0008` is a structural assessment and is suppressed when a stronger, address-specific finding already exists, so the report does not say the same thing twice.

---

## 3. Reading the severity and confidence columns

### 3.1 Severity and confidence are independent

Severity answers *how bad is this if it is real?* Confidence answers *how sure are we?* Neither constrains the other. `RS-VM-0008` is `high` severity and `HYPOTHESIS` confidence — a serious fragility that is not a present defect. `RS-VM-0005` is `medium` severity and `PROVEN` — certainly true, usually harmless.

Reports must display both. Sorting on one alone will mislead. The analyzer's own output order is `severity`, then `confidence`, then `id`, which is a determinism requirement rather than a claim about importance.

### 3.2 The default severity is a starting point

`adjust_severity()` raises or lowers the registry default according to the program's failure sink:

| `FailureSinkKind` | Effect |
| --- | --- |
| `fatal_assert`, `process_exit` | → `critical` |
| `unchecked` | → `critical` (silent misbehavior surfaces far from its cause) |
| `retry_loop` | → at least `high` |
| `error_return` | `critical` is relaxed to `high` |
| `none`, `unknown` | unchanged |

`info` is never raised, so `RS-VM-0017` stays informational whatever the caller does with the result.

### 3.3 The typical confidence is what the rule asks for

Each rule states a desired confidence and `Analysis::emit()` clamps it to the ceiling implied by the weakest fact in that finding's own evidence chain. A `RS-VM-0006` derived from a profile whose `page_size` carries `observed_invariant` is emitted as `OBSERVED_INVARIANT`, not `PROVEN`. The table's confidence column therefore describes the common case, never a guarantee.

### 3.4 Support impact is not severity

`support_impact` is the finding's contribution to the overall verdict, folded in with `combine()` under the order `UNSUPPORTED > UNKNOWN > CONDITIONALLY_SUPPORTED > SUPPORTED`. A `medium` finding with impact `UNSUPPORTED` drags the whole verdict down; a `critical` finding with impact `CONDITIONALLY_SUPPORTED` does not.

---

## 4. Known registry defects

Stated here rather than left for a reader to trip over.

<!-- checked: 2026-07-25 -->
**`RS-VM-0018` was unreachable and is not any more.** <!-- checked: 2026-07-25 --> The ID was registered while no rule read `file_map_beyond_eof` and `file_backed` together. `rule_file_mapping_beyond_eof()` now does, and the schema gained the `file_length` field the rule needed plus `eof_access_extent`, which distinguishes a read inside the final partial page (POSIX guarantees zero-fill; always safe) from a read a whole page past the end (implementation-defined). The second distinction was found by a ground-truth case contradicting the analyzer, and the analyzer was right the first time and wrong the second.

**`RS-VM-0017` is used for two different unknowns, with an overridden title.** `rule_range_availability()` emits it with its registered title. `rule_page_size()` also emits it for an unknown page size and then overwrites `Finding::title` with `"Host page size was never established"`. The emitted title therefore does not match the registry entry for that ID, which breaks the assumption that an ID determines a title. Two possible resolutions, neither applied: allocate a distinct ID per unknown-fact kind, or make `RS-VM-0017` a generic "required fact not established" finding whose registered title is neutral and whose specific fact is named in the `host_capability` field. The second is preferred, because per-fact IDs would proliferate with every new profile field.

**Two taxonomy categories have no primary ID.** *error-code laundering* needs the Phase 4 runtime wrapper, because detecting it requires observing the native error before a layer transforms it and the reported error after. *loader/ABI mismatch* needs the Phase 8 loader domain. Both are documented in `docs/failure_taxonomy.md`; neither is an oversight in this registry.

**ROADMAP section 6 error classes with no ID at all:** mapping overlap, shared-memory semantic mismatch, virtual-address fragmentation risk, and mapping lifetime inconsistencies. Fragmentation is inherently `PREDICTIVE` and belongs to Phase 9; overlap and lifetime are temporal and belong to Phase 7. When they are implemented they take new numbers; none of `0001`–`0018` will be repurposed.

---

## 5. Relationship to the ROADMAP's illustrative ID

ROADMAP section 10.7 shows an example report headed `RS-VM-0041`, titled "Exact virtual-memory mapping cannot be satisfied", with confidence `PROVEN_ENVIRONMENT_CONTRADICTION`.

That ID is **illustrative only**. It was chosen to make the example look like a real registry entry before a registry existed. The real registry starts at `0001`, and the finding described in that example is:

| ROADMAP illustration | Implemented |
| --- | --- |
| `RS-VM-0041` | **`RS-VM-0001`** |
| `PROVEN_ENVIRONMENT_CONTRADICTION` | **`PROVEN`** |

The mapping is recorded in `include/runtimeskeptic/vm/finding.hpp`:

```cpp
// IDs are permanent once published. See docs/findings/registry.md.
// The ROADMAP's illustrative example uses "RS-VM-0041"; the real registry
// starts at 0001 and the exact-address contradiction is RS-VM-0001.
```

`RS-VM-0041` is **not** reserved, tombstoned or blocked. It was never published as a finding ID, so it carries no history. It will be allocated normally when the registry reaches 41.

The long-form confidence label survives only as prose. `Finding::conclusion` for `RS-VM-0001` reads *"No execution satisfying all constraints exists on this host profile..."*, which is the sentence the ROADMAP's `PROVEN_ENVIRONMENT_CONTRADICTION` was conveying. The machine-readable label is `PROVEN`, one of exactly five values in `rs::Confidence`.

---

## 6. ID stability policy

Finding IDs appear in CI configurations, suppression files, ticket titles, dashboards and other people's documentation. They are an interface.

### 6.1 Permanence

**An ID is permanent from the moment it is published.** "Published" means: present in a tagged release, or present on a default branch that external users build from. Once published, the ID's *meaning* — the class of contradiction it names — is frozen.

### 6.2 Never reused

**A retired ID's number is never reassigned.** Not for a similar finding, not for a rewritten version of the same finding, not after any interval. A user who suppressed `RS-VM-0009` in 2026 must never discover in 2029 that the suppression now hides something unrelated. The number space is unbounded; reuse buys nothing and costs correctness.

### 6.3 What may change without a new ID

| Change | Allowed | Note |
| --- | --- | --- |
| Wording of the title | yes | keep the meaning |
| Wording of the summary, conclusion, remediation prose | yes | |
| Default severity | yes | announce in release notes; it can flip CI gates |
| Which rule emits it | yes | implementation detail |
| Adding a remediation class or a rejected fix | yes | |
| Broadening the trigger condition within the same class | yes | more instances of the same contradiction |
| Narrowing the trigger to remove false positives | yes | fewer instances of the same contradiction |
| **Changing which contradiction it names** | **no** | allocate a new ID and tombstone the old one |
| **Changing the taxonomy category** | **no** | implies the meaning changed |

The test: would a user who suppressed this ID last year still want the same thing suppressed? If yes, keep the ID.

### 6.4 Deprecation and tombstoning

An ID that is withdrawn is **tombstoned**, never deleted:

1. It stays in the registry with `status: deprecated`.
2. It records the release in which it was deprecated, the reason, and the replacement ID if there is one.
3. It is never emitted again.
4. Its number is never reallocated.
5. Tooling continues to accept it in suppression files and configuration, ignoring it with a warning rather than erroring — so a deprecation cannot break a downstream build.

Tombstone record format:

```text
RS-VM-00NN   Title as last published
             status:       deprecated
             deprecated:   v0.4.0
             reason:       merged into RS-VM-00MM; the distinction was not
                           actionable and split one contradiction across two IDs
             replaced_by:  RS-VM-00MM
```

No IDs are deprecated today. `FindingDefinition` in `include/runtimeskeptic/vm/finding.hpp` has no `status` field yet; adding one is a prerequisite for the first deprecation and is not needed before it.

### 6.5 Allocating a new ID

1. Take the next unused number in the domain. Do not fill gaps — there are none, and if a tombstone ever creates one, it stays.
2. Add the constant to `namespace rs::vm::ids` in `include/runtimeskeptic/vm/finding.hpp`.
3. Add the `FindingDefinition` — id, title, default severity, summary — to `registry_storage()` in `src/vm/finding.cpp`.
4. Add a row to the table in section 2 of this file, including the taxonomy category. An ID without a category is not registered.
5. Add the emitting rule and its row in section 2.1. If the rule is deferred to a later phase, say so in section 4 rather than leaving the ID silently unreachable.
6. Add or reference a corpus entry that motivates it (`corpus/runtime_failures/`).

### 6.6 Versioning

The registry is versioned with the tool, not independently. `AnalysisResult` carries `schema: runtime-skeptic.compatibility-result.v1`; a change to the *shape* of a finding is a schema version bump, while adding or deprecating an ID is not.

Consumers should treat an unrecognized ID as a finding they do not have local knowledge of — reporting it with its severity and confidence — rather than as an error. The `severity`, `confidence` and `support_impact` fields are self-describing precisely so a consumer can act on a finding it has never seen.

## Added after the July 2026 real-world campaign

These seven ids did not exist when the registry was first written. Each was
added because running real projects through the analyzer showed something it
could not say - and in five of the seven cases, something it was saying
*wrongly*.

| ID | Title | Severity | Why it exists |
|---|---|---|---|
| `RS-VM-0019` | Anonymous memory mapping is unavailable on this host | critical | A profile that established nothing produced `SUPPORTED`, exit 0. Absence of objections was being reported as support, so "can this host map memory at all" became an explicit fact that must be positively established. |
| `RS-VM-0020` | Address hint points into a range the host cannot provide | low | A hint aimed at an unavailable range does not fail the request - the mapping lands elsewhere - but the hint is silently useless and downstream code sometimes assumes otherwise. |
| `RS-VM-0021` | Requested size does not fit in the usable address space | critical | QEMU's aarch64 mode reserves 4 PiB. Every placement rule began `if (!request.address) return;` and nothing compared the size to anything, so 4 PiB and 4 KiB were indistinguishable. |
| `RS-VM-0022` | Reservation alignment exceeds what the mapping API guarantees | high | `required_alignment` was only read when an address was *also* given, so "4 GiB aligned to 4 GiB, anywhere" - V8's cage, mimalloc's segments - fell straight through. Deleting the field changed nothing, which is the definition of dead code. |
| `RS-VM-0023` | The host cannot place the mapping inside the program's address bound | critical | LuaJIT below 2^31, Box64 box32 below 2^32, Box64 dynarec above 2^32. With nowhere to express a bound, authors reached for identity requirements and the analyzer reported a contradiction it had manufactured. |
| `RS-VM-0024` | A relative-displacement constraint was carried but not evaluated | info | Every JIT with a rel32 branch needs "within ±N bytes of another region". v0.1 cannot decide it. Saying so turns an unnoticed question into an unanswered one. |
| `RS-VM-0025` | The program can use only a small part of this host's address space | low | Removing the (incorrect) truncation story from LuaJIT also removed the only warning that it depends on winning an address-space lottery. `PREDICTIVE` per ROADMAP section 11. |
| `RS-VM-0026` | Requested reservation is larger than any this host granted | critical | A 5-level-paging CI runner refused QEMU's 4 PiB reservation with `ENOMEM` while the analyzer said SUPPORTED, because `RS-VM-0021` compared the size against the width of the address space and nothing else. Fitting is necessary, not sufficient. `PROVEN` when the size exceeds a power of two the host was measured to refuse; `HYPOTHESIS` in the untested band between the largest success and the smallest failure. |
| `RS-VM-0027` | Whether a reservation of this size is grantable was never established | medium | The other half of the same defect: with `max_single_reservation` absent from a profile, a request larger than anything this project has observed a real program make (1.96 GiB across 1292 observations) has no basis for a positive answer, and silence used to read as yes. |

### Two ids whose meaning changed

`RS-VM-0017` was "Availability of the requested range was never established"
and rendered a different title depending on which rule emitted it, so the id
no longer determined the title. It is now "A platform fact this request
depends on was never established" - one id for the whole "we never looked"
class, so CI can filter it with a single rule.

`RS-VM-0015` was unreachable for statically inferred requirements. It asked
"did anything prove impossibility?" by testing `confidence == Proven`, but
confidence is clamped by evidence and a statically inferred requirement can
never reach `PROVEN` - which is the entire class of input a static extractor
produces. Findings now carry `structural_impossibility` separately, because
"no execution can succeed" is a property of the reasoning and `PROVEN` is a
property of the evidence.

### Findings that report a satisfied constraint

None. Satisfied constraints are not findings; they are recorded separately in
`AnalysisResult::satisfied` and rendered under "Checked and satisfied". The
campaign found that a contract written specifically to catch the
4 KiB-versus-16 KiB problem produced *no output at all* on a 4 KiB host, which
made "checked and fine" indistinguishable from "never examined".

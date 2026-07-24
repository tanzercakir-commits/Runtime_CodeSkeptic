# RuntimeSkeptic Roadmap

> **Working title:** RuntimeSkeptic  
> **Project category:** Cross-layer runtime failure prediction and semantic compatibility analysis  
> **Status:** Concept and architecture roadmap  
> **Initial focus:** Virtual memory compatibility for C/C++ systems  
> **Long-term direction:** Program–runtime–library–operating system assumption analysis

---

## 1. Purpose

RuntimeSkeptic aims to detect runtime failures by proving or estimating that a software assumption cannot be satisfied by the target execution environment.

The project is not intended to be another crash reporter, sanitizer, debugger, or log analysis tool.

Its core question is:

> **Does the environment in which the program will execute provide the exact semantics that the program assumes?**

The system will examine assumptions spanning multiple layers:

- application code
- libraries
- language runtime
- emulator or virtual machine
- system call interface
- operating system
- kernel behavior
- processor architecture
- virtual address-space layout
- filesystem, loader, time, networking, and other runtime facilities

The initial goal is to find contradictions before a full application run or before the contradiction produces an opaque crash.

---

## 2. Problem Statement

Many runtime failures are not caused by a single incorrect instruction.

They emerge because different layers implement different contracts.

A typical failure chain is:

```text
Application requires semantic guarantee X.

Compatibility layer approximates X with behavior Y.

Operating system permits Y but cannot provide X.

Compatibility layer reports success.

Application assumes X was satisfied.

Application later crashes, corrupts state, retries forever, or produces a wrong result.
```

The critical failure is often not the original operating-system error.

It is the loss, weakening, reinterpretation, or laundering of semantics between layers.

RuntimeSkeptic will treat this as a first-class error family:

## Silent Contract Degradation

```text
A lower layer cannot provide the requested guarantee,
performs a weaker operation,
and continues as if the original guarantee had been satisfied.
```

Examples include:

- an exact virtual-memory mapping silently becoming a relocated mapping
- a durable write being reduced to a page-cache write
- an atomic operation becoming a partial update
- a requested isolation level becoming a weaker isolation level
- a secure capability silently falling back to a weaker implementation
- a retryable error being produced from a permanently impossible operation
- a hardware-backed operation becoming a software fallback while upper layers still assume hardware semantics

---

## 3. Motivating Example

A game requests a direct-memory mapping at a fixed guest address:

```text
requested_address = 0x1000000000
```

The application requires:

```text
mapping_success => returned_address == requested_address
```

The host operating system cannot grant that virtual-address range.

The emulator lacks a guest-to-host address translation layer and therefore depends on identity mapping.

Instead of failing explicitly, the compatibility layer permits the mapping to land elsewhere.

The game later evaluates:

```text
returned_address == requested_address
```

The assertion fails and the process terminates.

RuntimeSkeptic should represent this as:

```text
Application requirement:
    exact guest/host address identity

Host capability:
    requested range unavailable

Adapter capability:
    no address translation

Adapter behavior:
    relocation may be reported as success

Caller behavior:
    relocation causes fatal assertion
```

Result:

```text
PROVEN ENVIRONMENT CONTRADICTION

No execution satisfying all constraints exists on the selected host profile.
```

This conclusion can be produced without rendering a frame and potentially before the complete application starts.

---

## 4. Product Thesis

RuntimeSkeptic will combine four forms of reasoning:

1. **Static assumption extraction**
2. **Executable environment profiling**
3. **Runtime semantic monitoring**
4. **Constraint and state-based failure analysis**

The central technical thesis is:

> Runtime behavior becomes more predictable when environment-dependent assumptions are represented explicitly and checked against measured or specified platform capabilities.

The project will not attempt to model every possible machine or operating-system behavior.

It will begin with narrowly bounded runtime domains whose operations, constraints, and failure modes can be represented with high confidence.

---

## 5. Initial Scope

The first supported domain will be:

# Virtual Memory Compatibility Analysis

Target software categories:

- emulators
- virtual machines
- JIT compilers
- databases
- game engines
- language runtimes
- memory allocators
- sandboxing systems
- high-performance native applications

Initial platforms:

- Windows x64
- Linux x86-64
- macOS on Apple Silicon
- x86-64 processes under Rosetta where measurable

Initial APIs:

### POSIX and Unix-like systems

- `mmap`
- `munmap`
- `mprotect`
- `madvise`
- shared-memory mappings
- file-backed mappings

### Windows

- `VirtualAlloc`
- `VirtualAlloc2`
- `VirtualFree`
- `VirtualProtect`
- section-backed mappings
- reserve/commit operations

### macOS-specific investigation

- `mach_vm_allocate`
- `mach_vm_map`
- `mach_vm_protect`
- JIT-related memory restrictions
- executable mapping behavior

---

## 6. Initial Error Classes

The first release family should detect or model:

- impossible exact-address mappings
- address hints that may be relocated
- reserved virtual-address collisions
- guest/host pointer identity assumptions
- page-size mismatch
- allocation-granularity mismatch
- alignment incompatibility
- mapping overlap
- invalid protection transitions
- write-xor-execute restrictions
- JIT entitlement or policy requirements
- executable-memory incompatibility
- reserve/commit semantic mismatch
- file mapping beyond end-of-file
- shared-memory semantic mismatch
- address truncation
- pointer-width incompatibility
- virtual-address fragmentation risk
- mapping lifetime inconsistencies
- silent fallback after failed fixed mapping
- wrong treatment of permanent errors as retryable errors
- success codes that do not satisfy caller postconditions

---

## 7. Non-Goals

The initial project will not attempt to:

- predict every possible runtime crash
- replace AddressSanitizer, ThreadSanitizer, Valgrind, debuggers, or profilers
- infer full kernel behavior from documentation alone
- model arbitrary device drivers
- support all operating-system versions
- provide probabilistic AI explanations without evidence
- treat observed behavior as a formal guarantee
- claim proof when only a heuristic or historical pattern exists
- emulate an operating system
- build a general-purpose symbolic execution engine from scratch
- solve arbitrary distributed-system correctness in the first releases

A narrow, evidence-driven tool with low false-positive rates is more valuable than a broad but unreliable “AI runtime analyzer.”

---

## 8. Core Design Principles

### 8.1 Semantic success matters more than return-code success

A call returning success does not prove that the requested guarantee was delivered.

```text
API success
!=
caller postcondition satisfied
```

RuntimeSkeptic must compare actual outcomes with caller-required postconditions.

### 8.2 Measured behavior and guaranteed behavior are different

Every fact in the system must include an evidence class:

- specified guarantee
- measured capability
- statically inferred assumption
- observed invariant
- bounded counterexample
- heuristic risk

### 8.3 Proof levels must never be mixed

A proven contradiction must not be presented in the same way as a statistical warning.

### 8.4 The environment is an input

Operating-system version, architecture, process mode, page size, security policy, loader state, and runtime configuration are part of program input.

### 8.5 Runtime behavior should be replayable

Environment probes, semantic events, constraints, and diagnoses should be serializable and reproducible.

### 8.6 Every diagnosis needs an evidence path

A useful report must answer:

- What was required?
- Which layer required it?
- What did the environment provide?
- Which layer weakened or reinterpreted the behavior?
- Why is the outcome impossible, possible, or risky?
- Which remediation classes remain architecturally valid?

### 8.7 AI may assist explanation, not establish truth

Machine learning or language models may:

- cluster incidents
- summarize traces
- suggest candidate contracts
- rank likely causes
- generate human-readable explanations

They must not replace deterministic evidence, solver output, or measured platform capability.

---

## 9. System Architecture

```text
                        ┌────────────────────────────┐
                        │ Static Program Analyzer    │
                        │ CodeSkeptic integration    │
                        └──────────────┬─────────────┘
                                       │
                                       │ extracted assumptions
                                       ▼
┌─────────────────────┐     ┌────────────────────────────┐
│ Environment Probes  │────▶│ Runtime Semantic IR        │
│ Measured profiles   │     │                            │
└─────────────────────┘     │ resources                  │
                            │ operations                 │
┌─────────────────────┐     │ effects                    │
│ API/OS Specifications│───▶│ postconditions             │
│ Curated contracts   │     │ failure modes              │
└─────────────────────┘     │ temporal transitions       │
                            │ evidence classifications    │
┌─────────────────────┐     └──────────────┬─────────────┘
│ Runtime Wrappers    │────────────────────▶│
│ Instrumentation     │                     │
└─────────────────────┘                     ▼
                               ┌──────────────────────────┐
                               │ Constraint and State     │
                               │ Analysis Engine          │
                               └─────────────┬────────────┘
                                             │
                                             ▼
                               ┌──────────────────────────┐
                               │ Evidence and Diagnosis   │
                               │ Reports                  │
                               └──────────────────────────┘
```

---

## 10. Major Components

## 10.1 Environment Probe Suite

Working command:

```text
rs-env-probe
```

Responsibilities:

- execute controlled platform experiments
- record successful and failed operations
- identify page size and allocation granularity
- test exact and hinted mappings
- identify unavailable address ranges
- characterize protection transitions
- test executable mapping behavior
- record process architecture and translation mode
- emit a deterministic capability manifest
- attach probe version and evidence hashes

Example output:

```yaml
schema: runtime-skeptic.environment-profile.v1

platform:
  os: macos
  os_version: "..."
  host_arch: arm64
  process_arch: x86_64
  translation_mode: rosetta2

virtual_memory:
  page_size: 16384

  exact_mapping:
    supported: conditional
    failure_codes:
      - ENOMEM
      - EINVAL

  unavailable_ranges:
    - start: 0x1000000000
      end: 0x6fffffffff
      evidence: measured

  hinted_mapping:
    may_relocate: true
```

The probe must never confuse:

```text
not observed
```

with:

```text
impossible
```

---

## 10.2 Runtime Semantic IR

A platform-independent intermediate representation will describe:

- operations
- resources
- preconditions
- postconditions
- side effects
- failure modes
- alternative outcomes
- temporal states
- caller assumptions
- platform capabilities
- evidence class
- source locations
- trace identifiers

Example operation:

```yaml
operation:
  kind: virtual_memory_map

request:
  address: 0x1000000000
  size: 16384
  exact: true
  executable: false

required_postconditions:
  - returned_address == requested_address

permitted_failures:
  - ENOMEM
  - EINVAL
```

The IR should be deterministic, versioned, serializable, and suitable for:

- static analysis
- runtime event recording
- solver input
- report generation
- replay
- differential comparison

---

## 10.3 Runtime Contract Language

A small declarative language should represent semantic requirements.

Initial example:

```text
contract ExactMapping {
    when operation.kind == virtual_memory_map
         and operation.exact == true
         and result.status == success;

    require result.address == request.address;

    otherwise emit RS_VM_EXACT_ADDRESS_VIOLATION;
}
```

Later temporal form:

```text
property JitMemoryLifecycle {
    Allocated
        -> Writable
        -> CodeWritten
        -> CacheFlushed
        -> Executable;

    forbid Writable -> Executable
        unless cache_flush_completed;
}
```

The language must remain narrower than a general-purpose programming language.

Its goals are:

- deterministic evaluation
- understandable diagnostics
- stable serialization
- versioned semantics
- compatibility with RuleDSL concepts where appropriate
- support for stateful properties without forcing them into a purely stateless rule model

---

## 10.4 Runtime Wrapper and Instrumentation Library

Working library name:

```text
libruntimeskeptic
```

Initial responsibilities:

- wrap supported virtual-memory APIs
- record semantic inputs and outputs
- validate immediate postconditions
- preserve original error codes
- identify fallback behavior
- emit deterministic semantic events
- add minimal runtime overhead when monitoring is enabled
- permit compilation without monitoring for production builds

Possible interfaces:

```c
void* rs_mmap(...);
int rs_mprotect(...);
void* rs_virtual_alloc(...);
```

Instrumentation strategies:

1. explicit wrapper calls
2. compile-time macro substitution
3. source rewriting
4. linker interposition where supported
5. dynamic instrumentation in later phases

The first release should prefer explicit wrappers and compile-time instrumentation because they are easier to reason about and test.

---

## 10.5 Static Assumption Extractor

This component should build on CodeSkeptic rather than duplicate it.

Initial extraction targets:

- assertions after runtime calls
- comparisons between requested and returned addresses
- unchecked semantic outcomes
- error-code handling
- retry loops
- fallback paths
- pointer truncation
- page-size assumptions
- alignment assumptions
- reserve/commit assumptions
- hard-coded virtual addresses
- fatal sinks
- semantic use of returned values

Example source:

```cpp
void* result = mmap(requested, size, prot, flags, fd, offset);

if (result == MAP_FAILED)
    return error;

assert(result == requested);
```

Expected summary:

```yaml
runtime_call:
  kind: mmap

handled_failures:
  - MAP_FAILED

required_success_postconditions:
  - result == requested

failure_sink:
  kind: fatal_assert

unhandled_semantic_outcomes:
  - relocated_success
```

The first implementation may accept manually supplied contracts before full automatic extraction is available.

---

## 10.6 Constraint and State Analysis Engine

The analysis engine will combine:

- caller-required postconditions
- adapter behavior
- platform capabilities
- runtime trace events
- API specifications
- temporal constraints

Initial solver questions:

```text
Can the host satisfy all required postconditions?
```

```text
Is there any legal runtime outcome that reaches a fatal sink?
```

```text
Is the observed success weaker than the required success?
```

```text
Does a fallback preserve all required semantics?
```

```text
Is a retry loop attempting a permanently impossible operation?
```

The first implementation should use bounded, explainable reasoning.

It should avoid immediately building a general symbolic executor.

---

## 10.7 Evidence and Diagnosis Engine

Every finding must include:

- finding ID
- domain
- severity
- confidence class
- required semantic guarantee
- actual or possible environment behavior
- contradiction or counterexample
- source locations
- environment-profile reference
- trace reference where applicable
- remediation classes
- rejected superficial fixes where provable

Example:

```text
RS-VM-0041

Title:
Exact virtual-memory mapping cannot be satisfied

Confidence:
PROVEN_ENVIRONMENT_CONTRADICTION

Required:
returned_address == requested_address

Requested address:
0x1000000000

Host capability:
requested range unavailable

Adapter capability:
guest-to-host translation unavailable

Observed or modeled fallback:
relocated mapping reported as success

Failure sink:
fatal assertion

Conclusion:
No valid execution satisfies all constraints on the selected profile.
```

---

## 11. Confidence Model

RuntimeSkeptic findings will use explicit confidence levels.

## Level 1 — Proven Environment Contradiction

```text
program requires X
environment guarantees or demonstrates not-X
```

The constraints are unsatisfiable.

Label:

```text
PROVEN
```

## Level 2 — Reachable Counterexample

A legal platform outcome or event sequence reaches a failure sink.

Label:

```text
COUNTEREXAMPLE
```

## Level 3 — Observed Invariant Violation

Historical traces consistently exhibited an invariant that has now changed.

Label:

```text
OBSERVED_INVARIANT
```

This is evidence, not proof.

## Level 4 — Predictive Resource Risk

A trend suggests a likely future failure:

- virtual-address fragmentation
- descriptor exhaustion
- queue growth
- memory pressure
- thread accumulation
- latency-tail expansion

Label:

```text
PREDICTIVE
```

## Level 5 — Hypothesis

A plausible cross-layer explanation has incomplete evidence.

Label:

```text
HYPOTHESIS
```

Hypotheses must not fail CI by default.

---

## 12. Relationship with Existing Projects

## 12.1 CodeSkeptic

CodeSkeptic should remain responsible for:

- static C/C++ semantic analysis
- dataflow
- contract extraction
- error-path analysis
- assumption extraction
- fatal-sink identification
- semantic API comparison

RuntimeSkeptic should consume CodeSkeptic summaries.

```text
CodeSkeptic:
    What does the program assume?

RuntimeSkeptic:
    Can the selected environment satisfy that assumption?
```

## 12.2 RuleDSL

RuleDSL may be used for:

- deterministic policy evaluation
- classification rules
- diagnostic routing
- severity policies
- environment compatibility policies
- stable decision replay

RuleDSL should not be forced to represent all temporal runtime behavior.

A separate trace or state-property language may be necessary.

## 12.3 Future State/Trace DSL

A specialized language may define:

- valid runtime state transitions
- event order
- resource lifecycle
- temporal requirements
- forbidden transitions
- timeout rules
- eventual conditions

This language should share infrastructure with RuleDSL where practical:

- lexer and parser conventions
- versioning
- deterministic bytecode
- diagnostics
- replay
- conformance tests

---

# 13. Development Roadmap

---

## Phase 0 — Research Boundary and Failure Taxonomy

### Objective

Define a sufficiently narrow problem that can produce high-confidence findings.

### Work

- collect 30–50 real cross-layer runtime failures
- classify failures by domain
- identify repeated semantic patterns
- distinguish conventional memory bugs from environment contradictions
- define “silent contract degradation” formally
- define evidence and confidence terminology
- identify initial platform versions
- identify APIs suitable for deterministic probing
- document unsupported claims

### Initial taxonomy

- semantic success violation
- unsupported exact capability
- invalid fallback
- error-code laundering
- permanent error treated as retryable
- capability present but required property absent
- resource-topology contradiction
- temporal contract violation
- loader/ABI mismatch
- platform-observed behavior treated as guaranteed behavior

### Deliverables

- `docs/problem_statement.md`
- `docs/failure_taxonomy.md`
- `docs/evidence_model.md`
- `docs/non_goals.md`
- `corpus/runtime_failures/`
- initial finding-ID registry

### Exit Criteria

- at least 30 classified real incidents
- at least 10 incidents in the virtual-memory category
- each category has a concrete evidence model
- initial terminology is stable enough for public documentation
- no claim depends solely on AI-generated interpretation

---

## Phase 1 — Virtual Memory Environment Probe

### Objective

Build a deterministic probe that characterizes virtual-memory behavior on supported hosts.

### Work

- detect OS, version, host architecture, and process architecture
- detect page size and allocation granularity
- test fixed-address mappings
- test hinted mappings
- test mappings across selected address ranges
- test protection transitions
- test executable mappings
- test reserve/commit semantics
- test mapping overlap
- test file-backed mappings
- preserve exact failure codes
- serialize all observations
- hash probe binary and result manifest
- make probe runs replayable and comparable

### Deliverables

- `rs-env-probe`
- environment profile schema
- Windows x64 profile fixtures
- Linux x86-64 profile fixtures
- macOS Apple Silicon profile fixtures
- Rosetta profile fixtures where available
- probe conformance tests
- profile-diff command

### Suggested CLI

```text
rs-env-probe vm --output host-profile.json
rs-profile verify host-profile.json
rs-profile diff profile-a.json profile-b.json
```

### Exit Criteria

- repeated runs on the same stable host produce equivalent canonical profiles
- failures preserve native error information
- probe cannot silently classify an untested behavior as supported
- profile schema is versioned
- profile output passes deterministic canonicalization tests
- at least three platform families are represented

---

## Phase 2 — Runtime Semantic IR and Contract Evaluator

### Objective

Represent memory operations, environment capabilities, and caller requirements in a common deterministic model.

### Work

- define semantic operation schema
- define resource and address-range types
- define evidence classes
- define preconditions and postconditions
- define permitted alternative outcomes
- define failure-sink representation
- define compatibility-result format
- implement deterministic contract evaluation
- implement explanation traces
- create manually authored example cases

### Deliverables

- Runtime Semantic IR v0.1
- JSON schema
- canonical serializer
- contract evaluator
- compatibility-result schema
- sample exact-mapping case
- sample page-size mismatch case
- sample W^X conflict case

### Suggested CLI

```text
rs-check request.json --profile host-profile.json
```

### Exit Criteria

The engine must classify a request as:

```text
SUPPORTED
UNSUPPORTED
CONDITIONALLY_SUPPORTED
UNKNOWN
```

Every non-supported result must include a machine-readable reason.

---

## Phase 3 — Virtual Memory Compatibility Analyzer MVP

### Objective

Produce useful pre-execution findings for real virtual-memory requirements.

### Work

- accept manually authored application requirements
- compare requirements with environment profiles
- detect exact-address contradictions
- detect page-size and alignment contradictions
- detect protection-policy contradictions
- detect reserve/commit mismatches
- detect pointer-width issues
- detect invalid fallback contracts
- provide remediation classes
- generate terminal, JSON, and Markdown reports

### Deliverables

- `rs-vm-check`
- finding registry for VM domain
- sample emulator integration
- sample JIT integration
- sample database integration
- human-readable evidence reports
- CI-friendly exit codes

### Example Input

```json
{
  "operation": "virtual_memory_map",
  "requested_address": "0x1000000000",
  "size": 16384,
  "exact_address_required": true,
  "guest_host_identity_required": true,
  "translation_layer_available": false
}
```

### Exit Criteria

- motivating exact-mapping incident is diagnosed correctly
- no unsupported platform fact is described as guaranteed
- findings are reproducible from the same requirement and profile
- each finding contains an evidence chain
- expected false-positive rate is low on curated examples
- analysis can run in CI without launching the complete target application

### First Public Milestone

```text
RuntimeSkeptic v0.1
Virtual Memory Compatibility Analyzer
```

---

## Phase 4 — Runtime Wrapper Library

### Objective

Observe actual runtime outcomes and detect semantic success violations.

### Work

- implement wrappers for selected APIs
- preserve calling semantics and error codes
- emit semantic events
- record requested versus returned addresses
- identify relocation after exact requests
- record protection transitions
- record mapping lifecycle
- add configurable assertion or report behavior
- support trace replay
- minimize recursion and allocator dependence inside wrappers

### Deliverables

- `libruntimeskeptic`
- C API
- event schema
- trace writer
- trace reader
- replay tool
- sample integrations
- overhead benchmark

### Exit Criteria

- wrapper behavior matches native calls in conformance tests
- monitoring does not alter error codes
- trace order is deterministic where the program execution is deterministic
- semantic violations are detectable at the call boundary
- monitoring can be disabled at build or runtime
- overhead is measured and documented

### Public Milestone

```text
RuntimeSkeptic v0.2
Virtual Memory Runtime Monitor
```

---

## Phase 5 — CodeSkeptic Static Integration

### Objective

Automatically extract caller assumptions and connect them to runtime operations.

### Work

- identify runtime API calls
- identify returned-value comparisons
- extract assertions
- identify unchecked semantic outcomes
- classify fatal and non-fatal sinks
- track transformed error codes
- detect retry loops
- detect hard-coded address assumptions
- export runtime-assumption summaries
- map source locations into Runtime Semantic IR

### Deliverables

Possible CodeSkeptic commands:

```text
codeskeptic --runtime-assumptions
codeskeptic --runtime-contract-export
codeskeptic --runtime-call-summary
```

Generated artifacts:

- assumption manifest
- runtime call graph
- error transformation graph
- fatal-sink map
- source-to-runtime evidence links

### Exit Criteria

- motivating pattern is extracted without manual annotations
- assertion-derived postconditions are represented correctly
- result distinguishes API failure handling from semantic-outcome handling
- source locations survive through final reports
- unsupported inference is labeled as uncertain
- CodeSkeptic and RuntimeSkeptic schemas are version-compatible

### Public Milestone

```text
RuntimeSkeptic v0.3
Static Assumption Extraction
```

---

## Phase 6 — Counterfactual Environment Exploration

### Objective

Find failures that did not occur in the observed run but are legal under the target environment.

### Core Question

```text
What other valid operating-system outcomes could have occurred?
```

### Work

- model sets of legal runtime outcomes
- vary syscall results within platform constraints
- explore fallback branches
- generate bounded event sequences
- connect alternative outcomes to failure sinks
- produce minimal counterexamples
- prioritize outcomes by realism and severity
- distinguish guaranteed behavior from permitted behavior

### Example

Observed:

```text
mmap returned requested address
```

Permitted by platform:

```text
mmap may return another address for a hint
```

Program:

```text
fails if address differs
```

Finding:

```text
LATENT PORTABILITY FAILURE
```

### Deliverables

- bounded counterfactual explorer
- minimal counterexample format
- path explanation
- platform-outcome model library
- regression corpus

### Exit Criteria

- generated counterexamples correspond to platform-legal outcomes
- every path is explainable
- bounds are explicit
- the tool does not label bounded absence of a counterexample as proof of safety
- at least five real latent compatibility failures are reproduced

### Public Milestone

```text
RuntimeSkeptic v0.4
Counterfactual Runtime Analysis
```

---

## Phase 7 — Temporal Runtime Contracts

### Objective

Model resource lifecycle and valid event ordering.

### Initial State Machines

- virtual-memory mapping lifecycle
- JIT memory lifecycle
- file-backed mapping lifecycle
- shared-memory lifecycle
- reserve/commit lifecycle

### Work

- define event and state model
- define valid transitions
- define forbidden transitions
- define eventual requirements
- define timeout semantics
- compile temporal contracts into deterministic monitors
- detect incomplete and contradictory contract definitions
- support trace replay
- connect violations to source and runtime evidence

### Deliverables

- Trace/State DSL v0.1
- compiler
- monitor bytecode or transition tables
- temporal conformance suite
- visualization output
- violation reports

### Exit Criteria

- monitors are deterministic
- invalid contract definitions are rejected
- runtime events can be replayed identically
- violations include the minimal relevant event subsequence
- state-machine semantics are versioned

### Public Milestone

```text
RuntimeSkeptic v0.5
Temporal Runtime Verification
```

---

## Phase 8 — Additional Runtime Domains

Expansion should occur one domain at a time.

A new domain should be accepted only if:

- its core operations can be modeled
- meaningful capabilities can be probed or specified
- caller assumptions can be extracted
- findings can be explained
- false positives can be controlled

### Candidate Domain A — Filesystem Semantics

Potential findings:

- atomic rename assumptions
- durability assumptions
- case-sensitivity collisions
- path normalization mismatch
- sparse-file assumptions
- file-lock semantics
- cross-filesystem operation mismatch
- partial-write handling
- sync versus persistence confusion

### Candidate Domain B — Dynamic Loader and ABI

Potential findings:

- wrong library version selected
- incompatible symbol semantics
- calling-convention mismatch
- struct-size mismatch
- TLS initialization ordering
- static initialization ordering
- CPU-feature dispatch mismatch
- plugin ABI drift

### Candidate Domain C — Time

Potential findings:

- wall-clock monotonicity assumption
- timezone dependence
- daylight-saving transitions
- timer-resolution mismatch
- leap-second handling
- clock rollback
- duration measured with wall clock
- timeout overflow

### Candidate Domain D — Network I/O

Potential findings:

- partial send or receive ignored
- retry semantics incorrect
- connection reset interpreted as empty data
- message-boundary assumptions on streams
- timeout behavior mismatch
- socket option availability
- address-family mismatch

### Candidate Domain E — Threading and Synchronization

Potential findings:

- scheduler fairness assumptions
- thread-ID reuse
- signal delivery assumptions
- lock fallback mismatch
- priority inversion risk
- weak memory-order assumptions
- thread stack exhaustion
- runtime-specific TLS behavior

### Candidate Domain F — GPU and Accelerator Memory

Potential findings:

- coherence assumptions
- visibility assumptions
- residency assumptions
- alignment mismatch
- host/device address identity assumptions
- unsupported fallback semantics
- synchronization omission
- protection or mapping incompatibility

---

## Phase 9 — Learned Invariants and Operational Prediction

### Objective

Add historical and statistical analysis without weakening the evidence model.

### Work

- mine stable runtime invariants
- compare profiles and traces across releases
- identify changed behaviors
- model resource-consumption trends
- detect address-space fragmentation growth
- detect descriptor and thread accumulation
- estimate risk thresholds
- retain raw evidence
- separate predictions from formal findings

### Deliverables

- invariant miner
- profile regression detector
- trace-diff engine
- resource-risk reports
- confidence calibration documentation

### Exit Criteria

- learned findings are never presented as proof
- every learned invariant lists its observation set
- models can be reproduced from stored data
- statistical warnings are optional in CI
- no opaque score is emitted without supporting measurements

---

## Phase 10 — Ecosystem and Productization

### Objective

Turn the technology into a sustainable developer tool.

### Work

- stable CLI
- C API
- versioned schemas
- CI integrations
- SARIF output where applicable
- GitHub Actions examples
- reproducible release bundles
- signed environment profiles
- profile registry
- documentation and tutorials
- benchmark corpus
- plugin architecture for runtime domains
- enterprise/private profile support
- licensing boundary between open and commercial components

### Potential Packaging

```text
RuntimeSkeptic Core
    semantic IR
    profile reader
    contract evaluator
    report engine

RuntimeSkeptic VM
    virtual-memory domain

RuntimeSkeptic Probe
    executable environment profiler

RuntimeSkeptic Monitor
    runtime wrapper and trace system

CodeSkeptic Runtime Bridge
    static assumption extraction

RuntimeSkeptic Pro
    private environment registries
    large trace comparison
    policy management
    advanced CI reporting
```

---

# 14. MVP Definition

The MVP should solve one concrete problem exceptionally well:

> Determine whether a native application’s required virtual-memory mapping semantics can be satisfied on a specific target host.

## MVP Inputs

- target environment profile
- requested address
- mapping size
- exact versus hinted mapping
- required alignment
- required protection flags
- identity-mapping requirement
- translation-layer availability
- permitted fallback behavior

## MVP Outputs

- supported
- unsupported
- conditionally supported
- unknown
- evidence chain
- native failure modes
- semantic mismatch
- remediation classes

## MVP Demonstrations

1. exact mapping impossible on selected host
2. hinted mapping may relocate
3. page-size mismatch
4. W^X or executable-memory restriction
5. reserve/commit mismatch
6. valid host operation rejected by caller assumption
7. invalid fallback reported as success

---

# 15. Success Metrics

## Technical Metrics

- proven findings reproduced reliably
- deterministic profile serialization
- deterministic contract evaluation
- low false-positive rate for `PROVEN` findings
- explicit unknown state instead of guessing
- native error preservation
- trace replay correctness
- wrapper conformance
- bounded analysis transparency
- schema compatibility tests

## Product Metrics

- time required to diagnose a known compatibility failure
- number of actionable findings before full application startup
- percentage of findings with complete evidence chains
- number of real cross-platform defects reproduced
- successful integration into emulator, JIT, or database test environments
- reduction in manual platform-specific debugging effort
- number of independently contributed environment profiles or domain contracts

## Research Metrics

- published failure taxonomy
- open incident corpus
- reproducible experimental profiles
- clearly stated soundness limits
- comparison with sanitizers, runtime verification tools, and symbolic execution systems
- peer feedback from emulator, compiler, systems, and OS developers

---

# 16. Testing Strategy

## Unit Tests

- schema validation
- canonical serialization
- address-range arithmetic
- page-size calculations
- protection-state transitions
- evidence classification
- finding rendering
- error-code preservation

## Conformance Tests

Each supported API operation should include:

- successful native case
- native failure case
- exact-mapping case
- relocated-hint case
- alignment failure
- protection failure
- lifecycle violation
- unsupported/unknown behavior

## Cross-Platform Tests

- Windows x64
- Linux x86-64
- macOS Apple Silicon
- translated process mode where available

## Differential Tests

Compare:

- native call versus wrapper call
- profile version versus later profile version
- manually authored contract versus statically extracted contract
- live trace versus replayed trace
- platform documentation versus measured behavior

## Mutation Tests

Mutate contracts and profiles:

- remove exact-address requirement
- change page size
- weaken success postcondition
- alter failure code
- permit invalid fallback
- invert protection transition
- remove translation capability

Tests must prove that meaningful semantic mutations are detected.

---

# 17. Data and Artifact Model

Every analysis run should be reproducible from an evidence bundle.

Suggested bundle:

```text
analysis_bundle/
├── manifest.json
├── environment_profile.json
├── application_requirements.json
├── static_assumptions.json
├── runtime_trace.jsonl
├── contracts/
├── findings.json
├── report.md
└── hashes.txt
```

The bundle should record:

- tool versions
- schema versions
- host identity at an appropriate privacy level
- process architecture
- input hashes
- contract hashes
- profile hashes
- analysis options
- finding IDs
- replay status

---

# 18. Security and Safety Considerations

Environment probes operate close to operating-system boundaries.

They must:

- avoid destructive memory operations
- use isolated child processes for risky tests
- apply strict range and size limits
- terminate hung probes
- record crashes as probe outcomes
- avoid writing outside controlled temporary locations
- avoid requiring elevated privilege for normal use
- document tests requiring special permissions
- never disable system security controls automatically
- avoid interpreting security restrictions as defects in the OS
- distinguish unsupported behavior from security-policy enforcement

Runtime wrappers must:

- avoid recursion
- avoid unsafe allocation inside hooks where possible
- preserve `errno` and native error state
- preserve thread behavior
- be signal-aware where relevant
- fail open or fail closed only according to explicit mode
- avoid leaking sensitive runtime data by default

---

# 19. Key Risks

## Risk 1 — Excessive Scope

Mitigation:

- remain virtual-memory-only through the first useful releases
- reject new domains until the VM model is stable

## Risk 2 — False Claims of Proof

Mitigation:

- explicit confidence model
- separate measured, specified, inferred, and heuristic evidence
- use `UNKNOWN` liberally

## Risk 3 — Platform Behavior Changes

Mitigation:

- versioned executable profiles
- profile expiration metadata
- profile comparison
- reproducible probes

## Risk 4 — Probe Side Effects

Mitigation:

- child-process isolation
- strict resource caps
- platform-specific safety review
- clear test inventory

## Risk 5 — Static Extraction Complexity

Mitigation:

- begin with annotations and manually authored requirements
- add CodeSkeptic extraction incrementally

## Risk 6 — Runtime Monitoring Overhead

Mitigation:

- domain-specific events
- sampling only for statistical modules
- compile-time disabling
- benchmark every release

## Risk 7 — Becoming a Generic Observability Tool

Mitigation:

- focus on semantic contradictions
- do not compete with logging and tracing platforms
- consume traces only when they contribute to contract analysis

## Risk 8 — Building a General Symbolic Executor

Mitigation:

- bounded environment-outcome exploration
- domain-specific constraints
- reuse established solvers
- keep counterexamples explainable

---

# 20. Decision Gates

The project should stop or change direction if one of the following occurs.

## Gate A — After Phase 1

Continue only if environment profiles are:

- reproducible
- meaningfully different across platforms
- able to expose real compatibility constraints

## Gate B — After Phase 3

Continue only if the VM analyzer:

- diagnoses real failures
- produces evidence better than ordinary logs
- maintains low false-positive rates

## Gate C — After Phase 5

Continue static integration only if CodeSkeptic can extract assumptions without unacceptable ambiguity.

## Gate D — Before Every New Domain

A domain must demonstrate:

- at least ten real incident examples
- a bounded operation model
- measurable or specified environment behavior
- actionable outputs

---

# 21. Recommended Repository Structure

A monorepo is recommended during early development.

```text
RuntimeSkeptic/
├── CMakeLists.txt
├── README.md
├── ROADMAP.md
├── LICENSE
├── include/
│   └── runtimeskeptic/
├── src/
│   ├── core/
│   ├── vm/
│   ├── probe/
│   ├── monitor/
│   ├── contracts/
│   └── reports/
├── tools/
│   ├── rs-env-probe/
│   ├── rs-profile/
│   ├── rs-check/
│   └── rs-replay/
├── schemas/
├── contracts/
├── profiles/
│   ├── fixtures/
│   └── generated/
├── tests/
│   ├── unit/
│   ├── conformance/
│   ├── cross_platform/
│   └── incidents/
├── corpus/
│   └── runtime_failures/
├── docs/
│   ├── architecture/
│   ├── concepts/
│   ├── domains/
│   └── evidence/
└── reports/
```

CodeSkeptic and RuleDSL should remain separate repositories initially.

Integration should occur through versioned artifacts rather than direct source dependencies.

---

# 22. First 12-Month Practical Plan

This schedule assumes a solo developer working part-time and prioritizes technical proof over feature count.

## Months 1–2

- formalize problem statement
- create incident corpus
- define failure taxonomy
- define evidence model
- design environment profile schema
- build minimal Linux and Windows probes

## Months 3–4

- add macOS probe
- canonicalize profiles
- implement profile verification and comparison
- publish initial measured behavior reports
- write conformance tests

## Months 5–6

- define Runtime Semantic IR
- implement exact-mapping compatibility evaluator
- implement page-size and alignment rules
- produce Markdown and JSON findings
- reproduce motivating failures

## Months 7–8

- package VM Compatibility Analyzer MVP
- add CI mode
- add deterministic evidence bundles
- integrate one real open-source emulator or JIT test case
- publish v0.1

## Months 9–10

- build runtime wrappers
- preserve error semantics
- record and replay VM events
- detect semantic success violations
- publish v0.2 preview

## Months 11–12

- add initial CodeSkeptic assumption export
- detect post-call assertions
- connect source assumptions to profiles
- produce first automated cross-layer diagnosis
- decide whether to continue toward counterfactual analysis

---

# 23. Immediate Next Actions

The next implementation steps should be:

1. Create the RuntimeSkeptic repository.
2. Add this roadmap as `ROADMAP.md`.
3. Write `docs/problem_statement.md`.
4. Create `docs/failure_taxonomy.md`.
5. Add the motivating fixed-mapping incident as the first corpus item.
6. Define environment profile schema v0.1.
7. Implement a minimal `rs-env-probe vm` for one operating system.
8. Produce the first canonical profile.
9. Define a manual exact-mapping requirement file.
10. Implement the first contradiction check:

```text
required exact address
AND address unavailable
AND no translation layer
= unsupported
```

The first complete vertical slice should be intentionally small:

```text
probe host
→ describe application requirement
→ evaluate compatibility
→ produce evidence report
```

No static extraction, runtime hooking, AI, or generalized symbolic execution is required for the first vertical slice.

---

# 24. Long-Term Vision

RuntimeSkeptic should become a platform for answering:

```text
What does this software assume about its execution environment?
```

```text
Which of those assumptions are guaranteed, merely observed, or false?
```

```text
Which legal runtime outcomes can produce failure?
```

```text
Which compatibility layer weakens the required semantics?
```

```text
Can the failure be reproduced from an evidence bundle?
```

The long-term project family becomes:

```text
CodeSkeptic
    extracts and verifies program semantics

RuleDSL
    evaluates deterministic decision policies

RuntimeSkeptic
    checks program assumptions against execution environments

Trace/State DSL
    specifies temporal runtime behavior
```

Together they support a broader engineering principle:

> **Software correctness depends not only on code, but also on whether every runtime layer preserves the semantics required by the layers above it.**

---

# 25. Project Positioning

Suggested technical description:

> RuntimeSkeptic is a cross-layer runtime compatibility analyzer that detects failures by comparing software assumptions with measured and specified execution-environment semantics.

Suggested concise description:

> Predict runtime failures before they become crashes.

Suggested research-oriented description:

> RuntimeSkeptic models environment-dependent postconditions and proves or explores contradictions across application, runtime, compatibility, operating-system, and kernel boundaries.

---

# 26. Final Roadmap Rule

Do not expand the project because a runtime domain is interesting.

Expand only when a domain can produce:

- explicit contracts
- measurable capabilities
- reproducible evidence
- explainable findings
- useful remediation guidance
- controlled false-positive rates

The first success is not a universal runtime intelligence system.

The first success is:

> **A virtual-memory analyzer that can explain, before or during execution, why a required memory semantic cannot be satisfied on a selected host.**

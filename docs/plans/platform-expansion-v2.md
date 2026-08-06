# Platform Expansion Plan v2

<!-- PLATFORM-EXPANSION-V2: accepted 2026-08-06 -->

**Status:** Accepted by the owner on 2026-08-06  
**Scope:** RuntimeSkeptic platform evidence after the standalone v0.2 release  
**Invariant:** root `plan.md`, `ROADMAP.md`, and ADR-0001 remain unchanged

This addendum expands measured execution environments without reopening the
standalone-product boundary. It does not turn a successful cross-compile, an
emulator run, or one cloud guest into a claim about an architecture family.

## Outcome

RuntimeSkeptic can build, test, probe, and publish attributable evidence on
native-ISA ARM64 hosted runners, and has a cost-bounded path for collecting the
same evidence on real RISC-V64 hardware. Architecture names are represented
correctly before any new profile is accepted.

## Claim vocabulary

The project uses these labels in increasing order of strength:

1. **build-smoke:** the source cross-compiles; no runtime claim follows.
2. **validated hosted environment:** tests and the probe ran in a named VM on
   native-ISA hardware. The claim is limited to that guest image and kernel.
3. **measured hardware environment:** tests and the probe ran on an identified
   physical target. The claim is limited to that target and installed OS.
4. **platform-family support:** multiple independent environments cover the
   relevant variants and the maintained CI matrix keeps them green.

Only labels 2 and 3 may publish a `measured` profile. Neither may be promoted
to label 4 by prose.

## Accepted delivery slice

### A. Hosted ARM64 evidence

- Linux ARM64 on GitHub's standard `ubuntu-24.04-arm` runner.
- Windows ARM64 on GitHub's standard `windows-11-arm` runner.
- Native architecture preflight before configure.
- Warning-clean build, complete CTest, two independent probe processes,
  `rs-profile verify`, profile-policy validation, and uploaded evidence.
- Linux additionally runs the ground-truth harness against the fresh profile.

These are validated hosted environments, not embedded-device evidence. Their
provenance must say `github_hosted_vm` and name the runner image.

### B. Architecture identity hardening

- Preserve `riscv64` as a first-class 64-bit architecture.
- Preserve 32-bit ARM as `arm`, rather than collapsing it to `x86`.
- Derive Linux process architecture from compiler target macros, never from
  pointer width plus an x86 default.
- Keep unknown machine names open and fail them to `other`, not a guessed ISA.

### C. RISC-V64 hardware path

- A manual-only workflow targets an already-provisioned RISC-V64 host over SSH.
- It never provisions or destroys a paid server.
- It requires an explicit `RUN` confirmation, an environment approval, pinned
  host keys, a 30-minute control-plane timeout, and exact temporary paths.
- The remote harness rejects any host whose native `uname -m` is not
  `riscv64`, then runs the same build/test/two-probe/profile gate.
- A successful harness is readiness evidence. RISC-V64 is not listed as a
  measured environment until an actual hardware run publishes its artifacts.

## Evidence gates

### G1 ? identity

`test_profile` proves architecture serialization and pointer widths;
platform validation rejects an expected/observed architecture mismatch.

### G2 ? native hosted execution

Each ARM64 lane must prove the runner architecture before CMake, build with
warnings as errors, and pass every registered CTest. `continue-on-error` is
forbidden on evidence-producing steps.

### G3 ? profile integrity

Each lane must:

- emit `origin: measured`;
- match expected OS, host architecture, and process architecture;
- pass `rs-profile verify`;
- produce the same `profile_id` in two processes;
- carry a valid page size and allocation granularity;
- publish a validation envelope that limits the claim to the runner class.

### G4 ? hardware evidence

RISC-V64, ARMv7, non-default ARM64 page sizes, QNX, and VxWorks require a real
or explicitly authorized target. QEMU may be used for build-smoke only and is
never hardware evidence.

### G5 ? public claim

README support rows may be added only after their authoritative workflow is
green on the commit carrying the claim. A workflow definition alone is not a
measurement.

## Cost and security guardrails

- Standard public-repository ARM64 runners are the default; larger billed
  runners are not used.
- RISC-V execution is manual-only and assumes a separately provisioned host.
- SSH host verification may not be disabled.
- No private key, token, hostname, corporate profile, `/proc/cpuinfo`, serial
  number, or proprietary SDK artifact is uploaded.
- Remote work happens below a run-specific temporary directory and is removed
  on success or failure.
- QNX/VxWorks work starts only with an authorized SDK, target, and owner for
  the resulting evidence.

## Deferred platform slices

The following are deliberately not part of the executable delivery slice:

- Linux ARM64 16 KiB and 64 KiB page kernels;
- Linux ARMv7 measured hardware;
- QNX 8/AArch64;
- VxWorks on Arm, Power, or RISC-V;
- Cortex-M/Cortex-R MPU analysis, which is a separate product domain rather
  than a virtual-memory platform port.

They remain visible in the consumable queue with their external evidence
requirements. Deferral is not support.

## Completion contract

This delivery slice is complete when:

1. the accepted plan is hash-pinned and its guard fails on mutation;
2. the architecture model and Linux process detection no longer collapse
   RISC-V64 or ARM into x86 identities;
3. both hosted ARM64 workflows pass their evidence gates;
4. the manual RISC-V64 path passes local structural/adversarial checks; and
5. TODO consumes completed tasks into a newest-first PROGRESS entry while
   hardware-dependent tasks remain explicit.

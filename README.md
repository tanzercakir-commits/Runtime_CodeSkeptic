# RuntimeSkeptic

[![CI](https://github.com/tanzercakir-commits/Runtime_CodeSkeptic/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/tanzercakir-commits/Runtime_CodeSkeptic/actions/workflows/ci.yml)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=c%2B%2B&logoColor=white)](CMakeLists.txt)
[![Platforms](https://img.shields.io/badge/platforms-Linux%20%7C%20macOS%20%7C%20Windows-4C566A)](.github/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-Apache--2.0-2C8EBB)](LICENSE)

**Find virtual-memory compatibility failures before deployment.**

RuntimeSkeptic measures what a host actually supports, compares it with a
program's declared runtime requirements, and returns an evidence-backed
verdict: `SUPPORTED`, `UNSUPPORTED`, `CONDITIONALLY_SUPPORTED`, or `UNKNOWN`.
It is a standalone, open-source CLI and runtime monitor with no external build
or runtime dependencies.

## Quick start

You need [CMake 3.20+](CMakeLists.txt) and a C++20 compiler.

### Linux and macOS

Clone, build, and run the test suite:

```console
$ git clone https://github.com/tanzercakir-commits/Runtime_CodeSkeptic
$ cd Runtime_CodeSkeptic
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
$ cmake --build build --parallel
$ ctest --test-dir build --output-on-failure
```

Then try a real compatibility question: *why does Redis refuse to start on an
Apple Silicon Mac?* You do not need a Mac; the repository includes a measured
profile from that host.

```console
$ build/bin/rs-check contracts/campaign/redis-jemalloc-page-size-lg12.json \
      --profile profiles/measured/macos-14-arm64-native.measured.json
```

The key lines are (abridged):

```text
  UNSUPPORTED

RS-VM-0006  Host page size differs from the required page size
  severity critical  |  confidence COUNTEREXAMPLE  |  impact UNSUPPORTED

  Required
    page size == 4096
  Host capability
    page size == 16384
  Failure sink
    process_exit at redis/redis@b53f65d:deps/jemalloc/src/pages.c:761
```

The result shows the complete conflict: Redis's bundled jemalloc requires a
4 KiB page, while the measured host provides 16 KiB pages. Redis does not need
to be installed on the machine running the analysis.

Measure your own machine and check a contract against it:

```console
$ build/bin/rs-env-probe vm --name my-laptop --output host.json
$ build/bin/rs-check contracts/emulator-highmem-guest-mapping.json --profile host.json
```

### Windows (PowerShell)

```powershell
git clone https://github.com/tanzercakir-commits/Runtime_CodeSkeptic
cd Runtime_CodeSkeptic
cmake -S . -B build
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
build\bin\Release\rs-check.exe `
  contracts\campaign\redis-jemalloc-page-size-lg12.json `
  --profile profiles\measured\macos-14-arm64-native.measured.json
```

Exit codes work directly in CI: `0` SUPPORTED · `1` UNSUPPORTED ·
`2` CONDITIONALLY_SUPPORTED · `3` UNKNOWN.

> RuntimeSkeptic v0.2 is source-first. CI packages verify release construction;
> they are not permanent GitHub Release downloads.

## Observe a real call

Phase 4 adds `libruntimeskeptic`, a stable C ABI around selected POSIX and
Windows virtual-memory calls. The wrapper preserves the native result and
post-call error state, records into a fixed allocation-free buffer, and emits a
sealed canonical JSONL trace:

```console
$ build/bin/rs-runtime-sample runtime_trace.jsonl
$ build/bin/rs-replay trace runtime_trace.jsonl
$ build/bin/rs-runtime-benchmark --iterations 128 --output overhead.json
```

On Visual Studio generators use `build\bin\Release\`. Raw wrappers are
native-compatible; checked allocation wrappers accept an explicit caller
expectation and detect successful relocation at the call boundary. Monitoring
can be removed at compile time or disabled at runtime. API, safety guarantees,
trace rejection rules and limitations: [docs/runtime-monitor.md](docs/runtime-monitor.md).

## The full output

The verdict above is the short form. Every finding also carries the chain that
produced it — which layer claimed what, and on what kind of evidence:

```text
  UNSUPPORTED

RS-VM-0003  Requested range extends past the end of the user address space
  severity critical  |  confidence PROVEN  |  impact UNSUPPORTED

  Evidence chain
    [application] program requires an exact mapping at 0x800000000000 of 65536 bytes
      specified_guarantee <- src/guest_memory.cpp:138 (GuestMemory::Init)
    [compatibility_layer] no guest-to-host translation layer exists, so the guest address must equal the host address
      specified_guarantee <- requirement.assumptions
    [operating_system] range extends past the end of the user address space (0x7ffffffff000)
      measured_capability <- my-laptop
  ...
  Will not work
    x Retry the mapping
      The obstacle is a property of this host's address space, not a transient
      resource shortage. Every attempt fails identically.
    x Request a smaller size at the same address
      The start address itself is unavailable; reducing the length does not
      make it available.
```

(Abridged at `...` — the real output also lists five remediation classes.)

`PROVEN` is the strongest claim the evidence allows, and it is capped
mechanically: a finding is never stronger than its weakest supporting fact, and
a range the probe never tested answers `UNKNOWN` rather than
`CONDITIONALLY_SUPPORTED`. See [docs/evidence_model.md](docs/evidence_model.md).

## Measured environments

| OS | CPU | Translation | Where the profile is |
|---|---|---|---|
| macOS | ARM64 (Apple Silicon) | — | [`profiles/measured/`](profiles/measured/) |
| macOS | x86-64 | Rosetta 2 | [`profiles/measured/`](profiles/measured/) |
| Windows | x86-64 | — | [`profiles/measured/`](profiles/measured/) |
| Windows (Wine on Linux) | x86-64 | Wine | [`profiles/measured/`](profiles/measured/) |
| Linux | x86-64 | — | measured by CI each run, not checked in |
| Linux | ARM64 | native | validated on `ubuntu-24.04-arm`; per-run evidence artifact |
| Windows | ARM64 | native | validated on `windows-11-arm`; per-run evidence artifact |

The four snapshot profiles for macOS, Windows x86-64 and Wine are checked in
under [profiles/measured/](profiles/measured/), each fact recording how it was
measured. Linux profiles and the hosted ARM64 profiles are not shipped: CI
measures those runners on every evidence run and publishes attributable
artifacts, keeping the result fresh rather than freezing another snapshot.

The ARM64 rows mean **validated hosted environments** only. They do not claim
that every ARM64 kernel, page-size configuration, device or architecture-family
variant is supported. Measure your own target with `rs-env-probe`, as above.

## Community validation wanted

Some useful targets cannot be represented honestly by a cross-compile or an
emulator. If you operate one of the environments below, you can extend the
evidence without changing RuntimeSkeptic's claim boundary:

- physical Linux RISC-V64;
- Linux ARM64 with a 16 KiB or 64 KiB kernel page size;
- native Linux ARMv7;
- an authorized, licensed QNX or VxWorks development target;
- a reproducible game, emulator or JIT failure caused by address placement,
  page geometry, executable-memory policy or translation-layer behavior.

Start with a public issue or a minimal reproducer, record the exact source
revision and target configuration, then produce two independent profiles plus
the native test output. The reusable native harness is
[`tools/platform/run_native_validation.sh`](tools/platform/run_native_validation.sh).
Do not publish credentials, device serials, proprietary source, private crash
logs or licensed SDK contents. A new environment is named as validated only
after its attributable evidence passes the same profile, CTest and ground-truth
checks as the hosted lanes.

## System architecture

```text
                        ┌────────────────────────────┐
                        │ Requirement Documents      │
                        │ Hand-authored / versioned  │
                        └──────────────┬─────────────┘
                                       │
                                       │ stated assumptions
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

RuntimeSkeptic v0.2 includes environment probes, requirement contracts, the
analysis engine, evidence reports, runtime wrappers, and pure trace replay.

## Documentation

- [Understand evidence and confidence](docs/evidence_model.md)
- [See more one-command diagnoses](docs/diagnosis-cards.md)
- [Integrate the CLI and its exit codes](docs/integrations.md)
- [Use the runtime monitor and replay format](docs/runtime-monitor.md)
- [Read the product boundary](docs/decisions/0001-standalone-product-boundary.md)

Maintainers can find current status in [docs/PLAN.md](docs/PLAN.md), the working
queue in [docs/TODO.md](docs/TODO.md), and immutable history in
[docs/PROGRESS.md](docs/PROGRESS.md).

## License

Apache-2.0. See [LICENSE](LICENSE).

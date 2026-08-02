# RuntimeSkeptic

**Predict runtime failures before they become crashes.**

RuntimeSkeptic compares what a program *assumes* about its execution
environment against what that environment *measurably provides*, and answers
with an evidence chain: supported here, refused here, or conditionally — before
the program runs there. v0.1 covers virtual memory.

Full pitch: [docs/problem_statement.md](docs/problem_statement.md) ·
what this deliberately is not: [docs/non_goals.md](docs/non_goals.md) ·
plan: [ROADMAP.md](ROADMAP.md)

---

## Try it in 60 seconds

A C++20 compiler and CMake 3.20 are enough — no external dependencies, no
network access, nothing to configure.

```console
$ git clone https://github.com/tanzercakir-commits/Runtime_CodeSkeptic && cd Runtime_CodeSkeptic
$ cmake -S . -B build && cmake --build build -j
```

Now ask a real question — *why does Redis refuse to start on an Apple Silicon
Mac?* You do not need a Mac; the repository ships that host's measured profile.

```console
$ build/bin/rs-check contracts/campaign/redis-jemalloc-page-size-lg12.json \
      --profile profiles/measured/macos-14-arm64-native.measured.json
```

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

Redis's bundled jemalloc was compiled for a 4 KiB page and aborts on a 16 KiB
host. Proven from one measurement and one declared requirement — Redis was
never installed, and no Mac was touched.

**Then measure your own machine** and check anything against it:

```console
$ build/bin/rs-env-probe vm --name my-laptop --output host.json
$ build/bin/rs-check contracts/emulator-highmem-guest-mapping.json --profile host.json
```

Exit codes are CI-ready: `0` SUPPORTED · `1` UNSUPPORTED ·
`2` CONDITIONALLY_SUPPORTED · `3` UNKNOWN. All tools and flags:
[docs/integrations.md](docs/integrations.md). More one-command cases:
[docs/diagnosis-cards.md](docs/diagnosis-cards.md).

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

| OS | CPU | Translation |
|---|---|---|
| Linux | x86-64 | — |
| macOS | ARM64 (Apple Silicon) | — |
| macOS | x86-64 | Rosetta 2 |
| Windows | x86-64 | — |
| Windows (Wine on Linux) | x86-64 | Wine |

Profiles live in [profiles/measured/](profiles/measured/); each fact records
how it was measured.

## Documentation

| Document | Job |
|---|---|
| [docs/scenarios/](docs/scenarios/) | the spirit — why this exists, who it is for |
| [docs/TODO.md](docs/TODO.md) | the compass — what we are doing now. **Start here.** |
| [docs/PLAN.md](docs/PLAN.md) | the map — status against the ROADMAP, criterion by criterion |
| [docs/PROGRESS.md](docs/PROGRESS.md) | the past — what changed and what was learned, append-only |
| [docs/evidence_model.md](docs/evidence_model.md) | evidence classes, confidence ceiling, verdicts |
| [docs/failure_taxonomy.md](docs/failure_taxonomy.md) | the failure family, with examples |
| [docs/findings/registry.md](docs/findings/registry.md) | every `RS-VM-xxxx` id |
| [docs/campaigns/](docs/campaigns/) | measurement campaigns — including the false-positive rate and the honest negative hunts |
| [docs/real-world-test-playbook.md](docs/real-world-test-playbook.md) | reproduce the external-project verdicts yourself |
| [docs/architecture/determinism.md](docs/architecture/determinism.md) | canonicalization and `profile_id` |

## License

Apache-2.0. See [LICENSE](LICENSE).

# RuntimeSkeptic v0.2.0 — verified CI package quickstart

RuntimeSkeptic predicts whether a program's virtual-memory requirements survive
a given host — *before* it runs there — with a cross-layer evidence chain.

This guide begins after downloading and extracting a successful CI package.
Those packages are verification artifacts, not permanent GitHub Release assets;
the stable public path is the source build in the repository README. Windows is
source-only in v0.2. RuntimeSkeptic is standalone and does not require
CodeSkeptic.

The five Linux analyzer executables are statically linked. The runtime monitor
ships as a shared library with its C headers, pure replay library, sample and
benchmark. The macOS arm64 package links only Apple's system libraries. Neither
package needs a downloaded dependency or network access at runtime.

## See a real verdict in 10 seconds

Why won't Redis start on an Apple Silicon Mac? You don't need a Mac — this
package ships that host's measured profile.

```console
$ ./bin/rs-check contracts/campaign/redis-jemalloc-page-size-lg12.json \
      --profile profiles/measured/macos-14-arm64-native.measured.json
```

```text
  UNSUPPORTED

RS-VM-0006  Host page size differs from the required page size
  Required          page size == 4096
  Host capability   page size == 16384
  Failure sink      process_exit at deps/jemalloc/src/pages.c:761
```

Redis's bundled jemalloc was compiled for a 4 KiB page and aborts on a 16 KiB
host. Proven from one measurement and one declared requirement — Redis was never
installed and no Mac was touched.

Two more shipped cases, each one command:

```console
$ ./bin/rs-check contracts/campaign/box64-dynarec-rwx-block.json \
      --profile profiles/measured/macos-14-arm64-native.measured.json   # W^X on Apple Silicon
$ ./bin/rs-check contracts/campaign/qemu-i386-etexec-fixed-noreplace.json \
      --profile profiles/measured/windows-server-2025-x86_64.measured.json   # 64 KiB granularity on Windows
```

## Then measure your own machine

```console
$ ./bin/rs-env-probe vm --name my-laptop --output host.json
$ ./bin/rs-check contracts/emulator-highmem-guest-mapping.json --profile host.json
```

## Exit codes (built for CI)

`0` SUPPORTED · `1` UNSUPPORTED · `2` CONDITIONALLY_SUPPORTED · `3` UNKNOWN.

## Tools in `bin/`

| Tool | Purpose |
|---|---|
| `rs-check` | evaluate a requirement (or a bundle) against a profile |
| `rs-env-probe` | measure this host's virtual-memory behaviour into a profile |
| `rs-profile` | verify a profile, diff two profiles, or re-check contracts across them |
| `rs-replay` | re-derive a verdict from a sealed evidence bundle (`rs-check --bundle DIR`) |
| `rs-mcp` | the same capabilities over the Model Context Protocol |
| `rs-runtime-sample` | record a real VM lifecycle into canonical JSONL |
| `rs-runtime-benchmark` | measure native, disabled, buffered and flush overhead |

## Observe and replay a real call

```console
$ ./bin/rs-runtime-sample runtime_trace.jsonl
$ ./bin/rs-replay trace runtime_trace.jsonl
$ ./bin/rs-runtime-benchmark --iterations 128 --output overhead.json
```

Headers, libraries and `RuntimeSkepticConfig.cmake` are under `include/` and
`lib/` for downstream C/C++ consumers.

## Full source, docs, and the honest limits

Everything here is reproducible from the public repository, which also carries
the measurement campaigns, the false-positive rate (0 across 1,576 kernel-
observed requests on three OSes), and what v0.2 deliberately does *not* do yet:

**https://github.com/tanzercakir-commits/Runtime_CodeSkeptic**

Apache-2.0.

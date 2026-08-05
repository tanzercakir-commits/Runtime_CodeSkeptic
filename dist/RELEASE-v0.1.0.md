# RuntimeSkeptic v0.1.0

A program's memory requirements aren't a constant. The environment — page size,
address space, W^X policy, allocation granularity — is an input, and most tools
find out at the crash. RuntimeSkeptic predicts whether a program's virtual-memory
requirements survive a given host **before** it runs there, with a cross-layer
evidence chain.

This is a first release of a side experiment that got more rigorous than it was
meant to. It covers one domain — virtual memory — and it is honest about that.

## Try it in 10 seconds — no build, no Mac

Download the prebuilt package for your platform below, extract, and ask a real
question: *why won't Redis start on an Apple Silicon Mac?* The package ships that
host's measured profile, so you don't need one.

```console
$ ./bin/rs-check contracts/campaign/redis-jemalloc-page-size-lg12.json \
      --profile profiles/measured/macos-14-arm64-native.measured.json
  UNSUPPORTED
  RS-VM-0006  Host page size differs from the required page size
    Required          page size == 4096
    Host capability   page size == 16384
    Failure sink      process_exit at deps/jemalloc/src/pages.c:761
```

Redis's bundled jemalloc was compiled for a 4 KiB page and aborts on a 16 KiB
host — proven from one measurement and one declared requirement, with Redis never
installed. Two more shipped cases, one command each: box64's dynarec hitting W^X
on Apple Silicon, and QEMU's i386 loader hitting Windows' 64 KiB granularity.

## What's inside

- **0 false positives** across 1,576 kernel-observed mapping requests on Linux,
  Windows and macOS — each traced by that OS's native instrument (strace / ETW /
  dtrace). When it says "runs here," that is trustworthy, which is why "refused
  here" is worth listening to.
- 28 cross-layer rules; every verdict seals into a tamper-evident, replayable
  bundle.
- Statically-linked Linux binaries (run on any x86-64 Linux, no dependencies)
  and a macOS arm64 package linked only to Apple's system libraries. Both use
  the same fail-closed package verifier before an archive is accepted.

## Honest limits

- **v0.1 covers virtual memory only.** Filesystem, loader, time and networking are
  named on the roadmap and gated behind ten real incidents each — not shipped.
- Prebuilt binaries: **Linux x86-64** (static) and **macOS arm64** (build it with
  the included script on an Apple Silicon Mac). Any other platform builds from
  source — a C++20 compiler and CMake 3.20, no external dependencies.
- The tool **predicts**; it does not scan your binaries. Requirements are written
  by hand in v0.1.

## Downloads

| Asset | Platform |
|---|---|
| `runtimeskeptic-v0.1.0-linux-x86_64.tar.gz` | any x86-64 Linux (static) |
| `runtimeskeptic-v0.1.0-macos-arm64.tar.gz` | Apple Silicon macOS |
| Source (zip / tar.gz, auto-attached) | build anywhere: `cmake -S . -B build && cmake --build build` |

Full source, the measurement campaigns, and everything v0.1 deliberately does not
do yet: **https://github.com/tanzercakir-commits/Runtime_CodeSkeptic**

Apache-2.0.

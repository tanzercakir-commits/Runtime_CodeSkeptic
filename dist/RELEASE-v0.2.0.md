# RuntimeSkeptic v0.2.0

Virtual Memory Runtime Monitor

## What ships

- The v0.1 compatibility analyzer, measured profiles, evidence bundles and five
  command-line tools.
- `libruntimeskeptic`: a versioned C ABI for selected POSIX and Windows
  virtual-memory calls. The prebuilt archives target Linux x86-64 and Apple
  Silicon; the Windows ABI is source-distributed and continuously built and
  tested with MSVC, but v0.2 does not claim a Windows binary archive.
- Canonical, bounded, integrity-sealed JSONL traces and pure lifecycle replay.
- ABI-v1 loader identity (`SONAME` / Mach-O install name) with public version
  0.2.0 and a version-compatible CMake package.
- Compile-time and runtime disable modes, report callbacks and assertion mode.
- C samples, a five-mode overhead benchmark, installed headers and versioned
  CMake package targets.

## Trust boundary

Wrappers issue the selected native call exactly once, capture the post-call
native error immediately, record into a fixed preallocated buffer, restore the
error and return the native result. Requested bytes and native page-rounded
effective ranges are recorded separately. Dropped, recursive, malformed,
truncated, reordered or platform-incompatible evidence is refused by replay.

## Verified platforms

The release gate builds and tests Linux with GCC and Clang, macOS with Apple
Clang, and Windows with MSVC. The package self-check executes the compatibility
demo, records and replays a real lifecycle, and runs the schema-versioned
benchmark. The installed SDK is also linked by a clean out-of-tree consumer.

## Deliberate limits

The ABI v1 boundary is explicit wrappers, not linker interposition. It covers
`mmap`/`mprotect`/`munmap` and
`VirtualAlloc`/`VirtualProtect`/`VirtualFree`. A checked allocation call
carries exact-address intent; raw allocation calls do not guess it. Runtime
events remain observations, not cross-run invariants. Static CodeSkeptic
integration and later roadmap gates are not part of v0.2.

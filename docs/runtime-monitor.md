# Runtime wrapper library

Status: Phase 4 implementation. The public boundary is ABI version 1.

`libruntimeskeptic` observes selected virtual-memory calls without replacing
their native result. The first boundary is deliberately explicit: applications
call the wrapper they want to observe. Linker interposition, `LD_PRELOAD`, Mach
VM APIs, `mremap`, `VirtualAlloc2`, and file-view APIs are outside ABI v1.

## C ABI

The common header is `include/runtimeskeptic/runtime/runtime.h`. POSIX callers
include `runtime_posix.h`; Windows callers include `runtime_windows.h`.
Every exported symbol and structure is suffixed `_v1` or carries
`abi_version` and `struct_size`. Reserved fields must be zero.

The selected native-compatible wrappers are:

| Platform | Native-compatible wrappers | Intent-aware wrapper |
| --- | --- | --- |
| Linux/macOS | `rs_mmap_v1`, `rs_mprotect_v1`, `rs_munmap_v1` | `rs_mmap_checked_v1` |
| Windows | `rs_virtual_alloc_v1`, `rs_virtual_protect_v1`, `rs_virtual_free_v1` | `rs_virtual_alloc_checked_v1` |

A raw `mmap` call cannot reveal whether its address was a hint or a hard caller
requirement. The checked variant accepts `rs_vm_expectation_v1`; an exact
request that succeeds elsewhere records `exact_address_relocated` while
returning the successful native result unchanged.

## Transparency and safety contract

Each wrapper performs one native call, captures `errno` or `GetLastError()`
immediately afterward, records a POD event, restores that captured state, and
returns the native result. The recorder uses a fixed 4096-slot maximum array,
atomic claim/publish sequencing, and a thread-local recursion guard. There is no
allocation, JSON construction, file I/O, mutex, or second native VM call in the
record path.

Infrastructure failure is fail-open for the application and fail-closed for
evidence. A full buffer increments `dropped_events`; recursive observation
increments `reentrant_events`. Either makes `complete=false`, and the reader
refuses the trace as replayable evidence. `flush`, `reset`, initialization and
shutdown are control-plane calls and must be made while observed operations are
quiescent. A POSIX `fork()` child inherits the library disabled with an empty
buffer and must initialize explicitly.

Modes are:

- `record`: buffer events.
- `report`: buffer events and invoke the optional callback.
- `assert`: terminate only after a semantic violation has been recorded. Native
  API failure alone is not a semantic violation.

Define `RS_MONITOR_COMPILE_DISABLED` before the platform header for link-free
aliases to native calls. At runtime, `rs_runtime_set_enabled_v1(0)` makes the
wrappers transparent without discarding the current buffer.

## Trace and replay

`rs_runtime_flush_trace_v1()` writes canonical JSONL governed by
`schemas/runtime-trace-record.v1.json`:

1. one header naming ABI, platform, mode, capacity and the complete instrumented
   API set;
2. contiguous events with request, result, native error state, lifecycle data
   and immediate violation;
3. one footer with counts, completeness and SHA-256 over every preceding
   canonical line including its newline.

Each event keeps the caller's requested address and byte length separate from
the native effective address and page-rounded extent. This distinction lets
replay reproduce a one-byte allocation followed by a page-sized protection
change without guessing a host page size. Windows reset and reset-undo events
require committed coverage but do not create a reserve or commit transition.

Addresses are lowercase hex strings; sizes and flags are integers. The default
trace omits timestamps, paths, file descriptors and OS thread identifiers.
The reader caps files at 16 MiB, lines at 64 KiB and events at 4096. It rejects
unknown versions, unknown fields, noncanonical JSON, sequence gaps, reordering,
missing or duplicate records, digest mismatch, overflow, dropped/reentrant
events, platform-incompatible API declarations, event counts beyond the
declared buffer, narrowing numeric values and impossible platform/operation
combinations.

Run pure replay with:

```text
rs-replay trace runtime_trace.jsonl
```

Replay never calls `mmap`, `mprotect`, `munmap`, `VirtualAlloc`,
`VirtualProtect`, or `VirtualFree`. It reduces POSIX map/protect/unmap and keeps
Windows reserve/commit/protect/decommit/release states distinct. It recomputes
call-boundary violations and rejects disagreement with the recorded event.

## Build, sample and benchmark

```text
cmake -S . -B build -DRS_BUILD_TESTS=ON
cmake --build build --config RelWithDebInfo
ctest --test-dir build --build-config RelWithDebInfo --output-on-failure
build/bin/rs-runtime-sample runtime_trace.jsonl
build/bin/rs-replay trace runtime_trace.jsonl
build/bin/rs-runtime-benchmark --iterations 128 --output overhead.json
```

Multi-config generators place executables under `build/bin/RelWithDebInfo`.
The sample source is under `samples/`; benchmark methodology and interpretation
are in `benchmarks/README.md`.

A normal CMake install publishes the C ABI, trace C++ API, shared runtime,
static replay library and versioned package targets. A downstream project can
use `find_package(RuntimeSkeptic CONFIG REQUIRED)` and link
`RuntimeSkeptic::runtime` and/or `RuntimeSkeptic::trace`. CTest verifies this
from a clean out-of-tree consumer rather than only checking that files exist.

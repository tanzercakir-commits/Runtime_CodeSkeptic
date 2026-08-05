# Runtime monitor samples

The C sample built as `rs-runtime-sample` exercises the complete selected
lifecycle for its host:

- POSIX: map, protect, unmap;
- Windows: reserve+commit, protect, decommit, release.

It initializes a bounded recorder, uses native-compatible wrappers, flushes a
sealed JSONL trace and reports whether evidence stayed complete. Pass a trace
path as the only argument, then run `rs-replay trace PATH`.

The sample is intentionally explicit integration, not linker interposition. To
compile observation completely away, define `RS_MONITOR_COMPILE_DISABLED`
before including the platform header; the wrappers then alias native calls and
no runtime library link is required.

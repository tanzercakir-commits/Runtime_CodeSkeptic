# W^X on real software — three JITs, and the cross-host caveat made concrete (Aug 2026)

Status: Phase 3 hunt, live observation. **Observed on x86_64 Linux (permissive);
predicted for the measured Apple-Silicon W^X profile.** Reproducible: the shapes
under `/tmp` are captured by `observe_requirements.py`; the predictions by
`rs-check` against `profiles/measured/macos-14-arm64-native.measured.json`.

## What we did

The synthetic control (`wx_probes/`) proved the pipeline. This points it at real
software — LuaJIT, Node/V8, and libffi (via cffi) — observed doing actual JIT /
FFI work on a permissive host, to see what the tool says and, more importantly,
what that saying is worth.

## What the three did

| Target | Observed protection call | simultaneous W+X | rs-check vs M1 (W^X) |
|---|---|---|---|
| LuaJIT 2.1 | mmap W, then mprotect X (a W→X flip) + exec-only maps | **0** | CONDITIONALLY_SUPPORTED (RS-VM-0011 only) |
| Node/V8 (node 22) | `mprotect` 240 KiB → **RWX** | 1 | **UNSUPPORTED** (RS-VM-0009 + 0011) |
| libffi 3.4 (cffi 2.0) | `mmap` 4 KiB → **RWX** closure trampoline | 1 | **UNSUPPORTED** (RS-VM-0009 + 0011) |

The tool detects the distinction cleanly: **LuaJIT flips (W then X, never both),
so it trips no RS-VM-0009; V8 and libffi hold a page writable-and-executable at
once, so they do.** That discrimination is real and comes straight from the
trace — it is exactly what a pre-flight check on your own target host should tell
you.

## Why the two UNSUPPORTEDs are NOT M1 findings (the caveat, quantified)

Both V8 and libffi carry **Apple-Silicon-specific W^X code**. What was observed
here is their **Linux branch**, and on a real M1 they take a different path:

- **V8** switches code pages per-thread with `pthread_jit_write_protect_np` on
  Apple Silicon (the documented mechanism; V8's own dev thread on Thread JIT
  Write Protection). Node runs on millions of M1 Macs — its Linux RWX `mprotect`
  is not how it behaves there.
- **libffi** has both a **static-trampoline** build (3.4.2+, which removes the
  RWX closure entirely) and a darwin-specific `ffi_closure_alloc` path. This
  Linux build used the legacy RWX closure; Apple Silicon does not.

So the observe-on-Linux → predict-for-macOS split, which is exactly right for
**single-path** software (a dynarec with one code path, like the box64 shape),
produces a **false positive** for portable software that branches on `__APPLE__`.
The prediction is faithful to the *observed request*; the request just is not the
one that host would make. This is the limitation named up front, now measured on
three well-known pieces of software rather than asserted.

## What this is worth

- **The detection works and is discriminating.** From a trace alone the tool
  separates a W^X-compliant JIT (LuaJIT) from a naive-RWX one (V8/libffi on
  Linux). Pointed at *your* software on *your* target host, that is a true
  pre-flight signal: "this build holds a page W+X; a hardened/W^X host will
  refuse it."
- **Cross-host prediction needs a single code path or the real host.** For
  portable software the honest verdict requires observing on the target host
  itself (Asahi for aarch64-Linux W^X), or a source check confirming there is no
  `__APPLE__`/`MAP_JIT` branch. A bare cross-host prediction on multi-path
  software is a hypothesis, not a finding — and is not filed as one.
- **Negative on novelty, again.** As with the 16K hunt: the mainstream surface is
  covered (LuaJIT compliant; V8 and libffi both have Apple-Silicon paths). A
  novel W^X finding lives where a single-path RWX allocator meets a host it was
  never ported to — un-ported dynarecs, less-maintained JITs, Asahi.

## Honest limits

- **Not the target host.** Every row was observed on x86_64 Linux. None is a live
  Apple-Silicon run; the verdict is what rs-check predicts from the captured
  request, and for multi-path software that request is the wrong-host one.
- **`pthread_jit_write_protect_np` is invisible to the tracer** — it is not a
  syscall. Even observing V8 on an M1 would show the MAP_JIT mmap but not the
  per-thread toggle, so "no flip seen" would not mean "no flip."
- **One workload each.** A hotter or different workload could exercise a code path
  none of these three showed here.

## Sources

- [pthread_jit_write_protect_np(3)](https://keith.github.io/xcode-man-pages/pthread_jit_write_protect_np.3.html) — the Apple Silicon per-thread JIT write-protect API
- [V8-dev: Apple Silicon Thread JIT Write Protection](https://groups.google.com/g/v8-dev/c/C7w3L4pL-cI)
- [saagarjha — fixing JIT apps that don't know about pthread_jit_write_protect_np](https://gist.github.com/saagarjha/d1ddd98537150e4a09520ed3ede54f5e)
- [Fedora — LIBFFI34 static trampolines](https://fedoraproject.org/wiki/Changes/LIBFFI34_static_trampolines)
- [libffi — Apple dylib trampoline PR #621](https://github.com/libffi/libffi/pull/621)

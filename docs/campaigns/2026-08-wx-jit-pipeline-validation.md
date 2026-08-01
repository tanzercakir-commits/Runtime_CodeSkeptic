# The W^X pipeline, validated end to end — August 2026

Status: Phase 3 hunt, instrument validation. **CI-adjacent, Linux-reproducible.**
`tools/campaign/wx_probes/run_control.sh` reproduces every number below on any
Linux host with a compiler and strace; on Apple Silicon it runs the same path
through dtrace against the host's own measured profile.

## Why this exists

The 16K hunt closed on a static predicate (`ldso_predicate.py`) whose
trustworthiness rested on a byte-exact positive control. The W^X / JIT hunt is
**live** — it observes a running program's protection calls and predicts a
verdict for a different (W^X-enforcing) host. Before pointing that instrument at
box64 on an M1 and believing whatever it says, the instrument itself needs a
positive control: two programs, one incompatibility apart, whose verdicts are
known in advance. If the control does not pass, the instrument is wrong, not the
target. This is that control, and it passes.

## The two probes (the whole W^X decision tree, in 20 lines each)

```
naive_rwx.c   mmap(PROT_READ|WRITE|EXEC) ; write code ; run it
              -> the box64-dynarec shape: writable AND executable at once,
                 never flipped. RS-VM-0009 territory.

wx_flip.c     mmap(PROT_READ|WRITE) ; write code ; mprotect(READ|EXEC) ; run it
              -> the W^X-compliant JIT shape: writable, THEN executable,
                 never both at once. Not an RS-VM-0009 violation.
```

Both **succeed** on x86_64 Linux — W^X is not enforced there, and whether they
run is not the question. The question is what the observer captures and what
rs-check predicts about a host that *does* enforce W^X.

## The result (observe → predict → bundle → replay)

Observed with `tools/campaign/observe_requirements.py` (strace on Linux), the
derived requirements were checked against the committed 16K/W^X profile
`macos-14-arm64-native.measured.json`:

| Probe | Observer saw | rs-check on macOS-arm64 | Findings |
|---|---|---|---|
| `naive_rwx` | 1 simultaneous-W+X mmap, **0** W→X transitions | **UNSUPPORTED** | RS-VM-0009 + RS-VM-0011 |
| `wx_flip` | **0** simultaneous-W+X, **1** W→X transition | (0009 absent) | RS-VM-0011 only |

Two things matter here beyond the headline UNSUPPORTED:

1. **The findings are `OBSERVED` confidence**, not PROVEN or HYPOTHESIS — the
   requirement came from a real kernel trace, so the tool is not guessing that
   the program asks for RWX; it watched it ask.
2. **The tool distinguishes the *kind* of W^X problem.** The naive map trips
   RS-VM-0009 (simultaneous write+execute restricted) *and* RS-VM-0011 (JIT
   entitlement). The compliant flip trips only RS-VM-0011 — it still needs the
   `allow-jit` entitlement under a hardened runtime, but it is *not* a
   simultaneous-W+X violation. A tool that only said "JIT bad on Apple Silicon"
   would flag both identically; this one draws the line the platform draws.

Cross-host, `naive_rwx` reads UNSUPPORTED only where W^X is enforced:

```
naive_rwx  × macos/aarch64+none (W^X)      -> UNSUPPORTED  (RS-VM-0009 + 0011)
naive_rwx  × macos/x86_64+rosetta2         -> SUPPORTED    (Rosetta relaxes W^X)
naive_rwx  × linux/x86_64+unknown          -> SUPPORTED
```

Finally the naive verdict was sealed into a tamper-evident bundle and replayed:
`rs-replay` re-ran the analysis from the bundle alone and reproduced UNSUPPORTED
with every finding ID intact. The evidence chain the hunt will hand upstream is
therefore known to round-trip before a single real target is touched.

## How to run it

```
tools/campaign/wx_probes/run_control.sh                 # default W^X profile
tools/campaign/wx_probes/run_control.sh path/to/host.json
```

On an M1 it will measure the host with `rs-env-probe` and check against that;
on Linux it uses the committed macOS-arm64 profile as the W^X reference (Linux
itself does not enforce W^X, so the probes run but the verdict needs a W^X host
to be about anything).

## Honest limits

- **Linux is not W^X.** The probes execute their RWX page on Linux; the
  *incompatibility* is entirely in what rs-check predicts for a W^X host from the
  observed request. That is the design — observe on any host, predict for the
  target — but it means this control proves the *pipeline*, not that any real
  program crashes on an M1. That is what the box64 live run is for.
- **`pthread_jit_write_protect_np` is invisible.** The Apple-blessed fast path
  (MAP_JIT + per-thread write-protect toggling) does not go through mmap or
  mprotect, so the tracer sees the MAP_JIT mmap but not the toggle. "No flip
  observed" is not "no flip happened."
- **macOS entitlement-gating ≠ Asahi enforcement.** On macOS a naive RWX map can
  still succeed *with* `allow-unsigned-executable-memory`; RS-VM-0011 models that
  gate. On Asahi aarch64 (16K + W^X together) the refusal is harder. Record which
  host a live observation was taken on.

## Relation to the hunt

This is the positive control for `docs/hunt-wx-jit-m1.md`. Run it first on the
M1; once it passes there, the box64 Card-2 live run (and the novel-target hunt
after it) rests on an instrument proven against known answers.

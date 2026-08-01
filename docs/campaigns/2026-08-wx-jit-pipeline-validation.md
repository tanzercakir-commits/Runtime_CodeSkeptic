# The W^X pipeline, validated end to end — August 2026

Status: Phase 3 hunt, instrument validation. **Split by host role.**
`tools/campaign/wx_probes/run_control.sh observe` reproduces the capture on any
permissive host with a compiler and strace (x86_64 Linux, or aarch64
Linux/Asahi); `run_control.sh predict` validates the prediction from committed
fixtures against a W^X profile on **any** host, including a hardened M1. The two
are separate because one machine often cannot play both halves — see "What the
M1 run found" below, which is why this note was revised.

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
tools/campaign/wx_probes/run_control.sh predict [W^X_PROFILE.json]  # any host, incl M1
tools/campaign/wx_probes/run_control.sh observe                     # permissive host only
tools/campaign/wx_probes/run_control.sh auto                        # predict, + observe if able
```

`predict` runs anywhere: it takes the committed `fixtures/` (captured on a
permissive host) and checks them against a W^X profile — measured live on an M1
via `rs-env-probe` (which needs no dtrace, so SIP does not block it) or the
committed macOS-arm64 profile elsewhere. `observe` self-gates: on a W^X or
untraceable host it prints why and skips (exit 0), it does not fail.

## What the M1 run found (why this note was revised)

The first version of this control was validated only on x86_64 Linux and shipped
telling the reader to "run it on an M1." A reviewer did exactly that, on a stock
M1 (macOS 26.5.2, **SIP enabled**), and it could not even reach a verdict. Three
defects, each confirmed by running rather than reading — the precise class of bug
this whole project exists to catch, committed inside the tool meant to catch it:

1. **x86_64 payload on aarch64.** Both probes embedded `B8 2A 00 00 00 C3`
   (`mov eax,42; ret`) — x86_64, an illegal instruction on aarch64 (`wx_flip`
   died `SIGILL`, exit 132). Fixed: the payload is now `#if __aarch64__`
   (`mov w0,#42; ret` = `52800540 d65f03c0`) plus `__builtin___clear_cache`,
   verified to return 42 on both x86_64 (native) and aarch64 (cross-compiled,
   under qemu).
2. **The circularity.** `naive_rwx` must *run* to be observed, but a W^X host
   *refuses* a naive RWX map (`EACCES`) — the shape it models is the shape that
   host forbids, so it exits 2 before emitting one traceable call. No payload fix
   rescues that. The fix is architectural: `observe` on a permissive host,
   `predict` from the captured fixtures anywhere. The control no longer asks one
   machine to do both.
3. **The tracer is out of reach on a stock Mac.** `observe_requirements.py` uses
   `sudo dtrace`; its own comment notes SIP was *disabled on the CI runners* — a
   property of those runners, not of a personal Mac (`csrutil status: enabled`,
   and non-interactive `sudo` cannot be satisfied). `observe` now detects this and
   skips with the reason, instead of surfacing an environment limit as a probe
   failure.

Net: **an M1 hosts the prediction half, not the observation half.** The numbers
above stand — they are the pipeline working on a permissive host — but the claim
that a hardened M1 could reproduce the capture was wrong, and is withdrawn.

Credit: the reviewer who ran it on their M1, root-caused all three by experiment,
and supplied the verified aarch64 payload.

## Honest limits

- **Linux is not W^X.** The probes execute their RWX page on Linux; the
  *incompatibility* is entirely in what rs-check predicts for a W^X host from the
  observed request. That is the design — observe on any host, predict for the
  target — but it means this control proves the *pipeline*, not that any real
  program crashes. That is what a box64 live run is for — and box64 is a Linux
  x86_64→ARM emulator with no macOS build, so that run belongs on aarch64 Linux
  (Asahi), not on macOS at all.
- **`pthread_jit_write_protect_np` is invisible.** The Apple-blessed fast path
  (MAP_JIT + per-thread write-protect toggling) does not go through mmap or
  mprotect, so the tracer sees the MAP_JIT mmap but not the toggle. "No flip
  observed" is not "no flip happened."
- **macOS entitlement-gating ≠ Asahi enforcement.** On macOS a naive RWX map can
  still succeed *with* `allow-unsigned-executable-memory`; RS-VM-0011 models that
  gate. On Asahi aarch64 (16K + W^X together) the refusal is harder. Record which
  host a live observation was taken on.

## Relation to the hunt

This is the positive control for `docs/hunt-wx-jit-m1.md`. Run `predict` on the
M1 first — it passes there, validating the prediction half against the host's own
W^X profile. The observation half (and box64's Card-2 live run) then belongs on a
permissive aarch64 host — Asahi Linux — with the shape brought back for the M1 to
predict on. The instrument is proven against known answers before either.

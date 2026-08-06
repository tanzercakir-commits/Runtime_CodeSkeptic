# Public game and emulator issue retrospective — 2026-08

This campaign tests whether public, already-fixed failures can be converted
into reproducible RuntimeSkeptic evidence. It does not comment on or otherwise
modify upstream projects.

## Accepted and replayed

### PCSX2 #11728 — executable-relative memory windows exhausted

- Public report: [PCSX2 issue #11728](https://github.com/PCSX2/pcsx2/issues/11728)
- Confirmed fix: [PCSX2 PR #11734](https://github.com/PCSX2/pcsx2/pull/11734)
- Affected source: `v1.7.5849`
- RuntimeSkeptic artifacts:
  [`RSC-0055`](../../corpus/runtime_failures/RSC-0055-pcsx2-relative-memory-window-exhausted.md)
  and
  [`pcsx2-v175849-data-window.json`](../../contracts/campaign/pcsx2-v175849-data-window.json)

The old x86-64 path searched eleven exact, 256 MiB-spaced candidates relative
to its executable. Reporters confirmed that removing the placement dependency
fixed startup on affected Intel Macs. RuntimeSkeptic returns `UNKNOWN` with
`RS-VM-0024` for every checked-in host profile because none records the future
process image address or availability of the eleven derived ranges. That is a
successful retrospective result: it preserves the real requirement without
inventing host evidence.

This replay also corrected an older guide entry that had described the event
as one approximately 400 MB reservation and mapped it to `RS-VM-0012`. The
affected constants are 155 MiB of data plus 305 MiB of recompiler memory, and
the decisive constraint was executable-relative placement.

## Qualified model gap

### RPCS3 #18701 — Apple Silicon per-thread W^X state leak

- Fix: [RPCS3 PR #18701](https://github.com/RPCS3/rpcs3/pull/18701)

The report ties a reproducible game crash to unbalanced per-thread
`pthread_jit_write_protect_np` state and fixes it with scoped restoration. A
host profile can establish that Apple Silicon exposes the JIT write-protect
mechanism, but it cannot prove that every application thread balances its
runtime transitions. Encoding this as a host-only compatibility verdict would
be a false positive. A future detector would need ordered, per-thread runtime
events or equivalent application instrumentation.

## Rejected from this campaign

The scan also surfaced GPU/Vulkan device loss, seccomp filtering failures and
emulator-internal executable-region bookkeeping bugs. Those may be important
software defects, but they are not host-versus-program virtual-memory
contradictions that the current analyzer can establish. They were not converted
into contracts merely to increase corpus size.

## Acceptance guard

A public incident enters the retrospective corpus only when it has:

1. a stable public report and source or fix reference;
2. an extractable environment-sensitive requirement;
3. a runnable contract, or an explicit and testable model-gap explanation;
4. a verdict no stronger than the available profile and runtime evidence.

Community evidence can promote a `sourced` case to `reproduced`, but only with
an attributable target profile, exact revision and native test output. Private
crash logs, credentials, device identifiers, proprietary source and licensed
SDK contents are never requested.

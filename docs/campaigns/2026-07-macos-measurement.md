# First measured macOS profiles, July 2026

Status: Phase 1 evidence. Produced by `.github/workflows/macos-probe.yml` on a
GitHub-hosted `macos-14` Apple Silicon runner, natively and as a translated
x86-64 process under Rosetta 2. Published as git refs
(`refs/measurements/<sha>/<job>`) because this project's development sandbox
can reach the repository over the git protocol only.

Until this run, **every macOS claim in this repository was a guess.** Three
hand-authored fixtures carried the whole argument, capped at `PREDICTIVE` or
`OBSERVED_INVARIANT` because nothing had been measured.

---

## The headline: one machine, two contracts

| | native arm64 | x86-64 under Rosetta 2 |
|---|---|---|
| page size | **16384** | **4096** |
| allocation granularity | 16384 | 4096 |
| simultaneous write+execute | **refused** | **permitted** |
| JIT entitlement required | **yes** | **no** |
| anonymous executable mapping | yes | yes |
| RW → RX transition | yes | yes |
| mapping past end of file | **SIGBUS** | **zero-fill** |
| `max_user_address` | `0x600000000000` | `0x600000000000` |
| non-destructive exact placement | yes (`VM_FLAGS_FIXED`) | yes |
| translation detected | `none` | `rosetta2` |

Every row is `measured_capability` on macOS 14.8.7.

The first, third and seventh rows are the project's thesis stated as data.
Same hardware, same kernel, same instant - two processes with different page
sizes, different write-xor-execute policies, and different behaviour when
reading past the end of a file. A requirement that holds in one column can
fail in the other, and no error code anywhere reports the difference.

The `sigbus` versus `zero_fill` split is the sharpest of the three. A program
that maps a file, reads past its end, and works fine under Rosetta will take
a bus fault when rebuilt natively - on the same machine, from the same source.

### What this confirmed about the fixtures

- 16 KiB pages on native arm64: the fixtures **guessed right**, now measured.
- W^X enforced on native arm64: **guessed right**, now measured.
- JIT entitlement required on native arm64: **guessed right**, now measured.
- W^X on the Rosetta lane: the fixtures were **wrong**. They asserted
  enforcement; a translated x86-64 process is granted RWX outright.

That last line is why the fixtures were capped at `PREDICTIVE`. The cap did
its job.

### What this confirmed about shadPS4

`address_space.cpp` sets `USER_MAX = 0x5FFFFFFFFFFF` for this platform and
`USER_MIN = 0x7000000000` under `#if defined(__APPLE__) && defined(ARCH_X86_64)`.

- Measured `max_user_address` is `0x600000000000`, which is exactly
  `USER_MAX + 1` in this project's half-open convention. **Confirmed.**
- `0x7000000000` is placeable in both lanes. **Confirmed.**

---

## The band, and four rounds of being wrong about it

Every run agrees on the observation: `mach_vm_allocate(VM_FLAGS_FIXED)`
refuses **every** probed address from `0xFC0000000` through `0x6FC0000000`
with `KERN_NO_SPACE`, in both lanes, while `0x7000000000` succeeds. That is
the commpage plus the GPU carveout, to the address, exactly as shadPS4
describes them.

Classifying that refusal took four iterations, and each wrong answer was found
by reading a measurement rather than by reasoning about the code:

1. **The search stopped at the first hole and called it the ceiling.**
   `max_user_address` came back `0xFC0000000` - the *bottom* of the commpage -
   and the scan then skipped everything above it, so the entire band went
   untested and `unavailable_ranges` was empty. The address space is a set,
   not an interval.
2. **"Occupied by me" and "refused by the kernel" shared a code path.** Fixed
   by splitting placement into three states. `min_map_address` had been
   reporting where the probe's own image ended, dressed as platform policy.
3. **`KERN_NO_SPACE` was read as "yours".** It means "not free" and says
   nothing about who holds it. A fresh process does not own 400 GiB at 64 GiB.
4. **"Is there a region covering it?" came back true.** macOS places the
   commpage and the carveout in *every* task's map, so the presence of a
   region proves nothing. The field that distinguishes them is
   `vm_region_basic_info.reserved`.

The current probe classifies a refusal as a host limitation unless a
**non-reserved** region covers the address, and records what `vm_region`
actually reported - bounds, coverage, reserved flag, protection - in the
range's note. The instrument now explains itself; four rounds of inferring a
remote platform's behaviour from this end was three too many.

> **Open at the time of writing.** Whether that classification finally lands
> the band in `unavailable_ranges` is pending the next CI run. The
> *observation* is stable across three runs and is not in doubt; only its
> label is. Check
> `refs/measurements/<sha>/rosetta-x86_64` for the current answer, and read
> the `note` on each unavailable range - it now carries the raw `vm_region`
> report.

### A finding for shadPS4, if it holds

The band is refused **on native arm64 as well as under Rosetta**. shadPS4
guards its `USER_MIN` workaround with
`#if defined(__APPLE__) && defined(ARCH_X86_64)` - x86-64 only. If the
reservation applies natively too, that guard is narrower than the condition.

This is stated as an observation from a GitHub-hosted runner, not as advice.
It has not been reproduced on physical hardware, the runner is virtualised,
and one machine is not a platform.

---

## Honest limitations

- **One machine, one OS version.** macOS 14.8.7 on a virtualised
  GitHub-hosted runner. A VM's address space is not guaranteed to match bare
  metal, and the GPU carveout in particular is a plausible place for them to
  differ.
- **The probe process is not a game.** shadPS4 maps its regions during
  startup, before the allocator has fragmented anything. The probe measures a
  nearly-empty process. Both are legitimate, and they are not the same
  question.
- **`min_map_address` is process-specific.** It reports where `__PAGEZERO`
  and the loader's own mappings end, which depends on how the binary was
  linked. It is not a platform policy the way Linux's `vm.mmap_min_addr` is,
  and the profile should probably say so more loudly than it does.
- **The GTA V contract has not been re-run against these profiles** in a way
  that is meaningful until the band is classified. Against the
  reported-measurement fixture it is `UNSUPPORTED` / `OBSERVED_INVARIANT`;
  a measured profile that classifies the band would take it to `PROVEN`.
- **Nothing here was reproduced independently.** One runner, one workflow.

## Reproducing

```console
$ gh workflow run macos-probe.yml          # or push to main
$ tools/campaign/fetch_measurement.sh <sha> rosetta-x86_64
$ tools/campaign/fetch_measurement.sh <sha> native-arm64
```

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

## The band

`mach_vm_region` reports, identically in both lanes:

```
region [0xfc0000000,  0x1000000000)   reserved=0   protection ---
region [0x1000000000, 0x7000000000)   reserved=0   protection ---
```

The Rosetta 2 commpage and the Apple GPU carveout, to the byte, exactly as
shadPS4's source comment describes them. `mach_vm_allocate(VM_FLAGS_FIXED)`
refuses every probed address inside them with `KERN_NO_SPACE`, while
`0x7000000000` succeeds.

macOS does not express these as `reserved` placeholders. It expresses them as
ordinary map entries that grant **no access at all**, and both now appear in
`unavailable_ranges` at their full extent, `measured_capability`. Recording
the entries rather than the probe windows also shortened the list, because
many probe points and every ladder hole in the band fall inside them:

| | ranges before | ranges after |
|---|---|---|
| native arm64 | 60 | 48 |
| Rosetta x86-64 | 59 | 47 |

### Six rounds of being wrong about it

The observation was stable from the first run. Only its label kept moving,
and every wrong label was corrected by reading a measurement rather than by
reasoning about the code:

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
   region proves nothing.
5. **"Is it reserved?" came back false.** The `reserved` flag is 0 on both
   entries. The signal that distinguishes them is `protection == VM_PROT_NONE`
   - and it is the better signal anyway, because a program asking for that
   address does not care who nominally owns the entry, only that nothing can
   be mapped there.
6. **The extent was the probe window, not the entry.** Steps 1-5 were wrong
   about what the kernel said. This one was wrong about writing it down.

Step 4 is where the instrument started recording what `vm_region` actually
reported - bounds, coverage, reserved flag, protection - in each range's note,
instead of only its own conclusion. Step 5 then took one round instead of
another guess, and step 6 was visible in the measurement's own text.

### What the sixth round cost

The probe recorded the 4 MiB window it happened to test while the note beside
it quoted the kernel describing a 384 GiB entry:

```
[0x1307200000, 0x1307600000)
  "...region [0x1000000000, 0x7000000000) covers it,
    and is a real mapping (reserved=0), protection ---"
```

Nine addresses in the band were hard-coded in the scan. For any *other*
address inside the same measured entry:

| | verdict | confidence |
|---|---|---|
| before | `UNKNOWN` | `HYPOTHESIS` |
| after | `UNSUPPORTED` | `COUNTEREXAMPLE` |

`UNKNOWN` on the one question this project exists to answer, with the evidence
to answer it already in the profile. The evidence model is why it read
`UNKNOWN` rather than `SUPPORTED` - it will not claim safety it cannot source
- but declining to conclude from a fact you measured and then discarded is not
skepticism, it is a dropped measurement.

The widening is not extrapolation from the sample: `mach_vm_region` reports
the entry's bounds directly, and an entry granting no access refuses placement
everywhere inside itself by construction. Each note now states which address
was probed *and* that the extent came from `vm_region`, so the two can never
be confused again. Adjacent entries are deliberately **not** merged - the
commpage ends exactly where the carveout begins, the kernel reports them
separately, and fusing them would manufacture a 400 GiB entry that
`vm_region` never described.

---

## The GTA V contract against a measured host

`contracts/gtav-rage-direct-memory-mapping.json`, transcribed from
shadps4-emu/shadPS4 issue #4157, against the measured Rosetta profile:

```
UNSUPPORTED

RS-VM-0001  Exact virtual-memory mapping cannot be satisfied
  severity critical  |  confidence COUNTEREXAMPLE  |  impact UNSUPPORTED

  Required        mapping placed exactly at [0x1307200000, 0x1307220000);
                  guest address must equal host address
  Host capability requested range intersects unavailable range
                  [0x1000000000, 0x7000000000)
  Failure sink    fatal_assert at src/core/memory.cpp:1467
```

The address from the issue is measured-unavailable, on a real macOS host, in
both lanes. The report names the assertion the title dies on and rejects
"retry", "map less", and "ignore the returned address" by name.

### The doc predicted `PROVEN`. It was wrong, and the reason matters

The previous revision of this file said a measured profile "would take it to
`PROVEN`". It does not, and the cap is right.

`PROVEN` requires that *every* fact in the chain support it. The host facts
are now `measured_capability`, which permits `PROVEN`. But two links are not
host facts:

```
[application]          exact mapping at 0x1307200000 of 131072 bytes
                       statically_inferred  <- memory.cpp:1467
[compatibility_layer]  no guest-to-host translation layer exists
                       statically_inferred  <- requirement.assumptions
```

`statically_inferred` ceilings at `COUNTEREXAMPLE`. Those parameters were read
out of a log excerpt in a GitHub issue, not out of the title's binary - the
contract says so in its own `extraction_limitations`. Measuring the host
raised the host's half of the argument and left the program's half exactly
where it was.

So the honest reading of `UNSUPPORTED / COUNTEREXAMPLE` is: *a platform-legal
execution reaches a fatal assertion, given a requirement we believe but have
not verified against the binary.* Reaching `PROVEN` needs the other half
measured too. The ceiling caught a claim this document had already made in
prose, which is the second time on this page that the cap has been right and
the surrounding confidence has not.

---

## A finding for shadPS4

The band is refused **on native arm64 as well as under Rosetta**, with
byte-identical bounds. shadPS4 guards its `USER_MIN` workaround with
`#if defined(__APPLE__) && defined(ARCH_X86_64)` - x86-64 only. The
reservation this project measured is not x86-64-only, so that guard is
narrower than the condition it defends against.

This is an observation from one GitHub-hosted runner, not advice. It has not
been reproduced on physical hardware, the runner is virtualised, and one
machine is not a platform.

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
- **The program side of the GTA V contract is still `statically_inferred`,**
  as the section above sets out. The verdict is `COUNTEREXAMPLE`, not proof.
- **Widening applies to macOS only.** A structural refusal on Linux is refused
  *because* nothing is mapped there, so `/proc/self/maps` has nothing to widen
  to. Same-looking gap, no measurement behind it, left alone on purpose.
- **Nothing here was reproduced independently.** One runner, one workflow.

### Known-open, found while doing this

`run_campaign.sh` compares every verdict against the `expected_verdict` stored
in the contract, but that field has no host qualifier - it was recorded against
the Linux reference host. Running the 26-contract campaign against a macOS
profile therefore marks 12 rows `!` where most are simply a different host
giving a different, correct answer. On the reference host the count is the
same four rows the July campaign already documents, so nothing here regressed;
the runner is just comparing against an expectation that does not say which
machine it was written for. Expectations need to be keyed by host before the
campaign means anything on a second platform.

## Reproducing

```console
$ gh workflow run macos-probe.yml          # or push to main
$ tools/campaign/fetch_measurement.sh <sha> rosetta-x86_64
$ tools/campaign/fetch_measurement.sh <sha> native-arm64
```

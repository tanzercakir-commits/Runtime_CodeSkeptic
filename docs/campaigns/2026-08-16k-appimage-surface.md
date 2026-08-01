# The 16K AppImage surface, August 2026 — a negative result

Status: Phase 3 hunt. **External, not CI-reproducible.** The corpus scan below
was run on an external reviewer's Apple Silicon host against AppImages they
downloaded; the repository's CI does not fetch third-party AppImages. What *is*
CI-reproducible is the tool that produced it: `tools/campaign/ldso_predicate.py
--selftest` encodes the byte-exact positive control and runs on every push.

## What we went looking for

CodeSkeptic (the static sibling) earned its keep by finding real defects in real
software and reporting them upstream. The question for RuntimeSkeptic was whether
the runtime/VM layer has the same kind of live prey: a **current, maintained,
still-shipping** aarch64 program that breaks on a 16 KiB-page host (Asahi / Pi 5
16K / Apple-Silicon Linux) and **has not already been reported**. Predict it with
a contract, prove it byte-for-byte, hand it upstream.

The honest finding: **we did not find one, and the negative is the result.** The
16 KiB page-size surface in maintained desktop software is closing, and the scan
shows *where* it closed and *why* the negative is trustworthy rather than lazy.

## Two classes, two instruments (conflating them manufactures false negatives)

```
CLASS A — ELF LOAD ALIGNMENT              caught by ldso_predicate.py (static)
  ld.so refuses a bundled .so because (p_vaddr - p_offset) mod 16384 != 0.
  The app never starts. Root cause: appimagetool/patchelf packed a 4K
  assumption. Anchor: MuseScore #20342 / libsndfile.so.1.

CLASS B — ELECTRON / V8 RUNTIME          NOT caught by ldso_predicate.py
  The binary loads fine, then the renderer crashes because Chromium 132
  hard-coded a 4K page in V8's pooled-page decommit. ELF layout is normal;
  ldso_predicate gives a FALSE NEGATIVE here by design. Signal is the bundled
  Chromium VERSION, not the ELF. Broken window: Electron 34.0.2-34.2.x
  (Chromium 132-133); fixed 34.3.0+; safe <= 33.3.2.
```

Both classes now have a committed contract: `contracts/campaign/musescore-
patchelf-4k-on-16k.json` (Class A) and `contracts/campaign/electron-chromium132-
16k.json` (Class B). Both read UNSUPPORTED on `macos/aarch64+none` (the only
measured 16K profile in this tree) and SUPPORTED on every 4K host, matching the
run_campaign expectation with no `!` disagreement.

## The positive control fires — so the zeros are real

`ldso_predicate.py` is not trusted on faith. Its predicate is the glibc
`elf/dl-load.c` check `((p_vaddr - p_offset) & (pagesize - 1)) != 0`, and it is
validated byte-exact against the exact reporter asset of MuseScore #20342:

| Binary | Ver | ELF objects | refused on 16K |
|---|---|---:|---:|
| **MuseScore (positive control)** | 4.2.0-beta.2 aarch64 | 154 | **15 ✅ expected** |

`lib/libsndfile.so.1` PT_LOAD: `p_offset=0x74000 p_vaddr=0x86000 delta=0x12000`
→ `0x12000 mod 16384 = 0x2000 != 0` (rejected on 16K), `mod 4096 = 0` (loads on
4K). The scanner reproduces 15/154 on this asset. Because the instrument
detects a known offender at the byte level, a `0` from it on a current binary is
evidence of absence, not absence of evidence.

## Class A — the current corpus (9 aarch64 AppImages, all clean)

| App | Ver | ELF objects | refused on 16K |
|---|---|---:|---:|
| FreeCAD | 1.0.2 | 2056 | 0 |
| darktable | 5.6.0 | 546 | 0 |
| Cryptomator | 1.19.3 | 44 | 0 |
| Logseq | 2.0.1 | 15 (+2 x86_64) | 0 |
| Notesnook | 3.4.5 | 17 | 0 |
| Mattermost | 6.2.2 | 13 | 0 |
| Zettlr | 4.6.0 | 10 | 0 |
| Standard Notes | 3.201.35 | 10 | 0 |

**2711 ELF objects across 8 shipping aarch64 AppImages, 0 refused on 16K.** The
triage signal explains it: every current tree is built with `p_align=0x10000`
(`-z max-page-size=65536`), the default in conda-forge and current linuxdeploy.
A tree whose every PT_LOAD is 64K-congruent has no 16K surface to break. The
ecosystem closed Class A at the toolchain, which is why FreeCAD — 2056 bundled
objects, the same appimagetool shape as MuseScore — comes back clean.

## Class B — bundled Chromium version (broken window closed by time)

| App | Bundled Chrome | Electron | Verdict |
|---|---|---|---|
| Logseq 2.0.1 | Chrome/148.0.7778.180 | 42.3.0 | safe |
| Zettlr 4.6.0 | Chrome/148.0.7778.218 | 42.3.3 | safe |
| Mattermost 6.2.2 | Chrome/146.0.7680.179 | 41.2.0 | safe |
| Notesnook 3.4.5 | Chrome/138.0.7204.25x | ~37 | safe |
| Cryptomator 1.19.3 | (Electron, post-window) | — | safe |

Every bundled Chromium is ≥ 138 — far past the 132-133 broken window. The three
known Class-B offenders (VSCode #242742, Obsidian forum, Vesktop #1125) are all
already reported and fixed. A novel Class-B report would need an app *still*
shipping a 34.0.2-34.2.x Electron with no 16K issue of its own; none in this set
qualifies.

## Skipped, with the reason stated (no silent truncation)

- **Ferdium** — no arm64 AppImage in the last 30 releases (x86_64 only).
- **Kdenlive, Krita, Audacity, OpenSCAD, Inkscape, Blender** — publish no
  aarch64 AppImage at all. The narrowing that made the hunt tractable (few
  projects ship aarch64 AppImages) is also what shrank the reportable surface.

## Side-finding (real, but a different class — out of RuntimeSkeptic's model)

Logseq 2.0.1's **arm64** AppImage carries two **x86_64** ELF objects inside the
arm64 package:

```
resources/app.asar.unpacked/node_modules/@zvec/bindings-linux-x64/zvec_node_binding.node        [x86_64]
resources/app.asar.unpacked/node_modules/@zvec/bindings-linux-x64/libzvec_diskann_plugin.so     [x86_64]
```

A genuine packaging defect — this native binding can never load on arm64 — but
it is an **architecture** mismatch, not a page-size class. RuntimeSkeptic models
"what does this VM requirement do on this host," not "is this the right ISA."
Noted here so it is not lost; it is separate work, not a 16K finding.

## Honest limits

- **Static, not live.** ELF layout (Class A) and bundled version (Class B) are
  strong static evidence, but neither is a live 16K run. A reportable finding
  still wants a matching open upstream issue or an actual crash on
  Asahi/Pi5/Apple-Silicon-Linux; a bare static prediction is HYPOTHESIS and is
  not filed.
- **Not CI-reproducible.** The corpus scan needs the AppImages downloaded; CI
  does not fetch them. Only the `--selftest` positive control runs in CI. This
  note records numbers taken on an external host, credited below.
- **RuntimeSkeptic PREDICTS, it does not SCAN.** `ldso_predicate.py` (Class A)
  and the version check (Class B) find candidates; RuntimeSkeptic answers "what
  does this requirement do on this host" with cross-layer evidence. Two jobs.

## What the negative is worth

A campaign that produces only green ticks measured nothing — and a hunt that
finds nothing, with a validated instrument, measured something real: the
page-size class this tool was first pointed at is **largely closed** in
maintained software, by `-z max-page-size=65536` (Class A) and by time (Class
B). That is a finding about the tool's home, not a failure of it. RuntimeSkeptic's
live value is **pre-flight prevention** — telling a maintainer *before* they ship
that a build knob or a bundled runtime will break a host they did not test — and
the novel *findings* live at the margins the ecosystem has not yet closed:
bleeding-edge hosts, less-maintained software, and other VM classes (W^X/JIT,
reserved-VA, pointer truncation). The next hunt goes there.

## Credit

The `ldso_predicate.py` predicate, its byte-exact validation against MuseScore
#20342, and this 9-app / 5-Electron corpus scan were produced by an external
reviewer running the tool on their own Apple Silicon host. They also corrected
two defects in an earlier draft of the hunt (keying off first-segment `p_align`
instead of the per-segment `(p_vaddr - p_offset)` congruence, which false-
negatives a known-broken binary; and x86_64's `0x1000` being normal rather than
a signal). Both corrections are baked into the tool and the contract.

## Sources

- glibc `elf/dl-load.c` — the loader predicate `(p_vaddr - p_offset) & (pagesize - 1)`
- [MuseScore #20342](https://github.com/musescore/MuseScore/issues/20342) — Class A anchor (closed 2023-12-07)
- [electron#45560](https://github.com/electron/electron/issues/45560) — Class B, 16K renderer crash
- [Vesktop#1125](https://github.com/Vencord/Vesktop/issues/1125) — fixed in Electron 34.3.0
- [vscode#242742](https://github.com/microsoft/vscode/issues/242742) — Class B in VSCode
- [Asahi Linux — Broken Software](https://asahilinux.org/docs/sw/broken-software/)

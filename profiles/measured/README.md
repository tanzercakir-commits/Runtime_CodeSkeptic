# Measured profiles

Profiles in this directory were produced by `rs-env-probe` on a real host and
copied here unmodified. Every fact in them is `measured_capability` or
stronger. They are not hand-authored, and nothing in them may be edited by
hand - a profile whose `profile_id` no longer matches its own facts is not a
measurement of anything.

Check that with:

```console
$ rs-profile verify PROFILE.json     # schema
$ rs-profile id PROFILE.json         # must equal the stored profile_id
```

Contrast with `profiles/fixtures/`, which holds hand-authored documents used
to exercise the analyzer. Those are capped at `PREDICTIVE` or
`OBSERVED_INVARIANT` on purpose, and they have been wrong before - see
`docs/campaigns/2026-07-macos-measurement.md`, where measurement contradicted
a fixture's W^X claim on the Rosetta lane.

## Provenance

| file | host | source |
|---|---|---|
| `macos-14-arm64-native.measured.json` | macOS 14.8.7, Apple Silicon, native arm64 | `macos-probe.yml`, run `31084206595`, commit `21fe3c4` |
| `macos-14-arm64-rosetta-x86_64.measured.json` | macOS 14.8.7, Apple Silicon, x86-64 under Rosetta 2 | `macos-probe.yml`, run `31084931185`, commit `fc2f075` |
| `windows-server-2025-x86_64.measured.json` | Windows Server 2025 (10.0.26100), x86-64 native | `windows-probe.yml`, run `31084204325`, commit `21fe3c4` |
| `wine-9.0-on-linux-x86_64.measured.json` | Wine 9.0 on a Linux x86-64 host — **not Windows**; the facts were measured through Wine's `ntdll`, so it does not count as the Windows platform family | `docs/real-world-test-playbook.md` |

The provenance run-metadata is best-effort and partial in v0.2, and the files
say so rather than pretend otherwise: `run_id` is recorded on the macOS lanes
and empty on the Windows and Wine ones, and `probe_binary_hash` is empty on all
of them — the probe does not yet hash its own image. None of these fields is
part of `profile_id` (they are excluded by design, see
`docs/architecture/determinism.md`), so their absence changes no identity and no
verdict; it only means the "which probe binary took this" link is not yet
populated. <!-- checked: 2026-08-06 -->

An earlier pair taken at `2d9ea6c` was withdrawn rather than corrected. They
carried a `min_map_address` that was really the ASLR slide of the probe's own
image, and a measured profile someone fixed up by hand is not a measurement.
These come from a run that passes the cross-process reproducibility gate.

Both runners are GitHub-hosted and virtualised. One machine is not a platform;
read the limitations section of the campaign document before treating these as
representative of macOS.

The originals remain published as git refs, which is the record of when they
were taken:

```console
$ tools/campaign/fetch_measurement.sh 21fe3c4 native-arm64
$ tools/campaign/fetch_measurement.sh fc2f075 rosetta-x86_64
$ tools/campaign/fetch_measurement.sh 21fe3c4 windows-x86_64
```

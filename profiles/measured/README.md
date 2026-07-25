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
| `macos-14-arm64-native.measured.json` | macOS 14.8.7, Apple Silicon, native arm64 | `.github/workflows/macos-probe.yml`, job `native-arm64`, commit `67dfbd8` |
| `macos-14-arm64-rosetta-x86_64.measured.json` | macOS 14.8.7, Apple Silicon, x86-64 under Rosetta 2 | same workflow, job `rosetta-x86_64`, commit `67dfbd8` |

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
$ tools/campaign/fetch_measurement.sh 67dfbd8 rosetta-x86_64
```

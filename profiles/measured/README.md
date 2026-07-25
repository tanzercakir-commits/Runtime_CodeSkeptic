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

> **Empty right now, on purpose.** The two macOS profiles taken at `2d9ea6c`
> were withdrawn: they carry a `min_map_address` fact that the probe should
> never have recorded, and under Rosetta that value moved ~48 MiB between two
> runs of the same CI job because it was really the ASLR slide of the probe's
> own image. See `docs/campaigns/2026-07-macos-measurement.md`. They will be
> replaced from a run of the corrected probe rather than edited - a measured
> profile that someone fixed up by hand is not a measurement.

Both runners are GitHub-hosted and virtualised. One machine is not a platform;
read the limitations section of the campaign document before treating these as
representative of macOS.

The originals remain published as git refs, which is the record of when they
were taken:

```console
$ tools/campaign/fetch_measurement.sh 2d9ea6c953178f2c38f907347877240ac8a187cf rosetta-x86_64
```

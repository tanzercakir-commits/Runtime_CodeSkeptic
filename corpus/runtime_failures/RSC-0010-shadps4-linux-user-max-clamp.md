---
id: RSC-0010
title: Observed placement of the executable on one host family frozen into a compile-time address-space ceiling
category: platform-observed behavior treated as guaranteed behavior
layers_involved:
  - application
  - compatibility_layer
  - operating_system
  - kernel
evidence_available:
  - statically_inferred
  - measured_capability
  - heuristic_risk
  - unknown
finding_ids:
  - RS-VM-0008
  - RS-VM-0017
status: draft
provenance: public_report
source: shadps4-emu/shadPS4@d392abe — src/core/address_space.cpp:45-53
---

# RSC-0010 — Observed placement of the executable on one host family frozen into a compile-time address-space ceiling

## Provenance notice

This entry uses `provenance: public_report`; its source is a specific public commit, which section 3.6 covers. Every claim about program behavior below is a claim about the contents of `shadps4-emu/shadPS4@d392abe`, cited with `file:line` and checkable by reading the file.

- **Verified:** what the shadPS4 source says and does; and the `max_user_address` value measured by `rs-env-probe` on this CI host.
- **NOT verified:** that any bug report, GitHub issue, CVE, forum post, user report, or affected game title exists. **None is cited here**, because none was confirmed.
- **NOT verified:** that Linux in general places the shadPS4 executable near `0x550000000000`. That is **shadPS4's own source comment**, describing what its authors observed, not a documented kernel contract.

`status` remains `draft`: what is verifiable is source code, not an incident.

## Summary

Where RSC-0009 is about a *lower* bound moved to dodge an obstacle, this entry is about an *upper* bound frozen from an observation. Both live in the same twenty lines of the same file, which is why they are worth reading together.

shadPS4 selects the top of its guest user area at compile time, per operating system:

```cpp
// src/core/address_space.cpp:45-53
#if defined(__linux__)
// Linux maps the shadPS4 executable around here, so limit the user maximum
constexpr VAddr USER_MAX = 0x54FFFFFFFFFFULL;
#elif defined(__FreeBSD__)
// FreeBSD address space is extremely volatile, keep this lower for safety.
constexpr VAddr USER_MAX = 0xFFFFFFFFFFFULL;
#else
constexpr VAddr USER_MAX = 0x5FFFFFFFFFFFULL;
#endif
```

Three different ceilings, chosen by `#ifdef`, each justified by an observation about where the host *tends* to put things. The Linux comment (line 46) states the mechanism outright: the ceiling exists because the loader was seen placing the executable above it.

The failure mode this category names is not "the constant is wrong". It is that a *distribution* of observed behaviors has been collapsed into a *single* compile-time constant, so the program's model of the host is correct only for the configurations its authors happened to sample.

## Failure chain

| Layer | What happens |
| --- | --- |
| `application` | shadPS4 assumes the usable user area ends at `USER_MAX`, fixed at compile time |
| `compatibility_layer` | no translation layer; the ceiling directly bounds every guest address |
| `operating_system` | the actual top of usable space, and the actual placement of the executable, vary by kernel, distribution, ASLR entropy, PIE settings and `ulimit` |
<!-- checked: 2026-07-25 -->
| `kernel` | on a host where the executable lands *below* `USER_MAX`, the reservation overlaps it; on a host with a *larger* usable space, guest memory that should exist is unreachable |
| `application` | the first case reaches `throw std::bad_alloc{}` (`address_space.cpp:680`); the second is silent under-provisioning with no sink at all |

The second outcome has no failure sink. Nothing in the program observes that its self-imposed ceiling is lower than the host's real one.

## Why this is `platform-observed behavior treated as guaranteed behavior`

The taxonomy's discriminator applies cleanly. Each constant encodes an empirical claim:

| Constant | Line | Empirical claim frozen into it |
| --- | --- | --- |
| `USER_MAX = 0x54FFFFFFFFFF` | `:47` | "Linux maps the shadPS4 executable around here" (`:46`) |
| `USER_MAX = 0xFFFFFFFFFFF` | `:50` | "FreeBSD address space is extremely volatile" (`:49`) |
| `USER_MAX = 0x5FFFFFFFFFFF` | `:52` | no comment; the default for everything else, including macOS |

None of the three is a documented platform guarantee. The FreeBSD comment is candid that the number is a safety margin against variance rather than a boundary.

The same pattern appears again on Windows, and there it is at least version-aware (`address_space.cpp:127-144`): shadPS4 calls `RtlGetVersion` and clamps `supported_user_max` to `0x10000000000` on builds at or below 22621, or when the title's compiled SDK version is at least `FW_300`. That is the same class of assumption, but tested at runtime instead of at compile time — which is the remediation this entry points toward for the POSIX ceilings.

## The measured contrast

`rs-env-probe` measured this CI host (written to `profiles/generated/`, which `.gitignore` excludes because probe output is not a fixture; regenerate with `rs-env-probe vm`, kernel 6.18.5):

```json
"max_user_address": {
  "value": "0x7ffffffff000",
  "evidence": "measured_capability",
  "source": "rs-env-probe vm (linux): binary search with MAP_FIXED_NOREPLACE"
}
```

Comparing against shadPS4's compile-time constant, with the inclusive/half-open conversion made explicit:

| Quantity | Value |
| --- | --- |
| shadPS4 `USER_MAX` (inclusive, `:47`) | `0x54FFFFFFFFFF` |
| as a half-open exclusive end (`+1`) | `0x550000000000` |
| measured host `max_user_address` (exclusive) | `0x7FFFFFFFF000` |
| headroom left unused | `0x2AFFFFFFF000` ≈ 43 TiB |

On this host shadPS4's ceiling is **conservative by roughly 43 TiB**. That is the benign direction of the error, and it is invisible: no error, no log line, no assert. The dangerous direction — a host whose executable lands below `0x550000000000` — is not exercised here and cannot be ruled out from this measurement.

Note also that the probe used `MAP_FIXED_NOREPLACE` to find this bound non-destructively. shadPS4 has no such call anywhere in the tree; it uses only destructive `MAP_FIXED` (`address_space.cpp:639`, `:751`, `:774`). The tool measuring the host is strictly more careful than the program relying on the measurement.

## The one place shadPS4 does relocate

For completeness, because it is the only fallback in the file and it contradicts the identity assumption everywhere else:

```cpp
// src/core/address_space.cpp:666-674
// FreeBSD can't stand MAP_FIXED or it may overwrite mmap() itself!
// Map memory wherever possible and instruction translation can handle offsetting to the
// base.
map_flags &= ~MAP_FIXED;
const auto virtual_base =
    reinterpret_cast<u8*>(mmap(nullptr, virtual_size, protection_flags, map_flags, -1, 0));
system_managed_base = virtual_base;
system_reserved_base = virtual_base + SYSTEM_RESERVED_MIN - SYSTEM_MANAGED_MIN;
user_base = virtual_base + USER_MIN - SYSTEM_MANAGED_MIN;
```

On non-x86-64 (and FreeBSD) targets, `MAP_FIXED` is dropped, the kernel chooses the base, and the bases become *offsets* from it. In RuntimeSkeptic's vocabulary this branch has `translation_layer_available: true` and permits `relocate` — the JIT absorbs the delta. The x86-64 branch two lines above (`:658-664`) has neither. **The same program is a different contract depending on which branch compiles**, which is precisely why a requirement document must name its configuration. The four contracts in `contracts/shadps4-*.json` each name theirs.

The comment on line 666 also documents that destructive `MAP_FIXED` can overwrite the C library's own mappings — an unusually direct statement of the hazard RSC-0009 describes.

## Inference, explicitly labelled as inference

**The following is inference, not fact.** The comment "Linux maps the shadPS4 executable around here" (`:46`) reads as a description of something observed rather than something looked up, and the constant is expressed to full 48-bit precision. It is *plausible* that this constant was tuned in response to a concrete startup failure.

**No such report was confirmed.** No issue number, CVE, user report or game title is claimed, and none should be added without a verified link.

## What RuntimeSkeptic says

| Contract | Profile | Verdict | Exit | Finding |
| --- | --- | --- | --- | --- |
| `shadps4-user-area-base.json` | `linux-x86_64` (measured) | `UNKNOWN` | 3 | RS-VM-0017 |
| `shadps4-system-managed-area.json` | `linux-x86_64` (measured) | `UNKNOWN` | 3 | RS-VM-0017 |

`UNKNOWN`, not `SUPPORTED`, and the reason is worth stating because it is the evidence model working as designed. The probe's `available_ranges` are 35 small windows of a few MiB each; the smaller of these two requests is `0x7FFBFC000` (≈32 GiB) and the larger is `0x54F000000000` (84.94 TiB). `query_range()` requires an available range to **fully contain** the request, and partial containment yields `UNKNOWN` (`src/vm/profile.cpp`, step 3). The host bounds are satisfied — `0x550000000000 < 0x7FFFFFFFF000` — so nothing is refuted either.

The honest reading: *this host does not contradict shadPS4's ceiling, and nothing here establishes that a multi-terabyte reservation would succeed.* Deciding it requires a probe that attempts reservations at that scale, which `rs-env-probe` v0.1 does not do.

## What would upgrade this entry

1. Extend `rs-env-probe` to attempt large reservations, so these two requests resolve instead of returning `UNKNOWN`.
2. A profile from a Linux host that places its executable below `0x550000000000`, turning the dangerous direction from hypothetical into `measured_capability`.
3. A verified public report of a startup failure attributable to this ceiling → `provenance: public_report`, `status: sourced`.

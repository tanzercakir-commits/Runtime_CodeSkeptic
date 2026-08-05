---
id: RSC-0008
title: File-backed mapping longer than the file succeeds at map time and faults on access to the tail
category: unsupported exact capability
layers_involved:
  - application
  - library
  - syscall_interface
  - operating_system
  - kernel
evidence_available:
  - specified_guarantee
  - unknown
finding_ids:
  - RS-VM-0018
status: draft
provenance: pattern_reconstruction
---

# RSC-0008 — File-backed mapping longer than the file succeeds at map time and faults on access to the tail

This entry describes a failure pattern reconstructed from general systems knowledge. It is not a citation of a specific reported incident. Replace with a verified incident (with source link) before counting it toward the Phase 0 exit criteria.

## Summary

A storage engine maps a file with a length taken from a header field, a configured maximum, or a rounded-up estimate, rather than from the file's actual size. The mapping call succeeds — mapping past end-of-file is not an error. Reading the region beyond the last page that the file backs faults. The program treats the mapping as an array of bytes it may address freely, because the mapping call told it so, and there is no API that says otherwise at the point where the decision is made.

The same failure has a second, nastier form: the mapping is created legitimately over a complete file, and the file is then truncated by another process. Pages that were valid become unbacked while the mapping is live. Nothing in the mapping's interface changes.

## Failure chain

| Layer | What happened |
| --- | --- |
| `application` / `library` | maps a length derived from a header, a configuration value, or an estimate, and treats the whole range as readable |
| `syscall_interface` | mapping past end-of-file succeeds; the length is not validated against the file size |
| `operating_system` | pages within the file's extent are backed; pages beyond it are not |
| `kernel` | an access to an unbacked page of a file mapping raises a fault |
| `application` | faults on a load in code that has no fault handling and no reason to expect one |

## What the program required

```c
// Length from the file's own header, or from configuration. Not from stat().
size_t map_len = round_up(header.data_extent, page_size);

void* base = mmap(NULL, map_len, PROT_READ, MAP_SHARED, fd, 0);
if (base == MAP_FAILED)
    return IO_ERROR;

// Every byte in [base, base + map_len) is now treated as readable.
const record* r = record_at(base, index);
return r->key;                 // may fault, with no error path
```

The requirement is stronger than "a mapping of `map_len` bytes exists":

> Every byte in `[base, base + map_len)` may be read without fault, for as long as the mapping is live.

The mapping call does not promise that, and cannot: it does not know what the file's length will be at the moment of the access.

## What the environment provided

Mapping beyond end-of-file is permitted and succeeds. The behavior on access to the region past the file's last backed page is host-specific:

| Behavior | Consequence for the program |
| --- | --- |
| fault (bus error) | the process dies, or must install a fault handler — neither is what the code expects |
| error at access | recoverable in principle, but the access is a load, not a call, so there is no return value to check |
| zero fill | silently returns zeros, which the program parses as valid data |

The profile models this as `virtual_memory.file_map_beyond_eof` with values `sigbus`, `error`, `zero_fill` or `unknown`. Evidence class `specified_guarantee` where the platform documents it.

The zero-fill case deserves emphasis: it is the one where nothing appears to go wrong. A record structure read from zeroed bytes has a zero key, a zero length and a zero type. Depending on the format, that is either an obvious corruption or a perfectly plausible empty record, and the second is a silent data-integrity failure.

The truncation variant adds a further platform property: whether a mapping's backing can be revoked while the mapping is live. On hosts where it can, the file's length at map time proves nothing about the length at access time.

## Why the mismatch is not detected at the call site

- the mapping call succeeds and returns a valid pointer for the full requested length;
- there is no field in the result that reports how much of the mapping is actually backed;
- the program never calls `stat()`, because the length it used came from a source it trusts more than the filesystem — its own header;
- the failing operation is a load, not a function call: there is no return value, no `errno`, and no place to put a check.

## Manifestation

- a bus error or access violation on a read from a mapping created successfully, in code with no memory-related logic;
- on zero-filling hosts, records that parse as empty or as zero-keyed, producing wrong query results with no error anywhere;
- failures that depend on the file's size at a moment in time, so they reproduce only with a specific data set and disappear when the file is copied or rebuilt;
- in the truncation variant, a crash in a reader process caused entirely by a writer process, with nothing in the reader's own history to explain it.

## Classification

**Primary category:** *unsupported exact capability*. The capability the program requires — `map_len` bytes of fault-free access derived from a file of length `file_len < map_len` — does not exist on any host. The tail bytes are not backed and cannot be made backed by any mapping parameter.

**Secondary categories:**
- *temporal contract violation* — the map succeeds and the access fails later, so the checkable failure point and the actual failure point are separated in time; in the truncation variant they are separated by an event in another process;
- *semantic success violation* — a success return accompanied a postcondition (all mapped bytes readable) that was false;
- *platform-observed behavior treated as guaranteed behavior* — on a zero-filling host the code appears correct indefinitely, and the appearance is not a contract.

**Finding ID:**

| ID | Why |
| --- | --- |
| `RS-VM-0018` | file-backed mapping extends beyond end of file — the registered ID for this contradiction |

> **`RS-VM-0018` is emitted and execution-paired.** `rule_file_mapping_beyond_eof()` reads `file_backed`, `file_length`, and `eof_access_extent` together with the profile's `file_map_beyond_eof` fact. Ground-truth cases prove the final partial page is addressable and pair a whole-page overrun with the analyzer verdict.

## Why this is not an ordinary memory bug

Apply the discriminator: the same source is correct when `map_len <= file_len` and incorrect when it is not, and on a zero-filling host it is *silently* incorrect rather than fatal, with no undefined behavior in either case.

The access is in bounds of the mapping. The pointer is valid. The arithmetic is right. AddressSanitizer models the mapping as a valid region — because it is one — and reports nothing. When the fault arrives it arrives from the kernel, not from the language runtime, and it names an address that every tool agrees is legitimately mapped.

The defect is a mismatch between two lengths that live in different subsystems: the length the program mapped, and the length the filesystem backs.

## Implemented diagnosis

**Requirement facts:** `file_backed: true`, `size`, `file_length`, and `eof_access_extent`.

**Environment fact:** `virtual_memory.file_map_beyond_eof`.

The analyzer emits `RS-VM-0018` only when those inputs make the static extent contradiction decidable. The ground-truth harness executes the final-partial-page and whole-page-overrun boundaries against the host kernel, then checks the case-specific outcome/verdict oracle. If a source extractor supplies a `statically_inferred` length relation, the evidence ceiling still limits confidence to `COUNTEREXAMPLE`; a hand-authored guarantee can reach `PROVEN`.

A different process truncating the file after mapping is intentionally not inferred from two static documents. That ordering-sensitive variant belongs to the Phase 7 file-backed mapping lifecycle monitor.

## Remediation classes

None of the registered `RemediationClass` values fits well, which is itself a finding about the enum. The applicable remediations are:

| Remediation | Note |
| --- | --- |
| derive the mapped length from the file's actual size | the direct fix; `min(header_extent, stat_size)` rounded to a page |
| validate the header extent against the file size before mapping | converts a fault into a checkable error at a point the program controls |
| handle the fault | install a handler for the platform's beyond-EOF signal; correct but invasive, and it does not help on zero-filling hosts where there is no fault |
| extend the file before mapping | where the program owns the file, making the requested length real removes the contradiction |
| do not map shared files that other processes may truncate | the only reliable answer to the truncation variant |

The closest existing class is `select_different_host_configuration`, which is unhelpful here: the problem is not the host's beyond-EOF behavior but the program's assumption about length. A new class along the lines of `derive_extent_from_resource` would be the honest addition, and is noted here rather than invented in the enum.

## Open questions

- Should the three `BeyondEofBehavior` values produce different severities? Silent zero-fill may be more dangerous than a synchronous fault.
- Which Phase 7 event should establish post-map truncation without confusing it with the already-decided static extent case?
- **Unverified:** the storage-engine structure, header field, and failure ordering are illustrative. This entry is a reconstruction, not a report.

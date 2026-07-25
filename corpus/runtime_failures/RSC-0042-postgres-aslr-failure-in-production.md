---
id: RSC-0042
title: The same address-space collision, a year later, as a recurring production failure
category: unsupported exact capability
layers_involved: [application, operating_system]
evidence_available: [specified_guarantee, unknown]
finding_ids: [RS-VM-0001]
status: sourced
provenance: public_report
source: https://www.mail-archive.com/pgsql-hackers@postgresql.org/msg231260.html
verified: 2026-07-25
---

# RSC-0042 — The same address-space collision, a year later, as a recurring production failure

**Source:** [[HACKERS] [bug fix] postgres.exe fails to start on Windows Server 2012 due to ASLR](https://www.mail-archive.com/pgsql-hackers@postgresql.org/msg231260.html)
**Fetched and read on 2026-07-25.** Quoted below verbatim from that page.

> This is caused by the improved ASLR (Address Space Layout Randomization) in Windows 8/2012. The problem was analyzed in a previous discussion but not addressed.

## Summary

As RSC-0041: an identical shared-memory base in every backend process. Windows
Server 2012 R2 ASLR. A log line every few minutes for a year.

## What the program required

As RSC-0041: an identical shared-memory base in every backend process.

## What the environment provided

Windows Server 2012 R2 ASLR. Roughly one failure every three minutes on a
customer's production system.

## Why the mismatch is not detected at the call site

Each individual failure is logged and each is survivable, so nothing
escalates. The process that died happened to be an autovacuum worker.

## Manifestation

A log line every few minutes for a year. The reporter's own note is the point:
'However, this can happen with any backend process.'

## Classification

Primary category: **unsupported exact capability**.
Layers: application → operating_system.
Finding ids that would diagnose it: `RS-VM-0001`.

## Why this is not an ordinary memory bug

Recorded separately from RSC-0041 deliberately: same contradiction, but this
is the field incident rather than the lab analysis, and it shows a
probabilistic address-space failure surviving a year in production because its
symptom was tolerable.

## What RuntimeSkeptic would need

Same as RSC-0041. The additional need is a way to express a *probability* -
the model is binary, and this failure is not.

## Remediation classes

- `SelectDifferentHostConfiguration` — run on a host configured differently

## Open questions

How a compatibility analyzer should report a contradiction that manifests 2%
of the time. `CONDITIONALLY_SUPPORTED` is the closest verdict and is not
obviously right.

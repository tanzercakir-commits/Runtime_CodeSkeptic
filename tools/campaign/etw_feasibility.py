#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Summarize a tracerpt XML dump: what did ETW actually record?

T-018 needs the false-positive campaign on a second OS, and on Windows the
instrument is ETW (`strace` does not exist there). Every document about the
kernel VirtualAlloc events describes their fields differently, and this
project has been burned before by writing a parser against documentation
instead of against the data: the mingw flag list typed from memory, the
`ctest` invocation that was correct on every platform but the one it ran on.

So the first CI round does not try to build the observer. It records a short
kernel trace around one busy Python process, decodes it with `tracerpt`, and
this script reports what is ACTUALLY in the XML:

    - a histogram of events by (provider-ish name, opcode/type)
    - the field names each event kind carries
    - a few raw samples of anything that looks allocation-related, verbatim

The observer gets written against this output, not against MSDN. One runner
minute now instead of five blind parser iterations later.

USAGE
    tools/campaign/etw_feasibility.py TRACERPT.xml [--max-samples N]

Exits 0 even on an empty or surprising file: a feasibility probe that fails
CI on the finding it was sent to make is not a probe.
"""
import argparse
import collections
import sys
import xml.etree.ElementTree as ET


def local(tag):
    """Strip the XML namespace: '{ns}Event' -> 'Event'."""
    return tag.rsplit("}", 1)[-1]


def find_local(elem, name):
    for child in elem.iter():
        if local(child.tag) == name:
            return child
    return None


def event_identity(event):
    """Best-effort (name, opcode) for classic kernel events.

    tracerpt renders classic MOF events differently across Windows builds:
    the useful name sometimes sits in RenderingInfo/EventName, sometimes in
    System/Provider@Name plus System/Opcode, sometimes only as a Task. Take
    everything and let the histogram show which fields exist here.
    """
    provider = task = opcode = eventname = ""
    for child in event.iter():
        tag = local(child.tag)
        if tag == "Provider":
            provider = child.get("Name") or child.get("Guid") or ""
        elif tag == "Task":
            task = (child.text or "").strip()
        elif tag == "Opcode":
            opcode = (child.text or "").strip()
        elif tag == "EventName":
            eventname = (child.text or "").strip()
    return provider, task, opcode, eventname


def data_fields(event):
    names = []
    for child in event.iter():
        if local(child.tag) == "Data":
            names.append(child.get("Name") or "(unnamed)")
    return tuple(names)


ALLOC_HINTS = ("alloc", "virt", "pagefault", "mem", "reserve", "commit")


def looks_allocation_related(identity, fields):
    text = " ".join(identity + tuple(fields)).lower()
    return any(h in text for h in ALLOC_HINTS)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("xml")
    ap.add_argument("--max-samples", type=int, default=8)
    args = ap.parse_args()

    histogram = collections.Counter()
    fields_by_kind = {}
    samples = []
    total = 0
    parse_error = None

    try:
        # iterparse: the file can be large and only the structure matters.
        for _, elem in ET.iterparse(args.xml):
            if local(elem.tag) != "Event":
                continue
            total += 1
            identity = event_identity(elem)
            fields = data_fields(elem)
            key = (identity, fields)
            histogram[key] += 1
            fields_by_kind.setdefault(key, fields)
            if (looks_allocation_related(identity, fields)
                    and len(samples) < args.max_samples):
                samples.append(ET.tostring(elem, encoding="unicode")[:2000])
            elem.clear()
    except ET.ParseError as exc:
        # Report and continue to the summary: a truncated file still shows
        # what the events look like, which is the whole point.
        parse_error = str(exc)

    print(f"events parsed: {total}")
    if parse_error:
        print(f"XML parse stopped early: {parse_error}")
    print()
    print("=== histogram: (provider, task, opcode, eventname) -> count ===")
    for (identity, fields), n in histogram.most_common(40):
        print(f"{n:8d}  {identity}")
        print(f"          fields: {list(fields)}")
    print()
    print(f"=== raw samples of allocation-looking events "
          f"({len(samples)}) ===")
    for s in samples:
        print(s)
        print("---")
    if total == 0 and not parse_error:
        print("the XML contained no Event elements at all - either the trace "
              "recorded nothing or tracerpt wrote a different schema. The "
              "first 500 bytes of the file:")
        try:
            with open(args.xml, "rb") as fh:
                sys.stdout.write(repr(fh.read(500)))
                print()
        except OSError as exc:
            print(f"(could not re-read the file: {exc})")
    return 0


if __name__ == "__main__":
    sys.exit(main())

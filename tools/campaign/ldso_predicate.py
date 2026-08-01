#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Does an ELF binary's PT_LOAD layout survive ld.so on a page size of N?

This is a static requirement extractor for one incompatibility class:
RSC-0053 / the MuseScore #20342 shape, where a shared object built or rewritten
for a 4 KiB page cannot be mapped by the dynamic loader on a 16 KiB-page host.

THE PREDICATE IS NOT p_align. It is the glibc check in elf/dl-load.c:

    if (((ph->p_vaddr - ph->p_offset) & (GLRO(dl_pagesize) - 1)) != 0)
        errstring = N_("ELF load command address/offset not page-aligned");

so for each PT_LOAD segment this computes (p_vaddr - p_offset) mod page_size and
flags any that is non-zero. A binary can have a first PT_LOAD aligned to 0x10000
and still fail, because a LATER segment - the one patchelf touched - is only
4 KiB-congruent. Keying off the first segment's p_align, as an earlier draft of
the hunt prompt did, produces a FALSE NEGATIVE on a known-broken binary; that
mistake, and the byte-exact fix, are why this tool exists as code rather than a
one-liner.

VALIDATED. An external reviewer wrote this predicate and validated it byte-exact
against the exact reporter asset of MuseScore #20342
(MuseScore-4.2.0.233381325-experimental-aarch64.AppImage): 15 of 154 ELF objects
refused on 16 KiB, 0 on 4 KiB, and lib/libsndfile.so.1 PT_LOAD with
p_offset=0x74000, p_vaddr=0x86000, delta=0x12000 -> 0x12000 mod 16384 = 0x2000.
That case is preserved as `--selftest` below, so the logic cannot regress
without the AppImage in hand.

CHEAP TRIAGE. p_align is still worth reading as a first-pass SIGNAL, not a
verdict: a tree whose every PT_LOAD is p_align=0x10000 (built with
`-z max-page-size=65536`, as conda-forge and current linuxdeploy do) has no 16K
surface at all and can be skipped; only p_align=0x1000 warrants the full
(p_vaddr - p_offset) check. This tool reports both.

WHAT IT DOES NOT CATCH. Runtime page-size assumptions - the Electron/Chromium
132 class, where the binary loads fine and V8 crashes later - are invisible
here. That class is detected by the bundled Chromium version, not by ELF layout.

USAGE
    tools/campaign/ldso_predicate.py PATH [PATH ...]   # files or trees
    tools/campaign/ldso_predicate.py --selftest
"""
import os
import struct
import sys

PAGES = (4096, 16384, 65536)          # 4K, 16K, 64K
PT_LOAD = 1
E_MACHINE = {0x03: "x86", 0x28: "arm", 0x3E: "x86_64", 0xB7: "aarch64",
             0xF3: "riscv"}


def ld_violation(p_offset, p_vaddr, page):
    """The glibc elf/dl-load.c test, exactly: the loader refuses the segment
    when (p_vaddr - p_offset) is not congruent to 0 modulo the host page."""
    return ((p_vaddr - p_offset) & (page - 1)) != 0


def read_loads(path):
    """Return (machine_name, [(p_offset, p_vaddr, p_align), ...]) for an ELF, or
    (None, None) if the file is not an ELF this tool reads."""
    try:
        with open(path, "rb") as fh:
            data = fh.read()
    except OSError:
        return None, None
    if len(data) < 64 or data[:4] != b"\x7fELF":
        return None, None
    ei_class, ei_data = data[4], data[5]
    if ei_data != 1:                  # little-endian only; big-endian ELF is
        return None, None             # not something these hosts ship
    machine = E_MACHINE.get(struct.unpack_from("<H", data, 18)[0], "?")
    loads = []
    if ei_class == 2:                 # ELF64
        e_phoff = struct.unpack_from("<Q", data, 0x20)[0]
        e_phentsize = struct.unpack_from("<H", data, 0x36)[0]
        e_phnum = struct.unpack_from("<H", data, 0x38)[0]
        for i in range(e_phnum):
            base = e_phoff + i * e_phentsize
            if base + 56 > len(data):
                break
            p_type = struct.unpack_from("<I", data, base)[0]
            if p_type != PT_LOAD:
                continue
            p_offset = struct.unpack_from("<Q", data, base + 8)[0]
            p_vaddr = struct.unpack_from("<Q", data, base + 16)[0]
            p_align = struct.unpack_from("<Q", data, base + 48)[0]
            loads.append((p_offset, p_vaddr, p_align))
    elif ei_class == 1:               # ELF32
        e_phoff = struct.unpack_from("<I", data, 0x1C)[0]
        e_phentsize = struct.unpack_from("<H", data, 0x2A)[0]
        e_phnum = struct.unpack_from("<H", data, 0x2C)[0]
        for i in range(e_phnum):
            base = e_phoff + i * e_phentsize
            if base + 32 > len(data):
                break
            p_type = struct.unpack_from("<I", data, base)[0]
            if p_type != PT_LOAD:
                continue
            p_offset = struct.unpack_from("<I", data, base + 4)[0]
            p_vaddr = struct.unpack_from("<I", data, base + 8)[0]
            p_align = struct.unpack_from("<I", data, base + 28)[0]
            loads.append((p_offset, p_vaddr, p_align))
    else:
        return None, None
    return machine, loads


def walk(paths):
    for p in paths:
        if os.path.isdir(p):
            for root, _dirs, files in os.walk(p):
                for name in files:
                    yield os.path.join(root, name)
        else:
            yield p


def selftest():
    # The byte-exact MuseScore #20342 positive control: libsndfile.so.1 PT_LOAD.
    assert ld_violation(0x74000, 0x86000, 16384) is True, "libsndfile must fail 16K"
    assert ld_violation(0x74000, 0x86000, 4096) is False, "and load on 4K"
    assert ld_violation(0x74000, 0x86000, 65536) is True, "and fail 64K"
    # A segment built with -z max-page-size=65536 is congruent on every page.
    assert ld_violation(0x10000, 0x210000, 65536) is False
    assert ld_violation(0x10000, 0x210000, 16384) is False
    assert ld_violation(0x10000, 0x210000, 4096) is False
    # A wholly page-shifted segment fails everywhere above its congruence.
    assert ld_violation(0x1000, 0x2800, 4096) is True   # delta 0x1800
    print("ldso_predicate selftest: OK "
          "(byte-exact against MuseScore #20342's libsndfile.so.1)")
    return 0


def main(argv):
    if argv == ["--selftest"]:
        return selftest()
    if not argv:
        print(__doc__.strip().splitlines()[-2].strip(), file=sys.stderr)
        return 64

    total_elf = 0
    align_hist = {}
    offenders = {page: [] for page in PAGES}
    for path in walk(argv):
        machine, loads = read_loads(path)
        if loads is None:
            continue
        total_elf += 1
        worst = {page: False for page in PAGES}
        for idx, (p_offset, p_vaddr, p_align) in enumerate(loads):
            align_hist[p_align] = align_hist.get(p_align, 0) + 1
            for page in PAGES:
                if ld_violation(p_offset, p_vaddr, page):
                    worst[page] = True
                    if page == 16384 and len(offenders[page]) < 200:
                        offenders[page].append(
                            f"{path} [{machine}] PT_LOAD[{idx}] "
                            f"off={p_offset:#x} vaddr={p_vaddr:#x} "
                            f"delta={(p_vaddr - p_offset):#x} "
                            f"align={p_align:#x}")
        for page in PAGES:
            if worst[page]:
                offenders.setdefault(("count", page), [0])
                offenders[("count", page)][0] += 1

    print(f"ELF objects scanned: {total_elf}")
    for page in PAGES:
        n = offenders.get(("count", page), [0])[0]
        print(f"  refused on {page // 1024:>2} KiB pages: {n} / {total_elf}")
    print("  p_align distribution (triage signal; 0x10000 = no 16K surface):")
    for align, n in sorted(align_hist.items()):
        print(f"    align {align:#08x}: {n} segment(s)")
    if offenders[16384]:
        print("\n16 KiB offenders (the reportable ones):")
        for line in offenders[16384]:
            print(f"  {line}")
    else:
        print("\nno 16 KiB offenders: this tree loads on a 16K-page host")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

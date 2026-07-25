/* SPDX-License-Identifier: Apache-2.0
 *
 * Does a hint get honoured, or does the kernel put the mapping somewhere else?
 *
 *   hinted_relocation <case-name> <size-bytes>
 *
 * POSIX is explicit that `addr` is advisory: the implementation may place the
 * mapping wherever it likes. Emulators that require guest and host addresses to
 * be equal - QEMU user mode with guest_base 0, shadPS4's direct-memory path -
 * depend on the hint being honoured and have no way to notice when it was not,
 * because mmap reports success either way. That is silent contract
 * degradation, and RS-VM-0007 and RS-VM-0008 exist for it.
 *
 * The case occupies an address, then asks for it as a hint WITHOUT MAP_FIXED.
 * `relocated` is the interesting outcome and the honest one: the call
 * succeeded, and the program that needed identity has already lost.
 *
 * This is also the only case that produces `relocated` against a real kernel.
 * Until it existed that outcome had been exercised only by the harness
 * selftest's stub.
 */
#include "gt_common.h"

#include <inttypes.h>
#include <sys/mman.h>

struct args { const char* name; size_t length; };

static void attempt(void* raw) {
    struct args* a = (struct args*)raw;

    /* Take an address the kernel chose, so we know it is valid and ours. */
    void* squatter = mmap(NULL, a->length, PROT_READ,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (squatter == MAP_FAILED) {
        gt_report(a->name, GT_SKIPPED, "could not place the first mapping: %s",
                  strerror(errno));
    }
    uint64_t wanted = (uint64_t)(uintptr_t)squatter;

    /* Now ask for the same address as a hint. No MAP_FIXED: this is the
     * advisory form, the one real programs use when they "prefer" an address. */
    void* got = mmap((void*)(uintptr_t)wanted, a->length, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (got == MAP_FAILED) {
        int e = errno;
        munmap(squatter, a->length);
        gt_report(a->name, GT_REFUSED,
                  "a hinted mapping at the occupied address %#" PRIx64
                  " failed outright: errno=%d %s",
                  wanted, e, gt_errno_name(e));
    }

    uint64_t at = (uint64_t)(uintptr_t)got;
    munmap(got, a->length);
    munmap(squatter, a->length);

    if (at != wanted) {
        gt_report(a->name, GT_RELOCATED,
                  "the hint %#" PRIx64 " was occupied, so the kernel placed the "
                  "mapping at %#" PRIx64 " instead and reported SUCCESS - a "
                  "program requiring guest/host identity has already lost here, "
                  "and no error code told it",
                  wanted, at);
    }
    gt_report(a->name, GT_SATISFIED,
              "the hint %#" PRIx64 " was honoured even though it was occupied, "
              "which mmap permits but does not promise",
              wanted);
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <case-name> <size>\n", argv[0]);
        return 64;
    }
    struct args a;
    a.name = argv[1];
    a.length = (size_t)strtoull(argv[2], NULL, 0);

    int sig = gt_run_in_child(attempt, &a);
    if (sig > 0) {
        gt_report(a.name, GT_FAULTED, "died on signal %d (%s)", sig,
                  strsignal(sig));
    }
    if (sig < 0) gt_report(a.name, GT_SKIPPED, "fork failed: %s", strerror(errno));
    return 0;
}

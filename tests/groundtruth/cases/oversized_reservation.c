/* SPDX-License-Identifier: Apache-2.0
 *
 * Ask for more address space than the machine has.
 *
 *   oversized_reservation <case-name> <size-bytes>
 *
 * QEMU's aarch64 user mode reserves MAX_RESERVED_VA = (1ul << 52) - 1. The July
 * campaign's first defect was that RuntimeSkeptic answered SUPPORTED for that,
 * with no findings at all: every placement rule opened with
 * `if (!request.address) return;` and no rule compared request.size against
 * anything. A request for four petabytes and a request for one page were
 * indistinguishable. RS-VM-0021 was written for it.
 *
 * Address-less-but-enormous is not exotic - it is how emulators, allocators and
 * JITs make their LARGEST requests, which is exactly where a wrong answer costs
 * the most. So the ground truth is worth having: does the kernel actually refuse?
 */
#include "gt_common.h"

#include <inttypes.h>
#include <sys/mman.h>

struct args { const char* name; uint64_t length; };

static void attempt(void* raw) {
    struct args* a = (struct args*)raw;

    if ((uint64_t)(size_t)a->length != a->length) {
        gt_report(a->name, GT_REFUSED,
                  "%" PRIu64 " bytes does not fit in size_t on this host, so "
                  "the request cannot even be expressed", a->length);
    }

    /* MAP_NORESERVE so this is a question about address space, not about swap. */
    void* got = mmap(NULL, (size_t)a->length, PROT_NONE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (got == MAP_FAILED) {
        gt_report(a->name, GT_REFUSED,
                  "mmap of %" PRIu64 " bytes (%.1f TiB) was refused: errno=%d "
                  "%s (%s)", a->length, (double)a->length / (1024.0*1024*1024*1024),
                  errno, gt_errno_name(errno), strerror(errno));
    }
    munmap(got, (size_t)a->length);
    gt_report(a->name, GT_SATISFIED,
              "the host actually reserved %" PRIu64 " bytes (%.1f TiB) of "
              "address space", a->length,
              (double)a->length / (1024.0*1024*1024*1024));
}

int main(int argc, char** argv) {
    if (argc != 3) { fprintf(stderr, "usage: %s <case-name> <size>\n", argv[0]); return 64; }
    struct args a;
    a.name = argv[1];
    a.length = strtoull(argv[2], NULL, 0);
    int sig = gt_run_in_child(attempt, &a);
    if (sig > 0) gt_report(a.name, GT_FAULTED, "died on signal %d (%s)", sig, strsignal(sig));
    if (sig < 0) gt_report(a.name, GT_SKIPPED, "fork failed: %s", strerror(errno));
    return 0;
}

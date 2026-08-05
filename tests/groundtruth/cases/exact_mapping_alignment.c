/* SPDX-License-Identifier: Apache-2.0
 *
 * Isolates address alignment from range availability: the aligned page
 * containing the requested base must map and retain bytes first, then the
 * otherwise-identical misaligned MAP_FIXED request must fail with EINVAL.
 */
#include "gt_common.h"

#include <inttypes.h>
#include <sys/mman.h>

struct args {
    const char* name;
    uint64_t address;
    size_t length;
};

static void attempt(void* raw) {
    struct args* a = (struct args*)raw;
    long raw_page = sysconf(_SC_PAGESIZE);
    if (raw_page <= 0) {
        gt_report(a->name, GT_SKIPPED, "sysconf(_SC_PAGESIZE) failed");
    }
    uint64_t page = (uint64_t)raw_page;
    if ((a->address % page) == 0) {
        gt_report(a->name, GT_SKIPPED,
                  "target %#" PRIx64 " is aligned on this host", a->address);
    }
    uint64_t aligned = a->address - (a->address % page);

    void* control = mmap((void*)(uintptr_t)aligned, a->length,
                         PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (control == MAP_FAILED ||
        (uint64_t)(uintptr_t)control != aligned) {
        int e = errno;
        if (control != MAP_FAILED) munmap(control, a->length);
        gt_report(a->name, GT_SKIPPED,
                  "aligned control at %#" PRIx64
                  " was unavailable: errno=%d %s (%s)",
                  aligned, e, gt_errno_name(e), strerror(e));
    }
    volatile unsigned char* p = (volatile unsigned char*)control;
    p[0] = 0x5a;
    p[a->length - 1] = 0xa5;
    if (p[0] != 0x5a || p[a->length - 1] != 0xa5) {
        munmap(control, a->length);
        gt_report(a->name, GT_SKIPPED,
                  "aligned control did not retain written bytes");
    }
    munmap(control, a->length);

    errno = 0;
    void* got = mmap((void*)(uintptr_t)a->address, a->length,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (got == MAP_FAILED) {
        int e = errno;
        if (e == EINVAL) {
            gt_report(a->name, GT_MISALIGNED_EINVAL,
                      "aligned neighbour %#" PRIx64
                      " mapped and retained bytes; the target %#" PRIx64
                      " then failed with EINVAL",
                      aligned, a->address);
        }
        gt_report(a->name, GT_REFUSED,
                  "aligned control succeeded but the target failed with "
                  "non-alignment errno=%d %s (%s)",
                  e, gt_errno_name(e), strerror(e));
    }
    munmap(got, a->length);
    gt_report(a->name, GT_SATISFIED,
              "misaligned MAP_FIXED unexpectedly succeeded at %#" PRIx64,
              a->address);
}

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s <case-name> <hex-address> <size>\n", argv[0]);
        return 64;
    }
    struct args a;
    a.name = argv[1];
    a.address = strtoull(argv[2], NULL, 16);
    a.length = (size_t)strtoull(argv[3], NULL, 0);
    if (a.length == 0) return 64;

    int sig = gt_run_in_child(attempt, &a);
    if (sig > 0) {
        gt_report(a.name, GT_FAULTED, "died on signal %d (%s)", sig,
                  strsignal(sig));
    }
    if (sig < 0) {
        gt_report(a.name, GT_SKIPPED, "fork failed: %s", strerror(errno));
    }
    return 0;
}

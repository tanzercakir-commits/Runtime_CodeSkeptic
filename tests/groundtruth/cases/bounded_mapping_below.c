/* SPDX-License-Identifier: Apache-2.0
 *
 * Enumerates every page-aligned mapping start below a measured exclusive
 * address bound. A positive control at the bound must first succeed with the
 * same MAP_FIXED_NOREPLACE call; only policy refusals below it satisfy the
 * RS-VM-0023 oracle.
 */
#include "gt_common.h"

#include <inttypes.h>
#include <stdint.h>
#include <sys/mman.h>

#if defined(__linux__)
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

struct args {
    const char* name;
    uint64_t upper;
    size_t length;
};

static void attempt(void* raw) {
    struct args* a = (struct args*)raw;
    long raw_page = sysconf(_SC_PAGESIZE);
    if (raw_page <= 0) {
        gt_report(a->name, GT_SKIPPED, "sysconf(_SC_PAGESIZE) failed");
    }
    uint64_t page = (uint64_t)raw_page;
    if (a->upper == 0 || a->upper % page != 0 ||
        a->length == 0 || a->length > a->upper ||
        a->length % page != 0 || a->upper / page > 4096) {
        gt_report(a->name, GT_SKIPPED,
                  "bound/size is unaligned, empty, or exceeds 4096 candidates");
    }

    errno = 0;
    void* control = mmap((void*)(uintptr_t)a->upper, a->length,
                         PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                         -1, 0);
    if (control == MAP_FAILED ||
        (uint64_t)(uintptr_t)control != a->upper) {
        int e = errno;
        if (control != MAP_FAILED) munmap(control, a->length);
        gt_report(a->name, GT_SKIPPED,
                  "positive control at bound %#" PRIx64
                  " failed: errno=%d %s (%s)",
                  a->upper, e, gt_errno_name(e), strerror(e));
    }
    volatile unsigned char* control_bytes =
        (volatile unsigned char*)control;
    control_bytes[0] = 0x5a;
    control_bytes[a->length - 1] = 0xa5;
    munmap(control, a->length);

    unsigned refusals = 0;
    uint64_t last = a->upper - (uint64_t)a->length;
    for (uint64_t candidate = 0; candidate <= last; candidate += page) {
        errno = 0;
        void* got = mmap((void*)(uintptr_t)candidate, a->length,
                         PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                         -1, 0);
        if (got != MAP_FAILED) {
            uint64_t placed = (uint64_t)(uintptr_t)got;
            volatile unsigned char* bytes = (volatile unsigned char*)got;
            if (placed != candidate) {
                munmap(got, a->length);
                gt_report(a->name, GT_SKIPPED,
                          "MAP_FIXED_NOREPLACE was ignored: requested %#" PRIx64
                          ", received %#" PRIx64,
                          candidate, placed);
            }
            bytes[0] = 0x3c;
            bytes[a->length - 1] = 0xc3;
            munmap(got, a->length);
            gt_report(a->name, GT_SATISFIED,
                      "mapping below bound succeeded at %#" PRIx64,
                      placed);
        }
        if (errno != EPERM && errno != EACCES) {
            gt_report(a->name, GT_REFUSED,
                      "candidate %#" PRIx64
                      " failed with non-policy errno=%d %s (%s)",
                      candidate, errno, gt_errno_name(errno), strerror(errno));
        }
        ++refusals;
        if (candidate > UINT64_MAX - page) break;
    }

    gt_report(a->name, GT_BELOW_BOUND_UNAVAILABLE,
              "positive control at %#" PRIx64
              " succeeded; all %u page-aligned starts below it were denied "
              "by policy",
              a->upper, refusals);
}
#endif

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s <case-name> <hex-bound> <size>\n", argv[0]);
        return 64;
    }
#if !defined(__linux__)
    gt_report(argv[1], GT_SKIPPED,
              "bounded below-minimum oracle is Linux-only");
#else
    struct args a;
    a.name = argv[1];
    a.upper = strtoull(argv[2], NULL, 16);
    a.length = (size_t)strtoull(argv[3], NULL, 0);
    int sig = gt_run_in_child(attempt, &a);
    if (sig > 0) {
        gt_report(a.name, GT_FAULTED, "worker died on signal %d (%s)",
                  sig, strsignal(sig));
    }
    if (sig < 0) {
        gt_report(a.name, GT_SKIPPED, "fork failed: %s", strerror(errno));
    }
#endif
    return 0;
}

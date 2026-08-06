/* SPDX-License-Identifier: Apache-2.0
 *
 * Proves that mmap rounds a ragged request up and leaves the surplus
 * addressable. Touching the final byte of the rounded host page is a direct
 * counterexample to a caller that relies on the tail staying unmapped.
 */
#include "gt_common.h"

#include <stdint.h>
#include <sys/mman.h>

struct args {
    const char* name;
    size_t length;
};

static void attempt(void* raw) {
    struct args* a = (struct args*)raw;
    long raw_page = sysconf(_SC_PAGESIZE);
    if (raw_page <= 0) {
        gt_report(a->name, GT_SKIPPED, "sysconf(_SC_PAGESIZE) failed");
    }
    size_t page = (size_t)raw_page;
    if (a->length > SIZE_MAX - (page - 1)) {
        gt_report(a->name, GT_SKIPPED, "rounding the request would overflow");
    }
    size_t rounded = ((a->length + page - 1) / page) * page;
    if (rounded == a->length) {
        gt_report(a->name, GT_SKIPPED,
                  "request %zu is already page aligned", a->length);
    }

    void* got = mmap(NULL, a->length, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (got == MAP_FAILED) {
        gt_report(a->name, GT_REFUSED,
                  "mmap(%zu) failed: errno=%d %s (%s)", a->length, errno,
                  gt_errno_name(errno), strerror(errno));
    }

    volatile unsigned char* p = (volatile unsigned char*)got;
    p[a->length] = 0x5a;
    p[rounded - 1] = 0xa5;
    if (p[a->length] != 0x5a || p[rounded - 1] != 0xa5) {
        munmap(got, a->length);
        gt_report(a->name, GT_REFUSED,
                  "the rounded tail did not retain written bytes");
    }
    munmap(got, a->length);
    gt_report(a->name, GT_TAIL_ADDRESSABLE,
              "mmap requested %zu bytes; bytes %zu and %zu were writable "
              "inside the host-rounded %zu-byte mapping",
              a->length, a->length, rounded - 1, rounded);
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <case-name> <size>\n", argv[0]);
        return 64;
    }
    struct args a;
    a.name = argv[1];
    a.length = (size_t)strtoull(argv[2], NULL, 0);
    if (a.length == 0) return 64;

    int sig = gt_run_in_child(attempt, &a);
    if (sig > 0) {
        gt_report(a.name, GT_FAULTED,
                  "touching the rounded tail died on signal %d (%s)", sig,
                  strsignal(sig));
    }
    if (sig < 0) {
        gt_report(a.name, GT_SKIPPED, "fork failed: %s", strerror(errno));
    }
    return 0;
}

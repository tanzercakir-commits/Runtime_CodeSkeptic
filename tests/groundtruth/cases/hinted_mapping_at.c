/* SPDX-License-Identifier: Apache-2.0
 *
 * Requests one explicit address as a hint, without MAP_FIXED. Relocation is
 * success for this caller and simultaneously a concrete oracle for RS-VM-0020:
 * the host accepted the mapping but could not honour the hint.
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
    void* got = mmap((void*)(uintptr_t)a->address, a->length,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (got == MAP_FAILED) {
        gt_report(a->name, GT_REFUSED,
                  "hinted mmap(%#" PRIx64 ", %zu) failed: errno=%d %s (%s)",
                  a->address, a->length, errno, gt_errno_name(errno),
                  strerror(errno));
    }

    volatile unsigned char* p = (volatile unsigned char*)got;
    p[0] = 0x5a;
    p[a->length - 1] = 0xa5;
    if (p[0] != 0x5a || p[a->length - 1] != 0xa5) {
        munmap(got, a->length);
        gt_report(a->name, GT_REFUSED,
                  "the returned mapping did not retain written bytes");
    }

    uint64_t at = (uint64_t)(uintptr_t)got;
    munmap(got, a->length);
    if (at != a->address) {
        gt_report(a->name, GT_SATISFIED_RELOCATED,
                  "the unavailable hint %#" PRIx64 " was ignored; mmap "
                  "succeeded at %#" PRIx64 " and the caller accepts relocation",
                  a->address, at);
    }
    gt_report(a->name, GT_SATISFIED,
              "the mapping succeeded and this run happened to honour hint %#"
              PRIx64,
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

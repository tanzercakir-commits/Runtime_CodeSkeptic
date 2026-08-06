/* SPDX-License-Identifier: Apache-2.0
 *
 * Repeats one structurally impossible exact mapping. This is the executable
 * oracle for RS-VM-0015: the loop really runs, every attempt reaches the same
 * permanent refusal, and no backoff or retry count changes the precondition.
 */
#include "gt_common.h"

#include <inttypes.h>
#include <sys/mman.h>

struct args {
    const char* name;
    uint64_t address;
    size_t length;
    unsigned attempts;
};

static void attempt(void* raw) {
    struct args* a = (struct args*)raw;
    int first_error = 0;

    for (unsigned i = 0; i < a->attempts; ++i) {
        void* got = mmap((void*)(uintptr_t)a->address, a->length,
                         PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        if (got == MAP_FAILED) {
            if (i == 0) {
                first_error = errno;
            } else if (errno != first_error) {
                gt_report(a->name, GT_SKIPPED,
                          "attempt %u changed errno from %d to %d; the case no "
                          "longer isolates a stable permanent refusal",
                          i + 1, first_error, errno);
            }
            continue;
        }

        uint64_t at = (uint64_t)(uintptr_t)got;
        if (at != a->address) {
            munmap(got, a->length);
            gt_report(a->name, GT_RELOCATED,
                      "attempt %u returned %#" PRIx64 " instead of %#" PRIx64,
                      i + 1, at, a->address);
        }
        volatile unsigned char* p = (volatile unsigned char*)got;
        p[0] = 0x5a;
        p[a->length - 1] = 0xa5;
        munmap(got, a->length);
        gt_report(a->name, GT_SATISFIED,
                  "attempt %u unexpectedly placed a usable exact mapping at "
                  "%#" PRIx64,
                  i + 1, a->address);
    }

    if (first_error != EINVAL) {
        gt_report(a->name, GT_REFUSED,
                  "all %u exact-mapping attempts failed consistently, but "
                  "errno=%d %s is not the structural EINVAL oracle",
                  a->attempts, first_error, gt_errno_name(first_error));
    }
    gt_report(a->name, GT_REPEATED_PERMANENT_REFUSAL,
              "all %u exact-mapping attempts reached the same structural "
              "EINVAL refusal (%s)", a->attempts, strerror(first_error));
}

int main(int argc, char** argv) {
    if (argc != 5) {
        fprintf(stderr,
                "usage: %s <case-name> <hex-address> <size> <attempts>\n",
                argv[0]);
        return 64;
    }

    struct args a;
    a.name = argv[1];
    a.address = strtoull(argv[2], NULL, 16);
    a.length = (size_t)strtoull(argv[3], NULL, 0);
    a.attempts = (unsigned)strtoul(argv[4], NULL, 0);
    if (a.length == 0 || a.attempts < 2 || a.attempts > 100) {
        fprintf(stderr, "size must be nonzero and attempts must be in [2,100]\n");
        return 64;
    }

    int sig = gt_run_in_child(attempt, &a);
    if (sig > 0) {
        gt_report(a.name, GT_FAULTED,
                  "retry worker died on signal %d (%s)", sig, strsignal(sig));
    }
    if (sig < 0) {
        gt_report(a.name, GT_SKIPPED, "fork failed: %s", strerror(errno));
    }
    return 0;
}

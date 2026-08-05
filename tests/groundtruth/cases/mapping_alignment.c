/* SPDX-License-Identifier: Apache-2.0
 *
 * Finds one ordinary anonymous mapping that lacks a stronger caller-requested
 * alignment. The mappings stay live so a lucky first address cannot simply be
 * returned again on every attempt.
 */
#include "gt_common.h"

#include <inttypes.h>
#include <stdint.h>
#include <sys/mman.h>

#define MAX_ATTEMPTS 256U

int main(int argc, char** argv) {
    if (argc != 5) {
        fprintf(stderr,
                "usage: %s <case-name> <size> <alignment> <attempts>\n",
                argv[0]);
        return 64;
    }
    const char* name = argv[1];
    size_t length = (size_t)strtoull(argv[2], NULL, 0);
    uint64_t alignment = strtoull(argv[3], NULL, 0);
    unsigned attempts = (unsigned)strtoul(argv[4], NULL, 0);
    if (length == 0 || alignment == 0 ||
        (alignment & (alignment - 1)) != 0 ||
        attempts < 2 || attempts > MAX_ATTEMPTS) {
        fprintf(stderr, "invalid size, power-of-two alignment, or attempts\n");
        return 64;
    }

    void* mappings[MAX_ATTEMPTS];
    unsigned placed = 0;
    for (; placed < attempts; ++placed) {
        void* got = mmap(NULL, length, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (got == MAP_FAILED) {
            for (unsigned i = 0; i < placed; ++i) {
                munmap(mappings[i], length);
            }
            gt_report(name, GT_REFUSED,
                      "attempt %u failed before an alignment counterexample: "
                      "errno=%d %s (%s)",
                      placed + 1, errno, gt_errno_name(errno), strerror(errno));
        }
        mappings[placed] = got;
        uint64_t at = (uint64_t)(uintptr_t)got;
        if ((at & (alignment - 1)) != 0) {
            for (unsigned i = 0; i <= placed; ++i) {
                munmap(mappings[i], length);
            }
            gt_report(name, GT_MISALIGNED,
                      "ordinary mmap returned %#" PRIx64
                      ", which is not aligned to %" PRIu64 " bytes",
                      at, alignment);
        }
    }

    for (unsigned i = 0; i < placed; ++i) {
        munmap(mappings[i], length);
    }
    gt_report(name, GT_SATISFIED,
              "all %u observed mappings happened to meet %" PRIu64
              "-byte alignment; no counterexample was observed",
              attempts, alignment);
}

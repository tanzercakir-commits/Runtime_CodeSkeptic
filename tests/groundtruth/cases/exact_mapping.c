/* SPDX-License-Identifier: Apache-2.0
 *
 * Does a MAP_FIXED mapping at an exact address actually succeed?
 *
 *   exact_mapping <case-name> <hex-address> <size-bytes>
 *
 * This is the shape of shadPS4 issue #4157: the title asks for
 * 0x1307200000 with MAP_FIXED and the emulator aborts when it cannot have it.
 * The reporter's own test program used mmap+MAP_FIXED and saw ENOMEM across
 * the band, so this reproduces that call rather than a politer equivalent.
 *
 * MAP_FIXED destroys whatever it lands on, so the whole attempt runs in a
 * forked child. If the address turns out to belong to this process after all,
 * the child is what pays for it.
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
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (got == MAP_FAILED) {
        gt_report(a->name, GT_REFUSED,
                  "mmap(%#" PRIx64 ", %zu, MAP_FIXED) failed: errno=%d %s (%s)",
                  a->address, a->length, errno, gt_errno_name(errno),
                  strerror(errno));
    }

    uint64_t at = (uint64_t)(uintptr_t)got;
    if (at != a->address) {
        munmap(got, a->length);
        gt_report(a->name, GT_RELOCATED,
                  "mmap(%#" PRIx64 ", MAP_FIXED) returned %#" PRIx64
                  " instead - the exact-address requirement is not met",
                  a->address, at);
    }

    /* Placement alone is not the request. The contract wants usable RW memory
     * there, so touch it before calling it satisfied. A fault here is reported
     * by the parent as `faulted`. */
    volatile unsigned char* p = (volatile unsigned char*)got;
    p[0] = 0x5a;
    p[a->length - 1] = 0xa5;
    if (p[0] != 0x5a || p[a->length - 1] != 0xa5) {
        munmap(got, a->length);
        gt_report(a->name, GT_REFUSED,
                  "mapping at %#" PRIx64 " did not retain written bytes",
                  a->address);
    }

    munmap(got, a->length);
    gt_report(a->name, GT_SATISFIED,
              "mmap(%#" PRIx64 ", %zu, MAP_FIXED) placed the mapping exactly "
              "there and it accepted reads and writes",
              a->address, a->length);
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

    int sig = gt_run_in_child(attempt, &a);
    if (sig > 0) {
        gt_report(a.name, GT_FAULTED,
                  "the process died on signal %d (%s) attempting the mapping",
                  sig, strsignal(sig));
    }
    if (sig < 0) {
        gt_report(a.name, GT_SKIPPED, "fork failed: %s", strerror(errno));
    }
    /* The child already reported. */
    return 0;
}

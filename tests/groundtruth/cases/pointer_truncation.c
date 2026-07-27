/* SPDX-License-Identifier: Apache-2.0
 *
 * The host succeeds, and the caller loses the result anyway.
 *
 *   pointer_truncation <case-name> <size-bytes>
 *
 * This is the seventh MVP demonstration and the only one here that needs the
 * host to do everything RIGHT. mmap(NULL, ...) is the most ordinary request
 * there is: the kernel chooses an address, returns a valid, writable mapping,
 * and reports success. Nothing about that is a host limitation.
 *
 * The failure is the PROGRAM's. LuaJIT without GC64 stores mcode pointers in
 * 32-bit slots (RSC-0018); code assuming MAP_32BIT semantics does the same
 * (RSC-0020). On every 64-bit host the kernel hands back an address above 2^32,
 * and a 32-bit slot keeps only the low half. The mapping the kernel gave is
 * perfect; the program can no longer name it.
 *
 * So this case does not try to make the host fail. It proves the mapping is
 * genuinely valid - writes a sentinel through the full 64-bit pointer and reads
 * it back - and THEN shows that the address does not survive 32-bit storage.
 * That is `lost`, not `faulted`: no signal, no refusal, a correct kernel result
 * the caller's own assumption discards.
 */
#include "gt_common.h"

#include <inttypes.h>
#include <stdint.h>
#include <sys/mman.h>

struct args { const char* name; size_t length; };

static void attempt(void* raw) {
    struct args* a = (struct args*)raw;

    void* got = mmap(NULL, a->length, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (got == MAP_FAILED) {
        gt_report(a->name, GT_SKIPPED,
                  "the host would not place an ordinary anonymous mapping: %s",
                  strerror(errno));
    }

    uint64_t addr = (uint64_t)(uintptr_t)got;

    /* The host's result is valid: prove it before blaming anyone. A sentinel
     * written through the FULL pointer must read back through the FULL pointer,
     * or the mapping the whole argument rests on was not actually usable. */
    volatile uint32_t* cell = (volatile uint32_t*)got;
    *cell = 0xA5A5A5A5u;
    if (*cell != 0xA5A5A5A5u) {
        munmap(got, a->length);
        gt_report(a->name, GT_SKIPPED,
                  "the mapping at %#" PRIx64 " did not hold a written value, so "
                  "the host result cannot be trusted as the baseline",
                  addr);
    }

    /* The caller's assumption: the returned address fits in a 32-bit slot. */
    uint32_t slot = (uint32_t)addr;
    uint64_t recovered = (uint64_t)slot;

    munmap(got, a->length);

    if (recovered == addr) {
        /* The host handed back a low address, so 32-bit storage happens to be
         * safe here. True on a 32-bit host, or wherever the allocator stays
         * below 4 GiB; never observed on the 64-bit hosts this project runs. */
        gt_report(a->name, GT_SATISFIED,
                  "the host placed the mapping at %#" PRIx64 ", which round-trips "
                  "through a 32-bit slot unchanged; a 32-bit-pointer program is "
                  "safe on this host",
                  addr);
    }

    gt_report(a->name, GT_LOST,
              "the host returned a valid, writable mapping at %#" PRIx64 " (a "
              "sentinel round-tripped through the full pointer); stored in 32 "
              "bits it becomes %#" PRIx64 ", a different address the program can "
              "no longer reach. The kernel did nothing wrong - the caller's "
              "storage is too narrow for the address it was given",
              addr, recovered);
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

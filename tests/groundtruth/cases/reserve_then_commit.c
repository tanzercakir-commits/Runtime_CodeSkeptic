/* SPDX-License-Identifier: Apache-2.0
 *
 * Reserve a large range without backing it, then commit one page of it.
 *
 *   reserve_then_commit <case-name> <reserve-bytes>
 *
 * Windows makes reservation and commitment two distinct, observable states
 * (VirtualAlloc MEM_RESERVE then MEM_COMMIT), and a program written against
 * that model expects the commit step to be a checked call that can fail. POSIX
 * hosts have no such state: the reservation is lazy, and the "commit" is an
 * mprotect that does not report the memory pressure a Windows commit would.
 * RS-VM-0012 is about that mismatch, and `reserve_commit_model` is the fact it
 * reads - measured `posix_lazy` on every host this project has probed.
 *
 * The observable is narrow on purpose: can a PROT_NONE reservation be made, and
 * can a single page inside it be turned into usable memory afterwards? What
 * this case cannot show is the part that actually bites - that no error arrives
 * when the host is out of memory - because provoking that would mean
 * exhausting the runner.
 */
#include "gt_common.h"

#include <inttypes.h>
#include <sys/mman.h>

struct args { const char* name; size_t reserve; long page; };

static void attempt(void* raw) {
    struct args* a = (struct args*)raw;

    void* base = mmap(NULL, a->reserve, PROT_NONE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (base == MAP_FAILED) {
        gt_report(a->name, GT_REFUSED,
                  "reserving %zu bytes PROT_NONE failed: errno=%d %s",
                  a->reserve, errno, gt_errno_name(errno));
    }

    /* Commit exactly one page in the middle. */
    unsigned char* page = (unsigned char*)base + (a->reserve / 2)
                        - ((a->reserve / 2) % (size_t)a->page);
    if (mprotect(page, (size_t)a->page, PROT_READ | PROT_WRITE) != 0) {
        int e = errno;
        munmap(base, a->reserve);
        gt_report(a->name, GT_REFUSED,
                  "committing one page inside the reservation failed: errno=%d "
                  "%s (%s)", e, gt_errno_name(e), strerror(e));
    }

    volatile unsigned char* p = (volatile unsigned char*)page;
    p[0] = 0x27;
    unsigned char back = p[0];
    munmap(base, a->reserve);

    if (back != 0x27) {
        gt_report(a->name, GT_REFUSED,
                  "the committed page did not retain a written byte");
    }
    gt_report(a->name, GT_SATISFIED,
              "%zu bytes reserved PROT_NONE, then one %ld-byte page inside it "
              "committed with mprotect and written",
              a->reserve, a->page);
}

int main(int argc, char** argv) {
    if (argc != 3) { fprintf(stderr, "usage: %s <case-name> <reserve>\n", argv[0]); return 64; }
    struct args a;
    a.name = argv[1];
    a.reserve = (size_t)strtoull(argv[2], NULL, 0);
    a.page = sysconf(_SC_PAGESIZE);
    if (a.page <= 0) a.page = 4096;
    int sig = gt_run_in_child(attempt, &a);
    if (sig > 0) gt_report(a.name, GT_FAULTED, "died on signal %d (%s)", sig, strsignal(sig));
    if (sig < 0) gt_report(a.name, GT_SKIPPED, "fork failed: %s", strerror(errno));
    return 0;
}

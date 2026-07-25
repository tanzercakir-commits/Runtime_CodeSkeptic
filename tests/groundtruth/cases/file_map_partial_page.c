/* SPDX-License-Identifier: Apache-2.0
 *
 * Reading between the end of a file and the end of its final page.
 *
 *   file_map_partial_page <case-name>
 *
 * The companion to file_map_beyond_eof.c, and the reason that one exists in its
 * current form. POSIX requires a conforming system to zero-fill the partial
 * page at the end of a mapped object, so this read is portable and cannot
 * fault - on Linux, on native macOS arm64, under Rosetta 2, anywhere.
 *
 * It is here because the analyzer used to call it UNSUPPORTED on any host whose
 * measured beyond-EOF behaviour was `sigbus`. The two accesses were spelled the
 * same way in a contract, so the safe one was judged against a fact about the
 * dangerous one. This case is what keeps that fixed: if the false positive ever
 * returns, a prediction of UNSUPPORTED will meet an observation of `satisfied`
 * and the pairing will fail.
 */
#include "gt_common.h"

#include <fcntl.h>
#include <sys/mman.h>

struct args { const char* name; long page; };

static void attempt(void* raw) {
    struct args* a = (struct args*)raw;

    char path[] = "/tmp/rs-gt-partial-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) gt_report(a->name, GT_SKIPPED, "mkstemp: %s", strerror(errno));
    unlink(path);

    /* One byte of file. The remaining page-1 bytes of the final page are the
     * partial page POSIX guarantees to be zero. */
    if (write(fd, "x", 1) != 1) {
        close(fd);
        gt_report(a->name, GT_SKIPPED, "write: %s", strerror(errno));
    }

    void* map = mmap(NULL, (size_t)a->page, PROT_READ, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        close(fd);
        gt_report(a->name, GT_REFUSED, "mmap failed: errno=%d %s", errno,
                  gt_errno_name(errno));
    }

    volatile const unsigned char* p = (volatile const unsigned char*)map;
    unsigned char first = p[0];                 /* the file's own byte */
    unsigned char inside = p[a->page - 1];      /* last byte of the partial page */

    munmap(map, (size_t)a->page);
    close(fd);

    if (first != 'x') {
        gt_report(a->name, GT_REFUSED,
                  "the file's own byte read back as 0x%02x, not 'x'", first);
    }
    if (inside != 0) {
        gt_report(a->name, GT_REFUSED,
                  "the last byte of the final partial page read as 0x%02x; "
                  "POSIX requires zero-fill there", inside);
    }
    gt_report(a->name, GT_SATISFIED,
              "the %ld bytes between the end of a 1-byte file and the end of "
              "its final page read as zero, without faulting", a->page - 1);
}

int main(int argc, char** argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s <case-name>\n", argv[0]); return 64; }
    struct args a;
    a.name = argv[1];
    a.page = sysconf(_SC_PAGESIZE);
    if (a.page <= 0) a.page = 4096;

    int sig = gt_run_in_child(attempt, &a);
    if (sig > 0) {
        gt_report(a.name, GT_FAULTED,
                  "reading inside the final partial page killed the process "
                  "with signal %d (%s) - this should be impossible on a "
                  "POSIX-conforming host", sig, strsignal(sig));
    }
    if (sig < 0) gt_report(a.name, GT_SKIPPED, "fork failed: %s", strerror(errno));
    return 0;
}

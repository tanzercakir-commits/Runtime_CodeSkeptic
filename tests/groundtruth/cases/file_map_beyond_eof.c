/* SPDX-License-Identifier: Apache-2.0
 *
 * What happens when you read a mapped file past its end?
 *
 *   file_map_beyond_eof <case-name>
 *
 * POSIX leaves this to the implementation, and the two lanes of one Apple
 * Silicon machine disagree: native arm64 raises SIGBUS, the same machine
 * running x86-64 under Rosetta 2 hands back zeroes. A program that maps a
 * file, reads a little past the end, and works fine under translation takes a
 * bus fault when rebuilt natively - from identical source.
 *
 * `satisfied` here means the read completed and returned zero-fill, which is
 * what a program relying on this behaviour needs. SIGBUS is `faulted`.
 */
#include "gt_common.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

struct args {
    const char* name;
    long page;
};

static void attempt(void* raw) {
    struct args* a = (struct args*)raw;

    char path[] = "/tmp/rs-gt-eof-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) gt_report(a->name, GT_SKIPPED, "mkstemp: %s", strerror(errno));
    unlink(path);

    /* EXACTLY ONE PAGE of file, TWO pages of mapping, so the second page lies
     * ENTIRELY past the end.
     *
     * The first version of this case wrote 1 byte, mapped one page, and read at
     * offset 2048 - inside the single partial page holding the file's data. It
     * reported zero-fill on Linux and contradicted the analyzer's UNSUPPORTED,
     * and the analyzer was right: POSIX *requires* the partial page at the end
     * of an object to be zero-filled, so that read is portable and can never
     * fault. It is a different question from the one `file_map_beyond_eof`
     * answers, and the case was measuring the guaranteed one while claiming to
     * measure the implementation-defined one.
     *
     * A whole page past the end is where platforms actually disagree: Linux and
     * native macOS arm64 raise SIGBUS, the same Apple machine under Rosetta 2
     * returns zeroes. */
    if (ftruncate(fd, (off_t)a->page) != 0) {
        close(fd);
        gt_report(a->name, GT_SKIPPED, "ftruncate: %s", strerror(errno));
    }

    size_t map_length = (size_t)a->page * 2;
    void* map = mmap(NULL, map_length, PROT_READ, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        close(fd);
        gt_report(a->name, GT_REFUSED,
                  "mmap of a %ld-byte file over %zu bytes failed: errno=%d %s",
                  a->page, map_length, errno, gt_errno_name(errno));
    }

    /* The read that decides it: the first byte of the second page, which no
     * part of the file backs. */
    volatile const unsigned char* p = (volatile const unsigned char*)map;
    unsigned char beyond = p[a->page];

    munmap(map, map_length);
    close(fd);

    gt_report(a->name, GT_SATISFIED,
              "reading the first byte of a page entirely past end of file "
              "returned 0x%02x without faulting", beyond);
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <case-name>\n", argv[0]);
        return 64;
    }
    struct args a;
    a.name = argv[1];
    a.page = sysconf(_SC_PAGESIZE);
    if (a.page <= 0) a.page = 4096;

    int sig = gt_run_in_child(attempt, &a);
    if (sig > 0) {
        gt_report(a.name, GT_FAULTED,
                  "reading past the end of the mapped file killed the process "
                  "with signal %d (%s)",
                  sig, strsignal(sig));
    }
    if (sig < 0) gt_report(a.name, GT_SKIPPED, "fork failed: %s", strerror(errno));
    return 0;
}

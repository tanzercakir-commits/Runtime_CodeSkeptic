/* SPDX-License-Identifier: Apache-2.0
 *
 * Would a program compiled for PAGE bytes survive this host's page size?
 *
 *   page_size_at_most <case-name> <compiled-in-page-size>
 *
 * This is jemalloc's check, longhand (deps/jemalloc/src/pages.c:760):
 *
 *     if (os_page > PAGE) {
 *         malloc_write("<jemalloc>: Unsupported system page size\n");
 *         abort();
 *     }
 *
 * The relation is at_most, not equal, and getting that wrong was defect 4 of
 * the July campaign: the analyzer tested equality, called a working
 * configuration critically broken, and advised moving to a host that does not
 * exist. So this case implements the comparison the shipping code actually
 * makes, and nothing more.
 *
 * No mapping is performed. The host page size IS the observable.
 */
#include "gt_common.h"

#include <unistd.h>

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <case-name> <compiled-page-size>\n", argv[0]);
        return 64;
    }
    const char* name = argv[1];
    unsigned long compiled = strtoul(argv[2], NULL, 0);

    long os_page = sysconf(_SC_PAGESIZE);
    if (os_page <= 0) {
        gt_report(name, GT_SKIPPED, "sysconf(_SC_PAGESIZE) failed: %s",
                  strerror(errno));
    }

    if ((unsigned long)os_page > compiled) {
        gt_report(name, GT_REFUSED,
                  "sysconf(_SC_PAGESIZE) is %ld and the build assumes %lu; "
                  "os_page > PAGE, so the shipping check aborts the process",
                  os_page, compiled);
    }
    gt_report(name, GT_SATISFIED,
              "sysconf(_SC_PAGESIZE) is %ld and the build assumes %lu; "
              "os_page <= PAGE, so the shipping check passes",
              os_page, compiled);
}

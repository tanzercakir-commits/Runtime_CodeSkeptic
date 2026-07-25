/* SPDX-License-Identifier: Apache-2.0
 *
 * Map read-write, generate code, flip to read-execute, run it.
 *
 *   write_then_execute <case-name> <size-bytes>
 *
 * The deliberate companion to rwx_anonymous.c, and the pair is the point. Both
 * ask the host for executable memory; they differ only in whether W and X are
 * ever live at the same moment. On native macOS arm64 the measured facts say
 * those two questions have OPPOSITE answers:
 *
 *     write_execute_simultaneous     false
 *     write_then_execute_transition  true
 *
 * So a JIT written the second way runs on Apple Silicon and the same JIT
 * written the first way does not. That is the project's whole thesis in one
 * pair of case programs, and RS-VM-0009 exists to tell them apart - which it
 * can only be trusted to do if both halves have been executed for real.
 */
#include "gt_common.h"

#include <sys/mman.h>

#if defined(__x86_64__)
static const unsigned char kReturn5a[] = {0xb8, 0x5a, 0x00, 0x00, 0x00, 0xc3};
#elif defined(__aarch64__)
static const unsigned char kReturn5a[] = {0x40, 0x0b, 0x80, 0x52,
                                          0xc0, 0x03, 0x5f, 0xd6};
#else
static const unsigned char kReturn5a[] = {0};
#endif

typedef int (*fn)(void);

struct args { const char* name; size_t length; };

static void attempt(void* raw) {
    struct args* a = (struct args*)raw;

#if !defined(__x86_64__) && !defined(__aarch64__)
    gt_report(a->name, GT_SKIPPED,
              "no test payload is compiled for this architecture");
#endif

    /* Step 1: writable, NOT executable. */
    void* block = mmap(NULL, a->length, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (block == MAP_FAILED) {
        gt_report(a->name, GT_REFUSED,
                  "mmap(PROT_READ|PROT_WRITE) of %zu bytes failed: errno=%d %s",
                  a->length, errno, gt_errno_name(errno));
    }
    memcpy(block, kReturn5a, sizeof(kReturn5a));

    /* Step 2: drop write, add execute. The flip is the thing being measured. */
    if (mprotect(block, a->length, PROT_READ | PROT_EXEC) != 0) {
        int e = errno;
        munmap(block, a->length);
        gt_report(a->name, GT_REFUSED,
                  "mprotect(PROT_READ|PROT_EXEC) failed: errno=%d %s (%s)",
                  e, gt_errno_name(e), strerror(e));
    }

#if defined(__aarch64__)
    __builtin___clear_cache((char*)block, (char*)block + sizeof(kReturn5a));
#endif

    int result = ((fn)block)();
    munmap(block, a->length);

    if (result != 0x5a) {
        gt_report(a->name, GT_REFUSED,
                  "the flipped mapping ran but returned %d, not 0x5a", result);
    }
    gt_report(a->name, GT_SATISFIED,
              "%zu bytes mapped RW, written, flipped to RX with mprotect, and "
              "executed - W and X were never live together",
              a->length);
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
        gt_report(a.name, GT_FAULTED,
                  "the RW->RX flip was accepted, then executing from the "
                  "mapping killed the process with signal %d (%s)",
                  sig, strsignal(sig));
    }
    if (sig < 0) gt_report(a.name, GT_SKIPPED, "fork failed: %s", strerror(errno));
    return 0;
}

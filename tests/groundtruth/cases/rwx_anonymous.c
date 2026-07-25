/* SPDX-License-Identifier: Apache-2.0
 *
 * Can this process get memory that is writable and executable at the same
 * time, and then actually execute out of it?
 *
 *   rwx_anonymous <case-name> <size-bytes>
 *
 * Box64's dynarec maps its code block PROT_READ|WRITE|EXEC and never flips it;
 * LuaJIT built with LUAJIT_SECURITY_MCODE=0 keeps its machine-code area RWX
 * for its whole lifetime. Both are the same question of the host.
 *
 * A successful mmap is NOT the answer. macOS hands back RWX pages and then
 * faults on the first instruction fetch when the process lacks the JIT
 * entitlement, which is the specific way this fails in the wild - so the case
 * writes a function and calls it. Nothing short of that distinguishes
 * "permitted" from "permitted-looking".
 */
#include "gt_common.h"

#include <sys/mman.h>

/* A function that returns 0x5a, in the instruction set we are compiled for.
 * Kept deliberately trivial: this is a probe of memory policy, not of codegen. */
#if defined(__x86_64__)
static const unsigned char kReturn5a[] = {
    0xb8, 0x5a, 0x00, 0x00, 0x00,  /* mov eax, 0x5a */
    0xc3                            /* ret           */
};
#elif defined(__aarch64__)
static const unsigned char kReturn5a[] = {
    0x40, 0x0b, 0x80, 0x52,  /* mov w0, #0x5a */
    0xc0, 0x03, 0x5f, 0xd6   /* ret           */
};
#else
static const unsigned char kReturn5a[] = {0};
#endif

typedef int (*fn)(void);

struct args {
    const char* name;
    size_t length;
};

static void attempt(void* raw) {
    struct args* a = (struct args*)raw;

#if !defined(__x86_64__) && !defined(__aarch64__)
    gt_report(a->name, GT_SKIPPED,
              "no test payload is compiled for this architecture");
#endif

    void* block = mmap(NULL, a->length, PROT_READ | PROT_WRITE | PROT_EXEC,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (block == MAP_FAILED) {
        gt_report(a->name, GT_REFUSED,
                  "mmap(PROT_READ|PROT_WRITE|PROT_EXEC) of %zu bytes failed: "
                  "errno=%d %s (%s)",
                  a->length, errno, gt_errno_name(errno), strerror(errno));
    }

    /* Write through the same mapping that will be executed. Flipping to RX
     * first would be testing a different contract - these programs never do. */
    memcpy(block, kReturn5a, sizeof(kReturn5a));

#if defined(__aarch64__)
    __builtin___clear_cache((char*)block, (char*)block + sizeof(kReturn5a));
#endif

    fn call = (fn)block;
    int result = call();

    munmap(block, a->length);
    if (result != 0x5a) {
        gt_report(a->name, GT_REFUSED,
                  "code in the RWX mapping ran but returned %d, not 0x5a",
                  result);
    }
    gt_report(a->name, GT_SATISFIED,
              "%zu bytes mapped PROT_READ|PROT_WRITE|PROT_EXEC, written "
              "through, and executed without flipping protection",
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
        /* This is the interesting failure on Apple Silicon: the mapping is
         * granted and the first instruction fetch kills the process. */
        gt_report(a.name, GT_FAULTED,
                  "the RWX mapping was granted, then executing from it killed "
                  "the process with signal %d (%s)",
                  sig, strsignal(sig));
    }
    if (sig < 0) gt_report(a.name, GT_SKIPPED, "fork failed: %s", strerror(errno));
    return 0;
}

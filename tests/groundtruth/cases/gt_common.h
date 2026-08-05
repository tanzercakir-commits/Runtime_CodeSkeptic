/* SPDX-License-Identifier: Apache-2.0
 *
 * Support for ground-truth cases. Deliberately self-contained: POSIX only, no
 * RuntimeSkeptic headers, no project types. See ../README.md - if a case could
 * reach into the probe to decide what happened, this directory would be the
 * tool agreeing with itself.
 */
#ifndef GT_COMMON_H
#define GT_COMMON_H

/* Before any include. Under strict ISO C the platform hides strsignal(),
 * mkstemp(), ftruncate() and MAP_ANONYMOUS, and the compiler then assumes an
 * undeclared function returns int - so printf("%s", strsignal(sig)) passes an
 * int where a pointer belongs and the case segfaults while reporting the very
 * signal it was built to observe. That is exactly what happened, and it was
 * invisible because the runner was hiding compiler warnings. It now builds
 * with -Werror. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#else
#define _DEFAULT_SOURCE 1
#endif

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* The outcomes the runner understands.
 *
 * The first five are host-side: what the kernel did with the request. `lost` is
 * the sixth and it is different in kind - the kernel SUCCEEDED and returned a
 * valid result, and the failure is entirely in the caller's handling of it. It
 * exists for the seventh MVP demonstration, "valid host operation rejected by
 * caller assumption", which none of the host-side outcomes can express: the
 * program did not fault (that is `faulted`, a host access refusal) and the kernel
 * did not relocate (that is `relocated`, a host placement choice) - the returned
 * address simply does not fit the storage the program keeps it in. Paired against
 * an UNSUPPORTED prediction it is held, through the runner's else branch, like
 * any other observed failure of the program. */
#define GT_SATISFIED "satisfied"
#define GT_REFUSED   "refused"
#define GT_RELOCATED "relocated"
#define GT_FAULTED   "faulted"
#define GT_SKIPPED   "skipped"
#define GT_LOST      "lost"
#define GT_SATISFIED_RELOCATED "satisfied-relocated"
#define GT_TAIL_ADDRESSABLE    "tail-addressable"
#define GT_MISALIGNED          "misaligned"
#define GT_MISALIGNED_EINVAL   "misaligned-einval"
#define GT_REPEATED_PERMANENT_REFUSAL "repeated-permanent-refusal"

#define GT_BELOW_BOUND_UNAVAILABLE "below-bound-unavailable"
__attribute__((unused))
static void gt_json_escape(const char* s, char* out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; s[i] != '\0' && o + 2 < cap; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = (char)c;
        } else if (c < 0x20) {
            if (o + 6 >= cap) break;
            o += (size_t)snprintf(out + o, cap - o, "\\u%04x", c);
        } else {
            out[o++] = (char)c;
        }
    }
    out[o] = '\0';
}

/* Prints the single JSON object the runner reads, and exits 0. The exit code
 * carries no meaning: a case that correctly observes a refusal has done its
 * job and must not look like a failed test. */
__attribute__((unused))
static void gt_report(const char* name, const char* outcome, const char* fmt, ...) {
    char detail[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(detail, sizeof(detail), fmt, ap);
    va_end(ap);

    char escaped[2048];
    gt_json_escape(detail, escaped, sizeof(escaped));
    printf("{\"case\":\"%s\",\"outcome\":\"%s\",\"detail\":\"%s\"}\n",
           name, outcome, escaped);
    fflush(stdout);
    exit(0);
}

__attribute__((unused))
static const char* gt_errno_name(int e) {
    switch (e) {
        case ENOMEM: return "ENOMEM";
        case EINVAL: return "EINVAL";
        case EACCES: return "EACCES";
        case EPERM:  return "EPERM";
        case EEXIST: return "EEXIST";
        case ENODEV: return "ENODEV";
        case EBADF:  return "EBADF";
        default:     return "";
    }
}

/* Runs `body` in a forked child so an operation that faults is observed rather
 * than fatal. Returns 0 if the child exited normally, or the signal number if
 * it died on one. The child's own stdout is inherited, so a body that calls
 * gt_report() reports for the whole case. */
typedef void (*gt_body)(void*);

__attribute__((unused))
static int gt_run_in_child(gt_body body, void* arg) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        body(arg);
        _exit(0);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    if (WIFSIGNALED(status)) return WTERMSIG(status);
    return 0;
}

#endif /* GT_COMMON_H */

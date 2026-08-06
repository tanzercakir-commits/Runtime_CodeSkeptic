/* SPDX-License-Identifier: Apache-2.0
 *
 * Linux cgroup-v2 half of the reserve/commit semantic control.
 *
 * The surrounding lane places only this process in a MemoryMax-bounded
 * transient service. Address reservation and mprotect must both succeed;
 * physical commitment is then forced one page at a time. The expected result
 * is a cgroup-local OOM kill during first touch, never machine-wide pressure.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

enum { kReserveBytes = 256 * 1024 * 1024 };

static int record(const char *path, const char *text) {
    const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return 20;
    const size_t length = strlen(text);
    const ssize_t written = write(fd, text, length);
    const int saved = errno;
    if (written == (ssize_t)length) (void)fsync(fd);
    (void)close(fd);
    errno = saved;
    return written == (ssize_t)length ? 0 : 21;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s STATUS-FILE\n", argv[0]);
        return 64;
    }

    void *const reservation = mmap(NULL, kReserveBytes, PROT_NONE,
                                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                                   -1, 0);
    if (reservation == MAP_FAILED) {
        char status[128];
        (void)snprintf(status, sizeof(status),
                       "reserve_ok=0 reserve_errno=%d\n", errno);
        (void)record(argv[1], status);
        return 10;
    }

    if (mprotect(reservation, kReserveBytes, PROT_READ | PROT_WRITE) != 0) {
        char status[160];
        (void)snprintf(status, sizeof(status),
                       "reserve_ok=1 mprotect_ok=0 mprotect_errno=%d\n",
                       errno);
        (void)record(argv[1], status);
        (void)munmap(reservation, kReserveBytes);
        return 11;
    }

    if (record(argv[1],
               "reserve_ok=1 mprotect_ok=1 commit_call_present=0 "
               "touch_started=1\n") != 0) {
        (void)munmap(reservation, kReserveBytes);
        return 12;
    }

    const long page = sysconf(_SC_PAGESIZE);
    if (page <= 0) return 13;
    volatile unsigned char *bytes = (volatile unsigned char *)reservation;
    for (size_t offset = 0; offset < kReserveBytes; offset += (size_t)page) {
        bytes[offset] = (unsigned char)(offset / (size_t)page);
    }

    (void)record(argv[1],
                 "reserve_ok=1 mprotect_ok=1 commit_call_present=0 "
                 "touch_started=1 touch_completed=1\n");
    (void)munmap(reservation, kReserveBytes);
    return 14;
}

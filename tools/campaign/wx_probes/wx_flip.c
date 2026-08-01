/* The W^X-compliant JIT shape: map WRITABLE, fill, then mprotect to EXECUTABLE
 * (never both at once). This is what a well-behaved JIT does on Apple Silicon,
 * and it runs on a W^X host: the reviewer's M1 got past both the mmap and the
 * mprotect flip. The observer's write_then_execute_pairs() extracts the W->X
 * transition, and rs-check must NOT flag a simultaneous-W+X requirement. */
#include <sys/mman.h>
#include <string.h>
#include <stdio.h>

/* See naive_rwx.c: the payload must match the host arch or it is SIGILL. */
#if defined(__aarch64__)
static const unsigned char code[] = { 0x40, 0x05, 0x80, 0x52,   /* mov  w0, #42 */
                                      0xc0, 0x03, 0x5f, 0xd6 };  /* ret          */
#else
static const unsigned char code[] = { 0xB8, 0x2A, 0x00, 0x00, 0x00, /* mov eax,42 */
                                      0xC3 };                        /* ret        */
#endif

int main(void) {
    size_t len = 16384;
    unsigned char *m = mmap(NULL, len, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) { perror("mmap w"); return 2; }
    memcpy(m, code, sizeof code);
    if (mprotect(m, len, PROT_READ | PROT_EXEC) != 0) { perror("mprotect x"); return 2; }
    __builtin___clear_cache((char *)m, (char *)m + len);  /* I-cache: required on aarch64 */
    int (*fn)(void) = (int (*)(void))m;
    int r = fn();
    printf("wx-flip returned %d\n", r);
    return r == 42 ? 0 : 1;
}

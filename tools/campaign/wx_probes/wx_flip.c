/* The W^X-compliant JIT shape: map WRITABLE, fill, then mprotect to EXECUTABLE
 * (never both at once). This is what a well-behaved JIT does on Apple Silicon.
 * The observer's write_then_execute_pairs() should extract the W->X transition,
 * and rs-check should NOT flag a simultaneous-W+X requirement. */
#include <sys/mman.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    size_t len = 4096;
    unsigned char *m = mmap(NULL, len, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) { perror("mmap w"); return 2; }
    unsigned char code[] = { 0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3 }; /* mov eax,42; ret */
    memcpy(m, code, sizeof code);
    if (mprotect(m, len, PROT_READ | PROT_EXEC) != 0) { perror("mprotect x"); return 2; }
    int (*fn)(void) = (int (*)(void))m;
    int r = fn();
    printf("wx-flip returned %d\n", r);
    return r == 42 ? 0 : 1;
}

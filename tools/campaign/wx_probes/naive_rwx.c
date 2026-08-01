/* The box64-dynarec shape (RS-VM-0009): one mapping, writable AND executable
 * at the same time, never flipped. Succeeds on x86_64 Linux (W^X not enforced);
 * the point is that the OBSERVER captures the simultaneous-W+X request, which
 * rs-check then predicts UNSUPPORTED on a W^X host (Apple Silicon). */
#include <sys/mman.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    size_t len = 4096;
    unsigned char *m = mmap(NULL, len, PROT_READ | PROT_WRITE | PROT_EXEC,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) { perror("mmap rwx"); return 2; }
    unsigned char code[] = { 0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3 }; /* mov eax,42; ret */
    memcpy(m, code, sizeof code);
    int (*fn)(void) = (int (*)(void))m;
    int r = fn();
    printf("naive-rwx returned %d\n", r);
    return r == 42 ? 0 : 1;
}

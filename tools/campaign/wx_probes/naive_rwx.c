/* The box64-dynarec shape (RS-VM-0009): one mapping, writable AND executable at
 * the same time, never flipped. This is a PERMISSIVE-HOST probe: it maps RWX and
 * runs code in it, which x86_64 Linux and aarch64 Linux/Asahi allow but a W^X
 * host (Apple Silicon macOS) REFUSES - mmap returns EACCES before any code runs.
 * That refusal is the whole point: observe this shape where RWX is granted, then
 * predict UNSUPPORTED for a W^X host from the captured request. Do not expect it
 * to run on the host it describes; see run_control.sh's observe/predict split. */
#include <sys/mman.h>
#include <string.h>
#include <stdio.h>

/* A function that returns 42, in native machine code. The payload MUST match the
 * host arch: x86_64 bytes are an illegal instruction on aarch64 (SIGILL), which
 * is exactly the defect a single x86_64 payload shipped as an "M1 control" hit. */
#if defined(__aarch64__)
static const unsigned char code[] = { 0x40, 0x05, 0x80, 0x52,   /* mov  w0, #42 */
                                      0xc0, 0x03, 0x5f, 0xd6 };  /* ret          */
#else
static const unsigned char code[] = { 0xB8, 0x2A, 0x00, 0x00, 0x00, /* mov eax,42 */
                                      0xC3 };                        /* ret        */
#endif

int main(void) {
    size_t len = 16384;   /* one whole page on a 16K host, one of four on a 4K one */
    unsigned char *m = mmap(NULL, len, PROT_READ | PROT_WRITE | PROT_EXEC,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) { perror("mmap rwx"); return 2; }
    memcpy(m, code, sizeof code);
    __builtin___clear_cache((char *)m, (char *)m + len);  /* I-cache: required on aarch64 */
    int (*fn)(void) = (int (*)(void))m;
    int r = fn();
    printf("naive-rwx returned %d\n", r);
    return r == 42 ? 0 : 1;
}

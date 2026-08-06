/* The pattern real emulators use, in C to keep the include surface small. */
#include <sys/mman.h>
#include <stdlib.h>
#include <assert.h>

#define USER_MIN 0x1000000000ULL
#define GUEST_SIZE (64ULL * 1024)

void* MapGuestBase(void) {
    void* requested = (void*)USER_MIN;
    void* result = mmap(requested, GUEST_SIZE, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (result == MAP_FAILED) {
        abort();
    }
    /* The real requirement: guest pointer must equal host pointer. */
    assert(result == requested);
    return result;
}

void* MapCodeBuffer(void) {
    /* A JIT buffer: no address demanded, but write+execute at once. */
    void* result = mmap(0, 1 << 20, PROT_READ | PROT_WRITE | PROT_EXEC,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return result;   /* never checked at all */
}

/* Reconstructed from the patterns the campaign contracts quote. */
#include <sys/mman.h>

#define LJ_PAGESIZE 16384

void* MapGuestDirectMemory(void) {
    void* p = mmap((void*)0x1307200000, 0x20000, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p == MAP_FAILED) {
        ASSERT_MSG(0, "Mapping cannot fit inside free region");
    }
    return p;
}

void* AllocDynarecBlock(size_t size) {
    void* p = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (p == MAP_FAILED)
        return NULL;
    return p;
}

int PublishCode(void* block, size_t len) {
    if (mprotect(block, len, PROT_READ | PROT_EXEC) != 0)
        return -1;
    return 0;
}

void* AllocLowHeap(void) {
    for (int i = 0; i < 30; i++) {
        void* p = mmap((void*)0x40000000, 65536, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (p != MAP_FAILED)
            return p;
    }
    return NULL;
}

void* AllocAnywhere(size_t n) {
    return mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

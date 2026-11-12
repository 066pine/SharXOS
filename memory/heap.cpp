// SharkXOS - kernel heap allocator
//
// The heap lives in a reserved region of kernel BSS (no paging / physical
// allocator is present yet, see ARCHITECTURE.md). Blocks are 16-byte aligned
// and carry a header with magic, size, free flag and address-ordered links.
// Free blocks are coalesced with their neighbours immediately, so the free
// list never contains adjacent free blocks.
#include <memory/heap.h>
#include <kernel/string.h>
#include <drivers/console.h>

#define HEAP_SIZE (8u * 1024u * 1024u)

namespace {

struct Block {
    u32    magic;
    u32    size;        // payload bytes
    u32    prev_size;   // payload size of the previous physical block (0 for first)
    u32    free;        // 1 = free
    Block* prev;        // previous block in address order
    Block* next;        // next block in address order
    u32    checksum;    // magic ^ size ^ prev_size
    u32    reserved;
};

static_assert(sizeof(Block) == 32, "heap block header must be 32 bytes");

alignas(16) u8 g_area[HEAP_SIZE];

Block* g_first = nullptr;
bool   g_initialised = false;

HeapStats g_stats;

inline u32 checksum_of(const Block* b) {
    return b->magic ^ b->size ^ b->prev_size;
}

inline void seal(Block* b) { b->checksum = checksum_of(b); }

inline bool header_valid(const Block* b) {
    return b->checksum == checksum_of(b);
}

inline u8* payload(Block* b) { return (u8*)b + sizeof(Block); }
inline Block* header_of(void* p) { return (Block*)((u8*)p - sizeof(Block)); }

void refresh_stats() {
    u32 used = 0, freeb = 0, blocks = 0, freeblocks = 0, largest = 0;
    for (Block* b = g_first; b; b = b->next) {
        blocks++;
        if (b->free) {
            freeblocks++;
            freeb += b->size + (u32)sizeof(Block);
            if (b->size > largest) largest = b->size;
        } else {
            used += b->size + (u32)sizeof(Block);
        }
    }
    g_stats.used_bytes = used;
    g_stats.free_bytes = freeb;
    g_stats.block_count = blocks;
    g_stats.free_block_count = freeblocks;
    g_stats.largest_free = largest;
    if (used > g_stats.peak_used) g_stats.peak_used = used;
}

void insert_after(Block* at, Block* b) {
    b->prev = at;
    b->next = at ? at->next : g_first;
    if (b->next) b->next->prev = b;
    if (at) at->next = b;
    else g_first = b;
}

void unlink(Block* b) {
    if (b->prev) b->prev->next = b->next;
    else g_first = b->next;
    if (b->next) b->next->prev = b->prev;
    b->prev = b->next = nullptr;
}

void split(Block* b, u32 want) {
    u32 remaining = b->size - want;
    if (remaining < sizeof(Block) + 16) return;   // not worth splitting

    Block* rest = (Block*)((u8*)payload(b) + want);
    rest->magic = HEAP_MAGIC_FREE;
    rest->size = remaining - (u32)sizeof(Block);
    rest->prev_size = want;
    rest->free = 1;
    rest->prev = b;
    rest->next = b->next;
    rest->reserved = 0;
    if (rest->next) rest->next->prev = rest;
    rest->checksum = checksum_of(rest);

    b->next = rest;
    b->size = want;
    seal(b);

    if (rest->next) {
        rest->next->prev_size = rest->size;
        seal(rest->next);
    }
}

void coalesce(Block* b) {
    // merge with next
    Block* n = b->next;
    if (n && n->free && header_valid(n)) {
        b->size += (u32)sizeof(Block) + n->size;
        b->next = n->next;
        if (b->next) b->next->prev = b;
        n->magic = HEAP_MAGIC_DEAD;
        seal(b);
        g_stats.coalesce_count++;
        if (b->next) {
            b->next->prev_size = b->size;
            seal(b->next);
        }
    }
    // merge with previous
    Block* p = b->prev;
    if (p && p->free && header_valid(p)) {
        p->size += (u32)sizeof(Block) + b->size;
        p->next = b->next;
        if (p->next) p->next->prev = p;
        b->magic = HEAP_MAGIC_DEAD;
        seal(p);
        g_stats.coalesce_count++;
        if (p->next) {
            p->next->prev_size = p->size;
            seal(p->next);
        }
    }
}

} // namespace

namespace heap {

void init() {
    memset(&g_stats, 0, sizeof(g_stats));
    g_stats.total_bytes = HEAP_SIZE;

    g_first = (Block*)g_area;
    g_first->magic = HEAP_MAGIC_FREE;
    g_first->size = HEAP_SIZE - (u32)sizeof(Block);
    g_first->prev_size = 0;
    g_first->free = 1;
    g_first->prev = nullptr;
    g_first->next = nullptr;
    g_first->reserved = 0;
    seal(g_first);

    g_initialised = true;
    refresh_stats();
}

void* kmalloc(size_t size) {
    if (!g_initialised) return nullptr;
    if (size == 0) return nullptr;
    if (size > HEAP_SIZE - sizeof(Block)) { g_stats.fail_count++; return nullptr; }

    u32 want = (u32)size;
    if (want % 16) want += 16 - (want % 16);

    for (Block* b = g_first; b; b = b->next) {
        if (!b->free) continue;
        if (!header_valid(b)) {
            // corruption: stop scanning
            break;
        }
        if (b->size < want) continue;

        split(b, want);
        b->free = 0;
        b->magic = HEAP_MAGIC_USED;
        seal(b);
        g_stats.alloc_count++;
        refresh_stats();
        return payload(b);
    }

    g_stats.fail_count++;
    return nullptr;
}

void* kzalloc(size_t size) {
    void* p = kmalloc(size);
    if (p) memset(p, 0, size);
    return p;
}

bool validate_ptr(void* ptr) {
    if (!ptr) return false;
    u32 addr = (u32)ptr;
    u32 base = (u32)g_area;
    if (addr < base + sizeof(Block) || addr >= base + HEAP_SIZE) return false;
    Block* b = header_of(ptr);
    if (!header_valid(b)) return false;
    if (b->magic != HEAP_MAGIC_USED) return false;
    return true;
}

bool kfree(void* ptr) {
    if (!ptr) return false;
    if (!g_initialised) { g_stats.invalid_free_count++; return false; }

    u32 addr = (u32)ptr;
    u32 base = (u32)g_area;
    if (addr < base + sizeof(Block) || addr >= base + HEAP_SIZE) {
        g_stats.invalid_free_count++;
        return false;
    }
    if ((addr & 0xF) != 0) {
        g_stats.invalid_free_count++;
        return false;
    }

    Block* b = header_of(ptr);

    if (b->magic == HEAP_MAGIC_DEAD || b->magic == HEAP_MAGIC_FREE) {
        g_stats.double_free_count++;
        return false;
    }
    if (b->magic != HEAP_MAGIC_USED || !header_valid(b) || b->free) {
        g_stats.invalid_free_count++;
        return false;
    }

    b->magic = HEAP_MAGIC_FREE;
    b->free = 1;
    seal(b);
    coalesce(b);
    g_stats.free_count++;
    refresh_stats();
    return true;
}

void* krealloc(void* ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    if (size == 0) { kfree(ptr); return nullptr; }
    if (!validate_ptr(ptr)) return nullptr;

    Block* b = header_of(ptr);
    if (b->size >= size) {
        // shrink in place
        split(b, (u32)size + ((size % 16) ? (16 - size % 16) : 0));
        refresh_stats();
        return ptr;
    }
    void* np = kmalloc(size);
    if (!np) return nullptr;
    memcpy(np, ptr, b->size);
    kfree(ptr);
    return np;
}

HeapStats stats() {
    refresh_stats();
    return g_stats;
}

u32 capacity() { return HEAP_SIZE; }

u32 block_info_at(u32 index, HeapBlockInfo* out) {
    u32 i = 0;
    for (Block* b = g_first; b; b = b->next, i++) {
        if (i == index) {
            out->address = (u32)payload(b);
            out->size = b->size;
            out->free = b->free != 0;
            out->valid = header_valid(b);
            return i;
        }
    }
    return 0xFFFFFFFFu;
}

int self_test() {
    // Returns a bitmask: bit N set means check N failed.
    //  0 basic allocation + alignment    4 free list coalescing
    //  1 writable payload               5 out of memory handling
    //  2 double free rejected           6 realloc preserves contents
    //  3 invalid free rejected          7 stress allocation loop
    int mask = 0;
    HeapStats before = stats();

    // 0. basic allocation and alignment
    void* a = kmalloc(1);
    void* b = kmalloc(64);
    void* c = kmalloc(1000);
    if (!a || !b || !c) mask |= 1 << 0;
    if (a && ((u32)a & 0xF)) mask |= 1 << 0;
    if (b && ((u32)b & 0xF)) mask |= 1 << 0;
    if (c && ((u32)c & 0xF)) mask |= 1 << 0;
    if (a && b && a == b) mask |= 1 << 0;

    // 1. writability
    if (c) {
        memset(c, 0xAB, 1000);
        u8* p = (u8*)c;
        for (int i = 0; i < 1000; i++) if (p[i] != 0xAB) { mask |= 1 << 1; break; }
    } else {
        mask |= 1 << 1;
    }

    // 2. double free must be rejected
    if (b) {
        u32 before_df = stats().double_free_count;
        if (!kfree(b)) mask |= 1 << 2;
        if (kfree(b)) mask |= 1 << 2;                    // must not succeed
        if (stats().double_free_count == before_df) mask |= 1 << 2;
    } else {
        mask |= 1 << 2;
    }

    // 3. invalid free must be rejected
    int bogus_local = 0;
    if (kfree(&bogus_local)) mask |= 1 << 3;
    if (kfree((void*)0x12345)) mask |= 1 << 3;

    // 4. coalescing / full deallocation
    kfree(a);
    kfree(c);
    HeapStats after = stats();
    if (after.used_bytes != before.used_bytes) mask |= 1 << 4;
    if (after.free_block_count != 1) mask |= 1 << 4;

    // 5. out of memory handling
    void* huge = kmalloc(HEAP_SIZE * 2);
    if (huge != nullptr) { mask |= 1 << 5; kfree(huge); }

    // 6. realloc grows and preserves content
    char* s = (char*)kmalloc(16);
    if (s) {
        strcpy(s, "sharkx");
        char* s2 = (char*)krealloc(s, 256);
        if (!s2 || strcmp(s2, "sharkx") != 0) mask |= 1 << 6;
        else kfree(s2);
    } else {
        mask |= 1 << 6;
    }

    // 7. stress: many small allocations
    void* ptrs[32];
    for (int i = 0; i < 32; i++) ptrs[i] = kmalloc(48);
    for (int i = 0; i < 32; i++) {
        if (!ptrs[i]) mask |= 1 << 7;
        else kfree(ptrs[i]);
    }

    HeapStats end = stats();
    if (end.used_bytes != before.used_bytes) mask |= 1 << 7;
    return mask;
}

} // namespace heap

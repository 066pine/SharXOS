// SharkXOS - kernel heap allocator (boundary-tagged, coalescing free list)
#pragma once
#include <kernel/common.h>

#define HEAP_MAGIC_USED 0x53484B42u  // 'SHKB'
#define HEAP_MAGIC_FREE 0x53484B46u  // 'SHKF'
#define HEAP_MAGIC_DEAD 0x53484B44u  // 'SHKD' - freed block, double free detector

struct HeapStats {
    u32 total_bytes;
    u32 used_bytes;
    u32 free_bytes;
    u32 peak_used;
    u32 block_count;
    u32 free_block_count;
    u32 largest_free;
    u32 alloc_count;
    u32 free_count;
    u32 fail_count;
    u32 double_free_count;
    u32 invalid_free_count;
    u32 coalesce_count;
};

struct HeapBlockInfo {
    u32 address;
    u32 size;
    bool free;
    bool valid;
};

namespace heap {
void init();
void* kmalloc(size_t size);
void* kzalloc(size_t size);
void* krealloc(void* ptr, size_t size);
bool  kfree(void* ptr);
bool  validate_ptr(void* ptr);
HeapStats stats();
u32  capacity();
u32  block_info_at(u32 index, HeapBlockInfo* out);
// Test hooks
int  self_test();     // returns number of failed checks
}

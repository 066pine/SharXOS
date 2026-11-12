// SharkXOS - Multiboot2 structures (informational tags only)
#pragma once
#include <kernel/common.h>

#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36D76289

struct PACKED MultibootTag {
    u32 type;
    u32 size;
};

struct PACKED MultibootInfo {
    u32 total_size;
    u32 reserved;
    // tags follow
};

struct PACKED MultibootTagString {
    u32 type;
    u32 size;
    char string[1];
};

struct PACKED MultibootTagBasicMeminfo {
    u32 type;
    u32 size;
    u32 mem_lower;
    u32 mem_upper;
};

struct PACKED MultibootMmapEntry {
    u64 addr;
    u64 len;
    u32 type;
    u32 zero;
};

struct PACKED MultibootTagMmap {
    u32 type;
    u32 size;
    u32 entry_size;
    u32 entry_version;
    MultibootMmapEntry entries[1];
};

#define MULTIBOOT_TAG_END       0
#define MULTIBOOT_TAG_CMDLINE   1
#define MULTIBOOT_TAG_BOOTLOADER 2
#define MULTIBOOT_TAG_BASIC_MEM 4
#define MULTIBOOT_TAG_MMAP      6
#define MULTIBOOT_TAG_FRAMEBUFFER 8

struct BootInfo {
    bool     valid;
    u32      magic;
    u32      info_addr;
    char     bootloader[64];
    char     cmdline[128];
    u32      mem_lower_kb;
    u32      mem_upper_kb;
    u64      total_ram_bytes;
    u64      available_ram_bytes;
    u32      mmap_entries;
    bool     framebuffer;
};

namespace multiboot {
void parse(u32 magic, u32 info_addr);
const BootInfo& info();
} // namespace multiboot

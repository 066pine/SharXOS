// SharkXOS - Multiboot2 information parsing
#include <kernel/multiboot2.h>
#include <kernel/string.h>

namespace {

BootInfo g_info;

void copy_tag_string(char* dst, size_t dst_size, const char* src) {
    if (!src) return;
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = 0;
}

} // namespace

namespace multiboot {

void parse(u32 magic, u32 info_addr) {
    memset(&g_info, 0, sizeof(g_info));
    g_info.magic = magic;
    g_info.info_addr = info_addr;

    if (magic != MULTIBOOT2_BOOTLOADER_MAGIC || info_addr == 0) {
        g_info.valid = false;
        return;
    }
    g_info.valid = true;

    MultibootInfo* info = (MultibootInfo*)info_addr;
    u32 total = info->total_size;
    if (total < 16 || total > (1024 * 1024)) {
        g_info.valid = false;
        return;
    }

    u32 offset = 8;
    while (offset + 8 <= total) {
        MultibootTag* tag = (MultibootTag*)(info_addr + offset);
        if (tag->type == MULTIBOOT_TAG_END) break;
        if (tag->size < 8 || offset + tag->size > total) break;

        switch (tag->type) {
        case MULTIBOOT_TAG_CMDLINE: {
            MultibootTagString* s = (MultibootTagString*)tag;
            copy_tag_string(g_info.cmdline, sizeof(g_info.cmdline), s->string);
            break;
        }
        case MULTIBOOT_TAG_BOOTLOADER: {
            MultibootTagString* s = (MultibootTagString*)tag;
            copy_tag_string(g_info.bootloader, sizeof(g_info.bootloader), s->string);
            break;
        }
        case MULTIBOOT_TAG_BASIC_MEM: {
            MultibootTagBasicMeminfo* m = (MultibootTagBasicMeminfo*)tag;
            g_info.mem_lower_kb = m->mem_lower;
            g_info.mem_upper_kb = m->mem_upper;
            break;
        }
        case MULTIBOOT_TAG_MMAP: {
            MultibootTagMmap* m = (MultibootTagMmap*)tag;
            u32 entry_size = m->entry_size;
            if (entry_size >= sizeof(MultibootMmapEntry)) {
                u32 count = (tag->size - 16) / entry_size;
                u8* base = (u8*)&m->entries[0];
                for (u32 i = 0; i < count; i++) {
                    MultibootMmapEntry* e = (MultibootMmapEntry*)(base + i * entry_size);
                    g_info.total_ram_bytes += e->len;
                    if (e->type == 1) g_info.available_ram_bytes += e->len;
                    g_info.mmap_entries++;
                }
            }
            break;
        }
        case MULTIBOOT_TAG_FRAMEBUFFER:
            g_info.framebuffer = true;
            break;
        default:
            break;
        }

        offset += (tag->size + 7) & ~7u;
    }

    if (g_info.total_ram_bytes == 0 && g_info.mem_upper_kb) {
        g_info.total_ram_bytes = ((u64)g_info.mem_upper_kb + 1024ull) * 1024ull;
        g_info.available_ram_bytes = g_info.total_ram_bytes;
    }
}

const BootInfo& info() { return g_info; }

} // namespace multiboot

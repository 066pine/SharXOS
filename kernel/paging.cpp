// SharkXOS - 32-bit paging with an identity map
//
// Paging is required for a real ring 3 transition (the U/S bit).  The kernel
// maps the low 64 MiB identity (physical == virtual) with the user bit set,
// which lets a ring 3 payload fetch its code and use its user stack.  This is
// coarse: kernel memory is user accessible, so ring 3 demonstrates privilege
// transitions (I/O and privileged instructions fault) but not full isolation.
#include <kernel/paging.h>
#include <drivers/console.h>

#define MAPPED_MB 64u
#define TABLES (MAPPED_MB / 4u)

namespace {

alignas(4096) u32 g_directory[1024];
alignas(4096) u32 g_tables[TABLES][1024];

bool g_enabled = false;

} // namespace

namespace paging {

void init() {
    // Map the first 64 MiB, 4 KiB pages, present + writable + user.
    for (u32 t = 0; t < TABLES; t++) {
        for (u32 i = 0; i < 1024; i++) {
            u32 phys = (t * 1024 + i) * 4096u;
            g_tables[t][i] = phys | 0x07;   // P | RW | US
        }
        g_directory[t] = ((u32)&g_tables[t][0]) | 0x07;
    }
    for (u32 i = TABLES; i < 1024; i++) g_directory[i] = 0;

    kstd::write_cr3((u32)&g_directory[0]);
    u32 cr0 = kstd::read_cr0();
    cr0 |= 0x80000000u;      // PG
    cr0 &= ~0x00010000u;     // clear WP so kernel writes ignore read-only bits
    kstd::write_cr0(cr0);

    g_enabled = true;
    klog("mmu", "paging enabled: %u MiB identity mapped, 4 KiB pages, user bit set",
         mapped_mb());
}

bool enabled() { return g_enabled; }
u32 mapped_mb() { return MAPPED_MB; }
u32 directory_physical() { return (u32)&g_directory[0]; }
u32 user_pages() { return MAPPED_MB * 256u; }

} // namespace paging

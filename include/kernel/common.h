// SharkXOS - common kernel types and low level helpers
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;
typedef uint32_t size_t32;

#define SHARKX_VERSION_MAJOR 1
#define SHARKX_VERSION_MINOR 0
#define SHARKX_VERSION_PATCH 0
#define SHARKX_VERSION_STRING "1.0.0"
#define SHARKX_ARCH_STRING "x86 32-bit (IA-32)"

#define PACKED __attribute__((packed))
#define NORETURN __attribute__((noreturn))
#define UNUSED(x) (void)(x)

namespace kstd {

// ---- port I/O -------------------------------------------------------------
static inline void outb(u16 port, u8 val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline u8 inb(u16 port) {
    u8 ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void outw(u16 port, u16 val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline u16 inw(u16 port) {
    u16 ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void io_wait() { outb(0x80, 0); }

// ---- cpu state ------------------------------------------------------------
static inline void cli() { __asm__ volatile("cli" ::: "memory"); }
static inline void sti() { __asm__ volatile("sti" ::: "memory"); }
static inline void hlt() { __asm__ volatile("hlt"); }
static inline void pause() { __asm__ volatile("pause"); }

static inline u32 irq_save() {
    u32 flags;
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    return flags;
}
static inline void irq_restore(u32 flags) {
    if (flags & 0x200) __asm__ volatile("sti" ::: "memory");
}

static inline void cpuid(u32 leaf, u32* a, u32* b, u32* c, u32* d) {
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf));
}

static inline u32 read_cr0() { u32 v; __asm__ volatile("mov %%cr0, %0" : "=r"(v)); return v; }
static inline void write_cr0(u32 v) { __asm__ volatile("mov %0, %%cr0" : : "r"(v) : "memory"); }
static inline u32 read_cr3() { u32 v; __asm__ volatile("mov %%cr3, %0" : "=r"(v)); return v; }
static inline void write_cr3(u32 v) { __asm__ volatile("mov %0, %%cr3" : : "r"(v) : "memory"); }

static inline u32 read_esp() { u32 v; __asm__ volatile("mov %%esp, %0" : "=r"(v)); return v; }

} // namespace kstd

// placement new / delete for freestanding C++
inline void* operator new(size_t, void* p) noexcept { return p; }
inline void* operator new[](size_t, void* p) noexcept { return p; }
inline void operator delete(void*, void*) noexcept {}
inline void operator delete[](void*, void*) noexcept {}

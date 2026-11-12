// SharkXOS - kernel panic
#pragma once
#include <kernel/common.h>
#include <kernel/descriptors.h>

NORETURN void kernel_panic(const char* message);
NORETURN void kernel_panic_regs(const char* message, Registers* r);

namespace panic {
u32 count();
} // namespace panic

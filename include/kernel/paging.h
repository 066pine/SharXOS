// SharkXOS - legacy 32-bit paging (identity map, user accessible)
#pragma once
#include <kernel/common.h>

namespace paging {
void init();
bool enabled();
u32  mapped_mb();
u32  directory_physical();
u32  user_pages();
} // namespace paging

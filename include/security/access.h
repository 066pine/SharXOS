// SharkXOS - access control model
#pragma once
#include <kernel/common.h>

enum Principal : u8 {
    PRINCIPAL_SYSTEM = 0,
    PRINCIPAL_USER = 1,
    PRINCIPAL_RESTRICTED = 2
};

namespace access {
void init();
Principal current();
const char* principal_name(Principal p);
bool set_current(Principal p);
bool is_valid_principal(u8 p);
// SYSTEM bypasses permission checks; an owner always has full rights.
bool check(Principal subject, Principal owner, u8 user_perms, u8 restricted_perms, u8 required);
struct Usage { u32 checks; u32 denials; };
Usage usage();
void note_denial(const char* what);
u32  denials();
} // namespace access

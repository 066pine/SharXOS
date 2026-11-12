// SharkXOS - access control (logical principals; no hardware isolation yet)
#include <security/access.h>
#include <kernel/string.h>

namespace {

Principal g_current = PRINCIPAL_SYSTEM;
u32 g_checks = 0;
u32 g_denials = 0;

} // namespace

namespace access {

void init() {
    g_current = PRINCIPAL_SYSTEM;
    g_checks = 0;
    g_denials = 0;
}

Principal current() { return g_current; }

bool is_valid_principal(u8 p) { return p <= (u8)PRINCIPAL_RESTRICTED; }

bool set_current(Principal p) {
    if (!is_valid_principal((u8)p)) return false;
    g_current = p;
    return true;
}

const char* principal_name(Principal p) {
    switch (p) {
    case PRINCIPAL_SYSTEM: return "SYSTEM";
    case PRINCIPAL_USER: return "USER";
    case PRINCIPAL_RESTRICTED: return "RESTRICTED";
    }
    return "UNKNOWN";
}

bool check(Principal subject, Principal owner, u8 user_perms, u8 restricted_perms, u8 required) {
    g_checks++;
    if (subject == PRINCIPAL_SYSTEM) return true;   // kernel / root context
    if (subject == owner) return true;              // owner always has full rights
    u8 mask = (subject == PRINCIPAL_USER) ? user_perms : restricted_perms;
    bool ok = (mask & required) == required;
    if (!ok) g_denials++;
    return ok;
}

u32 denials() { return g_denials; }

void note_denial(const char*) { g_denials++; }

Usage usage() { Usage u; u.checks = g_checks; u.denials = g_denials; return u; }

} // namespace access

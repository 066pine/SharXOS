// SharkXOS - RAM disk backed virtual file system
#include <fs/vfs.h>
#include <security/access.h>
#include <memory/heap.h>
#include <kernel/string.h>
#include <kernel/time.h>

extern "C" void kernel_lock();
extern "C" void kernel_unlock();

namespace {

VfsFile g_files[VFS_MAX_FILES];
u32 g_used = 0;
VfsStats g_stats;

VfsFile* lookup(const char* name) {
    if (!name) return nullptr;
    for (u32 i = 0; i < VFS_MAX_FILES; i++) {
        if (g_files[i].used && strcmp(g_files[i].name, name) == 0) return &g_files[i];
    }
    return nullptr;
}

bool permitted(VfsFile* f, u8 perm) {
    return access::check(access::current(), f->owner, f->user_perms, f->restricted_perms, perm);
}

void recompute_stats() {
    u32 files = 0;
    for (u32 i = 0; i < VFS_MAX_FILES; i++) if (g_files[i].used) files++;
    g_stats.file_count = files;
    g_stats.capacity_bytes = VFS_RAMDISK_BYTES;
    g_stats.used_bytes = g_used;
    g_stats.free_bytes = VFS_RAMDISK_BYTES - g_used;
}

} // namespace

const char* vfs_error_string(VfsError e) {
    switch (e) {
    case VFS_OK: return "ok";
    case VFS_ENOENT: return "no such file";
    case VFS_EEXIST: return "file exists";
    case VFS_EACCES: return "permission denied";
    case VFS_ENOSPC: return "no space left on device";
    case VFS_EINVAL: return "invalid argument";
    case VFS_ENAMETOOLONG: return "name too long";
    case VFS_EIO: return "i/o error";
    case VFS_EBUSY: return "device busy";
    }
    return "unknown error";
}

namespace vfs {

void init() {
    memset(g_files, 0, sizeof(g_files));
    memset(&g_stats, 0, sizeof(g_stats));
    g_used = 0;
    recompute_stats();
}

bool is_valid_name(const char* name) {
    if (!name) return false;
    size_t n = strlen(name);
    if (n == 0 || n >= VFS_NAME_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        char c = name[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

VfsFile* find(const char* name) { return lookup(name); }

VfsFile* at(u32 index) {
    u32 n = 0;
    for (u32 i = 0; i < VFS_MAX_FILES; i++) {
        if (!g_files[i].used) continue;
        if (n == index) return &g_files[i];
        n++;
    }
    return nullptr;
}

u32 count() {
    u32 n = 0;
    for (u32 i = 0; i < VFS_MAX_FILES; i++) if (g_files[i].used) n++;
    return n;
}

VfsStats stats() {
    recompute_stats();
    return g_stats;
}

VfsError check_access(const char* name, u8 perm) {
    VfsFile* f = lookup(name);
    if (!f) return VFS_ENOENT;
    if (!permitted(f, perm)) return VFS_EACCES;
    return VFS_OK;
}

VfsError create(const char* name, u8 user_perms, u8 restricted_perms) {
    if (!is_valid_name(name)) {
        return strlen(name ? name : "") >= (size_t)VFS_NAME_MAX ? VFS_ENAMETOOLONG : VFS_EINVAL;
    }

    kernel_lock();
    if (lookup(name)) { kernel_unlock(); return VFS_EEXIST; }

    VfsFile* slot = nullptr;
    for (u32 i = 0; i < VFS_MAX_FILES; i++) {
        if (!g_files[i].used) { slot = &g_files[i]; break; }
    }
    if (!slot) { kernel_unlock(); g_stats.enospc_count++; return VFS_ENOSPC; }

    memset(slot, 0, sizeof(VfsFile));
    slot->used = true;
    strncpy(slot->name, name, VFS_NAME_MAX - 1);
    slot->name[VFS_NAME_MAX - 1] = 0;
    slot->owner = access::current();
    slot->user_perms = user_perms;
    slot->restricted_perms = restricted_perms;
    slot->created_tick = (u32)time::ticks();
    slot->modified_tick = slot->created_tick;
    g_stats.created++;
    recompute_stats();
    kernel_unlock();
    return VFS_OK;
}

VfsError remove(const char* name) {
    kernel_lock();
    VfsFile* f = lookup(name);
    if (!f) { kernel_unlock(); return VFS_ENOENT; }
    if (!permitted(f, PERM_DELETE)) { g_stats.permission_denials++; kernel_unlock(); return VFS_EACCES; }

    if (f->data) {
        g_used -= f->capacity;
        heap::kfree(f->data);
    }
    memset(f, 0, sizeof(VfsFile));
    g_stats.deleted++;
    recompute_stats();
    kernel_unlock();
    return VFS_OK;
}

VfsError read(const char* name, u8* buffer, u32 max, u32* out_len) {
    if (!buffer || !out_len) return VFS_EINVAL;
    kernel_lock();
    VfsFile* f = lookup(name);
    if (!f) { kernel_unlock(); return VFS_ENOENT; }
    if (!permitted(f, PERM_READ)) { g_stats.permission_denials++; kernel_unlock(); return VFS_EACCES; }

    u32 n = f->size < max ? f->size : max;
    if (n && f->data) memcpy(buffer, f->data, n);
    *out_len = n;
    f->reads++;
    g_stats.read_ops++;
    kernel_unlock();
    return VFS_OK;
}

static VfsError ensure_capacity(VfsFile* f, u32 needed, bool append_from) {
    if (needed <= f->capacity) return VFS_OK;
    if (needed > VFS_MAX_FILE_BYTES) { g_stats.enospc_count++; return VFS_ENOSPC; }

    u32 new_cap = f->capacity ? f->capacity : 256;
    while (new_cap < needed) new_cap *= 2;
    if (new_cap > VFS_MAX_FILE_BYTES) new_cap = VFS_MAX_FILE_BYTES;

    u32 extra = new_cap - f->capacity;
    if (g_used + extra > VFS_RAMDISK_BYTES) {
        // try a smaller growth step that still fits
        if (needed - f->capacity > VFS_RAMDISK_BYTES - g_used) {
            g_stats.enospc_count++;
            return VFS_ENOSPC;
        }
        new_cap = f->capacity + (VFS_RAMDISK_BYTES - g_used);
        if (new_cap < needed) { g_stats.enospc_count++; return VFS_ENOSPC; }
        extra = new_cap - f->capacity;
    }

    u8* np = (u8*)heap::kmalloc(new_cap);
    if (!np) { g_stats.enospc_count++; return VFS_ENOSPC; }
    u32 copy = append_from ? f->size : 0;
    if (copy && f->data) memcpy(np, f->data, copy);
    if (f->data) heap::kfree(f->data);
    f->data = np;
    f->capacity = new_cap;
    g_used += extra;
    return VFS_OK;
}

VfsError write(const char* name, const u8* data, u32 len, bool append) {
    if (!data && len) return VFS_EINVAL;
    kernel_lock();
    VfsFile* f = lookup(name);
    if (!f) { kernel_unlock(); return VFS_ENOENT; }
    if (!permitted(f, PERM_WRITE)) { g_stats.permission_denials++; kernel_unlock(); return VFS_EACCES; }

    u32 new_size = (append ? f->size : 0) + len;
    if (new_size > VFS_MAX_FILE_BYTES) { g_stats.enospc_count++; kernel_unlock(); return VFS_ENOSPC; }

    VfsError e = ensure_capacity(f, new_size, append);
    if (e != VFS_OK) { kernel_unlock(); return e; }

    if (!append) f->size = 0;
    if (len) {
        memcpy(f->data + f->size, data, len);
        f->size += len;
    }
    f->writes++;
    f->modified_tick = (u32)time::ticks();
    g_stats.write_ops++;
    recompute_stats();
    kernel_unlock();
    return VFS_OK;
}

VfsError truncate(const char* name) {
    kernel_lock();
    VfsFile* f = lookup(name);
    if (!f) { kernel_unlock(); return VFS_ENOENT; }
    if (!permitted(f, PERM_WRITE)) { g_stats.permission_denials++; kernel_unlock(); return VFS_EACCES; }
    f->size = 0;
    f->modified_tick = (u32)time::ticks();
    kernel_unlock();
    return VFS_OK;
}

VfsError rename(const char* old_name, const char* new_name) {
    if (!is_valid_name(new_name)) return VFS_EINVAL;
    kernel_lock();
    VfsFile* f = lookup(old_name);
    if (!f) { kernel_unlock(); return VFS_ENOENT; }
    if (lookup(new_name)) { kernel_unlock(); return VFS_EEXIST; }
    if (!permitted(f, PERM_WRITE)) { g_stats.permission_denials++; kernel_unlock(); return VFS_EACCES; }
    strncpy(f->name, new_name, VFS_NAME_MAX - 1);
    f->name[VFS_NAME_MAX - 1] = 0;
    f->modified_tick = (u32)time::ticks();
    kernel_unlock();
    return VFS_OK;
}

VfsFile* open(const char* name, VfsError* err) {
    VfsFile* f = lookup(name);
    if (!f) { if (err) *err = VFS_ENOENT; return nullptr; }
    if (!permitted(f, PERM_READ)) { if (err) *err = VFS_EACCES; return nullptr; }
    if (err) *err = VFS_OK;
    return f;
}

} // namespace vfs

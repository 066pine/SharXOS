// SharkXOS - virtual file system over a RAM disk
#pragma once
#include <kernel/common.h>
#include <security/access.h>

#define VFS_MAX_FILES 32
#define VFS_NAME_MAX 32
#define VFS_RAMDISK_BYTES (256u * 1024u)
#define VFS_MAX_FILE_BYTES (64u * 1024u)

enum VfsError {
    VFS_OK = 0,
    VFS_ENOENT,
    VFS_EEXIST,
    VFS_EACCES,
    VFS_ENOSPC,
    VFS_EINVAL,
    VFS_ENAMETOOLONG,
    VFS_EIO,
    VFS_EBUSY
};

const char* vfs_error_string(VfsError e);

// Permission bits
#define PERM_READ    0x01
#define PERM_WRITE   0x02
#define PERM_DELETE  0x04
#define PERM_ALL     (PERM_READ | PERM_WRITE | PERM_DELETE)

struct VfsFile {
    bool     used;
    char     name[VFS_NAME_MAX];
    u8*      data;
    u32      size;
    u32      capacity;
    Principal owner;
    u8       user_perms;
    u8       restricted_perms;
    u32      created_tick;
    u32      modified_tick;
    u32      reads;
    u32      writes;
};

struct VfsStats {
    u32 file_count;
    u32 capacity_bytes;
    u32 used_bytes;
    u32 free_bytes;
    u32 created;
    u32 deleted;
    u32 read_ops;
    u32 write_ops;
    u32 permission_denials;
    u32 enospc_count;
};

namespace vfs {
void init();
VfsError create(const char* name, u8 user_perms = PERM_ALL, u8 restricted_perms = PERM_READ);
VfsError remove(const char* name);
VfsError read(const char* name, u8* buffer, u32 max, u32* out_len);
VfsError write(const char* name, const u8* data, u32 len, bool append = false);
VfsError truncate(const char* name);
VfsError rename(const char* old_name, const char* new_name);
VfsFile* find(const char* name);
VfsFile* at(u32 index);
u32      count();
VfsStats stats();
bool     is_valid_name(const char* name);
VfsError check_access(const char* name, u8 perm);
// metadata
VfsFile* open(const char* name, VfsError* err);
} // namespace vfs

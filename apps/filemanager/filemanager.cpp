// SharkXOS - terminal file manager
#include <apps/apps.h>
#include <apps/appui.h>
#include <fs/vfs.h>
#include <security/access.h>
#include <kernel/string.h>
#include <drivers/console.h>

namespace {

u8 g_buf[4096];

void print_usage() {
    kprintf("file manager commands:\n");
    kprintf("  ls               list files with size, owner and permissions\n");
    kprintf("  cat <file>       show a file\n");
    kprintf("  touch <file>     create an empty file\n");
    kprintf("  write <f> <text> append a line of text\n");
    kprintf("  rm <file>        delete a file\n");
    kprintf("  ren <old> <new>  rename a file\n");
    kprintf("  info <file>      file metadata\n");
    kprintf("  df               ramdisk usage\n");
    kprintf("  exit             leave the file manager\n");
}

void list_files() {
    u32 n = vfs::count();
    if (n == 0) { kprintf("(no files)\n"); return; }
    console::set_color(VGA_DARK_GREY, VGA_BLACK);
    kprintf("  %-20s %8s  %-10s %s\n", "name", "size", "owner", "modified");
    console::reset_color();
    for (u32 i = 0; i < VFS_MAX_FILES; i++) {
        VfsFile* f = vfs::at(i);
        if (!f) continue;
        kprintf("  %-20s %8u  %-10s %u ms\n", f->name, f->size,
                access::principal_name(f->owner), f->modified_tick * 10);
    }
    kprintf("%u file(s)\n", n);
}

int command(char* line) {
    kstd::trim(line);
    if (!line[0]) return 0;

    char* argv[6];
    int argc = 0;
    char* p = line;
    while (*p && argc < 6) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;
    }
    if (argc == 0) return 0;

    const char* cmd = argv[0];

    if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) return 1;
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) { print_usage(); return 0; }
    if (strcmp(cmd, "ls") == 0 || strcmp(cmd, "dir") == 0) { list_files(); return 0; }

    if (strcmp(cmd, "df") == 0) {
        VfsStats s = vfs::stats();
        kprintf("ramdisk: %u/%u KiB used, %u KiB free, %u files (%u slots)\n",
                s.used_bytes / 1024, s.capacity_bytes / 1024, s.free_bytes / 1024,
                s.file_count, VFS_MAX_FILES);
        return 0;
    }

    if (strcmp(cmd, "cat") == 0 || strcmp(cmd, "read") == 0) {
        if (argc < 2) { kprintf("usage: cat <file>\n"); return 0; }
        u32 len = 0;
        VfsError e = vfs::read(argv[1], g_buf, sizeof(g_buf) - 1, &len);
        if (e != VFS_OK) { appui::status(vfs_error_string(e), false); return 0; }
        g_buf[len] = 0;
        console::write((const char*)g_buf, len);
        if (!len || g_buf[len - 1] != '\n') console::newline();
        return 0;
    }

    if (strcmp(cmd, "touch") == 0 || strcmp(cmd, "create") == 0) {
        if (argc < 2) { kprintf("usage: touch <file>\n"); return 0; }
        VfsError e = vfs::create(argv[1]);
        if (e == VFS_EEXIST) appui::status("file already exists", false);
        else if (e != VFS_OK) appui::status(vfs_error_string(e), false);
        else { char msg[64]; ksnprintf(msg, sizeof(msg), "created '%s'", argv[1]); appui::status(msg, true); }
        return 0;
    }

    if (strcmp(cmd, "write") == 0) {
        if (argc < 3) { kprintf("usage: write <file> <text>\n"); return 0; }
        if (!vfs::find(argv[1])) {
            VfsError ce = vfs::create(argv[1]);
            if (ce != VFS_OK) { appui::status(vfs_error_string(ce), false); return 0; }
        }
        char text[256];
        int pos = 0;
        for (int i = 2; i < argc; i++) {
            const char* s = argv[i];
            while (*s && pos < (int)sizeof(text) - 2) text[pos++] = *s++;
            if (i + 1 < argc) text[pos++] = ' ';
        }
        text[pos++] = '\n';
        VfsError e = vfs::write(argv[1], (const u8*)text, (u32)pos, true);
        if (e != VFS_OK) appui::status(vfs_error_string(e), false);
        else appui::status("written", true);
        return 0;
    }

    if (strcmp(cmd, "rm") == 0 || strcmp(cmd, "delete") == 0) {
        if (argc < 2) { kprintf("usage: rm <file>\n"); return 0; }
        VfsError e = vfs::remove(argv[1]);
        if (e != VFS_OK) appui::status(vfs_error_string(e), false);
        else appui::status("deleted", true);
        return 0;
    }

    if (strcmp(cmd, "ren") == 0 || strcmp(cmd, "rename") == 0) {
        if (argc < 3) { kprintf("usage: ren <old> <new>\n"); return 0; }
        VfsError e = vfs::rename(argv[1], argv[2]);
        if (e != VFS_OK) appui::status(vfs_error_string(e), false);
        else appui::status("renamed", true);
        return 0;
    }

    if (strcmp(cmd, "info") == 0) {
        if (argc < 2) { kprintf("usage: info <file>\n"); return 0; }
        VfsFile* f = vfs::find(argv[1]);
        if (!f) { appui::status("no such file", false); return 0; }
        kprintf("name      : %s\n", f->name);
        kprintf("size      : %u bytes (capacity %u)\n", f->size, f->capacity);
        kprintf("owner     : %s\n", access::principal_name(f->owner));
        kprintf("reads     : %u\n", f->reads);
        kprintf("writes    : %u\n", f->writes);
        kprintf("created   : tick %u\n", f->created_tick);
        kprintf("modified  : tick %u\n", f->modified_tick);
        kprintf("permissions: user %c%c%c / restricted %c%c%c\n",
                (f->user_perms & PERM_READ) ? 'r' : '-',
                (f->user_perms & PERM_WRITE) ? 'w' : '-',
                (f->user_perms & PERM_DELETE) ? 'd' : '-',
                (f->restricted_perms & PERM_READ) ? 'r' : '-',
                (f->restricted_perms & PERM_WRITE) ? 'w' : '-',
                (f->restricted_perms & PERM_DELETE) ? 'd' : '-');
        return 0;
    }

    console::set_color(VGA_LIGHT_RED, VGA_BLACK);
    kprintf("unknown command '%s' (try help)\n", cmd);
    console::reset_color();
    return 0;
}

} // namespace

namespace apps {

int filemanager_main(int, char**) {
    console::set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    kprintf("SharkXOS file manager - 'help' for commands, 'exit' to leave\n");
    console::reset_color();
    list_files();

    char line[160];
    for (;;) {
        int n = appui::read_line("files> ", line, sizeof(line));
        if (n < 0) continue;
        if (command(line)) break;
    }
    kprintf("file manager closed\n");
    return 0;
}

} // namespace apps

// SharkXOS - terminal text editor
#include <apps/apps.h>
#include <apps/appui.h>
#include <fs/vfs.h>
#include <kernel/string.h>
#include <drivers/console.h>
#include <memory/heap.h>

namespace {

#define ED_MAX_LINES 64
#define ED_LINE_LEN 96
#define ED_FILE_BUF 8192

// The line buffer and the file I/O scratch buffer are allocated from the
// kernel heap when the editor starts and released when it exits.  Keeping the
// buffers off the static BSS means an idle system does not pay ~14 KiB for an
// editor that is not running.
char (*g_lines)[ED_LINE_LEN] = nullptr;
int  g_line_count = 0;
char g_filename[VFS_NAME_MAX];
bool g_modified = false;
bool g_loaded = false;

u8* g_io = nullptr;

void reset_buffer() {
    g_line_count = 0;
    g_modified = false;
    g_loaded = true;
}

bool load_file(const char* name) {
    if (!name || !name[0]) { reset_buffer(); return true; }

    u32 len = 0;
    VfsError e = vfs::read(name, g_io, ED_FILE_BUF - 1, &len);
    if (e == VFS_ENOENT) {
        kprintf("'%s' does not exist yet - it will be created on save\n", name);
        strncpy(g_filename, name, VFS_NAME_MAX - 1);
        g_filename[VFS_NAME_MAX - 1] = 0;
        reset_buffer();
        g_modified = true;
        return true;
    }
    if (e != VFS_OK) {
        kprintf("cannot open '%s': %s\n", name, vfs_error_string(e));
        return false;
    }

    strncpy(g_filename, name, VFS_NAME_MAX - 1);
    g_filename[VFS_NAME_MAX - 1] = 0;
    reset_buffer();

    int line = 0, col = 0;
    for (u32 i = 0; i < len && line < ED_MAX_LINES; i++) {
        char c = (char)g_io[i];
        if (c == '\r') continue;
        if (c == '\n') {
            g_lines[line][col] = 0;
            line++;
            col = 0;
            if (line >= ED_MAX_LINES) break;
            continue;
        }
        if (col < ED_LINE_LEN - 1) g_lines[line][col++] = c;
    }
    if (col > 0 || line == 0) {
        g_lines[line][col] = 0;
        line++;
    }
    g_line_count = line;
    g_modified = false;
    kprintf("loaded '%s' (%d lines, %u bytes)\n", g_filename, g_line_count, len);
    return true;
}

void ensure_filename() {
    if (g_filename[0]) return;
    char name[VFS_NAME_MAX];
    if (appui::read_line("save as: ", name, sizeof(name)) <= 0) return;
    strncpy(g_filename, name, VFS_NAME_MAX - 1);
    g_filename[VFS_NAME_MAX - 1] = 0;
}

bool save_file() {
    ensure_filename();
    if (!g_filename[0]) {
        kprintf("no filename, not saved\n");
        return false;
    }
    if (!vfs::is_valid_name(g_filename)) {
        kprintf("invalid filename '%s'\n", g_filename);
        return false;
    }

    u32 pos = 0;
    for (int i = 0; i < g_line_count; i++) {
        size_t n = strlen(g_lines[i]);
        if (pos + n + 1 >= ED_FILE_BUF) break;
        memcpy(g_io + pos, g_lines[i], n);
        pos += (u32)n;
        g_io[pos++] = '\n';
    }

    if (!vfs::find(g_filename)) {
        VfsError ce = vfs::create(g_filename);
        if (ce != VFS_OK) {
            kprintf("save failed: %s\n", vfs_error_string(ce));
            return false;
        }
    }

    VfsError e = vfs::write(g_filename, g_io, pos, false);
    if (e != VFS_OK) {
        kprintf("save failed: %s\n", vfs_error_string(e));
        return false;
    }
    g_modified = false;
    kprintf("saved %u bytes to '%s'\n", pos, g_filename);
    return true;
}

void list_lines() {
    if (g_line_count == 0) { kprintf("(empty buffer)\n"); return; }
    for (int i = 0; i < g_line_count; i++) {
        console::set_color(VGA_DARK_GREY, VGA_BLACK);
        kprintf("%3d | ", i + 1);
        console::reset_color();
        kprintf("%s\n", g_lines[i]);
    }
}

void insert_line(int at, const char* text) {
    if (g_line_count >= ED_MAX_LINES) { kprintf("buffer full (%d lines)\n", ED_MAX_LINES); return; }
    if (at < 0) at = 0;
    if (at > g_line_count) at = g_line_count;
    for (int i = g_line_count; i > at; i--) {
        strncpy(g_lines[i], g_lines[i - 1], ED_LINE_LEN);
    }
    strncpy(g_lines[at], text, ED_LINE_LEN - 1);
    g_lines[at][ED_LINE_LEN - 1] = 0;
    g_line_count++;
    g_modified = true;
}

void delete_line(int at) {
    if (at < 0 || at >= g_line_count) { kprintf("no such line\n"); return; }
    for (int i = at; i < g_line_count - 1; i++) {
        strncpy(g_lines[i], g_lines[i + 1], ED_LINE_LEN);
    }
    g_line_count--;
    g_modified = true;
    kprintf("deleted line %d\n", at + 1);
}

void print_help() {
    kprintf("editor - line oriented text editor\n");
    kprintf("  <text>        append a line of text at the end\n");
    kprintf("  :l            list all lines\n");
    kprintf("  :p N          print line N\n");
    kprintf("  :i N text     insert text before line N\n");
    kprintf("  :e N text     replace line N\n");
    kprintf("  :d N          delete line N\n");
    kprintf("  :w            write the file        :wq  write and quit\n");
    kprintf("  :q            quit (warns if unsaved)   :q!  quit without saving\n");
    kprintf("  :n name       change the file name\n");
    kprintf("  :s            show file status      :h   this help\n");
}

int editor_command(char* cmd) {
    kstd::trim(cmd);
    if (cmd[0] == 0) return 0;

    if (cmd[0] != ':') {
        insert_line(g_line_count, cmd);
        return 0;
    }

    char* p = cmd + 1;
    while (*p == ' ') p++;

    if (strcmp(p, "q") == 0 || strcmp(p, "quit") == 0) {
        if (g_modified) {
            kprintf("warning: unsaved changes. use :w to save or :q! to discard\n");
            return 0;
        }
        return 1;
    }
    if (strcmp(p, "q!") == 0) return 1;
    if (strcmp(p, "w") == 0) { save_file(); return 0; }
    if (strcmp(p, "wq") == 0 || strcmp(p, "x") == 0) { return save_file() ? 1 : 0; }
    if (strcmp(p, "l") == 0 || strcmp(p, "list") == 0) { list_lines(); return 0; }
    if (strcmp(p, "h") == 0 || strcmp(p, "help") == 0) { print_help(); return 0; }
    if (strcmp(p, "s") == 0) {
        kprintf("file: %s  lines: %d  modified: %s  %.4u bytes approx\n",
                g_filename[0] ? g_filename : "(unnamed)", g_line_count,
                g_modified ? "yes" : "no", 0);
        return 0;
    }
    if (kstd::starts_with(p, "n ")) {
        char* name = p + 2;
        if (vfs::is_valid_name(name)) {
            strncpy(g_filename, name, VFS_NAME_MAX - 1);
            g_filename[VFS_NAME_MAX - 1] = 0;
            kprintf("filename is now '%s'\n", g_filename);
        } else {
            kprintf("invalid filename\n");
        }
        return 0;
    }
    if (kstd::starts_with(p, "p ")) {
        int n = kstd::to_int(p + 2, nullptr);
        if (n < 1 || n > g_line_count) { kprintf("no such line\n"); return 0; }
        kprintf("%3d | %s\n", n, g_lines[n - 1]);
        return 0;
    }
    if (kstd::starts_with(p, "d ")) {
        delete_line(kstd::to_int(p + 2, nullptr) - 1);
        return 0;
    }
    if (kstd::starts_with(p, "i ")) {
        char* rest = p + 2;
        int n = 0;
        while (*rest >= '0' && *rest <= '9') { n = n * 10 + (*rest - '0'); rest++; }
        while (*rest == ' ') rest++;
        insert_line(n - 1, rest);
        kprintf("inserted before line %d\n", n);
        return 0;
    }
    if (kstd::starts_with(p, "e ")) {
        char* rest = p + 2;
        int n = 0;
        while (*rest >= '0' && *rest <= '9') { n = n * 10 + (*rest - '0'); rest++; }
        while (*rest == ' ') rest++;
        if (n < 1 || n > g_line_count) { kprintf("no such line\n"); return 0; }
        strncpy(g_lines[n - 1], rest, ED_LINE_LEN - 1);
        g_lines[n - 1][ED_LINE_LEN - 1] = 0;
        g_modified = true;
        kprintf("line %d updated\n", n);
        return 0;
    }

    kprintf("unknown command (try :h)\n");
    return 0;
}

void release_buffers() {
    if (g_lines) { heap::kfree(g_lines); g_lines = nullptr; }
    if (g_io)    { heap::kfree(g_io);    g_io = nullptr; }
}

int editor_fail() {
    release_buffers();
    return 1;
}

} // namespace

namespace apps {

int editor_main(int argc, char** argv) {
    g_lines = (char (*)[ED_LINE_LEN])
        heap::kzalloc((size_t)ED_MAX_LINES * ED_LINE_LEN);
    g_io = (u8*)heap::kmalloc(ED_FILE_BUF);
    if (!g_lines || !g_io) {
        kprintf("editor: out of memory (needs %u bytes)\n",
                (unsigned)(ED_MAX_LINES * ED_LINE_LEN + ED_FILE_BUF));
        return editor_fail();
    }

    console::set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    kprintf("SharkXOS text editor - ':h' for help, ':q' to quit\n");
    console::reset_color();

    if (argc > 1) {
        if (!load_file(argv[1])) return editor_fail();
    } else {
        char name[VFS_NAME_MAX];
        kprintf("open file (leave empty for a new buffer): ");
        int n = appui::read_line("", name, sizeof(name));
        if (n > 0) {
            if (!load_file(name)) return editor_fail();
        } else {
            reset_buffer();
            g_filename[0] = 0;
        }
    }

    if (g_line_count > 0) list_lines();

    char line[ED_LINE_LEN + 32];
    for (;;) {
        char prompt[48];
        ksnprintf(prompt, sizeof(prompt), "edit:%s%s> ",
                  g_filename[0] ? g_filename : "(new)", g_modified ? "*" : "");
        int n = appui::read_line(prompt, line, sizeof(line));
        if (n < 0) continue;
        if (editor_command(line)) break;
    }
    kprintf("editor closed\n");
    release_buffers();
    return 0;
}

} // namespace apps

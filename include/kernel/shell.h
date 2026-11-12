// SharkXOS - shell
#pragma once
#include <kernel/common.h>

#define SHELL_LINE_MAX 128
#define SHELL_ARGS_MAX 8

typedef int (*ShellCommand)(int argc, char** argv);

struct ShellCommandEntry {
    const char* name;
    const char* help;
    ShellCommand handler;
};

namespace shell {
void init();
void print_banner();
void prompt();
bool execute_line(const char* line);   // returns false to request shutdown
int  loop();                            // never returns normally
void register_command(const ShellCommandEntry& cmd);
void list_commands();
// helpers shared with applications
int  arg_to_int(const char* s, int fallback);
void print_error(const char* msg);
extern const char* prompt_string;
} // namespace shell

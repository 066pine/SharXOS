// SharkXOS - scientific calculator
#include <apps/apps.h>
#include <lib/kmath.h>
#include <kernel/string.h>
#include <drivers/console.h>
#include <drivers/keyboard.h>
#include <memory/heap.h>

namespace {

// ------------------------------------------------------------- expression parser
struct Parser {
    const char* src;
    const char* p;
    bool ok;
    const char* error;
    bool degrees;
    double ans;
};

void fail(Parser& ps, const char* msg) {
    if (ps.ok) { ps.ok = false; ps.error = msg; }
}

void skip_ws(Parser& ps) {
    while (*ps.p == ' ' || *ps.p == '\t') ps.p++;
}

bool eat(Parser& ps, char c) {
    skip_ws(ps);
    if (*ps.p == c) { ps.p++; return true; }
    return false;
}

double parse_expr(Parser& ps);

bool is_ident_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool is_ident_char(char c) { return is_ident_start(c) || (c >= '0' && c <= '9'); }

void read_ident(Parser& ps, char* out, int max) {
    int n = 0;
    skip_ws(ps);
    while (is_ident_char(*ps.p) && n < max - 1) out[n++] = kstd::to_lower(*ps.p++);
    out[n] = 0;
}

bool match_ident(const char* a, const char* b) { return strcmp(a, b) == 0; }

double call_function(Parser& ps, const char* name) {
    // two argument functions
    if (match_ident(name, "ncr") || match_ident(name, "npr") || match_ident(name, "c") ||
        match_ident(name, "p")) {
        if (!eat(ps, '(')) { fail(ps, "expected '('"); return 0; }
        double a = parse_expr(ps);
        if (!eat(ps, ',')) { fail(ps, "expected ','"); return 0; }
        double b = parse_expr(ps);
        if (!eat(ps, ')')) { fail(ps, "expected ')'"); return 0; }
        bool ok = true;
        if (a < 0 || b < 0 || a != kmath::floor(a) || b != kmath::floor(b)) {
            fail(ps, "arguments must be non-negative integers");
            return 0;
        }
        int n = (int)a, r = (int)b;
        double v = match_ident(name, "ncr") || match_ident(name, "c")
                       ? kmath::ncr(n, r, &ok)
                       : kmath::npr(n, r, &ok);
        if (!ok) fail(ps, "invalid combination/permutation");
        return v;
    }

    if (!eat(ps, '(')) { fail(ps, "expected '(' after function name"); return 0; }
    double x = parse_expr(ps);
    if (!eat(ps, ')')) { fail(ps, "expected ')'"); return 0; }
    if (!ps.ok) return 0;

    bool overflow = false;
    if (match_ident(name, "sin")) return kmath::sin(ps.degrees ? kmath::deg2rad(x) : x);
    if (match_ident(name, "cos")) return kmath::cos(ps.degrees ? kmath::deg2rad(x) : x);
    if (match_ident(name, "tan")) {
        double c = kmath::cos(ps.degrees ? kmath::deg2rad(x) : x);
        if (kmath::fabs(c) < 1e-15) { fail(ps, "tan undefined at this angle"); return 0; }
        return kmath::tan(ps.degrees ? kmath::deg2rad(x) : x);
    }
    if (match_ident(name, "asin")) {
        if (x < -1.0 || x > 1.0) { fail(ps, "asin domain error (|x| <= 1)"); return 0; }
        double r = kmath::asin(x);
        return ps.degrees ? kmath::rad2deg(r) : r;
    }
    if (match_ident(name, "acos")) {
        if (x < -1.0 || x > 1.0) { fail(ps, "acos domain error (|x| <= 1)"); return 0; }
        double r = kmath::acos(x);
        return ps.degrees ? kmath::rad2deg(r) : r;
    }
    if (match_ident(name, "atan")) {
        double r = kmath::atan(x);
        return ps.degrees ? kmath::rad2deg(r) : r;
    }
    if (match_ident(name, "sqrt")) {
        if (x < 0) { fail(ps, "sqrt domain error (negative argument)"); return 0; }
        return kmath::sqrt(x);
    }
    if (match_ident(name, "ln")) {
        if (x <= 0) { fail(ps, "ln domain error (argument must be > 0)"); return 0; }
        return kmath::ln(x);
    }
    if (match_ident(name, "log")) {
        if (x <= 0) { fail(ps, "log domain error (argument must be > 0)"); return 0; }
        return kmath::log10(x);
    }
    if (match_ident(name, "exp")) return kmath::exp(x);
    if (match_ident(name, "abs")) return kmath::fabs(x);
    if (match_ident(name, "floor")) return kmath::floor(x);
    if (match_ident(name, "ceil")) return kmath::ceil(x);
    if (match_ident(name, "round")) return kmath::round(x);
    if (match_ident(name, "fact")) {
        if (x < 0 || x != kmath::floor(x) || x > 170) { fail(ps, "fact expects 0..170"); return 0; }
        return kmath::factorial((int)x, &overflow);
    }

    fail(ps, "unknown function");
    return 0;
}

double parse_number(Parser& ps) {
    skip_ws(ps);
    bool any = false;
    double v = 0;
    while (*ps.p >= '0' && *ps.p <= '9') { v = v * 10.0 + (*ps.p - '0'); ps.p++; any = true; }
    if (*ps.p == '.') {
        ps.p++;
        double s = 0.1;
        while (*ps.p >= '0' && *ps.p <= '9') { v += (*ps.p - '0') * s; s *= 0.1; ps.p++; any = true; }
    }
    if (!any) { fail(ps, "expected a number"); return 0; }
    if (*ps.p == 'e' || *ps.p == 'E') {
        const char* save = ps.p;
        ps.p++;
        bool neg = false;
        if (*ps.p == '-') { neg = true; ps.p++; } else if (*ps.p == '+') ps.p++;
        if (*ps.p >= '0' && *ps.p <= '9') {
            int e = 0;
            while (*ps.p >= '0' && *ps.p <= '9') { e = e * 10 + (*ps.p - '0'); ps.p++; }
            if (e > 308) { fail(ps, "exponent overflow"); return 0; }
            double mul = 1.0;
            for (int i = 0; i < e; i++) mul *= 10.0;
            v = neg ? v / mul : v * mul;
        } else {
            ps.p = save;
        }
    }
    return v;
}

double parse_primary(Parser& ps) {
    skip_ws(ps);

    if (*ps.p == '(') {
        ps.p++;
        double v = parse_expr(ps);
        if (!eat(ps, ')')) { fail(ps, "unbalanced parentheses"); return 0; }
        return v;
    }

    if ((*ps.p >= '0' && *ps.p <= '9') || *ps.p == '.') return parse_number(ps);

    if (is_ident_start(*ps.p)) {
        char name[16];
        read_ident(ps, name, sizeof(name));
        if (match_ident(name, "pi")) return kmath::PI;
        if (match_ident(name, "e")) return kmath::E;
        if (match_ident(name, "ans")) return ps.ans;
        return call_function(ps, name);
    }

    fail(ps, "unexpected character");
    return 0;
}

double parse_postfix(Parser& ps) {
    double v = parse_primary(ps);
    for (;;) {
        skip_ws(ps);
        if (*ps.p != '!') break;
        ps.p++;
        if (v < 0 || v != kmath::floor(v) || v > 170) {
            fail(ps, "factorial needs an integer in 0..170");
            return 0;
        }
        bool ovf = false;
        v = kmath::factorial((int)v, &ovf);
        if (ovf) { fail(ps, "factorial overflow"); return 0; }
    }
    return v;
}

double parse_unary(Parser& ps) {
    skip_ws(ps);
    if (*ps.p == '-') { ps.p++; return -parse_unary(ps); }
    if (*ps.p == '+') { ps.p++; return parse_unary(ps); }
    return parse_postfix(ps);
}

double parse_power(Parser& ps) {
    double base = parse_unary(ps);
    skip_ws(ps);
    if (*ps.p == '^') {
        ps.p++;
        double exp = parse_unary(ps);      // right associative
        if (!ps.ok) return 0;
        double r = kmath::pow(base, exp);
        if (kmath::is_nan(r)) { fail(ps, "invalid power (domain error)"); return 0; }
        if (kmath::is_inf(r)) { fail(ps, "power overflow"); return 0; }
        return r;
    }
    return base;
}

bool starts_atom(Parser& ps) {
    skip_ws(ps);
    char c = *ps.p;
    return (c >= '0' && c <= '9') || c == '.' || c == '(' || is_ident_start(c);
}

double parse_term(Parser& ps) {
    double v = parse_power(ps);
    for (;;) {
        skip_ws(ps);
        if (*ps.p == '*') { ps.p++; v *= parse_power(ps); }
        else if (*ps.p == '/') {
            ps.p++;
            double d = parse_power(ps);
            if (d == 0.0) { fail(ps, "division by zero"); return 0; }
            v /= d;
        } else if (*ps.p == '%') {
            ps.p++;
            double d = parse_power(ps);
            if (d == 0.0) { fail(ps, "modulo by zero"); return 0; }
            v = kmath::fmod(v, d);
        } else if (starts_atom(ps)) {
            v *= parse_power(ps);          // implicit multiplication: 2pi, 3(4+1)
        } else {
            break;
        }
        if (!ps.ok) return 0;
        if (kmath::is_inf(v) || kmath::is_nan(v)) {
            fail(ps, "arithmetic overflow");
            return 0;
        }
    }
    return v;
}

double parse_expr(Parser& ps) {
    double v = parse_term(ps);
    for (;;) {
        skip_ws(ps);
        if (*ps.p == '+') { ps.p++; v += parse_term(ps); }
        else if (*ps.p == '-') { ps.p++; v -= parse_term(ps); }
        else break;
        if (!ps.ok) return 0;
        if (kmath::is_inf(v) || kmath::is_nan(v)) { fail(ps, "arithmetic overflow"); return 0; }
    }
    return v;
}

// -------------------------------------------------------------- application
bool g_degrees = false;
double g_memory = 0;
double g_ans = 0;
char g_render[64];

struct HistoryEntry {
    char expr[64];
    char result[32];
};
HistoryEntry g_history[8];
u32 g_history_count = 0;
u32 g_history_next = 0;

void add_history(const char* expr, const char* result) {
    strncpy(g_history[g_history_next].expr, expr, sizeof(g_history[0].expr) - 1);
    g_history[g_history_next].expr[sizeof(g_history[0].expr) - 1] = 0;
    strncpy(g_history[g_history_next].result, result, sizeof(g_history[0].result) - 1);
    g_history[g_history_next].result[sizeof(g_history[0].result) - 1] = 0;
    g_history_next = (g_history_next + 1) % 8;
    if (g_history_count < 8) g_history_count++;
}

void print_help() {
    kprintf("calculator commands:\n");
    kprintf("  <expression>   evaluate, e.g. 2+3*4, sin(pi/2), 5!, sqrt(2), 2^10\n");
    kprintf("  deg / rad      switch angle mode (currently %s)\n", g_degrees ? "DEG" : "RAD");
    kprintf("  m+ <expr>      store a value in memory     mr  recall\n");
    kprintf("  mc             clear memory                ans last result\n");
    kprintf("  hist           show recent calculations\n");
    kprintf("  bin|hex|dec <expr>   convert result to base 2/16/10\n");
    kprintf("  ncr(n,r) npr(n,r)    combinations / permutations\n");
    kprintf("  functions: sin cos tan asin acos atan ln log sqrt exp abs floor ceil\n");
    kprintf("             round fact(n)  constants: pi e  operators: + - * / %% ^ !\n");
    kprintf("  exit / quit    leave the calculator\n");
}

void print_conversion(const char* text, int base) {
    Parser ps;
    ps.src = text;
    ps.p = text;
    ps.ok = true;
    ps.error = nullptr;
    ps.degrees = g_degrees;
    ps.ans = g_ans;

    double v = parse_expr(ps);
    skip_ws(ps);
    if (ps.ok && *ps.p != 0) fail(ps, "trailing characters");
    if (!ps.ok) {
        console::set_color(VGA_LIGHT_RED, VGA_BLACK);
        kprintf("error: %s\n", ps.error ? ps.error : "invalid expression");
        console::reset_color();
        return;
    }
    if (v != kmath::floor(v) || kmath::fabs(v) > 2147483647.0) {
        kprintf("base conversion needs an integer that fits in 32 bits\n");
        return;
    }
    long long iv = (long long)v;
    u32 uv = (u32)(iv < 0 ? -iv : iv);
    char buf[40];
    kstd::itoa_u32(uv, buf, base);
    const char* prefix = base == 2 ? "0b" : (base == 16 ? "0x" : "");
    kprintf("%s%s%s = %lld decimal\n", iv < 0 ? "-" : "", prefix, buf, iv);
}

int run_line(const char* line) {
    char buf[160];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    kstd::trim(buf);
    if (!buf[0]) return 0;

    if (strcmp(buf, "exit") == 0 || strcmp(buf, "quit") == 0) return 1;
    if (strcmp(buf, "help") == 0 || strcmp(buf, "?") == 0) { print_help(); return 0; }
    if (strcmp(buf, "deg") == 0) { g_degrees = true; kprintf("angle mode: degrees\n"); return 0; }
    if (strcmp(buf, "rad") == 0) { g_degrees = false; kprintf("angle mode: radians\n"); return 0; }
    if (strcmp(buf, "mc") == 0) { g_memory = 0; kprintf("memory cleared\n"); return 0; }
    if (strcmp(buf, "mr") == 0) {
        kmath::format_double(g_memory, g_render, sizeof(g_render));
        kprintf("memory = %s\n", g_render);
        return 0;
    }

    if (kstd::starts_with(buf, "m+")) {
        const char* expr = buf + 2;
        Parser ps;
        ps.src = expr;
        ps.p = expr;
        ps.ok = true;
        ps.error = nullptr;
        ps.degrees = g_degrees;
        ps.ans = g_ans;
        double v = parse_expr(ps);
        skip_ws(ps);
        if (!ps.ok || *ps.p != 0) {
            kprintf("usage: m+ <number or expression>\n");
            return 0;
        }
        g_memory += v;
        kmath::format_double(g_memory, g_render, sizeof(g_render));
        kprintf("memory = %s\n", g_render);
        return 0;
    }

    if (strcmp(buf, "hist") == 0) {
        if (g_history_count == 0) { kprintf("history is empty\n"); return 0; }
        for (u32 i = 0; i < g_history_count; i++) {
            u32 idx = (g_history_next + 8 - g_history_count + i) % 8;
            kprintf("  %s = %s\n", g_history[idx].expr, g_history[idx].result);
        }
        return 0;
    }

    if (kstd::starts_with(buf, "bin ")) { print_conversion(buf + 4, 2); return 0; }
    if (kstd::starts_with(buf, "hex ")) { print_conversion(buf + 4, 16); return 0; }
    if (kstd::starts_with(buf, "dec ")) { print_conversion(buf + 4, 10); return 0; }

    Parser ps;
    ps.src = buf;
    ps.p = buf;
    ps.ok = true;
    ps.error = nullptr;
    ps.degrees = g_degrees;
    ps.ans = g_ans;

    double v = parse_expr(ps);
    skip_ws(ps);
    if (ps.ok && *ps.p != 0) fail(ps, "trailing characters");

    if (!ps.ok) {
        console::set_color(VGA_LIGHT_RED, VGA_BLACK);
        kprintf("error: %s\n", ps.error ? ps.error : "invalid expression");
        console::reset_color();
        return 0;
    }
    if (kmath::is_nan(v)) {
        kprintf("error: result is not a number\n");
        return 0;
    }
    if (kmath::is_inf(v)) {
        kprintf("error: overflow (result out of range)\n");
        return 0;
    }

    g_ans = v;
    kmath::format_double(v, g_render, sizeof(g_render));
    console::set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    kprintf("= %s\n", g_render);
    console::reset_color();
    add_history(buf, g_render);
    return 0;
}

} // namespace

namespace apps {

int calculator_main(int, char**) {
    console::set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    kprintf("SharkXOS scientific calculator - type 'help' for commands, 'exit' to leave\n");
    console::reset_color();

    char line[160];
    for (;;) {
        console::set_color(VGA_YELLOW, VGA_BLACK);
        kprintf("calc [%s]> ", g_degrees ? "deg" : "rad");
        console::reset_color();

        int len = 0;
        for (;;) {
            char c = keyboard::getchar();
            if (c == '\n') { console::putc('\n'); break; }
            if (c == '\b' || c == 127) {
                if (len > 0) { len--; console::backspace(); }
                continue;
            }
            if (c == 3) { len = 0; console::write("^C\n"); break; }
            if (c < 32) continue;
            if (len < (int)sizeof(line) - 1) { line[len++] = c; console::putc(c); }
        }
        line[len] = 0;
        if (run_line(line)) break;
    }
    kprintf("calculator closed\n");
    return 0;
}

} // namespace apps

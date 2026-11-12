// SharkXOS - freestanding math library (no libm, no host OS)
#include <lib/kmath.h>
#include <kernel/string.h>

namespace kmath {

const double PI = 3.14159265358979323846;
const double E  = 2.71828182845904523536;

static const double LN2  = 0.69314718055994530942;
static const double LN10 = 2.30258509299404568402;
static const double INV_LN2 = 1.44269504088896340736;

double fabs(double x) {
    union { double d; u64 u; } v;
    v.d = x;
    v.u &= 0x7FFFFFFFFFFFFFFFULL;
    return v.d;
}

bool is_nan(double x) { return x != x; }

bool is_inf(double x) {
    if (is_nan(x)) return false;
    double a = fabs(x);
    return a > 1.7976931348623157e308;
}

double floor(double x) {
    if (is_nan(x) || is_inf(x)) return x;
    if (fabs(x) >= 9.2e18) return x;
    double t = (double)(long long)x;   // truncation toward zero
    if (t > x) t -= 1.0;
    return t;
}

double ceil(double x) {
    if (is_nan(x) || is_inf(x)) return x;
    if (fabs(x) >= 9.2e18) return x;
    double t = (double)(long long)x;
    if (t < x) t += 1.0;
    return t;
}

double round(double x) { return (x >= 0.0) ? floor(x + 0.5) : ceil(x - 0.5); }

double fmod(double x, double y) {
    if (y == 0.0 || is_nan(x) || is_nan(y) || is_inf(x)) return 0.0 / 0.0;
    if (is_inf(y)) return x;
    double q = x / y;
    if (fabs(q) < 9.2e18) q = (double)(long long)q;
    else q = (q > 0) ? 9.2e18 : -9.2e18;
    double r = x - q * y;
    if (fabs(r) >= fabs(y)) {
        if (r > 0) r -= fabs(y); else r += fabs(y);
    }
    return r;
}

double sqrt(double x) {
    if (x < 0.0) return 0.0 / 0.0;
    if (x == 0.0) return 0.0;
    if (is_inf(x)) return x;
    double r;
    __asm__ volatile("fsqrt" : "=t"(r) : "0"(x));
    return r;
}

// ---------------------------------------------------------------- log / exp
static double ln_series(double m) {
    // ln(m) for m in [0.5, 2) using atanh series
    double z = (m - 1.0) / (m + 1.0);
    double z2 = z * z;
    double term = z;
    double sum = term;
    for (int n = 3; n < 40; n += 2) {
        term *= z2;
        sum += term / (double)n;
    }
    return 2.0 * sum;
}

static int ilog2(double x) {
    // nearest integer k with 2^k <= x < 2^(k+1), x > 0
    int k = 0;
    while (x >= 2.0) { x *= 0.5; k++; }
    while (x < 1.0)  { x *= 2.0; k--; }
    return k;
}

double ln(double x) {
    if (is_nan(x) || x < 0.0) return 0.0 / 0.0;
    if (x == 0.0) return -1.0 / 0.0;
    if (is_inf(x)) return x;
    int k = ilog2(x);
    double m = x;
    // scale m into [0.75, 1.5) for best convergence
    for (int i = 0; i < k; i++) m *= 0.5;
    for (int i = 0; i > k; i--) m *= 2.0;
    return (double)k * LN2 + ln_series(m);
}

double log10(double x) { return ln(x) / LN10; }
double log2(double x)  { return ln(x) * INV_LN2; }

double exp(double x) {
    if (is_nan(x)) return x;
    if (x > 709.78) return 1.0 / 0.0;
    if (x < -745.13) return 0.0;
    int k = (int)round(x * INV_LN2);
    double r = x - (double)k * LN2;
    // e^r, |r| <= ln2/2
    double term = 1.0;
    double sum = 1.0;
    for (int n = 1; n < 22; n++) {
        term *= r / (double)n;
        sum += term;
    }
    // scale by 2^k
    double scale = 1.0;
    if (k > 0) { for (int i = 0; i < k; i++) scale *= 2.0; }
    else if (k < 0) { for (int i = 0; i < -k; i++) scale *= 0.5; }
    return sum * scale;
}

double pow(double b, double e) {
    if (is_nan(b) || is_nan(e)) return 0.0 / 0.0;
    if (e == 0.0) return 1.0;
    if (b == 0.0) {
        if (e > 0.0) return 0.0;
        return 1.0 / 0.0;
    }
    if (b < 0.0) {
        double ei = round(e);
        if (fabs(e - ei) > 1e-12) return 0.0 / 0.0;   // non-integer power of negative base
        double mag = exp(ei * ln(-b));
        // odd integer exponent keeps the sign
        if (fmod(fabs(ei), 2.0) > 0.5) return -mag;
        return mag;
    }
    return exp(e * ln(b));
}

// ------------------------------------------------------------- trig functions
static double reduce_2pi(double x) {
    if (fabs(x) < 1e6) {
        double two_pi = 2.0 * PI;
        double n = round(x / two_pi);
        x -= n * two_pi;
    } else {
        x = fmod(x, 2.0 * PI);
    }
    if (x > PI) x -= 2.0 * PI;
    if (x < -PI) x += 2.0 * PI;
    return x;
}

static double sin_core(double x) {
    // |x| <= pi/4
    double x2 = x * x;
    double term = x;
    double sum = x;
    const double fact_coeffs[] = {
        -1.0 / 6.0, 1.0 / 120.0, -1.0 / 5040.0, 1.0 / 362880.0,
        -1.0 / 39916800.0, 1.0 / 6227020800.0, -1.0 / 1307674368000.0,
        1.0 / 355687428096000.0
    };
    for (int i = 0; i < 8; i++) {
        term *= x2;
        sum += term * fact_coeffs[i];
    }
    return sum;
}

static double cos_core(double x) {
    double x2 = x * x;
    double term = 1.0;
    double sum = 1.0;
    const double c[] = {
        -1.0 / 2.0, 1.0 / 24.0, -1.0 / 720.0, 1.0 / 40320.0,
        -1.0 / 3628800.0, 1.0 / 479001600.0, -1.0 / 87178291200.0,
        1.0 / 20922789888000.0
    };
    for (int i = 0; i < 8; i++) {
        term *= x2;
        sum += term * c[i];
    }
    return sum;
}

double sin(double x) {
    if (is_nan(x) || is_inf(x)) return 0.0 / 0.0;
    x = reduce_2pi(x);
    if (x < 0) return -sin(-x);
    if (x <= PI / 4.0) return sin_core(x);
    if (x <= PI / 2.0) return cos_core(PI / 2.0 - x);
    return sin_core(PI - x);
}

double cos(double x) {
    if (is_nan(x) || is_inf(x)) return 0.0 / 0.0;
    x = reduce_2pi(x);
    if (x < 0) x = -x;
    if (x <= PI / 4.0) return cos_core(x);
    if (x <= PI / 2.0) return sin_core(PI / 2.0 - x);
    return -cos_core(PI - x);
}

double tan(double x) {
    double c = cos(x);
    if (fabs(c) < 1e-15) return (x >= 0) ? 1.0 / 0.0 : -1.0 / 0.0;
    return sin(x) / c;
}

double atan(double x) {
    if (is_nan(x)) return x;
    if (is_inf(x)) return (x > 0) ? PI / 2.0 : -PI / 2.0;
    if (x < 0) return -atan(-x);
    double offset = 0.0;
    if (x > 1.0) return PI / 2.0 - atan(1.0 / x);
    if (x > 0.41421356237309503) {
        x = (x - 1.0) / (x + 1.0);
        offset = PI / 4.0;
    }
    double x2 = x * x;
    double term = x;
    double sum = x;
    for (int n = 3; n < 60; n += 2) {
        term *= -x2;
        sum += term / (double)n;
    }
    return offset + sum;
}

double atan2(double y, double x) {
    if (x > 0) return atan(y / x);
    if (x < 0) return atan(y / x) + (y >= 0 ? PI : -PI);
    if (y > 0) return PI / 2.0;
    if (y < 0) return -PI / 2.0;
    return 0.0;
}

double asin(double x) {
    if (x > 1.0 || x < -1.0) return 0.0 / 0.0;
    if (x == 1.0) return PI / 2.0;
    if (x == -1.0) return -PI / 2.0;
    return atan(x / sqrt(1.0 - x * x));
}

double acos(double x) {
    if (x > 1.0 || x < -1.0) return 0.0 / 0.0;
    return PI / 2.0 - asin(x);
}

double sinh(double x) { double e = exp(x); return (e - 1.0 / e) * 0.5; }
double cosh(double x) { double e = exp(x); return (e + 1.0 / e) * 0.5; }
double tanh(double x) {
    if (x > 20.0) return 1.0;
    if (x < -20.0) return -1.0;
    double e = exp(2.0 * x);
    return (e - 1.0) / (e + 1.0);
}

double deg2rad(double d) { return d * PI / 180.0; }
double rad2deg(double r) { return r * 180.0 / PI; }

double factorial(int n, bool* overflow) {
    if (overflow) *overflow = false;
    if (n < 0) { if (overflow) *overflow = true; return 0.0; }
    if (n > 170) { if (overflow) *overflow = true; return 1.0 / 0.0; }
    double r = 1.0;
    for (int i = 2; i <= n; i++) r *= (double)i;
    return r;
}

double ncr(int n, int r, bool* ok) {
    if (ok) *ok = true;
    if (n < 0 || r < 0 || r > n) { if (ok) *ok = false; return 0.0; }
    if (r > n - r) r = n - r;
    double res = 1.0;
    for (int i = 1; i <= r; i++) {
        res = res * (double)(n - r + i) / (double)i;
        if (is_inf(res)) { if (ok) *ok = false; return res; }
    }
    return round(res);
}

double npr(int n, int r, bool* ok) {
    if (ok) *ok = true;
    if (n < 0 || r < 0 || r > n) { if (ok) *ok = false; return 0.0; }
    double res = 1.0;
    for (int i = 0; i < r; i++) {
        res *= (double)(n - i);
        if (is_inf(res)) { if (ok) *ok = false; return res; }
    }
    return res;
}

// ------------------------------------------------------------- number output
static void reverse_str(char* s, int n) {
    for (int i = 0; i < n / 2; i++) {
        char t = s[i];
        s[i] = s[n - 1 - i];
        s[n - 1 - i] = t;
    }
}

static int u32_to_dec(u32 v, char* out) {
    char tmp[12];
    int i = 0;
    if (v == 0) { out[0] = '0'; return 1; }
    while (v > 0 && i < 11) { tmp[i++] = (char)('0' + v % 10u); v /= 10u; }
    int j = 0;
    while (i > 0) out[j++] = tmp[--i];
    return j;
}

int format_fixed(double v, char* out, int out_size, int decimals) {
    if (out_size <= 0) return 0;
    if (decimals < 0) decimals = 0;
    if (decimals > 9) decimals = 9;

    int pos = 0;
    bool neg = false;
    if (is_nan(v)) { strncpy(out, "nan", (size_t)out_size); return 3; }
    if (is_inf(v)) {
        const char* s = (v < 0) ? "-inf" : "inf";
        strncpy(out, s, (size_t)out_size);
        return (int)strlen(s);
    }
    if (v < 0) { neg = true; v = -v; }

    // rounding
    double scale = 1.0;
    for (int i = 0; i < decimals; i++) scale *= 10.0;
    v += 0.5 / scale;

    u32 ip = 0;
    if (v < 4294967295.0) ip = (u32)v;
    else {
        // too large for fixed notation: fall back to scientific
        char tmp[32];
        int n = format_double(v, tmp, sizeof(tmp), 12);
        if (neg && pos + 1 < out_size) out[pos++] = '-';
        for (int i = 0; i < n && pos + 1 < out_size; i++) out[pos++] = tmp[i];
        out[pos] = 0;
        return pos;
    }

    if (neg) { if (pos + 1 < out_size) out[pos++] = '-'; }

    char tmp[12];
    int n = u32_to_dec(ip, tmp);
    for (int i = 0; i < n && pos + 1 < out_size; i++) out[pos++] = tmp[i];

    if (decimals > 0) {
        if (pos + 1 < out_size) out[pos++] = '.';
        double frac = v - (double)(u32)v;
        for (int i = 0; i < decimals && pos + 1 < out_size; i++) {
            frac *= 10.0;
            int d = (int)frac;
            if (d < 0) d = 0;
            if (d > 9) d = 9;
            out[pos++] = (char)('0' + d);
            frac -= (double)d;
        }
    }
    out[pos] = 0;
    return pos;
}

static void strip_trailing_zeros(char* s) {
    int n = (int)strlen(s);
    if (n == 0) return;
    // only strip if there is a decimal point and no exponent
    char* ep = strchr(s, 'e');
    if (ep) return;
    if (!strchr(s, '.')) return;
    while (n > 0 && s[n - 1] == '0') s[--n] = 0;
    if (n > 0 && s[n - 1] == '.') s[--n] = 0;
}

int format_double(double v, char* out, int out_size, int max_sig) {
    if (out_size <= 0) return 0;
    if (max_sig < 1) max_sig = 1;
    if (max_sig > 12) max_sig = 12;

    if (is_nan(v)) { strncpy(out, "nan", (size_t)out_size); return 3; }
    if (is_inf(v)) {
        const char* s = (v < 0) ? "-inf" : "inf";
        strncpy(out, s, (size_t)out_size);
        return (int)strlen(s);
    }
    if (v == 0.0) { strncpy(out, "0", (size_t)out_size); return 1; }

    double av = fabs(v);
    int e10 = (int)floor(log10(av));
    // guard against log10 rounding exactly at powers of ten
    double p = 1.0;
    for (int i = 0; i < e10; i++) p *= 10.0;
    if (p > 0 && av >= p * 10.0) e10++;
    if (p > 0 && av < p) e10--;

    char buf[48];
    if (e10 >= 12 || e10 <= -7) {
        // scientific: normalise to [1,10)
        double m = av;
        int e = e10;
        for (int i = 0; i < e; i++) m /= 10.0;
        for (int i = 0; i > e; i--) m *= 10.0;
        int decimals = max_sig - 1;
        if (decimals < 0) decimals = 0;
        int n = format_fixed(m, buf, sizeof(buf), decimals);
        strip_trailing_zeros(buf);
        char* p2 = buf + strlen(buf);
        if ((int)(p2 - buf) < (int)sizeof(buf) - 8) {
            *p2++ = 'e';
            if (e >= 0) *p2++ = '+';
            else { *p2++ = '-'; e = -e; }
            char et[8];
            int en = u32_to_dec((u32)e, et);
            for (int i = 0; i < en; i++) *p2++ = et[i];
            *p2 = 0;
        }
        int len = (int)strlen(buf);
        int pos = 0;
        if (v < 0 && pos + 1 < out_size) out[pos++] = '-';
        for (int i = 0; i < len && pos + 1 < out_size; i++) out[pos++] = buf[i];
        out[pos] = 0;
        return pos;
    }

    int decimals = max_sig - 1 - e10;
    if (decimals < 0) decimals = 0;
    if (decimals > 9) decimals = 9;
    format_fixed(v, buf, sizeof(buf), decimals);
    strip_trailing_zeros(buf);
    strncpy(out, buf, (size_t)out_size);
    out[out_size - 1] = 0;
    return (int)strlen(out);
}

} // namespace kmath

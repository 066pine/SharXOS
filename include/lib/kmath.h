// SharkXOS - math library (freestanding, no libm)
#pragma once
#include <kernel/common.h>

namespace kmath {
extern const double PI;
extern const double E;

double fabs(double x);
double floor(double x);
double ceil(double x);
double round(double x);
double fmod(double x, double y);
double sqrt(double x);
double exp(double x);
double ln(double x);          // natural log
double log10(double x);
double log2(double x);
double pow(double b, double e);
double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double sinh(double x);
double cosh(double x);
double tanh(double x);
double deg2rad(double d);
double rad2deg(double r);
bool   is_nan(double x);
bool   is_inf(double x);

// Factorial / combinatorics (exact up to 20!)
double factorial(int n, bool* overflow);
double ncr(int n, int r, bool* ok);
double npr(int n, int r, bool* ok);

// Number formatting used by the calculator
// Returns number of characters written (excluding NUL).
int format_double(double v, char* out, int out_size, int max_sig = 12);
int format_fixed(double v, char* out, int out_size, int decimals);
} // namespace kmath

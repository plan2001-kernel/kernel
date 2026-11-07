#include "p9compat.h"
/*
 * ISO C's math: musl's (lib/libm_dbl; exp, log and pow from lib/musl;
 * as exact as CPython's libm) over libc's of the same names, which it
 * replaces when linked first; sqrt and fabs are libc's; p9math.c has
 * the rest
 */
#ifndef P9MATH_H
#define P9MATH_H
#define NAN NaN()
#define INFINITY Inf(1)
#define HUGE_VAL Inf(1)
#define isnan(x) isNaN(x)
#define isinf(x) isInf((x), 0)
#define isfinite(x) (!isNaN(x) && !isInf((x), 0))
#define signbit(x) p9signbit(x)
/* FLT_EVAL_METHOD 0: SSE */
typedef float float_t;
typedef double double_t;
#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4
int __fpclassify(double);
int p9signbit(double);
double copysign(double, double);
double trunc(double);
double nearbyint(double);
double rint(double);
double round(double);
double nan(const char *);
double scalbn(double, int);
double acosh(double);
double asinh(double);
double atanh(double);
double expm1(double);
double log1p(double);
double erf(double);
double erfc(double);
double tgamma(double);
double lgamma(double);
#endif

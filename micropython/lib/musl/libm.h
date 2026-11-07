/*
 * What musl's src/internal/libm.h gives exp.c, log.c, pow.c and
 * __math_*.c (README), for Plan 9's 6c: SSE doubles (FLT_EVAL_METHOD 0),
 * round to nearest, no fma, no signaling NaNs.  Plan2001's.
 */
#ifndef _LIBM_H
#define _LIBM_H

#include <stdint.h>
#include <float.h>
#include <math.h>

#define WANT_ROUNDING 1
#define WANT_SNAN 0
#define issignaling_inline(x) 0
#define TOINT_INTRINSICS 0
#define __FP_FAST_FMA 0

#define predict_true(x) (x)
#define predict_false(x) (x)

static inline uint64_t asuint64(double f) {
	union {double f; uint64_t i;} u;

	u.f = f;
	return u.i;
}

static inline double asdouble(uint64_t i) {
	union {uint64_t i; double f;} u;

	u.i = i;
	return u.f;
}

/* doubles are evaluated as doubles (SSE) */
#define eval_as_double(x) (x)

/* the compiler may not move or drop x (the exceptions it raises) */
static inline double fp_barrier(double x) {
	volatile double y;

	y = x;
	return y;
}

static inline void fp_force_eval(double x) {
	volatile double y;

	y = x;
	(void)y;
}

double __math_oflow(uint32_t);
double __math_uflow(uint32_t);
double __math_xflow(uint32_t, double);
double __math_invalid(double);
double __math_divzero(uint32_t);

#endif

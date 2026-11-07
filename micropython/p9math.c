/* ISO C's math that neither Plan 9's libc nor lib/libm_dbl has (include/math.h). */
#include "p9compat.h"
#include <math.h>

int p9signbit(double x) {
    FPdbleword w;

    w.x = x;
    return (w.hi >> 31) != 0;
}

double copysign(double x, double y) {
    FPdbleword w, s;

    w.x = x;
    s.x = y;
    w.hi = (w.hi & 0x7fffffff) | (s.hi & 0x80000000);
    return w.x;
}

double nan(const char *s) {
    (void)s;
    return NaN();
}

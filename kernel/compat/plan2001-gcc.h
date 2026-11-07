/*
 * Plan2001-O GCC compatibility helpers.
 *
 * This header is force-included only by the opt-in Linux GCC compile path
 * (tools/build-gcc.sh) with -DPLAN2001_GCC.  The 9front 6c/6l build never
 * sees it, so the canonical bootable path is unchanged.
 *
 * The helpers are deliberately small.  They do not redefine core Plan 9
 * primitive types: in particular, `long` remains the host LP64 type.  The
 * GCC objects are compile-only evidence; see docs/GCC-PORT.md for the
 * primitive-width and calling-convention limits.
 */
#ifndef PLAN2001_GCC_COMPAT_H
#define PLAN2001_GCC_COMPAT_H

#ifndef PLAN2001_GCC
#define PLAN2001_GCC 1
#endif

/*
 * 6c accepts USED(...) as a discard expression.  GCC has no such builtin;
 * expand it to a normal discarded expression.  The kernel passes one or
 * two arguments.
 */
#ifndef USED
#define USED(...) ((void)0)
#endif

/*
 * 6c accepts SET(x) to mark a local as assigned before use.  It has no
 * run-time effect; GCC needs no equivalent.  Do not define the generic
 * name SET: libc's atomic64.c has its own SET(A,T,n) macro.
 */
#define PLAN2001_SET(x) ((void)0)

#endif

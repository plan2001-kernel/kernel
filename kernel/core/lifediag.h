/*
 * Opt-in lifecycle accounting for process and capability resources.
 *
 * This is a build-time diagnostic only (PLAN2001_LIFECYCLE_DIAG).  It adds
 * no syscall, object kind or ABI field; the emitted counters are consumed by
 * tools/host/t30/run-lifecycle-diag.sh from the serial log.  The counters
 * describe live allocations, not pointers or persistent identity.
 */
#pragma once

#ifdef PLAN2001_LIFECYCLE_DIAG
void lifeproc(int delta);
ulong lifedelegateid(void);
void lifedelegate(int kind, ulong id, int delta);
void lifefpu(int delta);
void lifefpudiscard(int fpsave, int kfpsave);
#else
#define lifeproc(delta)
#define lifedelegateid() 0
#define lifedelegate(kind, id, delta)
#define lifefpu(delta)
#define lifefpudiscard(fpsave, kfpsave)
#endif

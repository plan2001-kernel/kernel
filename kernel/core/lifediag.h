/*
 * Opt-in lifecycle accounting for Pgrp and OContext (issue #30).
 *
 * This is a build-time diagnostic only (PLAN2001_LIFECYCLE_DIAG).  It adds
 * no syscall, object kind or ABI field; the emitted counters are consumed by
 * tools/host/t30/run-lifecycle-diag.sh from the serial log.  The counters
 * describe live allocations, not pointers or persistent identity.
 */
#pragma once

#ifdef PLAN2001_LIFECYCLE_DIAG
void lifepgrp(void *pgrp, int delta);
void lifecontrol(void *pgrp, int delta);
void lifeoctx(int delta);
#else
#define lifepgrp(pgrp, delta)
#define lifecontrol(pgrp, delta)
#define lifeoctx(delta)
#endif

/*
 * Native PS/2 keyboard process hooks.
 *
 * OBJOPEN claims the controller through kbdscancodeacquire() and installs a
 * VkindKbd Handle in the single Vhand table; OBJINVOKE Read returns raw
 * scan-code bytes, never decoded KeyEvents.  OBJINVOKE KbdLeds stays an
 * eve-only capability because LED state is a shared hardware control, not
 * scan-code authority.  This file keeps only the exec/exit hooks.
 */
#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../core/error.h"
#include	"../amd64/kbdapi.h"
#include	"../../runtime/abi/vobj.h"
#include	"../core/vobjint.h"

void
kbdprocexit(Proc *p)
{
	USED(p);	/* Vhand teardown releases the exclusive claim */
}

void
kbdprocexec(Proc *p)
{
	USED(p);	/* Vhand teardown releases the exclusive claim */
}

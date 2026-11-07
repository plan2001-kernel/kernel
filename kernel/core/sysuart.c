/*
 * Native UART process hooks.
 *
 * A UART open is a VkindUart Handle in the one process-local Vhand table,
 * not a Uarthtab entry.  The mode fixes the default Vread/Vwrite rights and
 * Vcontrol covers control/status; OBJOPEN and OBJINVOKE are the only entry
 * points.  This file keeps only the exec/exit hooks.
 */
#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../core/error.h"
#include	"../drivers/uart/uartnative.h"
#include	"../../runtime/abi/vobj.h"
#include	"../core/vobjint.h"

void
uartprocexit(Proc *p)
{
	USED(p);	/* Vhand teardown releases the port */
}

void
uartprocexec(Proc *p)
{
	USED(p);	/* Vhand teardown releases the port */
}

/*
 * errstr() over the typed OBJINVOKE operation, not the legacy ERRSTR(41)
 * trap.  The operation uses handle 0 because it only exchanges the calling
 * process's own error buffers; it opens no object Handle and does not depend
 * on Handle-table capacity.  The kernel side remains generrstr(), so the
 * read/set swap protocol is byte-for-byte the historical one.
 */
#include <u.h>
#include <libc.h>
#include "../../abi/obj.h"

int
errstr(char *buf, uint nbuf)
{
	ObjCall c;

	memset(&c, 0, sizeof c);
	c.version = OBJ_ABI_VERSION;
	c.op = ObjOpErrstrExchange;
	c.out = buf;
	c.outlen = nbuf;
	return obj_invoke(0, &c, sizeof c);
}

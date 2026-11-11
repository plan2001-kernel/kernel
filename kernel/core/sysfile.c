#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"

#include	"../core/error.h"

/*
 * The canonical amd64 process has no reachable pathname or FD producer.
 * The historical pathname/FS and always-failing FD syscall numbers are
 * reserved and dispatch to nothing; this file retains only the functioning
 * process-local native output stream.
 */

enum {
	Iomax	= 64*1024,
};

/*
 * Plan2001: process-local native output stream.  The stream number is not an
 * fd, Chan or pathname: it is authorized by the Proc's iomask (or, for
 * Iohout, an explicit stdout Pipe) and the backend is putstrn(): kernel log
 * collection and any attached console sinks.  A UART or framebuffer is not
 * required to accept output.
 *
 * The former IOWRITE(62) syscall is retired.  Its only in-tree user is the
 * MicroPython console, which now opens an OBJ console Handle and writes with
 * OBJINVOKE(Write); this helper is the shared kernel-side implementation.
 * The buffer is a kernel address and n<=Iomax is the caller's contract.
 */
int
iowriteok(int h)
{
	ulong bit;

	if(h < Iohout || h > Iohdiag)
		return 0;
	bit = 1UL << (h-1);
	return (up->iomask & bit) != 0;
}

long
iowritek(int h, void *data, long n)
{
	if(!iowriteok(h))
		error(Ebadarg);
	if(n < 0 || n > Iomax)
		error(Ebadarg);
	if(n == 0)
		return 0;
	if(data == nil)
		error(Ebadarg);
	putstrn(data, (int)n);
	return n;
}

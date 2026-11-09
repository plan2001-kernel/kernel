/*
 * Native Pipe process hooks.
 *
 * The Pipe provider is a VkindPipe capability in the process-local Vhand
 * table; there is no separate Pipehtab and no raw syscall entry point here.
 * OBJCREATE/OBJINVOKE/OBJCLOSE and OSPAWN drive the provider in sysvobj.c.
 * This file keeps only the RFORK/exec/exit, OSPAWN and bootstrap-output
 * hooks the core calls.  Pipe stays a blocking byte stream, not an
 * ObjectFIFO and not a file descriptor.
 */
#include	"u.h"
#include	"../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../core/error.h"
#include	"../../runtime/abi/vobj.h"
#include	"../core/vobjint.h"

void
pipeprocexit(Proc *p)
{
	p->stdoutpipe = 0;
}

void
pipeprocexec(Proc *p)
{
	p->stdoutpipe = 0;
}

/* A console route is only a selected Pipe right, not hardware authority. */
void
pipeoutputset(Proc *p, int h)
{
	vobjpipeoutputset(p, h);
}

long
pipeoutputwrite(int h, void *data, long n)
{
	return vobjpipeoutputwrite(h, data, n);
}

/* Trusted bootstrap only: install one already-open native endpoint. */
int
pipekernelinstall(Proc *proc, void *pipe, int end)
{
	return vobjpipekernelinstall(proc, pipe, end);
}

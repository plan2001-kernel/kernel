#include "u.h"
#include <oplanboot.h>

/*
 * Architecture-independent early boot, see include/oplanboot.h.  Keep this
 * file free of kernel services: both the AMD64 kernel and the experimental
 * ARM64 boot kernel call it at a point where only the CPU and the BootInfo
 * blob are known to work.
 */

static int
fail(char *err, int errlen, const char *why)
{
	int i;

	if(err != nil && errlen > 0){
		for(i = 0; i < errlen-1 && why[i] != 0; i++)
			err[i] = why[i];
		err[i] = 0;
	}
	return 0;
}

/* [off, off+len) lies within the blob, after the header */
static int
inblob(BootInfo *b, u64int off, u64int len)
{
	return off >= b->headersize && off <= b->totalsize
	    && len <= b->totalsize - off;
}

/* do [a, a+al) and [b, b+bl), both within the blob, share a byte? */
static int
overlap(u64int a, u64int al, u64int b, u64int bl)
{
	return al != 0 && bl != 0 && a < b + bl && b < a + al;
}

static u32int
be32(uchar *p)
{
	return (u32int)p[0]<<24 | (u32int)p[1]<<16 | (u32int)p[2]<<8 | p[3];
}

int
oplanbootvalidate(BootInfo *b, u64int blob, u32int arch, char *err, int errlen)
{
	uchar *p, *fdt;
	u64int off[4], len[4], mlen;
	int i, j;

	if(b == nil || blob == 0 || (blob & (OplanBootPageSize-1)) != 0)
		return fail(err, errlen, "blob address");
	if(b->magic != BootInfoMagic || b->version != BootInfoVersion)
		return fail(err, errlen, "bad magic or version");
	if(b->headersize < OplanBootMinHeader || b->headersize > OplanBootPageSize
	|| b->totalsize < b->headersize)
		return fail(err, errlen, "bad header size");
	if(b->arch != arch)
		return fail(err, errlen, "blob made for another ISA");

	/* every section after the header, within the blob, none overlapping */
	off[0] = b->configoff, len[0] = b->configlen;
	off[1] = b->logoff, len[1] = b->loglen;
	off[2] = b->fdtoff, len[2] = b->fdtlen;
	mlen = (u64int)b->mmapcount * b->mmapentsize;
	off[3] = b->mmapoff, len[3] = mlen;
	for(i = 0; i < 4; i++){
		if(!inblob(b, off[i], len[i]))
			return fail(err, errlen, "section outside blob");
		for(j = 0; j < i; j++)
			if(overlap(off[i], len[i], off[j], len[j]))
				return fail(err, errlen, "sections overlap");
	}
	if(b->configlen == 0 || b->mmapcount == 0
	|| b->mmapentsize < sizeof(BootMem))
		return fail(err, errlen, "missing config or memory map");
	p = (uchar*)(uintptr)blob;
	if(p[b->configoff + b->configlen - 1] != 0)
		return fail(err, errlen, "config is not NUL-terminated");

	/* a device tree, if there is one, is a whole one */
	if(b->fdtlen != 0){
		fdt = p + b->fdtoff;
		if(b->fdtlen < 40 || be32(fdt) != 0xd00dfeed
		|| be32(fdt+4) > b->fdtlen)
			return fail(err, errlen, "bad device tree");
	}

	if(err != nil && errlen > 0)
		err[0] = 0;
	return 1;
}

int
oplanbootmemclass(u32int type)
{
	switch(type){
	case BootMemLoaderCode:
	case BootMemLoaderData:
	case BootMemBootCode:
	case BootMemBootData:
	case BootMemConventional:
		return OplanBootRAM;
	case BootMemACPIReclaim:
		return OplanBootACPI;
	case BootMemReserved:
	case BootMemRuntimeCode:
	case BootMemRuntimeData:
	case BootMemUnusable:
	case BootMemACPINVS:
	case BootMemMMIO:
	case BootMemMMIOPort:
	case BootMemPalCode:
		return OplanBootReserved;
	}
	return -1;
}

u64int
oplanbootram(BootInfo *b, uchar *blob)
{
	BootMem *m;
	u64int total;
	int i;

	total = 0;
	for(i = 0; i < b->mmapcount; i++){
		m = (BootMem*)(blob + b->mmapoff + (u64int)i*b->mmapentsize);
		if(oplanbootmemclass(m->type) == OplanBootRAM)
			total += m->len;
	}
	return total;
}

static char*
putu64(char *p, u64int v)
{
	char t[24];
	int n;

	n = 0;
	do{
		t[n++] = '0' + v%10;
		v /= 10;
	}while(v != 0);
	while(n > 0)
		*p++ = t[--n];
	return p;
}

static void
line(void (*out)(void*, const char*), void *arg, const char *s,
     u64int v, const char *tail)
{
	char buf[160], *p;
	const char *q;

	p = buf;
	for(q = s; *q != 0 && p < buf+sizeof buf-1; q++)
		*p++ = *q;
	p = putu64(p, v);
	for(q = tail; *q != 0 && p < buf+sizeof buf-1; q++)
		*p++ = *q;
	*p = 0;
	out(arg, buf);
}

u64int
oplanbootannounce(void (*out)(void*, const char*), void *arg, BootInfo *b,
    uchar *blob)
{
	u64int ram;

	if(out == nil || b == nil || blob == nil)
		return 0;
	ram = oplanbootram(b, blob);
	out(arg, "PLAN2001-O: shared boot: BootInfo v1 accepted\n");
	switch(b->arch){
	case BootArchAmd64:
		out(arg, "PLAN2001-O: shared boot: arch AMD64\n");
		break;
	case BootArchArm64:
		out(arg, "PLAN2001-O: shared boot: arch ARM64\n");
		break;
	case BootArchRiscv64:
		out(arg, "PLAN2001-O: shared boot: arch RISC-V 64\n");
		break;
	case BootArchWasm32:
		out(arg, "PLAN2001-O: shared boot: arch WASM32\n");
		break;
	default:
		out(arg, "PLAN2001-O: shared boot: arch unknown\n");
		break;
	}
	line(out, arg, "PLAN2001-O: shared boot: RAM ",
	    ram/(1024*1024), " MiB\n");
	return ram;
}

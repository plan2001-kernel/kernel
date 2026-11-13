/* Kernel console session input and native output streams. */
#include "p9compat.h"
#include <keyboard.h>
#include "../../runtime/abi/obj.h"
#include "../../runtime/abi/vobj.h"
#include "p9console.h"

enum {
	Iohout	= 1,
	Iohdiag	= 2,
};

static int nativeout = 0;
static int outh = -1;
static int diagh = -1;

/* Region session transport.  The input region is read-only here (the console
 * kernel is its only writer); the output and diagnostic regions are writable
 * here (the kernel maps them read-only).  Each region carries an ObjRingHeader
 * and fixed slots. */
static int sessin = -1, sessout = -1, sessdiag = -1;
static char sesserr[128];
static uintptr_t inbase, outbase, diagbase;
static unsigned long long inlast, outseq, diagseq;
static unsigned char inbuf[OBJ_RING_DATA];
static unsigned inbufpos, inbuflen;

static unsigned long long
ring_load(unsigned long long *p)
{
	return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

static void
ring_store(unsigned long long *p, unsigned long long v)
{
	__atomic_store_n(p, v, __ATOMIC_RELEASE);
}

static int
ringvalid(uintptr_t base)
{
	ObjRingHeader *h = (ObjRingHeader*)base;

	return h->version == OBJ_RING_VERSION &&
		h->magic == OBJ_RING_MAGIC &&
		h->slot_size == OBJ_RING_SLOT && h->slots == OBJ_RING_SLOTS;
}

static int
ringwrite(uintptr_t base, unsigned long long *seqp, const void *data, size_t n, unsigned flags)
{
	const unsigned char *p = data;
	unsigned long long seq, off;
	unsigned int len;

	while(n != 0){
		len = n > OBJ_RING_DATA ? OBJ_RING_DATA : (unsigned int)n;
		seq = ++*seqp;
		off = OBJ_RING_HEADER +
			((seq-1) % OBJ_RING_SLOTS)*OBJ_RING_SLOT;
		ring_store((unsigned long long*)(base+off), 2*seq-1);
		*(volatile unsigned int*)(base+off+8) = len;
		*(volatile unsigned int*)(base+off+12) = flags;
		memmove((void*)(base+off+16), p, len);
		__atomic_thread_fence(__ATOMIC_RELEASE);
		ring_store((unsigned long long*)(base+off), 2*seq);
		ring_store((unsigned long long*)(base+OBJ_RING_PUBOFF), seq);
		region_wake(base, OBJ_RING_PUBOFF, 1);
		p += len;
		n -= len;
	}
	return 0;
}

long
p9_console_attach(int inh, int outhnd, int diaghnd)
{
	uintptr_t ib, ob, db;
	ObjRingHeader *h;

	if(inh < 0 || outhnd < 0)
		return -1;
	ib = (uintptr_t)obj_region_map(inh, 0);
	if((long)ib < 0){
		rerrstr(sesserr, sizeof sesserr);
		return -2;
	}
	ob = (uintptr_t)obj_region_map(outhnd, 0);
	if((long)ob < 0){
		rerrstr(sesserr, sizeof sesserr);
		return -3;
	}
	db = 0;
	if(diaghnd >= 0){
		db = (uintptr_t)obj_region_map(diaghnd, 0);
		if((long)db < 0)
			return -4;
		if(!ringvalid(db))
			return -5;
	}
	if(!ringvalid(ib))
		return -6;
	if(!ringvalid(ob))
		return -7;
	sessin = inh;
	sessout = outhnd;
	sessdiag = diaghnd;
	inbase = ib;
	outbase = ob;
	diagbase = db;
	h = (ObjRingHeader*)ib;
	inlast = ring_load(&h->write_seq);
	h = (ObjRingHeader*)ob;
	outseq = ring_load(&h->write_seq);
	if(db != 0){
		h = (ObjRingHeader*)db;
		diagseq = ring_load(&h->write_seq);
	}else
		diagseq = 0;
	inbufpos = inbuflen = 0;
	return 0;
}

const char *
p9_console_session_error(void)
{
	return sesserr;
}

static int
session_read(void)
{
	ObjRingHeader *h;
	unsigned long long seq, slot;
	unsigned int len;
	long r;

	for(;;){
		if(inbufpos < inbuflen)
			return inbuf[inbufpos++];
		h = (ObjRingHeader*)inbase;
		seq = ring_load(&h->write_seq);
		while(inlast < seq){
			inlast++;
			slot = OBJ_RING_HEADER +
				((inlast-1) % OBJ_RING_SLOTS)*OBJ_RING_SLOT;
			if(ring_load((unsigned long long*)(inbase+slot)) != 2*inlast)
				continue;
			len = *(volatile unsigned int*)(inbase+slot+8);
			if(len > OBJ_RING_DATA)
				continue;
			memmove(inbuf, (void*)(inbase+slot+16), len);
			if(ring_load((unsigned long long*)(inbase+slot)) != 2*inlast)
				continue;
			inbufpos = 0;
			inbuflen = len;
			if(len != 0)
				return inbuf[inbufpos++];
		}
		r = region_wait(inbase, OBJ_RING_PUBOFF, inlast, -1);
		if(r == 3)
			return -1;	/* writer/session ended */
		if(r < 0)
			return -2;
	}
}

static int
console_open(int stream)
{
	ObjOpenConsoleReq req;

	req.stream = stream;
	req.reserved = 0;
	return (int)obj_open(ObjOpenConsole, &req, sizeof req, 0, nil);
}

void
p9_console_native(int on)
{
	nativeout = on ? -1 : 0;
}

/* A spawned child may have the native output grant without an -O volume.
 * Probe it without writing: the grant itself authorizes the stream. */
void
p9_console_probe(void)
{
	/* The trusted kernel installs exactly three region capabilities after
	 * OEXECUTE: RO input, writable stdout and writable diagnostics.
	 * No process searches for or claims the physical keyboard here. */
	VobjListRec list[96];
	long count, i;
	int inh = -1, out = -1, diag = -1;
	count = obj_list(list, 96);
	if(count >= 0 && count <= 96){
		for(i=0; i<count; i++){
			if(list[i].kind != VkindRegion)
				continue;
			if(list[i].rights & Vwrite){
				if(out < 0) out = (int)list[i].handle;
				else if(diag < 0) diag = (int)list[i].handle;
				else return;
			}else if(list[i].rights & Vread){
				if(inh >= 0) return;
				inh = (int)list[i].handle;
			}
		}
		if(inh >= 0 && out >= 0 && diag >= 0){
			if(p9_console_attach(inh, out, diag) == 0)
				return;
			p9_dprintf("console: inherited session invalid: %s\\n",
				p9_console_session_error());
		}
	}
	if(nativeout == 0){
		if(outh < 0)
			outh = console_open(Iohout);
		if(outh >= 0)
			nativeout = 1;
	}
}

int
p9_console_is_session(void)
{
	return sessin >= 0;
}

int
p9_native_output(void)
{
	if(nativeout < 0){
		if(outh < 0)
			outh = console_open(Iohout);
		nativeout = outh >= 0;
	}
	return nativeout;
}

void p9_console_restore(void) {
    sessin = sessout = sessdiag = -1;

}

// The native keyboard is raw by construction; there is no consctl channel.
void p9_console_raw(int on) {
    (void)on;
}

int
p9_console_init_native(void)
{
    return sessin >= 0 ? 0 : -1;
}

// -1 at EOF/without the native keyboard, -2 on a read error.
int p9_console_read(void) {
    if (sessin >= 0) return session_read();
    return -1;
}

/* VM-loop hook: consume one pending Ctrl-C record from the session input and
 * let MicroPython raise KeyboardInterrupt.  Normal typed-ahead input is left
 * untouched for the REPL to read. */
int p9_console_poll_interrupt(void)
{
	ObjRingHeader *h;
	unsigned long long seq, slot;
	unsigned int len, flags;
	unsigned char c;

	if(sessin < 0)
		return 0;
	if(inbufpos < inbuflen){
		if(inbuf[inbufpos] != 3)
			return 0;
		inbufpos = inbuflen;
		return 1;
	}
	h = (ObjRingHeader*)inbase;
	seq = ring_load(&h->write_seq);
	if(inlast >= seq)
		return 0;
	slot = OBJ_RING_HEADER +
		((inlast) % OBJ_RING_SLOTS)*OBJ_RING_SLOT;
	if(ring_load((unsigned long long*)(inbase+slot)) != 2*(inlast+1))
		return 0;
	len = *(volatile unsigned int*)(inbase+slot+8);
	flags = *(volatile unsigned int*)(inbase+slot+12);
	if(len != 1 || flags != 0)
		return 0;
	c = *(volatile unsigned char*)(inbase+slot+16);
	if(c != 3)
		return 0;
	inlast++;
	return 1;
}

int p9_console_write(const char *data, size_t size) {
    long n;
    if (sessout >= 0)
        return ringwrite(outbase, &outseq, data, size, 0);
    if (p9_native_output()) {
        while (size) {
            size_t m = size > 4096 ? 4096 : size;
            n = obj_write(outh, (void *)data, (unsigned long long)m, 0);
            if (n != (long)m) return -1;
            data += m;
            size -= m;
        }
        return 0;
    }
    return -1;
}

/*
 * Bounded diagnostic formatter using the authorized DIAGNOSTIC Handle.
 */
void
p9_dprintf(char *fmt, ...)
{
    char buf[2048];
    va_list va;
    int n;

    va_start(va, fmt);
    n = vsnprint(buf, sizeof buf, fmt, va);
    va_end(va);
    if (n <= 0) return;
    if (sessdiag >= 0) {
        ringwrite(diagbase, &diagseq, buf, (size_t)n, 1);
        return;
    }
    if (sessout >= 0) {
        ringwrite(outbase, &outseq, buf, (size_t)n, 1);
        return;
    }
    if (diagh < 0)
        diagh = console_open(Iohdiag);
    if (diagh >= 0)
        obj_write(diagh, buf, (unsigned long long)n, 0);
}

unsigned long long p9_console_msec(void) {
    return (unsigned long long)nsec() / 1000000;
}

void p9_console_exit(int failed) {
    p9_console_restore();
    exits(failed ? "micropython" : nil);
}

unsigned int
p9_console_columns(void)
{
    ObjCall c;
    ObjConsoleInfo info;
    memset(&c, 0, sizeof c);
    c.version = OBJ_ABI_VERSION;
    c.op = ObjOpConsoleInfo;
    c.out = &info;
    c.outlen = sizeof info;
    if(sessin < 0 || obj_invoke(sessin, &c, sizeof c) < 0 ||
       info.version != 1 || info.columns < 8) return 0;
    return info.columns;
}

#include	"u.h"
#include	"../lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../error.h"
#include	"pipecore.h"

struct Pipe
{
	QLock qlk;
	int	ref;
	Queue	*q[2];
	int	qref[2];
};

static void
pipeinit(void)
{
	if(conf.pipeqsize == 0)
		conf.pipeqsize = 256*1024;
	/* make pipeqsize a multiple of our i/o unit */
	conf.pipeqsize = qiomaxatomic * ((conf.pipeqsize + qiomaxatomic-1)/qiomaxatomic);
}

static Pipe*
newpipe(void)
{
	Pipe *p;

	p = malloc(sizeof(Pipe));
	if(p == 0)
		exhausted("memory");
	p->ref = 1;
	p->qref[0] = p->qref[1] = 0;

	p->q[0] = qopen(conf.pipeqsize, 0, 0, 0);
	if(p->q[0] == 0){
		free(p);
		exhausted("memory");
	}
	p->q[1] = qopen(conf.pipeqsize, 0, 0, 0);
	if(p->q[1] == 0){
		qfree(p->q[0]);
		free(p);
		exhausted("memory");
	}
	return p;
}

/*
 *  create a native pair with both endpoints already open
 */
void*
pipecorepair(void)
{
	Pipe *p;

	pipeinit();
	p = newpipe();
	p->qref[0] = p->qref[1] = 1;
	p->ref = 2;
	return p;
}

long
pipecoreread(void *v, int end, void *va, long n)
{
	Pipe *p;

	p = v;
	if(end != 0 && end != 1)
		error(Ebadarg);
	return qread(p->q[end], va, n);
}

static long
pipecorewrite1(void *v, int end, void *va, long n, int note)
{
	Pipe *p;

	p = v;
	if(end != 0 && end != 1)
		error(Ebadarg);

	if(waserror()){
		/* avoid notes when pipe is a mounted queue */
		if(note)
			postnote(up, 1, "sys: write on closed pipe", NUser);
		nexterror();
	}
	n = qwrite(p->q[1-end], va, n);
	poperror();
	return n;
}

long
pipecorewrite(void *v, int end, void *va, long n)
{
	return pipecorewrite1(v, end, va, n, 1);
}

/*
 *  drop one endpoint reference and tear down when the pair is done
 */
void
pipecoreclose(void *v, int end)
{
	Pipe *p;

	p = v;
	if(end != 0 && end != 1)
		error(Ebadarg);

	qlock(&p->qlk);

	if(p->qref[end] <= 0){
		qunlock(&p->qlk);
		error(Ebadarg);
	}
	p->qref[end]--;
	if(p->qref[end] == 0){
		qhangup(p->q[1-end], 0);
		qclose(p->q[end]);
	}

	/*
	 *  if both sides are closed, they are reusable
	 */
	if(p->qref[0] == 0 && p->qref[1] == 0){
		qreopen(p->q[0]);
		qreopen(p->q[1]);
	}

	/*
	 *  free the structure on last close
	 */
	p->ref--;
	if(p->ref == 0){
		qunlock(&p->qlk);
		free(p->q[0]);
		free(p->q[1]);
		free(p);
	} else
		qunlock(&p->qlk);
}

/* Kernel service replies use errors, never a user note on the parentless
 * service worker if its userspace controller closes during a write. */
long
pipecorewritequiet(void *v,int end,void *data,long n)
{
    return pipecorewrite1(v,end,data,n,0);
}

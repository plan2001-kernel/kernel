#include	"u.h"
#include	"../../core/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../../core/error.h"

#include	"uartnative.h"

enum
{
	/* soft flow control chars */
	CTLS= 023,
	CTLQ= 021,
};

extern PhysUart* physuart[];

static Uart* uartlist;
static Uart** uart;
static int uartnuart;
static Timer *uarttimer;

static struct Uartalloc {
	Lock	lk;
	Uart *elist;	/* list of enabled interfaces */
} uartalloc;

static void	uartclock(void);
static void	uartflow(void*);

/*
 *  enable/disable uart and add/remove to list of enabled uarts
 */
static Uart*
uartenable(Uart *p)
{
	Uart **l;
	char ctl[8];

	if(p->enabled)
		return p;
	if(p->iq == nil){
		if((p->iq = qopen(8*1024, Qcoalesce, uartflow, p)) == nil)
			return nil;
	}
	else
		qreopen(p->iq);
	if(p->oq == nil){
		if((p->oq = qopen(8*1024, 0, uartkick, p)) == nil){
			qfree(p->iq);
			p->iq = nil;
			return nil;
		}
	}
	else
		qreopen(p->oq);

	p->ir = p->istage;
	p->iw = p->istage;
	p->ie = &p->istage[Stagesize];
	p->op = p->ostage;
	p->oe = p->ostage;

	p->hup_dsr = p->hup_dcd = 0;
	p->dsr = p->dcd = 0;

	/* assume we can send */
	p->cts = 1;
	p->blocked = 0;
	p->ctsbackoff = 0;

	if(p->bits == 0){
		strcpy(ctl, "l8");
		uartctl(p, ctl);
	}
	if(p->stop == 0){
		strcpy(ctl, "s1");
		uartctl(p, ctl);
	}
	if(p->parity == 0){
		strcpy(ctl, "pn");
		uartctl(p, ctl);
	}
	if(p->baud == 0){
		strcpy(ctl, "b9600");
		uartctl(p, ctl);
	}
	(*p->phys->enable)(p, 1);

	/*
	 * use ilock because uartclock can otherwise interrupt here
	 * and would hang on an attempt to lock uartalloc.
	 */
	ilock(&uartalloc.lk);
	for(l = &uartalloc.elist; *l; l = &(*l)->elist){
		if(*l == p)
			break;
	}
	if(*l == 0){
		p->elist = uartalloc.elist;
		uartalloc.elist = p;
	}
	p->enabled = 1;
	iunlock(&uartalloc.lk);

	return p;
}

static void
uartdisable(Uart *p)
{
	Uart **l;

	if(!p->enabled)
		return;
	(*p->phys->disable)(p);

	ilock(&uartalloc.lk);
	for(l = &uartalloc.elist; *l; l = &(*l)->elist){
		if(*l == p){
			*l = p->elist;
			break;
		}
	}
	p->enabled = 0;
	iunlock(&uartalloc.lk);
}

static Uart*
uartport(char *which)
{
	int port;
	char *p;

	port = strtol(which, &p, 0);
	if(p == which)
		error(Ebadarg);
	if(port < 0 || port >= uartnuart || uart[port] == nil)
		error(Enodev);
	return uart[port];
}

void
uartmouse(char *which, int (*putc)(Queue*, int), int setb1200)
{
	Uart *p;

	p = uartport(which);
	qlock(&p->qlk);
	if(p->opens == 0 && uartenable(p) == nil){
		qunlock(&p->qlk);
		error(Enodev);
	}
	p->opens++;
	if(setb1200)
		uartctl(p, "b1200");
	p->putc = putc;
	p->special = 1;
	qunlock(&p->qlk);
}

void
uartsetmouseputc(char *which, int (*putc)(Queue*, int))
{
	Uart *p;

	p = uartport(which);
	qlock(&p->qlk);
	if(p->opens == 0 || p->special == 0){
		qunlock(&p->qlk);
		error(Enodev);
	}
	p->putc = putc;
	qunlock(&p->qlk);
}

/*
 *  enumerate the physical UARTs and enable the console
 */
void
uartnreset(void)
{
	int i;
	Uart *p, *tail;

	tail = nil;
	for(i = 0; physuart[i] != nil; i++){
		if(physuart[i]->pnp == nil)
			continue;
		if((p = physuart[i]->pnp()) == nil)
			continue;
		if(uartlist != nil)
			tail->next = p;
		else
			uartlist = p;
		for(tail = p; tail->next != nil; tail = tail->next)
			uartnuart++;
		uartnuart++;
	}

	if(uartnuart)
		uart = xalloc(uartnuart*sizeof(Uart*));
	if(uartnuart && uart == nil)
		panic("uartnreset: no memory");

	p = uartlist;
	for(i = 0; i < uartnuart; i++){
		uart[i] = p;
		p->dev = i;

		/*
		 * enable serial console for uarts detected
		 * late during boot (like pci card).
		 */
		if(consuart == nil){
			char *s, *options;

			if((s = getconf("console")) != nil
			&& strtoul(s, &options, 0) == i
			&& options > s){
				p->console = 1;
				if(*options != '\0')
					uartctl(p, options);
			}
		}

		if(p->console || p->special){
			if(uartenable(p) != nil){
				if(p->console){
					if(consuart == nil){
						consuart = p;
						uartputs(kmesg.buf, kmesg.n);
					}
				}
				p->opens++;
			}
		}
		p = p->next;
	}

	if(uartnuart){
		/*
		 * at 115200 baud, the 1024 char buffer takes 56 ms to process,
		 * processing it every 22 ms should be fine.
		 */
		uarttimer = addclock0link(uartclock, 22);
	}
}

static int
uartdrained(void* arg)
{
	Uart *p;

	p = arg;
	return qlen(p->oq) == 0 && p->op == p->oe;
}

static void
uartdrainoutput(Uart *p)
{
	if(!p->enabled || up == nil || !islo())
		return;
	p->drain = 1;
	if(waserror()){
		p->drain = 0;
		nexterror();
	}
	sleep(&p->r, uartdrained, p);
	poperror();
}

int
uartctl(Uart *p, char *cmd)
{
	char *f[16];
	int i, n, nf;

	nf = tokenize(cmd, f, nelem(f));
	for(i = 0; i < nf; i++){
		if(strncmp(f[i], "break", 5) == 0){
			(*p->phys->dobreak)(p, 0);
			continue;
		}

		n = atoi(f[i]+1);
		switch(*f[i]){
		case 'B':
		case 'b':
			uartdrainoutput(p);
			if((*p->phys->baud)(p, n) < 0)
				return -1;
			break;
		case 'C':
		case 'c':
			p->hup_dcd = n;
			break;
		case 'D':
		case 'd':
			uartdrainoutput(p);
			(*p->phys->dtr)(p, n);
			break;
		case 'E':
		case 'e':
			p->hup_dsr = n;
			break;
		case 'f':
		case 'F':
			if(p->oq != nil)
				qflush(p->oq);
			break;
		case 'H':
		case 'h':
			if(p->iq != nil)
				qhangup(p->iq, 0);
			if(p->oq != nil)
				qhangup(p->oq, 0);
			break;
		case 'i':
		case 'I':
			uartdrainoutput(p);
			(*p->phys->fifo)(p, n);
			break;
		case 'K':
		case 'k':
			uartdrainoutput(p);
			(*p->phys->dobreak)(p, n);
			break;
		case 'L':
		case 'l':
			uartdrainoutput(p);
			if((*p->phys->bits)(p, n) < 0)
				return -1;
			break;
		case 'm':
		case 'M':
			uartdrainoutput(p);
			(*p->phys->modemctl)(p, n);
			break;
		case 'n':
		case 'N':
			if(p->oq != nil)
				qnoblock(p->oq, n);
			break;
		case 'P':
		case 'p':
			uartdrainoutput(p);
			if((*p->phys->parity)(p, *(f[i]+1)) < 0)
				return -1;
			break;
		case 'Q':
		case 'q':
			if(n < 0 || n > 1024*1024)
				return -1;
			if(p->iq != nil)
				qsetlimit(p->iq, n);
			if(p->oq != nil)
				qsetlimit(p->oq, n);
			break;
		case 'R':
		case 'r':
			uartdrainoutput(p);
			(*p->phys->rts)(p, n);
			break;
		case 'S':
		case 's':
			uartdrainoutput(p);
			if((*p->phys->stop)(p, n) < 0)
				return -1;
			break;
		case 'W':
		case 'w':
			if(uarttimer == nil || n < 1)
				return -1;
			uarttimer->tns = (vlong)n * 100000LL;
			break;
		case 'X':
		case 'x':
			ilock(&p->tlock);
			p->xonoff = n;
			p->blocked = 0;
			iunlock(&p->tlock);
			break;
		}
	}
	return 0;
}

/*
 * Native UART core.  These entry points use the same Uart objects, queues,
 * enable/disable accounting and PhysUart callbacks as the console and driver
 * core; they merely avoid constructing a Chan.
 */
static Uart*
uartnport(int id)
{
	if(id < 0 || uart == nil || id >= uartnuart || uart[id] == nil)
		error(Enodev);
	return uart[id];
}

static Uart*
uartnuserport(int id)
{
	Uart *p;

	p = uartnport(id);
	if(p->console || p->special)
		error(Eperm);
	return p;
}

int
uartnports(void)
{
	return uartnuart;
}

void
uartnopen(int id, int mode)
{
	Uart *p;

	if(mode != OREAD && mode != OWRITE && mode != ORDWR)
		error(Ebadarg);
	p = uartnport(id);
	qlock(&p->qlk);
	if(p->console || p->special){
		qunlock(&p->qlk);
		error(Eperm);
	}
	if(p->opens == 0 && uartenable(p) == nil){
		qunlock(&p->qlk);
		error(Enodev);
	}
	p->opens++;
	qunlock(&p->qlk);
}

void
uartnclose(int id)
{
	Uart *p;

	p = uartnuserport(id);
	qlock(&p->qlk);
	if(p->opens <= 0){
		qunlock(&p->qlk);
		error(Ebadfd);
	}
	if(--(p->opens) == 0){
		qclose(p->iq);
		ilock(&p->rlock);
		p->ir = p->iw = p->istage;
		iunlock(&p->rlock);

		/*
		 * Drain output, then disable the port, as the old close path did.
		 */
		qhangup(p->oq, nil);
		if(!waserror()){
			uartdrainoutput(p);
			poperror();
		}
		qclose(p->oq);
		uartdisable(p);
		p->dcd = p->dsr = p->dohup = 0;
	}
	qunlock(&p->qlk);
}

long
uartnread(int id, void *buf, long n)
{
	Uart *p;

	if(n < 0 || n > 0x7fffffffL)
		error(Etoobig);
	p = uartnuserport(id);
	if(p->opens <= 0 || p->iq == nil)
		error(Ebadfd);
	return qread(p->iq, buf, n);
}

long
uartnwrite(int id, void *buf, long n)
{
	Uart *p;
	int r;

	if(n < 0 || n > 0x7fffffffL)
		error(Etoobig);
	p = uartnuserport(id);
	if(p->opens <= 0 || p->oq == nil)
		error(Ebadfd);
	qlock(&p->qlk);
	if(waserror()){
		qunlock(&p->qlk);
		nexterror();
	}
	r = qwrite(p->oq, buf, n);
	qunlock(&p->qlk);
	poperror();
	return r;
}

void
uartnctl(int id, Uartcfg *cfg)
{
	Uart *p;
	char cmd[192], *s, *e;
	int c, set;

	p = uartnuserport(id);
	if(cfg == nil)
		error(Ebadarg);
	qlock(&p->qlk);
	if(waserror()){
		qunlock(&p->qlk);
		nexterror();
	}
	if(p->opens <= 0)
		error(Ebadfd);

	set = cfg->set;
	if(set == 0 || (set & ~(Uartbaud|Uartbits|Uartstop|Uartparity|
	    Uartfifo|Uartrts|Uartdtr|Uarthupdsr|Uarthupdcd)) != 0)
		error(Ebadarg);

	s = cmd;
	e = cmd + sizeof(cmd);
	if(set & Uartbaud){
		if(cfg->baud <= 0 || p->phys->baud == nil)
			error(Ebadarg);
		s = seprint(s, e, " b%d", cfg->baud);
	}
	if(set & Uartbits){
		if(cfg->bits < 5 || cfg->bits > 8 || p->phys->bits == nil)
			error(Ebadarg);
		s = seprint(s, e, " l%d", cfg->bits);
	}
	if(set & Uartstop){
		if(cfg->stop < 1 || cfg->stop > 2 || p->phys->stop == nil)
			error(Ebadarg);
		s = seprint(s, e, " s%d", cfg->stop);
	}
	if(set & Uartparity){
		c = 0;
		switch(cfg->parity){
		case Uartparitynone:
			c = 'n';
			break;
		case Uartparityodd:
			c = 'o';
			break;
		case Uartparityeven:
			c = 'e';
			break;
		default:
			error(Ebadarg);
		}
		if(p->phys->parity == nil)
			error(Ebadarg);
		s = seprint(s, e, " p%c", c);
	}
	if(set & Uartfifo){
		if(p->phys->fifo == nil ||
		    (cfg->fifo != 0 && cfg->fifo != 1 && cfg->fifo != 4 &&
		     cfg->fifo != 8 && cfg->fifo != 14))
			error(Ebadarg);
		s = seprint(s, e, " i%d", cfg->fifo);
	}
	if(set & Uartrts){
		if((cfg->rts != 0 && cfg->rts != 1) || p->phys->rts == nil)
			error(Ebadarg);
		s = seprint(s, e, " r%d", cfg->rts);
	}
	if(set & Uartdtr){
		if((cfg->dtr != 0 && cfg->dtr != 1) || p->phys->dtr == nil)
			error(Ebadarg);
		s = seprint(s, e, " d%d", cfg->dtr);
	}
	if(set & Uarthupdsr){
		if(cfg->hupdsr != 0 && cfg->hupdsr != 1)
			error(Ebadarg);
		s = seprint(s, e, " e%d", cfg->hupdsr);
	}
	if(set & Uarthupdcd){
		if(cfg->hupdcd != 0 && cfg->hupdcd != 1)
			error(Ebadarg);
		s = seprint(s, e, " c%d", cfg->hupdcd);
	}
	if(s >= e)
		error(Ebadarg);
	*s = 0;

	if(uartctl(p, cmd) < 0)
		error(Ebadarg);
	qunlock(&p->qlk);
	poperror();
}

long
uartnstatus(int id, char *buf, long n)
{
	Uart *p;
	char *s;

	if(n < 0 || n > 0x7fffffffL)
		error(Etoobig);
	p = uartnport(id);
	if(p->phys == nil)
		error(Enodev);

	s = smalloc(READSTR);
	if(waserror()){
		free(s);
		nexterror();
	}
	if(p->phys->status == nil)
		seprint(s, s + READSTR,
			"b%d\n"
			"dev(%d) type(%d) framing(%d) overruns(%d) "
			"berr(%d) serr(%d)\n",
			p->baud,
			p->dev, p->type, p->ferr, p->oerr,
			p->berr, p->serr);
	else
		(*p->phys->status)(p, s, s + READSTR);
	n = readstr(0, buf, n, s);
	free(s);
	poperror();
	return n;
}

/*
 *  restart input if it's off
 */
static void
uartflow(void *v)
{
	Uart *p;

	p = v;
	if(p->modem)
		(*p->phys->rts)(p, 1);
}

/*
 *  put some bytes into the local queue to avoid calling
 *  qconsume for every character
 */
int
uartstageoutput(Uart *p)
{
	int n;

	if(!p->enabled || p->blocked)
		return 0;

	n = qconsume(p->oq, p->ostage, Stagesize);
	if(n <= 0)
		return 0;
	p->op = p->ostage;
	p->oe = p->ostage + n;
	return n;
}

/*
 *  restart output
 */
void
uartkick(void *v)
{
	Uart *p = v;

	if(!p->enabled || p->blocked)
		return;

	ilock(&p->tlock);
	(*p->phys->kick)(p);
	iunlock(&p->tlock);

	if(p->drain && uartdrained(p)){
		p->drain = 0;
		wakeup(&p->r);
	}
}

/*
 * Move data from the interrupt staging area to
 * the input Queue.
 */
static void
uartstageinput(Uart *p)
{
	int n;
	uchar *ir, *iw;

	while(p->ir != p->iw){
		ir = p->ir;
		if(p->ir > p->iw){
			iw = p->ie;
			p->ir = p->istage;
		}
		else{
			iw = p->iw;
			p->ir = p->iw;
		}
		if((n = qproduce(p->iq, ir, iw - ir)) < 0){
			if(p->modem)
				(*p->phys->rts)(p, 0);
			p->serr++;
		}
		else if(n == 0)
			p->berr++;
	}
}

/*
 *  receive a character at interrupt time
 */
void
uartrecv(Uart *p,  char ch)
{
	uchar *next;

	/* software flow control */
	if(p->xonoff){
		if(ch == CTLS){
			p->blocked = 1;
		}else if(ch == CTLQ){
			p->blocked = 0;
			p->ctsbackoff = 2; /* clock gets output going again */
		}
	}

	/* receive the character */
	if(p->putc)
		p->putc(p->iq, ch);
	else if (p->iw) {		/* maybe the line isn't enabled yet */
		ilock(&p->rlock);
		next = p->iw + 1;
		if(next == p->ie)
			next = p->istage;
		if(next == p->ir)
			uartstageinput(p);
		if(next != p->ir){
			*p->iw = ch;
			p->iw = next;
		}
		iunlock(&p->rlock);
	}
}

/*
 *  we save up input characters till clock time to reduce
 *  per character interrupt overhead.
 */
static void
uartclock(void)
{
	Uart *p;

	ilock(&uartalloc.lk);
	for(p = uartalloc.elist; p; p = p->elist){

		/* this hopefully amortizes cost of qproduce to many chars */
		if(p->iw != p->ir){
			ilock(&p->rlock);
			uartstageinput(p);
			iunlock(&p->rlock);
		}

		/* hang up if requested */
		if(p->dohup){
			qhangup(p->iq, 0);
			qhangup(p->oq, 0);
			p->dohup = 0;
		}

		/* this adds hysteresis to hardware/software flow control */
		if(p->ctsbackoff){
			ilock(&p->tlock);
			if(p->ctsbackoff){
				if(--(p->ctsbackoff) == 0 && !p->blocked)
					(*p->phys->kick)(p);
			}
			iunlock(&p->tlock);
		}
	}
	iunlock(&uartalloc.lk);
}

/*
 * polling console input, output
 */

Uart* consuart;

int
uartgetc(void)
{
	if(consuart == nil || consuart->phys->getc == nil)
		return -1;
	return consuart->phys->getc(consuart);
}

void
uartputc(int c)
{
	if(consuart == nil || consuart->phys->putc == nil)
		return;
	consuart->phys->putc(consuart, c);
}

void
uartputs(char *s, int n)
{
	char *e;

	if(consuart == nil || consuart->phys->putc == nil)
		return;

	e = s+n;
	for(; s<e; s++){
		if(*s == '\n')
			consuart->phys->putc(consuart, '\r');
		consuart->phys->putc(consuart, *s);
	}
}

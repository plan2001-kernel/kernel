/* T13 raw Ethernet device provider.  The only IRQ-side state is a bounded
 * array of owned Blocks.  The ordinary ObjectFIFO remains the sole message
 * distribution mechanism; publication and allocation run in a kproc. */
#include "u.h"
#include "../core/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../core/error.h"
#include "../core/netif.h"
#include "../core/etherif.h"
#include "../core/vobjint.h"

enum { HandoffCapacity = 64 };

typedef struct Rxslot Rxslot;
struct Rxslot {
    Block *block;
    uvlong sequence;
};

struct Veth {
    Ref ref;                 /* owner + worker until its last access */
    QLock gate;              /* serialize TX with detach, process only */
    Lock lk;                 /* IRQ-safe handoff/counters */
    Rendez rxready;
    Rendez workerexit;
    Ether *ether;            /* ethercore registry has boot lifetime */
    Vobj *fifo;              /* strong, ordinary ObjectFIFO reference */
    Rxslot slot[HandoffCapacity];
    int head, tail, queued;
    int registered, started, closed, done;
    uvlong arrivals, published, handoffdrops, publishdrops, txaccepted;
    uvlong highwater;
};

_Static_assert(sizeof(ObjEtherRxMeta) == 24, "Ethernet metadata ABI");
_Static_assert(sizeof(ObjEtherStatus) == 88, "Ethernet status ABI");

static void
vetherput(Veth *v)
{
    if(decref(&v->ref) != 0)
        return;
    vobjput(v->fifo);
    free(v);
}

/* Called by etheriq in arbitrary driver/IRQ context. Owns bp on entry.
 * No allocation, QLock, user memory, protocol parsing or OBJ/FIFO call.
 * freeb and wakeup are IRQ-safe kernel primitives. */
static void
vetherinput(void *arg, Ether*, Block *bp)
{
    Veth *v = arg;
    int n = BLEN(bp);

    ilock(&v->lk);
    v->arrivals++;
    if(v->closed || n < v->ether->minmtu || n > v->ether->maxmtu
    || n > OBJ_DEVICE_MAXMESSAGE || v->queued == HandoffCapacity){
        v->handoffdrops++;
        iunlock(&v->lk);
        freeb(bp);
        return;
    }
    v->slot[v->tail].block = bp;
    v->slot[v->tail].sequence = v->arrivals;
    v->tail = (v->tail+1) % HandoffCapacity;
    v->queued++;
    if(v->queued > v->highwater)
        v->highwater = v->queued;
    iunlock(&v->lk);
    wakeup(&v->rxready);
}

static int
vetherready(void *arg)
{
    Veth *v = arg;
    int ready;
    ilock(&v->lk);
    ready = v->closed || v->queued != 0;
    iunlock(&v->lk);
    return ready;
}

static int
vetherdone(void *arg)
{
    Veth *v = arg;
    int done;
    ilock(&v->lk);
    done = v->done;
    iunlock(&v->lk);
    return done;
}

static void
vetherworker(void *arg)
{
    Veth *v = arg;
    Block *bp;
    Vobj *object;
    ObjEtherRxMeta meta;
    int closed;

    for(;;){
        ilock(&v->lk);
        closed = v->closed;
        if(v->queued == 0){
            iunlock(&v->lk);
            if(closed)
                break;
            tsleep(&v->rxready, vetherready, v, 1000);
            continue;
        }
        bp = v->slot[v->head].block;
        memset(&meta, 0, sizeof meta);
        meta.version = OBJ_DEVICE_ABI_VERSION;
        meta.controller = v->ether->ctlrno;
        meta.length = BLEN(bp);
        meta.sequence = v->slot[v->head].sequence;
        v->slot[v->head].block = nil;
        v->head = (v->head+1) % HandoffCapacity;
        v->queued--;
        iunlock(&v->lk);
        if(closed){
            freeb(bp);
            continue;
        }
        object = vobjnewkind(VkindBytes, meta.length);
        if(object == nil){
            freeb(bp);
            ilock(&v->lk);
            v->publishdrops++;
            iunlock(&v->lk);
            continue;
        }
        if(waserror()){
            vobjput(object);
            freeb(bp);
            ilock(&v->lk);
            v->publishdrops++;
            iunlock(&v->lk);
            continue;
        }
        memmove(object->data, bp->rp, meta.length);
        vobjfifopublish(v->fifo, object, Vread|Vmeta|Vdelegate,
            &meta, sizeof meta);
        poperror();
        vobjput(object);
        freeb(bp);
        ilock(&v->lk);
        v->published++;
        iunlock(&v->lk);
    }
    ilock(&v->lk);
    v->done = 1;
    iunlock(&v->lk);
    wakeup(&v->workerexit);
    /* A joining owner may already have observed done before wakeup. The
     * worker reference pins both Rendez and state through its last access. */
    vetherput(v);
}

int
vetherlist(ObjDeviceEntry *out, int capacity)
{
    Ether *e;
    ObjDeviceEntry entry;
    int i, count;

    if(ethercore_init() < 0)
        return 0;
    count = 0;
    for(i = 0; (e = ethercore_byindex(i)) != nil; i++){
        memset(&entry, 0, sizeof entry);
        entry.version = OBJ_DEVICE_ABI_VERSION;
        entry.family = ObjDeviceEthernet;
        entry.selector0 = e->ctlrno;
        entry.maxsend = e->maxmtu;
        if(entry.maxsend > OBJ_DEVICE_MAXMESSAGE)
            entry.maxsend = OBJ_DEVICE_MAXMESSAGE;
        entry.maxrecv = entry.maxsend;
        if(count < capacity)
            memmove(out+count, &entry, sizeof entry);
        count++;
    }
    return count;
}

Veth*
vetheropen(int controller)
{
    Veth *v;
    int rc;

    v = mallocz(sizeof *v, 1);
    if(v == nil)
        error(Enomem);
    v->ref.ref = 1;
    if(waserror()){
        vetherclose(v);
        nexterror();
    }
    v->ether = ethercore_lookup(controller);
    if(v->ether == nil)
        error(Ebadarg);
    v->fifo = vobjnewkind(VkindFifo, 0);
    if(v->fifo == nil)
        error(Enomem);
    rc = ethercore_attach(v->ether);
    if(rc != EtherErrOk)
        error("ether: attach failed");
    rc = ethercore_rxregister(v->ether, vetherinput, v);
    if(rc != EtherErrOk)
        error(Einuse);
    v->registered = 1;
    incref(&v->ref);
    if(waserror()){
        vetherput(v); /* kproc did not start */
        nexterror();
    }
    kproc("device-ether-rx", vetherworker, v);
    v->started = 1;
    poperror();
    poperror();
    return v;
}

void
vetherdetach(Veth *v)
{
    Block *bp;

    qlock(&v->gate);
    /* Teardown cannot abandon live callbacks/worker state on a note. */
    while(waserror())
        ;
    ilock(&v->lk);
    v->closed = 1;
    iunlock(&v->lk);
    if(v->registered){
        ethercore_rxunregister(v->ether, vetherinput, v);
        v->registered = 0;
    }
    wakeup(&v->rxready);
    if(v->started){
        while(!vetherdone(v))
            tsleep(&v->workerexit, vetherdone, v, 10);
    }else{
        /* Open failed before the worker started. Callback removal above
         * makes the handoff private, so drain its owned Blocks here. */
        while(v->queued != 0){
            bp = v->slot[v->head].block;
            v->head = (v->head+1) % HandoffCapacity;
            v->queued--;
            freeb(bp);
        }
    }
    if(v->fifo != nil)
        vobjfifoclose(v->fifo);
    poperror();
    qunlock(&v->gate);
}

void
vetherclose(Veth *v)
{
    if(v == nil)
        return;
    vetherdetach(v);
    vetherput(v);
}

Vobj*
vetherfifo(Veth *v)
{
    int closed;
    ilock(&v->lk);
    closed = v->closed;
    iunlock(&v->lk);
    if(closed)
        error(Ehungup);
    return v->fifo; /* borrowed; caller holds a live device Vhand */
}

static char*
vethererror(int rc)
{
    switch(rc){
    case EtherErrDown: return "ether: link down";
    case EtherErrQueue: return "ether: TX queue full";
    case EtherErrClosed: return "ether: TX queue closed";
    case EtherErrBadLen: return "ether: invalid frame length";
    case EtherErrNoMem: return Enomem;
    case EtherErrNotReady: return "ether: not attached";
    default: return "ether: transmit failed";
    }
}

long
vethersend(Veth *v, void *data, long len)
{
    Block *bp;
    int rc;

    if(data == nil || len < v->ether->minmtu || len > v->ether->maxmtu
    || len > OBJ_DEVICE_MAXMESSAGE)
        error(Ebadarg);
    validaddr((uintptr)data, len, 0);
    bp = allocb(len);
    if(bp == nil)
        error(Enomem);
    if(waserror()){
        if(bp != nil) freeb(bp);
        nexterror();
    }
    memmove(bp->wp, data, len);
    bp->wp += len;
    qlock(&v->gate);
    if(waserror()){
        qunlock(&v->gate);
        nexterror();
    }
    if(v->closed)
        error(Ehungup);
    rc = ethercore_tx(v->ether, bp);
    if(rc != EtherErrOk)
        error(vethererror(rc));
    bp = nil; /* the core consumed it only on success */
    ilock(&v->lk);
    v->txaccepted++;
    iunlock(&v->lk);
    poperror();
    qunlock(&v->gate);
    poperror();
    return len;
}

void
vetherstatus(Veth *v, ObjEtherStatus *out)
{
    memset(out, 0, sizeof *out);
    out->version = OBJ_DEVICE_ABI_VERSION;
    out->controller = v->ether->ctlrno;
    out->minframe = v->ether->minmtu;
    out->maxframe = v->ether->maxmtu;
    if(out->maxframe > OBJ_DEVICE_MAXMESSAGE)
        out->maxframe = OBJ_DEVICE_MAXMESSAGE;
    memmove(out->mac, v->ether->ea, sizeof out->mac);
    out->link = v->ether->netif.link;
    ilock(&v->lk);
    out->closed = v->closed;
    out->arrivals = v->arrivals;
    out->published = v->published;
    out->handoff_drops = v->handoffdrops;
    out->publish_drops = v->publishdrops;
    out->tx_accepted = v->txaccepted;
    out->highwater = v->highwater;
    out->handoff_capacity = HandoffCapacity;
    out->handoff_queued = v->queued;
    iunlock(&v->lk);
}

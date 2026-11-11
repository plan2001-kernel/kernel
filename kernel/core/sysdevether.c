/* T13 raw Ethernet device provider.  The only IRQ-side state is a bounded
 * array of owned Blocks.  Capability-mapped region rings are the sole
 * transport in both directions: the kernel is the only receive writer and
 * publishes complete L2 frames, while the authorized userspace transmit
 * publisher owns the unique transmit writer mapping and the kernel consumes
 * complete frames as a read-only reader.  Neither direction copies a frame
 * through a per-frame syscall. */
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
    Ref ref;                 /* owner + workers until their last access */
    QLock gate;              /* serialize TX with detach, process only */
    Lock lk;                 /* IRQ-safe handoff/counters */
    Rendez rxready;
    Rendez txsleep;          /* bounded recheck when no TX writer exists */
    Rendez workerexit;
    Ether *ether;            /* ethercore registry has boot lifetime */
    Vobj *rxregion;          /* strong reference to the receive region */
    uvlong rxseq;            /* published frame sequence */
    Vobj *txregion;          /* strong reference to the transmit region */
    uvlong txack;            /* last consumed transmit sequence */
    Rxslot slot[HandoffCapacity];
    int head, tail, queued;
    int registered, started, txstarted, closed, done, txdone;
    int rxwriter;            /* this Veth holds the RX region writer slot */
    uvlong arrivals, published, handoffdrops, publishdrops;
    uvlong txaccepted, txdropped, txerrors;
    uvlong highwater;
};

_Static_assert(sizeof(ObjEtherRxMeta) == 24, "Ethernet metadata ABI");
_Static_assert(sizeof(ObjEtherStatus) == 104, "Ethernet status ABI");

static void
vetherput(Veth *v)
{
    if(decref(&v->ref) != 0)
        return;
    if(v->rxregion != nil)
        vobjput(v->rxregion);
    if(v->txregion != nil)
        vobjput(v->txregion);
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

static int
vethertxdone(void *arg)
{
    Veth *v = arg;
    int done;
    ilock(&v->lk);
    done = v->txdone;
    iunlock(&v->lk);
    return done;
}

static void
vetherworker(void *arg)
{
    Veth *v = arg;
    Block *bp;
    ulong len;
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
        len = BLEN(bp);
        v->slot[v->head].block = nil;
        v->head = (v->head+1) % HandoffCapacity;
        v->queued--;
        iunlock(&v->lk);
        if(closed){
            freeb(bp);
            continue;
        }
        if(waserror()){
            freeb(bp);
            ilock(&v->lk);
            v->publishdrops++;
            iunlock(&v->lk);
            continue;
        }
        {
            int rc = vobjregionringpublish(v->rxregion, &v->rxseq,
                bp->rp, len);
            ilock(&v->lk);
            if(rc < 0)
                v->publishdrops++;
            else
                v->published++;
            iunlock(&v->lk);
        }
        poperror();
        freeb(bp);
    }
    ilock(&v->lk);
    v->done = 1;
    iunlock(&v->lk);
    wakeup(&v->workerexit);
    /* A joining owner may already have observed done before wakeup. The
     * worker reference pins both Rendez and state through its last access. */
    vetherput(v);
}

static int
vethertxclosed(void *arg)
{
    Veth *v = arg;
    int closed;

    ilock(&v->lk);
    closed = v->closed;
    iunlock(&v->lk);
    return closed;
}

/* Consume complete frames published by the authorized userspace writer.  The
 * region slot is the only frame transport: there is no per-frame send syscall
 * and no user pointer validation here.  A bounded internal copy into a
 * NIC-owned Block is the only copy. */
static void
vethertxworker(void *arg)
{
    Veth *v = arg;
    Block *bp;
    uvlong pub;
    ulong len;
    int closed, rc;

    for(;;){
        ilock(&v->lk);
        closed = v->closed;
        iunlock(&v->lk);
        if(closed)
            break;

        pub = vobjregionringload(v->txregion, OBJ_RING_PUBOFF);
        if(pub > v->txack){
            /* One pass is bounded by the ring capacity.  Older sequences
             * cannot still be live if the producer overran the ring. */
            if(pub - v->txack > OBJ_RING_SLOTS){
                uvlong skip = pub - OBJ_RING_SLOTS - v->txack;

                ilock(&v->lk);
                v->txdropped += skip;
                iunlock(&v->lk);
                v->txack += skip;
            }
            while(v->txack < pub){
                v->txack++;
                bp = allocb(OBJ_RING_DATA);
                if(bp == nil){
                    v->txack--;
                    ilock(&v->lk);
                    v->txerrors++;
                    iunlock(&v->lk);
                    tsleep(&v->txsleep, vethertxclosed, v, 100);
                    break;
                }
                len = 0;
                rc = vobjregionringconsume(v->txregion, v->txack,
                    bp->wp, OBJ_RING_DATA, &len);
                if(rc == 1 && len >= v->ether->minmtu
                && len <= v->ether->maxmtu){
                    bp->wp += len;
                    rc = ethercore_tx(v->ether, bp);
                    ilock(&v->lk);
                    if(rc == EtherErrOk)
                        v->txaccepted++;
                    else
                        v->txerrors++;
                    iunlock(&v->lk);
                    if(rc != EtherErrOk)
                        freeb(bp);
                }else{
                    freeb(bp);
                    ilock(&v->lk);
                    v->txdropped++;
                    iunlock(&v->lk);
                }
                vobjregionringack(v->txregion, v->txack);
                ilock(&v->lk);
                closed = v->closed;
                iunlock(&v->lk);
                if(closed)
                    break;
            }
            continue;
        }
        /* Enroll without losing a publication that races the load above.
         * A missing or released writer cannot wake this word, so recheck at a
         * bounded interval instead of spinning. */
        if(waserror()){
            if(vethertxclosed(v))
                break;
            tsleep(&v->txsleep, vethertxclosed, v, 100);
            continue;
        }
        rc = vobjregionwaitkern(v->txregion, OBJ_RING_PUBOFF, pub, 100);
        poperror();
        if(rc == 3 || rc < 0)
            tsleep(&v->txsleep, vethertxclosed, v, 100);
    }
    ilock(&v->lk);
    v->txdone = 1;
    iunlock(&v->lk);
    wakeup(&v->workerexit);
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
    v->rxregion = vobjregionalloc(OBJ_RING_BYTES);
    if(v->rxregion == nil)
        error(Enomem);
    if(vobjregionwriterhold(v->rxregion) < 0)
        error(Einuse);
    v->rxwriter = 1;
    vobjregionringinit(v->rxregion);
    v->txregion = vobjregionalloc(OBJ_RING_BYTES);
    if(v->txregion == nil)
        error(Enomem);
    vobjregionringinit(v->txregion);
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
    incref(&v->ref);
    if(waserror()){
        vetherput(v); /* kproc did not start */
        nexterror();
    }
    kproc("device-ether-tx", vethertxworker, v);
    v->txstarted = 1;
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
    wakeup(&v->txsleep);
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
    if(v->txstarted){
        while(!vethertxdone(v))
            tsleep(&v->workerexit, vethertxdone, v, 10);
    }
    if(v->rxregion != nil && v->rxwriter){
        v->rxwriter = 0;
        vobjregionwriterrelease(v->rxregion);
    }
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
vetherrxregion(Veth *v)
{
    int closed;
    ilock(&v->lk);
    closed = v->closed;
    iunlock(&v->lk);
    if(closed)
        error(Ehungup);
    return v->rxregion; /* borrowed; caller holds a live device Vhand */
}

Vobj*
vethertxregion(Veth *v)
{
    int closed;
    ilock(&v->lk);
    closed = v->closed;
    iunlock(&v->lk);
    if(closed)
        error(Ehungup);
    return v->txregion; /* borrowed; caller holds a live device Vhand */
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
    out->tx_dropped = v->txdropped;
    out->tx_errors = v->txerrors;
    iunlock(&v->lk);
}

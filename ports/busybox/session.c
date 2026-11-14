/* Kernel-owned boot session console over native capability-region rings.
 * This adapter never opens a physical keyboard, framebuffer or guessed OID.
 * Only an EXACT inherited stdin RO + stdout RW + diagnostics RW grant triple
 * is treated as a session; SPAWN's legacy single output region is NOT one.
 */
#include "libbb.h"
#include "obj.h"
#include "vobj.h"
#include "../unix/uxpipe.h"

extern long obj_list(void *, unsigned long long);
extern long obj_region_map(int, unsigned long long);
extern long obj_region_unmap(unsigned long long);
extern long region_wait(unsigned long long, unsigned long long,
                        unsigned long long, long long);
extern long region_wake(unsigned long long, unsigned long long,
                        unsigned long long);

static int probe_state;                 /* 0 unprobed, 1 session, -1 absent, -2 broken */
static int proxy_streams; /* 0=boot, 2=stdout, 4=stdin+stdout, 6=F5 */
static int proxy_state;                 /* 1: six explicitly delegated UXPIPE regions */
static UxPipe proxy_in, proxy_out, proxy_err;
static unsigned long long inbase, outbase, diagbase;
static unsigned long long inlast;
static unsigned char inbuf[OBJ_RING_DATA];
static unsigned int inpos, inlen;

static unsigned long long acquire(const unsigned long long *p)
{
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

static void release(unsigned long long *p, unsigned long long n)
{
    __atomic_store_n(p, n, __ATOMIC_RELEASE);
}

static int ring_valid(unsigned long long base)
{
    const ObjRingHeader *h = (const ObjRingHeader *)base;
    return h->version == OBJ_RING_VERSION &&
           h->magic == OBJ_RING_MAGIC &&
           h->slot_size == OBJ_RING_SLOT &&
           h->slots == OBJ_RING_SLOTS;
}

int p2001_session_probe(void)
{
    VobjListRec handles[32];
    int regions[6];
    unsigned int rights[6];
    long mapped[6] = {-1,-1,-1,-1,-1,-1};
    long n, i, found=0;
    if (probe_state)
        return probe_state==1 ? 1 : probe_state==-1 ? 0 : -1;
    n=obj_list(handles,32);
    if (n<0 || n>32) goto broken;
    for(i=0;i<n;++i) {
        if (handles[i].kind!=VkindRegion) continue;
        if (found>=6) goto broken;
        regions[found]=(int)handles[i].handle;
        rights[found]=handles[i].rights;
        ++found;
    }
    /* Exactly TWO or FOUR attenuated SPAWN UXPIPE grants. Never
     * mistake these for a native console or a volume capability. */
    /* The pre-existing ls fixture inherits a read-only listing image and a
     * write-only one-shot output frame. It is NOT a UXPIPE pair. Preserve
     * that intentionally separate transport without granting authority.
     */
    if(found==2 && rights[0]==Vread && rights[1]==Vwrite) {
        probe_state=-1;
        return 0;
    }
    if(found==2 || found==4) {
        for(i=0;i<found;++i) {
            unsigned int required = found==2
                ? (i==0 ? Vwrite : Vread)
                : (i==0 || i==3 ? Vread : Vwrite);
            if(rights[i]!=required) goto broken;
            mapped[i]=obj_region_map(regions[i],0);
            if(mapped[i]<0) goto broken;
        }
        if(found==4 &&
           uxpipe_bind(&proxy_in,(UxPipeData *)mapped[0],
                       (UxPipeAck *)mapped[1],
                       (unsigned long long)mapped[0],
                       (unsigned long long)mapped[1],UXPIPE_READER)!=0)
            goto broken;
        i=found==2 ? 0 : 2;
        if(uxpipe_bind(&proxy_out,(UxPipeData *)mapped[i],
                       (UxPipeAck *)mapped[i+1],
                       (unsigned long long)mapped[i],
                       (unsigned long long)mapped[i+1],UXPIPE_WRITER)!=0)
            goto broken;
        proxy_state=1;
        proxy_streams=(int)found;
        probe_state=1;
        return 1;
    }
    if (found==6) {
        /* Only these six process-local grants form a proxy console.
         * Extra GE/O volume Handles are classified separately by Unix init.
         * No raw volume OID supplies console authority.
         */
        for (i=0;i<6;++i) {
            unsigned int required =
                i==0 || i==3 || i==5 ? Vread : Vwrite;
            if ((rights[i]&~Vdelegate)!=required) goto broken;
            mapped[i]=obj_region_map(regions[i],0);
            if (mapped[i]<0) goto broken;
        }
        if (uxpipe_bind(&proxy_in,
                        (UxPipeData *)mapped[0],(UxPipeAck *)mapped[1],
                        (unsigned long long)mapped[0],
                        (unsigned long long)mapped[1],UXPIPE_READER)!=0 ||
            uxpipe_bind(&proxy_out,
                        (UxPipeData *)mapped[2],(UxPipeAck *)mapped[3],
                        (unsigned long long)mapped[2],
                        (unsigned long long)mapped[3],UXPIPE_WRITER)!=0 ||
            uxpipe_bind(&proxy_err,
                        (UxPipeData *)mapped[4],(UxPipeAck *)mapped[5],
                        (unsigned long long)mapped[4],
                        (unsigned long long)mapped[5],UXPIPE_WRITER)!=0)
            goto broken;
        proxy_state=1;
        proxy_streams=6;
        probe_state=1;
        return 1;
    }
    if (found<3) {
        probe_state=-1; /* non-console SPAWN children */
        return 0;
    }
    if (found!=3 ||
        rights[0]!=(Vread|Vmeta) ||
        rights[1]!=(Vwrite|Vmeta) ||
        rights[2]!=(Vwrite|Vmeta))
        goto broken;
    for(i=0;i<3;++i) {
        mapped[i]=obj_region_map(regions[i],0);
        if (mapped[i]<0 || !ring_valid((unsigned long long)mapped[i]))
            goto broken;
    }
    inbase=(unsigned long long)mapped[0];
    outbase=(unsigned long long)mapped[1];
    diagbase=(unsigned long long)mapped[2];
    inlast=acquire(&((ObjRingHeader *)inbase)->write_seq);
    inpos=inlen=0;
    probe_state=1;
    return 1;
broken:
    for(i=0;i<6;++i)
        if (mapped[i]>=0)
            (void)obj_region_unmap((unsigned long long)mapped[i]);
    probe_state=-2;
    return -1;
}

/* Bounded ring with backpressure from the kernel's ACK sequence.
 * A write is successful only after its bytes have been published. */
ssize_t p2001_session_write(int fd, const void *buf, size_t count)
{
    const unsigned char *p = (const unsigned char *)buf;
    unsigned long long base, seq, ack, off;
    ObjRingHeader *h;
    size_t done = 0;
    unsigned int n;
    long status;

    if (p2001_session_probe() != 1 || (fd != 1 && fd != 2))
        return -1;
    if (proxy_state && fd==2 && proxy_streams!=6) return -1;
    if (proxy_state)
        return (ssize_t)uxpipe_write(fd==1 ? &proxy_out : &proxy_err,
                                     buf,(unsigned int)count,-1);
    base = fd == 1 ? outbase : diagbase;
    h = (ObjRingHeader *)base;
    while (done < count) {
        seq = acquire(&h->write_seq);
        ack = acquire(&h->ack_seq);
        while (seq - ack >= OBJ_RING_SLOTS) {
            status = region_wait(base, OBJ_RING_ACKOFF, ack, -1);
            if (status < 0 || status == 3)
                return done ? (ssize_t)done : -1;
            ack = acquire(&h->ack_seq);
        }
        n = count - done > OBJ_RING_DATA ?
            OBJ_RING_DATA : (unsigned int)(count - done);
        off = OBJ_RING_HEADER + (seq % OBJ_RING_SLOTS) * OBJ_RING_SLOT;
        release((unsigned long long *)(base + off), 2*(seq+1)-1);
        *(volatile unsigned int *)(base + off + 8) = n;
        *(volatile unsigned int *)(base + off + 12) = fd == 2 ? 1u : 0u;
        for (unsigned int j=0; j<n; ++j)
            *(volatile unsigned char *)(base + off + 16 + j) = p[done+j];
        __atomic_thread_fence(__ATOMIC_RELEASE);
        release((unsigned long long *)(base + off), 2*(seq+1));
        release(&h->write_seq, seq+1);
        (void)region_wake(base, OBJ_RING_PUBOFF, 1);
        done += n;
    }
    return (ssize_t)done;
}

/* Raw console byte input, not yet POSIX canonical/termios semantics.
 * Avoid consuming unpublished or overwritten slots. */
/* Nonblocking poll of the original kernel input ring. The supervisor
 * needs multiplexed console input/output without owning a kernel Pipe.
 * Ordinary direct F5 reads retain the blocking helper below.
 */
ssize_t p2001_session_try_read(int fd, void *buf, size_t count)
{
    ObjRingHeader *h;
    unsigned long long seq, off;
    unsigned int len;
    unsigned char *p=(unsigned char *)buf;
    size_t n;
    if (fd!=0 || p2001_session_probe()!=1 || proxy_state ||
        (!buf && count)) return -1;
    if (!count) return 0;
    h=(ObjRingHeader *)inbase;
    if (inpos<inlen) {
        n=count < (size_t)(inlen-inpos) ? count : (size_t)(inlen-inpos);
        for(size_t j=0;j<n;++j) p[j]=inbuf[inpos+j];
        inpos+=(unsigned int)n;
        return (ssize_t)n;
    }
    seq=acquire(&h->write_seq);
    if (seq-inlast>OBJ_RING_SLOTS) inlast=seq-OBJ_RING_SLOTS;
    while(inlast<seq) {
        ++inlast;
        off=OBJ_RING_HEADER+((inlast-1)%OBJ_RING_SLOTS)*OBJ_RING_SLOT;
        if (acquire((unsigned long long *)(inbase+off))!=2*inlast)
            continue;
        len=*(volatile unsigned int *)(inbase+off+8);
        if (!len || len>OBJ_RING_DATA) continue;
        for(unsigned int j=0;j<len;++j)
            inbuf[j]=*(volatile unsigned char *)(inbase+off+16+j);
        if(acquire((unsigned long long *)(inbase+off))!=2*inlast)
            continue;
        inpos=0; inlen=len;
        break;
    }
    if(inpos==inlen) return 0;
    n=count<(size_t)(inlen-inpos)?count:(size_t)(inlen-inpos);
    for(size_t j=0;j<n;++j)p[j]=inbuf[inpos+j];
    inpos+=(unsigned int)n;
    return (ssize_t)n;
}
ssize_t p2001_session_read(int fd, void *buf, size_t count)
{
    long n, rc;
    if(fd!=0 || p2001_session_probe()!=1 || (!buf && count))
        return -1;
    if (proxy_state && proxy_streams==2) return 0; /* no stdin */
    if (proxy_state)
        return (ssize_t)uxpipe_read(&proxy_in,buf,(unsigned int)count,-1);
    if(!count) return 0;
    for (;;) {
        n=p2001_session_try_read(fd,buf,count);
        if(n!=0) return (ssize_t)n;
        rc=region_wait(inbase,OBJ_RING_PUBOFF,inlast,-1);
        if(rc==3) return 0;
        if(rc<0) return -1;
    }
}

/* A producer must publish EOF before exit; disappearing mappings are not
 * EOF. F5's persistent six-region proxy is never closed by an applet. */
void p2001_session_finish(void)
{
    if(p2001_session_probe()==1 &&
       (proxy_streams==2 || proxy_streams==4))
        (void)uxpipe_finish(&proxy_out);
}

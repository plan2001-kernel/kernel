/* Native boot-session transport. Only the exact three console grants are accepted. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>
#include "obj.h"
#include "vobj.h"
extern long obj_list(void *, unsigned long long);
extern long obj_region_map(int, unsigned long long);
extern long obj_region_unmap(unsigned long long);
extern long region_wait(unsigned long long,unsigned long long,unsigned long long,long long);
extern long region_wake(unsigned long long,unsigned long long,unsigned long long);
static int probe_state;
static unsigned long long inbase,outbase,diagbase,inlast;
static unsigned char inbuf[OBJ_RING_DATA];
static unsigned int inpos,inlen;
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
    VobjListRec handles[32]; int regions[3], found=0;
    long mapped[3]={-1,-1,-1};
    if(probe_state) return probe_state;
    long n=obj_list(handles,32);
    if(n<0 || n>32) goto broken;
    for(long i=0;i<n;i++){
        if(handles[i].kind!=VkindRegion) continue;
        unsigned rights=found==0 ? Vread|Vmeta : Vwrite|Vmeta;
        if(found==3 || handles[i].rights!=rights) goto broken;
        regions[found++]=handles[i].handle;
    }
    if(found!=3) goto broken;
    for(int i=0;i<3;i++){
        mapped[i]=obj_region_map(regions[i],0);
        if(mapped[i]<=0 || !ring_valid(mapped[i])) goto broken;
    }
    inbase=mapped[0];outbase=mapped[1];diagbase=mapped[2];
    inlast=acquire(&((ObjRingHeader *)inbase)->write_seq);
    probe_state=1;return 1;
broken:
    for(int i=0;i<3;i++) if(mapped[i]>0) obj_region_unmap(mapped[i]);
    probe_state=-1;return -1;
}
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
    if (fd!=0 || p2001_session_probe()!=1 ||
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
    if(!count) return 0;
    for (;;) {
        n=p2001_session_try_read(fd,buf,count);
        if(n!=0) return (ssize_t)n;
        rc=region_wait(inbase,OBJ_RING_PUBOFF,inlast,-1);
        if(rc==3) return 0;
        if(rc<0) return -1;
    }
}

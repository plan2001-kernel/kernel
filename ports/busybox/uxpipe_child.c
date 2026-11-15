/* Native QEMU fixture for the two-region byte stream.
 * uxpipe-send: data RW, ACK RO
 * uxpipe-recv: data RO, ACK RW
 * No ambient keyboard, console, pathname, volume or object authority.
 */
#include "libbb.h"
#include "../unix/uxpipe.h"
#include "obj.h"
#include "vobj.h"
extern long obj_list(void *, unsigned long long);
extern long obj_region_map(int, unsigned long long);
extern long obj_region_unmap(unsigned long long);
extern long sleep(long millis);

static int launch_stream(int sending)
{
    VobjListRec entries[16];
    unsigned int i;
    int writer = -1, observer = -1, ok = 0;
    long count, wb = -1, rb = -1, n;
    UxPipe pipe;
    unsigned int total = 0;
    unsigned char chunk[257];
    count = obj_list(entries, 16);
    if (count < 0 || count > 16) return 1;
    for (i=0;i<(unsigned int)count;++i) {
        if (entries[i].kind != VkindRegion) continue;
        if ((entries[i].rights & Vwrite) != 0) {
            if (writer >= 0) return 1;
            writer = (int)entries[i].handle;
        } else if ((entries[i].rights & Vread) != 0) {
            if (observer >= 0) return 1;
            observer = (int)entries[i].handle;
        }
    }
    if (writer < 0 || observer < 0) return 1;
    wb = obj_region_map(writer,0);
    if (wb < 0) return 1;
    rb = obj_region_map(observer,0);
    if (rb < 0) goto cleanup;
    if (sending) {
        if (uxpipe_bind(&pipe,(UxPipeData *)(unsigned long long)wb,
                        (UxPipeAck *)(unsigned long long)rb,
                        (unsigned long long)wb,(unsigned long long)rb,
                        UXPIPE_WRITER) != 0) goto cleanup;
        /* REGIONWAIT reports 3 when a previous ACK writer was unmapped.
         * The producer must not fill the ring before the new consumer has
         * mapped its ACK writer. The bounded startup rendezvous uses a
         * release-published ready word and ordinary process sleep.
         */
        for (i=0;i<5000u;++i) {
            if (__atomic_load_n(&pipe.ack->ready,__ATOMIC_ACQUIRE)) break;
            (void)sleep(1);
        }
        if (i == 5000u) goto cleanup;
        while (total < 10000u) {
            unsigned int amount=10000u-total;
            if (amount > sizeof(chunk)) amount=sizeof(chunk);
            for (i=0;i<amount;++i)
                chunk[i]=(unsigned char)((total+i)*37u+17u);
            n=uxpipe_write(&pipe,chunk,amount,-1);
            if (n != (long)amount) goto cleanup;
            total+=amount;
        }
    } else {
        if (uxpipe_bind(&pipe,(UxPipeData *)(unsigned long long)rb,
                        (UxPipeAck *)(unsigned long long)wb,
                        (unsigned long long)rb,(unsigned long long)wb,
                        UXPIPE_READER) != 0) goto cleanup;
        for (;;) {
            n=uxpipe_read(&pipe,chunk,sizeof(chunk),-1);
            if (n < 0) goto cleanup;
            if (n == 0) break; /* only explicit publisher EOF */
            if (total + (unsigned long)n > 10000u) goto cleanup;
            for (i=0;i<(unsigned int)n;++i)
                if (chunk[i] !=
                    (unsigned char)((total+i)*37u+17u)) goto cleanup;
            total+=(unsigned int)n;
        }
    }
    if (total != 10000u || uxpipe_finish(&pipe) < 0) goto cleanup;
    ok = 1;
cleanup:
    if (rb >= 0) (void)obj_region_unmap((unsigned long long)rb);
    if (wb >= 0) (void)obj_region_unmap((unsigned long long)wb);
    return ok ? 0 : 1;
}
int p2001_uxpipe_send_main(int argc, char **argv)
{
    (void)argv;
    return argc == 1 ? launch_stream(1) : 64;
}
int p2001_uxpipe_recv_main(int argc, char **argv)
{
    (void)argv;
    return argc == 1 ? launch_stream(0) : 64;
}

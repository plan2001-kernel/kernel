/* Native BusyBox stdout. Boot-owner may open its own console, but ordinary
 * SPAWN children have iomask=0: no ambient console authority.
 * Such a child receives an explicit delegated writable region Handle,
 * publishes its stdout there, and its MicroPython launcher reads it RO.
 * No kernel changes, fake success, or implicit Handle guessing.
 */
#include "libbb.h"
#include "obj.h"
#include "vobj.h"

extern long obj_open(unsigned long long, void *, long, unsigned long long, ObjId *);
extern long obj_write(int, void *, unsigned long long, unsigned long long);
extern long obj_list(void *records, unsigned long long count);
extern long obj_region_map(int handle, unsigned long long addr);
extern void *malloc(unsigned long);
extern void exits(char *);

int errno;
const char bb_msg_write_error[] = "native stdout write failed";

static int out_handle = -1;
static volatile unsigned long long *region_length;
static unsigned char *region_data;
enum { RegionOutputMax = 4096 - 8 };

static int open_stdout(void)
{
    ObjOpenConsoleReq req = { 1, 0 };
    long h;
    if (out_handle >= 0)
        return out_handle;
    h = obj_open(ObjOpenConsole, &req, sizeof(req), 0, 0);
    if (h < 0)
        return -1;
    out_handle = (int)h;
    return out_handle;
}

static int use_inherited_stdout(void)
{
    VobjListRec records[8];
    long count = obj_list(records, 8);
    long i;
    if (count < 0 || count > 8)
        return -1;
    for (i = 0; i < count; ++i) {
        if (records[i].kind == VkindRegion &&
                (records[i].rights & Vwrite)) {
            long base = obj_region_map((int)records[i].handle, 0);
            if (base < 0)
                return -1;
            region_length = (volatile unsigned long long *)base;
            region_data = (unsigned char *)(base + 8);
            return 0;
        }
    }
    return -1;
}

/* Successful output is committed to exactly one chosen authorized backend.
 * Once a region backend is selected, later writes must not lose the cursor.
 */
ssize_t full_write(int fd, const void *buf, size_t count)
{
    const unsigned char *p = (const unsigned char *)buf;
    size_t total = 0;
    int h;
    if (fd != STDOUT_FILENO)
        return -1;
    /* A manifest process uses only its kernel-provided session region.
     * A non-session SPAWN child keeps its explicitly delegated legacy frame.
     */
    h = p2001_session_probe();
    if (h < 0)
        return -1;
    if (h == 1)
        return p2001_session_write(fd, buf, count);
    if (out_handle < 0 && region_data == 0) {
        if (open_stdout() < 0 && use_inherited_stdout() < 0)
            return -1;
    }
    if (region_data != 0) {
        unsigned long long old = __atomic_load_n(
            (unsigned long long *)region_length, __ATOMIC_ACQUIRE);
        if (old > RegionOutputMax || count > RegionOutputMax - old)
            return -1;
        for (total = 0; total < count; ++total)
            region_data[old + total] = p[total];
        __atomic_store_n((unsigned long long *)region_length,
                         old + total, __ATOMIC_RELEASE);
        return (ssize_t)total;
    }
    h = out_handle;
    while (total < count) {
        long n = obj_write(h, (void *)(p + total), count - total, 0);
        if (n <= 0 || (size_t)n > count - total)
            return -1;
        total += (size_t)n;
    }
    return (ssize_t)total;
}

/* Raw session input is available only to a process with an actual RO
 * console stdin grant. No fallback to a physical keyboard or fake EOF.
 */
ssize_t full_read(int fd, void *buf, size_t count)
{
    if (fd != STDIN_FILENO)
        return -1;
    return p2001_session_read(fd, buf, count);
}

void *xmalloc(size_t size)
{
    void *p = malloc(size ? size : 1);
    if (!p)
        exits("BusyBox xmalloc failed");
    return p;
}

char *stpcpy(char *dest, const char *src)
{
    while ((*dest = *src) != 0) {
        ++dest;
        ++src;
    }
    return dest;
}

void bb_simple_perror_msg(const char *message)
{
    const char prefix[] = "BusyBox: ";
    (void)full_write(1, prefix, sizeof(prefix) - 1);
    (void)full_write(1, message, strlen(message));
    (void)full_write(1, "\n", 1);
}

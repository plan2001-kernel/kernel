/* Plan2001 native AMD64 syscall adapters for genuine Newlib libc.a.
 *
 * No Plan9 libc references: the only OS entry points below are the kernel's
 * generated user syscall stubs from rt_stubs.o. Newlib supplies errno,
 * strings, malloc and stdio. This target is single-threaded.
 *
 * fd 0/1/2: supervisor-delegated UXPIPE descriptor view when bound.
 * Standalone boot probes retain opt-in region stdin and native console I/O.
 * fd 3+: trusted userland UXFS file-service descriptors when attached.
 * No path, raw OID or integer Unix fd confers a kernel capability.
 */
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>
#include "obj.h"
#include "sysctl.h"
#include "vobj.h"
#include "../../unix/uxpipe.h"
#include "../../unix/uxfd.h"

extern long objcreate(unsigned long long, unsigned long long, void *,
                      unsigned long long, void *);
extern long objopen(unsigned long long, void *, long, unsigned long long,
                    void *);
extern long objinvoke(int, void *, long);
extern long objclose(int);
extern long regionwait(unsigned long long, unsigned long long,
                       unsigned long long, long long);
extern long regionwake(unsigned long long, unsigned long long,
                       unsigned long long);
extern long brk_(void *);
extern unsigned long long _nsec(void);
extern void _exits(char *);
extern char end[];

/* Newlib and the Unix supervisor use the SAME two-region UXPIPE wire format.
 * The kernel enforces RO/RW map rights. Handles do not turn into Unix FDs.
 * Binding is opt-in and does not alter standalone Newlib console probes. */
static UxPipe proxy[3];
static uintptr_t proxy_maps[6];
static int proxy_bound;
static unsigned char proxy_closed[3];
static uintptr_t read_base;
static uint64_t read_cursor;
static unsigned read_pos, read_len;
static unsigned char read_cache[OBJ_RING_DATA];
/* File descriptors 3+ use one explicitly attached trusted Unix service. */
static UxFdTable file_table;
static int files_bound;
static int console_handles[2] = {-1, -1};
static unsigned char console_closed[2];

static long invoke(int handle, unsigned op, void *in, uint64_t inlen,
                   void *out, uint64_t outlen, uint64_t arg0, uint64_t arg1)
{
    ObjCall c;
    long r;
    memset(&c, 0, sizeof c);
    c.version = OBJ_ABI_VERSION;
    c.op = op;
    c.in = in;
    c.inlen = inlen;
    c.out = out;
    c.outlen = outlen;
    c.arg0 = arg0;
    c.arg1 = arg1;
    r = objinvoke(handle, &c, sizeof c);
    return r;
}

long p9_full_region_create(uint64_t n)
{
    return objcreate(0, ObjKindRegion, 0, n, 0);
}

long p9_full_region_map(int handle, int readonly)
{
    return invoke(handle, ObjOpRegionMap, 0, 0, 0, 0, 0,
                  readonly ? ObjRegionMapReadonly : 0);
}

long p9_full_region_unmap(uintptr_t base)
{
    return invoke(0, ObjOpRegionUnmap, 0, 0, 0, 0, base, 0);
}

long p9_full_region_close(int handle)
{
    return objclose(handle);
}



int p9_full_bind_files(UxService *service)
{
    if(!service || !service->fs.view || files_bound) {
        errno = EINVAL; return -1;
    }
    uxfd_init(&file_table,service);
    files_bound=1;
    return 0;
}
void p9_full_unbind_files(void)
{
    uxfd_init(&file_table,0);
    files_bound=0;
}
UxService *p9_full_file_service(void)
{
    return files_bound ? file_table.service : 0;
}
static int translate_fs(int code)
{
    switch(code) {
    case PVD_ENOENT: return ENOENT;
    case PVD_EACCES: return EACCES;
    case PVD_EIO: return EIO;
    case PVD_EEXIST: return EEXIST;
    case PVD_ENAMETOOLONG: return ENAMETOOLONG;
    case PVD_ENOSPC: return ENOSPC;
    case PVD_ELOOP: return ELOOP;
    case PVD_EAMBIG: return EINVAL;
    default: return EINVAL;
    }
}
static int fail_fs(int code)
{
    errno = translate_fs(code);
    return -1;
}

long region_wait(unsigned long long base, unsigned long long off,
                 unsigned long long expected, long long timeout_ms)
{
    return regionwait(base, off, expected, timeout_ms);
}
long region_wake(unsigned long long base, unsigned long long off,
                 unsigned long long count)
{
    return regionwake(base, off, count);
}
void p9_full_unbind_uxpipe(void)
{
    unsigned i;
    /* A normal unbind must publish EOF/disconnect before releasing the
     * writer mapping; disappearing region writers are not protocol EOF. */
    if(proxy_bound) {
        for(i = 0; i < 3; ++i) {
            if(!proxy_closed[i]) {
                (void)uxpipe_finish(&proxy[i]);
                proxy_closed[i] = 1;
            }
        }
    }
    for(i = 0; i < 6; ++i) {
        if(proxy_maps[i]) {
            (void)p9_full_region_unmap(proxy_maps[i]);
            proxy_maps[i] = 0;
        }
    }
    memset(proxy, 0, sizeof proxy);
    memset(proxy_closed, 0, sizeof proxy_closed);
    proxy_bound = 0;
}
int p9_full_bind_uxpipe(const int handles[6])
{
    unsigned i;
    if(!handles) { errno = EINVAL; return -1; }
    if(proxy_bound || read_base || console_closed[0] || console_closed[1]) {
        errno = EBUSY; return -1;
    }
    for(i = 0; i < 6; ++i) {
        long mapped;
        int writable = (i == 1 || i == 2 || i == 4);
        if(handles[i] <= 0) goto denied;
        mapped = p9_full_region_map(handles[i], !writable);
        if(mapped <= 0) goto denied;
        proxy_maps[i] = (uintptr_t)mapped;
    }
    for(i = 0; i < 3; ++i) {
        int role = i == 0 ? UXPIPE_READER : UXPIPE_WRITER;
        if(uxpipe_bind(&proxy[i], (UxPipeData *)proxy_maps[2*i],
                       (UxPipeAck *)proxy_maps[2*i+1],
                       proxy_maps[2*i], proxy_maps[2*i+1], role))
            goto denied;
    }
    proxy_bound = 1;
    return 0;
denied:
    p9_full_unbind_uxpipe();
    errno = EACCES;
    return -1;
}

/* Import ONLY the exact stream capabilities delegated by the trusted
 * Unix supervisor. SYSCTL reveals the calling process's own Handle table,
 * never another process's capabilities. Slot order is supervisor's explicit
 * Oinheritv contract, not a search for arbitrary writable regions.
 *
 * Six RO/RW regions plus one execute-only volume are expected. The volume
 * is launch authority only; it is never used for stdio or file I/O.
 * An incomplete or excessive grant list fails closed, without falling back
 * to ObjOpenConsole or another inherited region.
 */
int p9_full_bind_inherited_uxpipe(void)
{
    VobjListRec records[16];
    SysctlReq req;
    static const unsigned rights[6] = {
        Vread, Vwrite, Vwrite, Vread, Vwrite, Vread
    };
    int handles[6];
    unsigned streams = 0, executable_volumes = 0, i;
    long n;
    memset(&req, 0, sizeof req);
    req.version = SYSCTL_ABI_VERSION;
    req.data = records;
    req.datalen = sizeof records;
    if(sysctl(SysctlScopeOwnProcess, SysctlHandleList,
              &req, sizeof req) < 0 ||
       req.result < 0 || req.result > 16 ||
       req.aux != (unsigned long long)req.result) {
        errno = EACCES;
        return -1;
    }
    n = (long)req.result;
    for(i = 0; i < (unsigned)n; ++i) {
        const VobjListRec *r = &records[i];
        if(r->kind == VkindRegion) {
            if(streams == 6 || r->rights != rights[streams] ||
               r->handle == 0) goto denied;
            handles[streams++] = (int)r->handle;
        } else if(r->kind == VkindGeVolume && r->rights == Vexecute) {
            if(++executable_volumes != 1) goto denied;
        } else {
            goto denied;
        }
    }
    if(streams != 6 || executable_volumes != 1) goto denied;
    return p9_full_bind_uxpipe(handles);
denied:
    errno = EACCES;
    return -1;
}

/* The standard Newlib process startup calls this before main() or any
 * application stdio. Zero inherited region Handles means standalone native
 * execution (the original console probe/exit tests still use native I/O).
 * Any nonzero region grant set MUST satisfy the complete, attenuated
 * supervisor contract; a two-region legacy frame is not a Unix stream.
 */
int p9_full_auto_bind_uxpipe(void)
{
    VobjListRec records[16];
    SysctlReq req;
    unsigned i;
    long n;
    memset(&req, 0, sizeof req);
    req.version = SYSCTL_ABI_VERSION;
    req.data = records;
    req.datalen = sizeof records;
    if(sysctl(SysctlScopeOwnProcess, SysctlHandleList,
              &req, sizeof req) < 0 ||
       req.result < 0 || req.result > 16 ||
       req.aux != (unsigned long long)req.result) {
        errno = EACCES;
        return -1;
    }
    n = (long)req.result;
    for(i = 0; i < (unsigned)n; ++i) {
        if(records[i].kind == VkindRegion)
            return p9_full_bind_inherited_uxpipe() == 0 ? 1 : -1;
    }
    return 0; /* no delegated stream: preserve standalone Newlib mode */
}

int p9_full_bind_stdin(int region_handle)
{
    long mapped;
    ObjRingHeader *header;
    if(read_base != 0 || proxy_bound) { errno = EBUSY; return -1; }
    if(region_handle <= 0) { errno = EBADF; return -1; }
    mapped = p9_full_region_map(region_handle, 1);
    if(mapped <= 0) { errno = EACCES; return -1; }
    header = (ObjRingHeader *)(uintptr_t)mapped;
    if(header->version != OBJ_RING_VERSION ||
       header->magic != OBJ_RING_MAGIC ||
       header->slots != OBJ_RING_SLOTS ||
       header->slot_size != OBJ_RING_SLOT) {
        (void)p9_full_region_unmap((uintptr_t)mapped);
        errno = EINVAL;
        return -1;
    }
    read_base = (uintptr_t)mapped;
    read_cursor = 0;
    read_pos = read_len = 0;
    return 0;
}

void p9_full_unbind_stdin(void)
{
    if(read_base != 0) {
        (void)p9_full_region_unmap(read_base);
        read_base = 0;
        read_cursor = 0;
        read_pos = read_len = 0;
    }
}

/* Newlib's reentrant read wrapper calls this platform primitive. */
int _read(int fd, char *buf, int count)
{
    ObjRingHeader *h;
    unsigned n;
    if(fd >= 3 && files_bound) {
        long result;
        if(count < 0 || (count && !buf)) { errno = EINVAL; return -1; }
        result = uxfd_read(&file_table,fd,buf,(unsigned)count);
        return result < 0 ? fail_fs((int)result) : (int)result;
    }
    if(fd != 0 || (read_base == 0 && !proxy_bound) || proxy_closed[0]) { errno = EBADF; return -1; }
    if(count < 0) { errno = EINVAL; return -1; }
    if(count == 0) return 0;
    if(buf == 0) { errno = EFAULT; return -1; }
    if(proxy_bound) {
        long n = uxpipe_read(&proxy[0], buf,
                             (unsigned)count > UXPIPE_MAX_IO ? UXPIPE_MAX_IO : (unsigned)count, -1);
        if(n < 0) { errno = EIO; return -1; }
        return (int)n;
    }
    h = (ObjRingHeader *)read_base;
    while(read_pos == read_len) {
        uint64_t published = __atomic_load_n(&h->write_seq, __ATOMIC_ACQUIRE);
        uint64_t seq, off, marker;
        ObjRingSlot *slot;
        long result;
        if(published <= read_cursor) {
            result = regionwait(read_base, OBJ_RING_PUBOFF, published, -1);
            if(result == 3) return 0; /* producer released */
            if(result < 0) { errno = EIO; return -1; }
            continue;
        }
        if(published - read_cursor > OBJ_RING_SLOTS) {
            errno = EOVERFLOW;
            return -1;
        }
        seq = read_cursor + 1;
        off = OBJ_RING_HEADER + ((seq - 1) % OBJ_RING_SLOTS) * OBJ_RING_SLOT;
        slot = (ObjRingSlot *)(read_base + off);
        marker = __atomic_load_n(&slot->seq, __ATOMIC_ACQUIRE);
        if(marker != 2*seq || slot->length > OBJ_RING_DATA) {
            errno = EIO;
            return -1;
        }
        read_len = slot->length;
        if(read_len != 0)
            memcpy(read_cache, (void *)(read_base + off + 16), read_len);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if(__atomic_load_n(&slot->seq, __ATOMIC_ACQUIRE) != marker) {
            errno = EOVERFLOW;
            return -1;
        }
        read_pos = 0;
        read_cursor = seq;
    }
    n = read_len - read_pos;
    if(n > (unsigned)count) n = (unsigned)count;
    memcpy(buf, read_cache + read_pos, n);
    read_pos += n;
    return (int)n;
}

int _write(int fd, const char *buf, int count)
{
    ObjOpenConsoleReq req;
    unsigned sent = 0;
    long h, ret;
    int index;
    if(fd >= 3 && files_bound) {
        long result;
        if(count < 0 || (count && !buf)) { errno = EINVAL; return -1; }
        result = uxfd_write(&file_table,fd,buf,(unsigned)count);
        return result < 0 ? fail_fs((int)result) : (int)result;
    }
    if(fd != 1 && fd != 2) { errno = EBADF; return -1; }
    if(count < 0) { errno = EINVAL; return -1; }
    if(count == 0) return 0;
    if(buf == 0) { errno = EFAULT; return -1; }
    index = fd - 1;
    if(proxy_bound) {
        unsigned wrote = 0;
        if(proxy_closed[fd]) { errno = EBADF; return -1; }
        while(wrote < (unsigned)count) {
            unsigned n = (unsigned)count - wrote;
            long got;
            if(n > UXPIPE_MAX_IO) n = UXPIPE_MAX_IO;
            got = uxpipe_write(&proxy[fd], buf + wrote, n, -1);
            if(got <= 0 || (unsigned long)got > n) {
                if(wrote) return (int)wrote;
                errno = EIO; return -1;
            }
            wrote += (unsigned)got;
        }
        return (int)wrote;
    }
    if(console_closed[index]) { errno = EBADF; return -1; }
    h = console_handles[index];
    if(h < 0) {
        req.stream = fd;
        req.reserved = 0;
        h = objopen(ObjOpenConsole, &req, sizeof req, 0, 0);
        if(h < 0) { errno = EACCES; return -1; }
        console_handles[index] = (int)h;
    }
    while(sent < (unsigned)count) {
        unsigned n = (unsigned)count - sent;
        if(n > OBJ_STREAM_MAXIO) n = OBJ_STREAM_MAXIO;
        ret = invoke((int)h, ObjOpWrite, (void *)(buf + sent), n,
                     0, 0, 0, 0);
        if(ret <= 0 || (unsigned long)ret > n) {
            if(sent) return (int)sent;
            errno = EIO;
            return -1;
        }
        sent += (unsigned)ret;
    }
    return (int)sent;
}

void *_sbrk(ptrdiff_t increment)
{
    /* Exactly one break, owned by Newlib (no Plan9 libc allocator).
     * Keep the Plan2001 legacy 8-byte BRK_ rounding.
     */
    static uintptr_t current;
    uintptr_t previous, next, inc;
    if(current == 0) {
        current = ((uintptr_t)end + 7u) & ~(uintptr_t)7u;
    }
    if(increment < 0) { errno = EINVAL; return (void *)-1; }
    previous = (current + 7u) & ~(uintptr_t)7u;
    inc = (uintptr_t)increment;
    if(previous > UINTPTR_MAX - inc) {
        errno = ENOMEM;
        return (void *)-1;
    }
    next = previous + inc;
    if(next > UINTPTR_MAX - 7u ||
       brk_((void *)((next + 7u) & ~(uintptr_t)7u)) < 0) {
        errno = ENOMEM;
        return (void *)-1;
    }
    current = next;
    return (void *)previous;
}

_Noreturn void _exit(int code)
{
    unsigned i;
    /* Newlib exit() flushes stdio before entering this native primitive.
     * Publish explicit UXPIPE close for any still-open descriptor so a
     * successful process exit never looks like a broken region writer. */
    if(proxy_bound) {
        for(i = 0; i < 3; ++i) {
            if(!proxy_closed[i]) {
                (void)uxpipe_finish(&proxy[i]);
                proxy_closed[i] = 1;
            }
        }
    }
    /* EXITS preserves success/failure, not the exact numeric Unix code. */
    _exits(code == 0 ? "" : "error");
    __builtin_unreachable();
}

int _close(int fd)
{
    if(fd >= 3 && files_bound) {
        int code = uxfd_close(&file_table,fd);
        return code == PVD_OK ? 0 : fail_fs(code);
    }
    if(proxy_bound && fd >= 0 && fd <= 2) {
        if(proxy_closed[fd]) { errno = EBADF; return -1; }
        proxy_closed[fd] = 1;
        if(uxpipe_finish(&proxy[fd])) { errno = EIO; return -1; }
        return 0;
    }
    if(fd == 0 && read_base != 0) { p9_full_unbind_stdin(); return 0; }
    if(fd == 1 || fd == 2) {
        int index = fd - 1;
        if(console_closed[index]) { errno = EBADF; return -1; }
        console_closed[index] = 1;
        if(console_handles[index] >= 0)
            return objclose(console_handles[index]) < 0
                ? (errno = EIO, -1) : 0;
        return 0;
    }
    errno = EBADF;
    return -1;
}

int _fstat(int fd, struct stat *st)
{
    if(st == 0) { errno = EFAULT; return -1; }
    if(fd >= 3 && files_bound) {
        unsigned kind, length;
        int code=uxfd_info(&file_table,fd,&kind,&length);
        if(code) return fail_fs(code);
        memset(st,0,sizeof *st);
        st->st_mode=(kind==UXFS_DIR ? S_IFDIR : S_IFREG) | 0600;
        st->st_size=length;
        st->st_nlink=1;
        st->st_blksize=UXFS_DATA_MAX;
        return 0;
    }
    if(proxy_bound && fd >= 0 && fd <= 2 && !proxy_closed[fd]) {
        memset(st, 0, sizeof *st);
        st->st_mode = S_IFIFO | 0600;
        st->st_nlink = 1;
        st->st_blksize = UXPIPE_CAPACITY;
        return 0;
    }
    if((fd == 0 && read_base != 0) ||
       (fd >= 1 && fd <= 2 && !console_closed[fd-1])) {
        memset(st, 0, sizeof *st);
        st->st_mode = S_IFCHR | 0600;
        st->st_nlink = 1;
        st->st_blksize = 1;
        return 0;
    }
    errno = EBADF;
    return -1;
}

int _isatty(int fd)
{
    if(fd>=3 && files_bound) {
        unsigned kind, length;
        errno = uxfd_info(&file_table,fd,&kind,&length) == PVD_OK ? ENOTTY : EBADF;
        return 0;
    }
    if(proxy_bound && fd >= 0 && fd <= 2) {
        errno = proxy_closed[fd] ? EBADF : ENOTTY;
        return 0;
    }
    if(fd == 1 || fd == 2) {
        if(!console_closed[fd-1]) return 1;
    } else if(fd == 0 && read_base != 0) {
        return 1;
    }
    errno = EBADF;
    return 0;
}

off_t _lseek(int fd, off_t offset, int whence)
{
    if(fd >= 3 && files_bound) {
        long long pos = uxfd_seek(&file_table,fd,(long long)offset,whence);
        if(pos < 0) { (void)fail_fs((int)pos); return (off_t)-1; }
        return (off_t)pos;
    }
    (void)offset; (void)whence;
    if(proxy_bound && fd >= 0 && fd <= 2) {
        errno = proxy_closed[fd] ? EBADF : ESPIPE;
        return (off_t)-1;
    }
    if((fd >= 1 && fd <= 2 && !console_closed[fd-1]) ||
       (fd == 0 && read_base != 0)) {
        errno = ESPIPE;
    } else {
        errno = EBADF;
    }
    return (off_t)-1;
}

int _getpid(void)
{
    SysctlReq req;
    SysctlProcInfo info;
    memset(&req, 0, sizeof req);
    memset(&info, 0, sizeof info);
    req.version = SYSCTL_ABI_VERSION;
    req.data = &info;
    req.datalen = sizeof info;
    if(sysctl(SysctlScopeOwnProcess, SysctlProcessInfo, &req, sizeof req) < 0) {
        errno = EIO;
        return -1;
    }
    return (int)info.pid;
}

int _kill(int pid, int sig)
{
    (void)pid; (void)sig;
    errno = ENOSYS; /* PID does not grant process-control authority */
    return -1;
}

int _open(const char *path, int flags, ...)
{
    unsigned rights = 0;
    int acc, result;
    if(!files_bound) { errno = ENOSYS; return -1; }
    if(!path) { errno = EFAULT; return -1; }
    acc = flags & O_ACCMODE;
    if(acc == O_RDONLY) rights=UXFD_READ;
    else if(acc == O_WRONLY) rights=UXFD_WRITE;
    else if(acc == O_RDWR) rights=UXFD_READ|UXFD_WRITE;
    else { errno = EINVAL; return -1; }
    if(flags & O_CREAT) rights |= UXFD_CREATE;
    if(flags & O_TRUNC) rights |= UXFD_TRUNC;
    if(flags & O_APPEND) rights |= UXFD_APPEND;
#ifdef O_EXCL
    if(flags & O_EXCL) { errno = ENOSYS; return -1; }
#endif
    result = uxfd_open(&file_table,path,rights);
    return result >= 0 ? result : fail_fs(result);
}
int _unlink(const char *path)
{
    int result;
    if(!files_bound) { errno = ENOSYS; return -1; }
    if(!path) { errno = EFAULT; return -1; }
    result = uxfd_unlink(&file_table,path);
    return result == PVD_OK ? 0 : fail_fs(result);
}
int _link(const char *existing, const char *newpath)
{
    (void)existing; (void)newpath; errno = ENOSYS; return -1;
}
int _stat(const char *path, struct stat *st)
{
    unsigned kind, length;
    int result;
    if(!files_bound) { errno = ENOSYS; return -1; }
    if(!path || !st) { errno = EFAULT; return -1; }
    result=uxfs_info(&file_table.service->fs,path,&kind,&length);
    if(result) return fail_fs(result);
    memset(st,0,sizeof *st);
    st->st_mode=(kind == UXFS_DIR ? S_IFDIR : S_IFREG) | 0600;
    st->st_size=length;
    st->st_nlink=1;
    st->st_blksize=UXFS_DATA_MAX;
    return 0;
}
/* The POSIX cwd is a userland PVD projection, not kernel state.
 * Every pathname operation requires an explicitly attached trusted service.
 * Names/OIDs never create capabilities. A child with only UXPIPE stdio has
 * no pathname authority until a service is attached or RPC is delegated.
 */
static UxService *file_service(void)
{
    return files_bound ? file_table.service : 0;
}

int _chdir(const char *path)
{
    UxService *s=file_service();
    int result;
    if(!s) { errno=ENOSYS; return -1; }
    if(!path) { errno=EFAULT; return -1; }
    result=pvd_chdir(s->fs.view,path);
    return result==PVD_OK ? 0 : fail_fs(result);
}

char *getcwd(char *buf, size_t size)
{
    UxService *s=file_service();
    char path[PVD_PATH_MAX];
    unsigned int len;
    int result;
    if(!s) { errno=ENOSYS; return 0; }
    result=pvd_getcwd(s->fs.view,path,sizeof path);
    if(result!=PVD_OK) { (void)fail_fs(result); return 0; }
    len=(unsigned int)strlen(path)+1;
    if(!buf) {
        /* Newlib supports the commonly used getcwd(NULL, 0) allocation. */
        if(size==0) size=len;
        if(size<len) { errno=ERANGE; return 0; }
        buf=(char *)malloc(size);
        if(!buf) { errno=ENOMEM; return 0; }
    } else if(size<len || size==0) {
        errno=ERANGE; return 0;
    }
    memcpy(buf,path,len);
    return buf;
}

int _mkdir(const char *path, mode_t mode)
{
    UxService *s=file_service();
    int result;
    (void)mode; /* v1 PVD records no Unix mode: no capability is minted. */
    if(!s) { errno=ENOSYS; return -1; }
    if(!path) { errno=EFAULT; return -1; }
    result=uxfs_mkdir(&s->fs,path);
    if(result!=PVD_OK) return fail_fs(result);
    result=uxservice_sync(s);
    return result==PVD_OK ? 0 : fail_fs(result);
}

int _rename(const char *src, const char *dst)
{
    UxService *s=file_service();
    int result;
    if(!s) { errno=ENOSYS; return -1; }
    if(!src || !dst) { errno=EFAULT; return -1; }
    /* v1 UXFS supports regular-file moves only, not directory rename. */
    result=uxfs_move(&s->fs,src,dst);
    if(result!=PVD_OK) return fail_fs(result);
    result=uxservice_sync(s);
    return result==PVD_OK ? 0 : fail_fs(result);
}

int _access(const char *path, int mode)
{
    UxService *s=file_service();
    unsigned int kind,length;
    int result;
    if(!s) { errno=ENOSYS; return -1; }
    if(!path) { errno=EFAULT; return -1; }
    if(mode & ~(F_OK|R_OK|W_OK|X_OK)) { errno=EINVAL; return -1; }
    /* Existence is queryable with the service's explicit authority.
     * Unix rwx bits cannot authorize ObjectSpace operations, so deny
     * nontrivial synthetic permission queries instead of returning 0. */
    if(mode!=F_OK) { errno=ENOSYS; return -1; }
    result=uxfs_info(&s->fs,path,&kind,&length);
    return result==PVD_OK ? 0 : fail_fs(result);
}

int _lstat(const char *path, struct stat *st)
{
    /* UXFS has no symlinks; do not claim the existence of symlink metadata. */
    return _stat(path,st);
}

int _fork(void)
{
    errno = ENOSYS; return -1; /* native RFORKEX is not POSIX fork */
}
int _execve(const char *path, char *const argv[], char *const envp[])
{
    (void)path; (void)argv; (void)envp;
    errno = ENOSYS; return -1;
}
int _wait(int *status)
{
    (void)status; errno = ENOSYS; return -1;
}
clock_t _times(struct tms *t)
{
    /* Fail-closed, and never leak uninitialized tms fields to ash times. */
    if(t) memset(t,0,sizeof(*t));
    errno = ENOSYS; return (clock_t)-1;
}
int _gettimeofday(struct timeval *tv, void *tz)
{
    unsigned long long ns;
    if(tv == 0) { errno = EFAULT; return -1; }
    ns = _nsec(); /* Direct native _NSEC(53), UTC nanoseconds since epoch. */
    tv->tv_sec = (time_t)(ns / 1000000000ULL);
    tv->tv_usec = (suseconds_t)((ns % 1000000000ULL) / 1000ULL);
    if(tz != 0) {
        /* Legacy timezone output: UTC; no per-process timezone service. */
        struct timezone *zone = (struct timezone *)tz;
        zone->tz_minuteswest = 0;
        zone->tz_dsttime = 0;
    }
    return 0;
}

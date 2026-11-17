/* Native executable backed exclusively by upstream Newlib libc.a/libm.a.
 * Real QEMU guest checks stdio, malloc, errno, string, math, capability
 * stdin _read and native console _write. Failure means no PASS marker.
 */
#include <errno.h>
#include <stddef.h>
#include <unistd.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "obj.h"
#include "plan2001_full.h"
#include "../../unix/uxfd.h"
#include "../../unix/uxpipe.h"
#include <fcntl.h>
#include <dirent.h>
#include <sys/utsname.h>
#include <poll.h>
#include <termios.h>
#include <sys/resource.h>

extern int _fstat(int fd, struct stat *st);
extern int _open(const char *path, int flags, ...);
extern void _exit(int status);
extern int _read(int fd, char *buf, int count);
extern int _unlink(const char *path);
extern int _stat(const char *path, struct stat *st);
extern off_t _lseek(int fd, off_t offset, int whence);
extern void *_sbrk(ptrdiff_t change);

static void check(int predicate, const char *what)
{
    if(!predicate) {
        fprintf(stderr, "PLAN2001-NEWLIB-FULL-FAIL: %s\n", what);
        fflush(stderr);
        _exit(9);
    }
}


/* Test the *actual* native Plan2001 region maps and the supervisor's
 * UXPIPE format, not a Linux pipe or synthetic success. */

static void check_uxfiles(void)
{
    static Pvd view;
    static UxService service;
    const char *filename="/tmp/newlib-file";
    char buffer[64]={0};
    struct stat st;
    int fd;
    check(pvd_bootstrap(&view)==PVD_OK,"bootstrap PVD file view");
    uxservice_init(&service,&view);
    check(p9_full_bind_files(&service)==0,"bind explicit Unix file service");
    /* Ash-relevant POSIX entrypoints must share this single PVD/UXFS
     * authority and correctly deny absent paths. No kernel cwd/VFS. */
    {
        char cwd[PVD_PATH_MAX];
        char *allocated;
        struct stat info;
        check(getcwd(cwd,sizeof cwd)==cwd && strcmp(cwd,"/")==0,
              "Newlib getcwd PVD root");
        check(mkdir("/tmp/ash",0700)==0,"Newlib mkdir PVD directory");
        {
            DIR *directory=opendir("/tmp");
            struct dirent *item;
            int seen=0;
            check(directory!=NULL,"POSIX opendir authorized /tmp");
            while((item=readdir(directory))!=NULL) {
                if(strcmp(item->d_name,"ash")==0 && item->d_type==DT_DIR)
                    ++seen;
            }
            check(seen==1,"POSIX readdir lists real UXFS directory");
            rewinddir(directory);
            check(readdir(directory)!=NULL,"POSIX rewinddir");
            errno=0;
            check(dirfd(directory)==-1 && errno==ENOSYS,
                  "do not manufacture a directory FD");
            check(closedir(directory)==0,"POSIX closedir");
        }

        check(chdir("/tmp/ash")==0,"Newlib chdir PVD directory");
        check(getcwd(cwd,sizeof cwd)==cwd &&
              strcmp(cwd,"/tmp/ash")==0,"Newlib getcwd changed PVD");
        errno=0;
        check(getcwd(cwd,1)==NULL && errno==ERANGE,
              "Newlib getcwd checks buffer length");
        allocated=getcwd(NULL,0);
        check(allocated!=NULL && strcmp(allocated,"/tmp/ash")==0,
              "Newlib allocated getcwd");
        free(allocated);
        fd=open("input",O_CREAT|O_RDWR|O_TRUNC,0600);
        check(fd>=3,"public POSIX open relative cwd");
        check(write(fd,"ash",3)==3,"public POSIX write");
        check(close(fd)==0,"public POSIX close");
        check(rename("input","renamed")==0,"POSIX rename through UXFS");
        check(stat("renamed",&info)==0 && info.st_size==3,
              "POSIX stat renamed file");
        check(lstat("renamed",&info)==0 && S_ISREG(info.st_mode),
              "POSIX lstat ordinary file");
        check(access("renamed",F_OK)==0,"POSIX access F_OK");
        errno=0;
        check(access("renamed",X_OK)==-1 && errno==ENOSYS,
              "Do not forge executable permissions from path");
        check(unlink("renamed")==0,"public POSIX unlink");
        errno=0;
        check(access("renamed",F_OK)==-1 && errno==ENOENT,
              "POSIX access no longer exists");
        check(chdir("/")==0,"Newlib chdir root");
    }
    printf("PLAN2001-NEWLIB-ASH-PATH-PASS\n");
    fflush(stdout);
    fd=_open(filename,O_CREAT|O_RDWR|O_TRUNC,0600);
    check(fd>=3,"Newlib _open via delegated Unix file view");
    check(write(fd,"newlib-file-test",16)==16,"Newlib write file");
    check(lseek(fd,0,SEEK_SET)==0,"Newlib lseek file");
    check(read(fd,buffer,sizeof(buffer))==16,"Newlib read file");
    check(memcmp(buffer,"newlib-file-test",16)==0,"Newlib file contents");
    {
        int alias=dup(fd);
        check(alias>=3,"duplicate regular file description");
        check(lseek(alias,0,SEEK_SET)==0,"duplicate shares file offset");
        check(read(fd,buffer,3)==3 && !memcmp(buffer,"new",3),
              "original reads from duplicate seek");
        check(read(alias,buffer,3)==3 && !memcmp(buffer,"lib",3),
              "duplicate observes original offset update");
        check(close(alias)==0,"close duplicate regular file");
    }
    check(_fstat(fd,&st)==0 && S_ISREG(st.st_mode) && st.st_size==16,
          "Newlib fstat regular file");
    check(close(fd)==0,"Newlib close regular file");
    check(_stat(filename,&st)==0 && st.st_size==16,
          "Newlib stat via UXFS");
    check(_unlink(filename)==0,"Newlib unlink via UXFS");
    errno=0;
    check(_open(filename,O_RDONLY)==-1 && errno==ENOENT,
          "Newlib file path no longer exists");
    p9_full_unbind_files();
    errno=0;
    check(getcwd(buffer,sizeof(buffer))==NULL && errno==ENOSYS,
          "without file service Newlib has no implicit cwd authority");
    errno=0;
    check(opendir("/tmp")==NULL && errno==ENOSYS,
          "without file service there is no ambient directory authority");

    printf("PLAN2001-NEWLIB-FULL-UXFILE-PASS\n");
    fflush(stdout);
}

static void check_uxpipe(void)
{
    int handles[6];
    uintptr_t maps[6];
    unsigned i;
    static const char payload[] = "newlib-uxpipe-input";
    static const char output[] = "newlib-uxpipe-output";
    char readback[sizeof(payload)];
    struct stat st;
    for(i=0;i<6;++i) {
        long h = p9_full_region_create(UXPIPE_PAGE);
        check(h>0, "create UXPIPE region");
        handles[i]=(int)h;
        maps[i]=(uintptr_t)p9_full_region_map(handles[i],0);
        check(maps[i]!=0 && maps[i]!=(uintptr_t)-1, "initialize UXPIPE");
    }
    for(i=0;i<3;++i)
        check(uxpipe_format((UxPipeData *)maps[2*i],
                            (UxPipeAck *)maps[2*i+1])==0, "format UXPIPE");
    /* This self-contained legacy probe has no separate peer process:
     * explicitly simulate peer mappings for the new UXPIPE startup
     * rendezvous. The real delegated-child CI test below is authoritative
     * for cross-process readiness and capability attenuation.
     */
    __atomic_store_n(&((UxPipeData *)maps[0])->ready,1,__ATOMIC_RELEASE);
    __atomic_store_n(&((UxPipeAck *)maps[3])->ready,1,__ATOMIC_RELEASE);
    __atomic_store_n(&((UxPipeAck *)maps[5])->ready,1,__ATOMIC_RELEASE);
    /* Simulate an already-published stdin + explicit producer EOF. */
    memcpy(((UxPipeData *)maps[0])->payload,payload,sizeof(payload));
    __atomic_store_n(&((UxPipeData *)maps[0])->produced,
                     sizeof(payload),__ATOMIC_RELEASE);
    __atomic_store_n(&((UxPipeData *)maps[0])->closed,1,__ATOMIC_RELEASE);
    for(i=0;i<6;++i)
        check(p9_full_region_unmap(maps[i])==0, "transfer UXPIPE writers");
    check(p9_full_bind_uxpipe(handles)==0, "bind supervisor stream grants");
    check(_fstat(1,&st)==0 && S_ISFIFO(st.st_mode), "stdout UXPIPE fstat");
    check(read(0,readback,sizeof readback)==sizeof(payload),
          "read stdin UXPIPE bytes");
    check(memcmp(readback,payload,sizeof(payload))==0,"stdin content");
    check(read(0,readback,1)==0,"stdin UXPIPE explicit EOF");
    check(write(1,output,sizeof(output))==sizeof(output),
          "write Newlib stdout UXPIPE");
    check(write(2,output,sizeof(output))==sizeof(output),
          "write Newlib stderr UXPIPE");
    check(_lseek(0,0,0)==-1 && errno==ESPIPE,"UXPIPE is not seekable");
    /* End this explicit test session without closing the process's Unix
     * stdout/stderr descriptors; the subsequent probes still use them. */
    p9_full_unbind_uxpipe();
    /* Read-only observers are permitted while the producer has RW. */
    for(i=1;i<3;++i) {
        long mapped=p9_full_region_map(handles[2*i],1);
        UxPipeData *p;
        check(mapped>0,"observe published UXPIPE");
        p=(UxPipeData *)(uintptr_t)mapped;
        check(p->produced==sizeof(output) && p->closed==1 &&
              memcmp(p->payload,output,sizeof(output))==0,
              "Newlib output bytes reached delegated regions");
        check(p9_full_region_unmap((uintptr_t)mapped)==0,"unmap observer");
    }
    for(i=0;i<6;++i)
        check(p9_full_region_close(handles[i])==0,"close UXPIPE handle");
    printf("PLAN2001-NEWLIB-FULL-UXPIPE-PASS\n");
    fflush(stdout);
}

static void check_descriptors(void)
{
    int streams[2], alias, saved;
    char bytes[8];
    struct stat st;
    check(pipe(streams)==0,"POSIX pipe creates native UXPIPE regions");
    alias=dup(streams[1]);
    check(alias>=3,"duplicate pipe writer");
    check(fcntl(alias,F_SETFD,FD_CLOEXEC)==0,"set descriptor CLOEXEC");
    check(fcntl(streams[1],F_GETFD)==0,"descriptor flags are independent");
    saved=dup(1);
    check(saved>=3,"save stdout descriptor");
    fflush(stdout);
    check(dup2(streams[1],1)==1,"redirect stdout to UXPIPE");
    check(write(1,"ash",3)==3,"write through redirected stdout");
    check(dup2(saved,1)==1 && close(saved)==0,"restore stdout");
    check(close(streams[1])==0,"close original writer");
    check(write(alias,"!",1)==1,"duplicate keeps writer alive");
    check(close(alias)==0,"last close publishes EOF");
    check(read(streams[0],bytes,sizeof(bytes))==4 && !memcmp(bytes,"ash!",4),
          "pipe data survives writer close");
    check(read(streams[0],bytes,sizeof(bytes))==0,"POSIX pipe EOF");
    check(fstat(streams[0],&st)==0 && S_ISFIFO(st.st_mode),"POSIX pipe metadata");
    check(close(streams[0])==0,"last reader releases regions");
    printf("PLAN2001-NEWLIB-FULL-FD-PASS\n");
    fflush(stdout);
}

int main(int argc, char **argv)
{
    char tmp[100];
    char *p, *endp;
    long region, write_map;
    ObjRingHeader *hdr;
    ObjRingSlot *slot;
    static const char source[] = "libc-stdin-test\n";
    volatile double theta = 0.5;
    double v;
    long value;
    struct stat st;
    struct timeval before, after;
    long long elapsed_us;
    (void)argc; (void)argv;
    errno = 0;
    check(_read(0, tmp, 1) == -1 && errno == EBADF,
          "stdin must require an explicit capability");
    check(_sbrk(0) != (void *)-1, "BRK_ used by Newlib allocator");
    check(sbrk(0) == _sbrk(0), "sbrk POSIX forward shares break");

    {
        struct utsname uts;
        struct termios attr;
        struct rlimit limits;
        check(uname(&uts)==0 &&
              strcmp(uts.sysname,"Plan2001")==0 &&
              strcmp(uts.machine,"x86_64")==0,
              "ash uname Plan2001 target identity");
        errno=0;
        check(poll(NULL,0,0)==0,
              "empty immediate poll returns no ready descriptors");
        errno=0;
        check(tcgetattr(1,&attr)==-1 && errno==ENOTTY,
              "ash must not claim tty control on native console");
        check(getrlimit(RLIMIT_NOFILE,&limits)==0 &&
              limits.rlim_cur==3+UXFD_SLOTS &&
              limits.rlim_max==3+UXFD_SLOTS,
              "ash getrlimit reports actual bounded UXFD capacity");
        errno=0;
        check(setrlimit(RLIMIT_NOFILE,&limits)==-1 && errno==ENOSYS,
              "ash setrlimit must not bypass native capability budgets");
        errno=0;
        check(getrlimit(RLIMIT_DATA,&limits)==-1 && errno==ENOSYS,
              "ash cannot infer kernel memory budget from POSIX RLIMIT");
        printf("PLAN2001-NEWLIB-ASH-POSIX-PASS\n");
        fflush(stdout);
    }
    check(snprintf(tmp, sizeof tmp, "answer:%d:%s", 42, "newlib") == 16,
          "snprintf length");
    check(strcmp(tmp, "answer:42:newlib") == 0, "snprintf content");
    printf("PLAN2001-NEWLIB-FULL-PRINTF-PASS %s\n", tmp);
    check(write(1, "", 0) == 0, "public write forwards to native adapter");
    fflush(stdout);

    p = (char *)malloc(8192);
    check(p != NULL, "Newlib malloc");
    memset(p, 0x5c, 8192);
    check(p[0] == 0x5c && p[8191] == 0x5c, "Newlib heap data");
    free(p);
    errno = 0;
    value = strtol("12345", &endp, 10);
    check(value == 12345 && *endp == 0 && errno == 0, "strtol");
    printf("PLAN2001-NEWLIB-FULL-MALLOC-PASS\n");
    fflush(stdout);

    v = sin(theta);
    check(v > 0.479 && v < 0.480, "Newlib libm sin");
    printf("PLAN2001-NEWLIB-FULL-LIBM-PASS\n");
    fflush(stdout);

    check(gettimeofday(&before, NULL) == 0, "native gettimeofday start");
    check(sleep(1) == 0, "POSIX sleep(1) must succeed");
    check(gettimeofday(&after, NULL) == 0, "native gettimeofday end");
    elapsed_us = ((long long)after.tv_sec - (long long)before.tv_sec)*1000000LL +
        ((long long)after.tv_usec - (long long)before.tv_usec);
    check(elapsed_us >= 500000LL, "sleep(1) must not mean 1 millisecond");
    check(elapsed_us < 20000000LL, "sleep(1) unexpectedly too long");
    printf("PLAN2001-NEWLIB-FULL-TIME-PASS\n");
    fflush(stdout);

    check(_fstat(1, &st) == 0 && S_ISCHR(st.st_mode), "native stdout fstat");
    errno = 0;
    check(_open("/plan2001-not-ambient", 0) == -1 && errno == ENOSYS,
          "unsupported pathname open is ENOSYS");
    printf("PLAN2001-NEWLIB-FULL-ERRNO-PASS\n");
    fflush(stdout);

    region = p9_full_region_create(OBJ_RING_BYTES);
    check(region > 0, "create native input region");
    write_map = p9_full_region_map((int)region, 0);
    check(write_map > 0, "map writable native input ring");
    hdr = (ObjRingHeader *)(unsigned long)write_map;
    memset(hdr, 0, sizeof *hdr);
    hdr->version = OBJ_RING_VERSION;
    hdr->magic = OBJ_RING_MAGIC;
    hdr->slot_size = OBJ_RING_SLOT;
    hdr->slots = OBJ_RING_SLOTS;
    slot = (ObjRingSlot *)(unsigned long)(write_map + OBJ_RING_HEADER);
    __atomic_store_n(&slot->seq, 1, __ATOMIC_RELAXED);
    slot->length = sizeof(source)-1;
    slot->flags = 0;
    memcpy((char *)(slot + 1), source, sizeof(source)-1);
    __atomic_store_n(&slot->seq, 2, __ATOMIC_RELEASE);
    __atomic_store_n(&hdr->write_seq, 1, __ATOMIC_RELEASE);
    check(p9_full_bind_stdin((int)region) == 0, "bind stdin capability");
    check(read(0, tmp, 0) == 0, "public read forwards to native adapter");
    check(fgets(tmp, sizeof tmp, stdin) != NULL, "Newlib fgets from region");
    check(strcmp(tmp, source) == 0, "Newlib stdin content");
    p9_full_unbind_stdin();
    check(p9_full_region_unmap((uintptr_t)write_map) == 0,
          "unmap writer region");
    check(p9_full_region_close((int)region) == 0, "close owned region");
    printf("PLAN2001-NEWLIB-FULL-STDIN-PASS\n");
    fflush(stdout);

    check_descriptors();
    check_uxpipe();
    check_uxfiles();
    printf("PLAN2001-NEWLIB-FULL-PASS\n");
    fflush(stdout);
    for(;;)
        (void)sleep(1);
}

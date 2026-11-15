/* Bounded Unix process projection; native Handles remain the authority. */
#ifndef PLAN2001_NEWLIB_PROCESS_H
#define PLAN2001_NEWLIB_PROCESS_H
#include <stdint.h>
#include <sys/types.h>
#define P9_GRANTS 1024
#define P9_FDS 19
#define P9_EXECUTABLES 4
#define P9_CHILDREN 8
#define P9_ENV_BYTES 4096
#define P9_CONTEXT_MAGIC UINT64_C(0x3158544358553950)
typedef struct { int handle; uint64_t rights; } P9Grant;
typedef struct { unsigned count; P9Grant grants[P9_GRANTS]; } P9Plan;
typedef struct {
    int object, flags;
} P9FdEntry;
typedef struct {
    int kind, flags, first, second, backend, source;
} P9FdObject;
typedef struct {
    P9FdEntry fds[P9_FDS];
    P9FdObject objects[P9_FDS];
    unsigned count;
    int files;
    char cwd[256];
} P9FdImage;
typedef struct { char path[256]; int volume; uint64_t oid; } P9Executable;
typedef struct { int pid, handle, mailbox, reported, status, stop_seen, stop_notified; } P9Child;
typedef struct {
    uint64_t magic, version, pending, blocked, ignored, parent_pending;
    uint64_t alarm_deadline;
    int pid, ppid, ready, exec;
    unsigned stop_count, stop_signal;
    P9FdImage fds;
    P9Executable executables[P9_EXECUTABLES];
    P9Child children[P9_CHILDREN];
    int grants, self, parent;
    char environment[P9_ENV_BYTES];
    unsigned environment_size;
} P9Context;
int p9_plan_add(P9Plan *,int,uint64_t);
int p9_plan_resolve(int *,unsigned);
int p9_fd_export(P9FdImage *,P9Plan *,int);
int p9_fd_import(const P9FdImage *,const int *,unsigned);
void p9_fd_export_undo(const P9FdImage *);
int p9_file_export(P9Plan *);
int p9_file_import(int);
int p9_file_ref(int,int);
void p9_file_refresh(void);
void p9_file_detach(void);
int p9_process_startup(void);
int p9_process_bind_executable(const char *,int,uint64_t);
/* Generated immutable executable projection; names only select an existing
 * delegated registration. These hooks are absent from legacy profiles. */
extern const char *p9_multicall_resolve(const char *) __attribute__((weak));
extern const char *p9_multicall_path(unsigned) __attribute__((weak));
int p9_process_executable(const char *,uint64_t *);
void p9_signal_checkpoint(void);
void p9_process_exit(int);
void p9_process_signal_exit(int);
int p9_process_interrupted(void);
unsigned p9_process_tick(void);
int p9_process_has_interrupt(void);
void p9_process_enter(void);
void p9_process_leave(void);
/* Native helper operations, deliberately separate from Unix symbols. */
long p9_process_call(int,unsigned,void *,unsigned long long,long long);
#endif

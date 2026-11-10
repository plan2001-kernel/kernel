/*
 * Plan 9 compatibility module for the process, timing and
 * error operations.  The inherited pathname open/create/remove, mount,
 * stat/dirread, exec and console helpers are gone; the native GE/O
 * object interface is the space module (docs/O-FORMAT.md).
 * A failure raises OSError with the error string.
 */
#include <string.h>
#include "py/runtime.h"
#include "py/objstr.h"
#include "py/objtuple.h"
#ifndef NO_QSTR
#include "../runtime/abi/vobj.h"
#include "../runtime/abi/obj.h"
#include "../runtime/abi/sys.h"
#include "../runtime/abi/sysctl.h"
#include "../runtime/abi/ioctl.h"
#include <envapi.h>
#include <uartapi.h>
#endif

// p9space.c: a rest before a call blocks (a Space's commit point 3)
void p9_space_rest(void);

extern int sleep(long);
extern long long nsec(void);

static void raise_err(void) {
    char buf[ERRMAX];

    // a call the interrupt note cut short: KeyboardInterrupt
    mp_handle_pending(true);
    rerrstr(buf, sizeof buf);
    mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("%s"), buf);
}

static mp_obj_t check(long r) {
    if (r < 0) {
        raise_err();
    }
    return mp_obj_new_int(r);
}

static const char *str(mp_obj_t o) {
    return mp_obj_str_get_str(o);
}

// alarm(ms) -> the ms the one before had left; at its end the note
// "alarm" cuts a blocked call short (OSError, interrupted); 0 cancels
static mp_obj_t p9_alarm(mp_obj_t ms) {
    return mp_obj_new_int(alarm(mp_obj_get_int(ms)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_alarm_obj, p9_alarm);

// wait() -> (pid, status): status '' if the child succeeded
static mp_obj_t p9_wait(void) {
    Waitmsg *w;
    mp_obj_t t[2];

    p9_space_rest();
    if ((w = wait()) == nil) {
        raise_err();
    }
    t[0] = mp_obj_new_int(w->pid);
    t[1] = mp_obj_new_str(w->msg, strlen(w->msg));
    free(w);
    return mp_obj_new_tuple(2, t);
}
static MP_DEFINE_CONST_FUN_OBJ_0(p9_wait_obj, p9_wait);

// rfork(flags) -> pid: name space, environment, note group and the
// like; with RFPROC the child runs on in Python (a copy of everything)
static mp_obj_t p9_rfork(mp_obj_t flags) {
    return check(rfork(mp_obj_get_int(flags)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_rfork_obj, p9_rfork);

/*
 * rforkex(flags, handles=(), version=None, count=None) -> (pid, child handles)
 *
 * Each handles entry is (handle, rights) or (handle, rights, dstslot); a bare
 * handle means "copy its rights to the lowest free child slot".  The optional
 * version/count arguments exist only so a test can present a malformed
 * options record; normal callers omit them.  In the child PID is 0 and the
 * mapping tuple is empty (the parent learns the assigned numbers).
 */
static mp_obj_t p9_rforkex(size_t n, const mp_obj_t *a) {
    struct Rforkex req;
    unsigned long long map[RFORKEX_MAXHANDLES];
    mp_obj_t it, item;
    size_t have, want, i;
    long r;

    if(n < 1 || n > 4)
        mp_raise_TypeError(MP_ERROR_TEXT("rforkex: 1 to 4 arguments"));
    memset(&req, 0, sizeof req);
    memset(map, 0, sizeof map);
    req.version = RFORKEX_VERSION;
    have = 0;
    if(n > 1 && a[1] != mp_const_none) {
        it = mp_getiter(a[1], NULL);
        while((item = mp_iternext(it)) != MP_OBJ_STOP_ITERATION) {
            unsigned long long handle;
            unsigned long long rights = 0;
            unsigned long long dstslot = 0;

            if(mp_obj_is_int(item)) {
                handle = (unsigned long long)mp_obj_get_int(item);
            } else {
                size_t plen; mp_obj_t *pair;
                mp_obj_get_array(item, &plen, &pair);
                if(plen != 2 && plen != 3)
                    mp_raise_ValueError(MP_ERROR_TEXT("rforkex: expected handle or (handle, rights[, dstslot])"));
                handle = (unsigned long long)mp_obj_get_int(pair[0]);
                rights = (unsigned long long)mp_obj_get_int(pair[1]);
                if(plen == 3)
                    dstslot = (unsigned long long)mp_obj_get_int(pair[2]);
            }
            if(have < RFORKEX_MAXHANDLES) {
                req.h[have].handle = handle;
                req.h[have].rights = rights;
                req.h[have].dstslot = dstslot;
            }
            have++;
        }
    }
    if(n > 2 && a[2] != mp_const_none)
        req.version = (unsigned int)mp_obj_get_int(a[2]);
    req.count = (unsigned int)have;
    if(n > 3 && a[3] != mp_const_none)
        req.count = (unsigned int)mp_obj_get_int(a[3]);
    r = rforkex((unsigned long)mp_obj_get_int(a[0]), &req, map);
    if(r < 0)
        raise_err();
    if(r == 0)
        return mp_obj_new_tuple(2, (mp_obj_t[]){ mp_obj_new_int(0),
            mp_obj_new_tuple(0, NULL) });
    {
        mp_obj_t child[RFORKEX_MAXHANDLES];
        want = have < RFORKEX_MAXHANDLES ? have : RFORKEX_MAXHANDLES;
        for(i = 0; i < want; i++)
            child[i] = mp_obj_new_int_from_ull(map[i]);
        mp_obj_t t[2] = { mp_obj_new_int(r),
            mp_obj_new_tuple(want, child) };
        return mp_obj_new_tuple(2, t);
    }
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_rforkex_obj, 1, 4, p9_rforkex);

/* Existing ENVGET syscall, bounded native bytes (no pathname getenv). */
static mp_obj_t p9_envget(mp_obj_t name) {
    const char *key=mp_obj_str_get_str(name);
    char data[128];long n=0;
    if(envget(strlen(key),(char*)key,data,sizeof data,&n)<0)raise_err();
    return mp_obj_new_bytes((const byte*)data,n);
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_envget_obj,p9_envget);

static mp_obj_t p9_envset(mp_obj_t name, mp_obj_t value) {
    const char *key=mp_obj_str_get_str(name);
    mp_buffer_info_t b;
    mp_get_buffer_raise(value,&b,MP_BUFFER_READ);
    return check(envset(strlen(key),(char*)key,b.buf,b.len));
}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_envset_obj,p9_envset);

static mp_obj_t p9_envdelete(mp_obj_t name) {
    const char *key=mp_obj_str_get_str(name);
    return check(envdelete(strlen(key),(char*)key));
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_envdelete_obj,p9_envdelete);

static mp_obj_t p9_envlist(void) {
    long need=0,r;vstr_t v;
    if(envlist(nil,0,&need)<0)raise_err();
    vstr_init_len(&v,need>0?need:1);
    r=envlist(v.buf,need,&need);
    if(r<0){vstr_clear(&v);raise_err();}
    v.len=need;
    return mp_obj_new_bytes_from_vstr(&v);
}
static MP_DEFINE_CONST_FUN_OBJ_0(p9_envlist_obj,p9_envlist);

static mp_obj_t p9_procinfo(void) {
    SysctlProcInfo i;mp_obj_t a[3];
    if(proc_info(&i)<0)raise_err();
    a[0]=mp_obj_new_int_from_ll(i.pid);
    a[1]=mp_obj_new_int_from_ull(i.cpu);
    a[2]=mp_obj_new_int_from_ull(i.membudget);
    return mp_obj_new_tuple(3,a);
}
static MP_DEFINE_CONST_FUN_OBJ_0(p9_procinfo_obj,p9_procinfo);

/* Explicit short-buffer probe: returns (total, copied, handles). */
static mp_obj_t p9_objlistcap(mp_obj_t capo) {
    int cap=mp_obj_get_int(capo);VobjListRec *rec;unsigned long long copied=0;
    long total,i;mp_obj_t *t,result;
    if(cap<0||cap>96)mp_raise_ValueError(MP_ERROR_TEXT("handle list capacity"));
    rec=m_new(VobjListRec,cap?cap:1);
    total=obj_list_count(rec,cap,&copied);
    if(total<0){m_del(VobjListRec,rec,cap?cap:1);raise_err();}
    t=m_new(mp_obj_t,copied?copied:1);
    for(i=0;i<(long)copied;i++)t[i]=mp_obj_new_int((mp_int_t)rec[i].handle);
    result=mp_obj_new_tuple(3,(mp_obj_t[3]){mp_obj_new_int(total),mp_obj_new_int_from_ull(copied),mp_obj_new_tuple(copied,t)});
    m_del(mp_obj_t,t,copied?copied:1);
    m_del(VobjListRec,rec,cap?cap:1);
    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_objlistcap_obj,p9_objlistcap);

static mp_obj_t p9_sleep(mp_obj_t ms) {
    long n=mp_obj_get_int(ms);
    p9_space_rest();
    check(sleep(n));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_sleep_obj, p9_sleep);

// time_ns(): the direct 64-bit _NSEC(53) todget(nil,nil) source.
static mp_obj_t p9_time_ns(void) {
    return mp_obj_new_int_from_ull((unsigned long long)nsec());
}
static MP_DEFINE_CONST_FUN_OBJ_0(p9_time_ns_obj, p9_time_ns);

/*
 * Diagnostic access to the same libc errstr()/rerrstr()/werrstr() functions
 * used by the error adapters.  They let a scripted REPL test observe the
 * two-swap read protocol and a formatted werrstr() without adding a new
 * error mechanism.  errstr_exchange() is the raw IN/OUT call: it copies the
 * supplied bytes into the previous error area and returns the current text.
 */
static mp_obj_t p9_errstr_get(void) {
    char buf[ERRMAX];
    rerrstr(buf, sizeof buf);
    return mp_obj_new_str(buf, strlen(buf));
}
static MP_DEFINE_CONST_FUN_OBJ_0(p9_errstr_get_obj, p9_errstr_get);

static mp_obj_t p9_werrstr(mp_obj_t fmt, mp_obj_t arg) {
    werrstr(mp_obj_str_get_str(fmt), mp_obj_str_get_str(arg));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_werrstr_obj, p9_werrstr);

static mp_obj_t p9_errstr_exchange(mp_obj_t data, mp_obj_t n) {
    mp_buffer_info_t b;
    char buf[ERRMAX];
    uint len = (uint)mp_obj_get_int(n);

    if(len == 0 || len > ERRMAX)
        mp_raise_ValueError(MP_ERROR_TEXT("errstr buffer length"));
    memset(buf, 0, sizeof buf);
    mp_get_buffer_raise(data, &b, MP_BUFFER_READ);
    if(b.len >= len)
        b.len = len - 1;
    memmove(buf, b.buf, b.len);
    if(errstr(buf, len) < 0)
        raise_err();
    return mp_obj_new_bytes((const byte*)buf, strlen(buf));
}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_errstr_exchange_obj, p9_errstr_exchange);

static mp_obj_t p9_getpid(void) {
    return mp_obj_new_int(getpid());
}
static MP_DEFINE_CONST_FUN_OBJ_0(p9_getpid_obj, p9_getpid);

// exits(msg=''): the process ends with that status
static mp_obj_t p9_exits(size_t n, const mp_obj_t *a) {
    const char *m;

    m = n > 0 ? str(a[0]) : "";
    exits(*m ? (char *)m : nil);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_exits_obj, 0, 1, p9_exits);

/* Historical Pipe method names are userland adapters over the canonical
 * OBJCREATE/INVOKE/CLOSE paths; the retired numbers have no dispatch entry. */
static mp_obj_t p9_pipenew(void) {
    ObjId id; long a,b; mp_obj_t t[2];
    a=obj_pipe_create(&id);
    if(a<0)raise_err();
    b=obj_pipe_peer((int)a);
    if(b<0)raise_err();
    t[0]=mp_obj_new_int((mp_int_t)a); t[1]=mp_obj_new_int((mp_int_t)b);
    return mp_obj_new_tuple(2,t);
}
static MP_DEFINE_CONST_FUN_OBJ_0(p9_pipenew_obj,p9_pipenew);
static mp_obj_t p9_piperead(mp_obj_t h,mp_obj_t count) {
    long n=mp_obj_get_int(count),r; vstr_t v;
    if(n<0 || n>65536) mp_raise_ValueError(MP_ERROR_TEXT("Pipe read limit"));
    vstr_init_len(&v,n);
    r=obj_read(mp_obj_get_int(h),v.buf,(unsigned long long)n,0);
    if(r<0){vstr_clear(&v);raise_err();}
    v.len=r;return mp_obj_new_bytes_from_vstr(&v);
}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_piperead_obj,p9_piperead);
static mp_obj_t p9_pipewrite(mp_obj_t h,mp_obj_t data) {
    mp_buffer_info_t b;mp_get_buffer_raise(data,&b,MP_BUFFER_READ);
    return check(obj_write(mp_obj_get_int(h),b.buf,(unsigned long long)b.len,0));
}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_pipewrite_obj,p9_pipewrite);
static mp_obj_t p9_pipeclose(mp_obj_t h) {
    check(obj_close(mp_obj_get_int(h)));return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_pipeclose_obj,p9_pipeclose);
/* Process creation declares Handle inheritance in the creation call itself
 * (RFORKEX or the explicit OSPAWN options); there is no pipeinherit side
 * channel that could arm a later fork. */
/* The TCP6/UDP6 object interface and its Python adapters were removed.  The
 * supported network boundary is raw device frames/ObjectFIFO with userspace
 * protocol handling. */

/* Historical Python object/object-FIFO method names.  They are compatibility
 * adapters over the canonical five-operation OBJ* ABI; the retired VOBJ*
 * kernel numbers have no dispatch entry. */
static mp_obj_t p9_vobjcreate(size_t n,const mp_obj_t *a){
 mp_buffer_info_t b;long r;unsigned long long space=0;
 mp_get_buffer_raise(a[0],&b,MP_BUFFER_READ);
 if(n>1)space=(unsigned long long)mp_obj_get_int(a[1]);
 r=obj_create(space,ObjKindBytes,b.buf,(unsigned long long)b.len,NULL);
 if(r<0)raise_err();
 return mp_obj_new_int((mp_int_t)r);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_vobjcreate_obj,1,2,p9_vobjcreate);

static mp_obj_t p9_vobjread(mp_obj_t handle){
 long need=obj_read(mp_obj_get_int(handle),NULL,0,0),r;vstr_t v;
 if(need<0)raise_err();
 vstr_init_len(&v,need);
 if(need>0){r=obj_read(mp_obj_get_int(handle),v.buf,(unsigned long long)need,0);if(r<0){vstr_clear(&v);raise_err();}v.len=r;}
 return mp_obj_new_bytes_from_vstr(&v);
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_vobjread_obj,p9_vobjread);

/* Generic metadata comes from ObjOpInfo; FIFO/subscription counters come
 * from the separate ObjOpStat contract.  This adapter keeps the historical
 * 16-tuple view for existing scripts. */
static mp_obj_t p9_infotuple(int handle){
 VobjInfo i;VobjFifostat f;
 if(obj_info(handle,&i)<0)raise_err();
 memset(&f,0,sizeof f);
 if(i.kind==VkindFifo)obj_stat(handle,&f);
 mp_obj_t a[16]={mp_obj_new_int_from_ull(i.incarnation),mp_obj_new_int_from_ull(i.space),
  mp_obj_new_int_from_ull(i.oid),mp_obj_new_int_from_ull(i.size),mp_obj_new_int_from_ull(i.holds),
  mp_obj_new_int_from_ull(f.subscribers),mp_obj_new_int_from_ull(f.sends),mp_obj_new_int_from_ull(f.drops),
  mp_obj_new_int(i.kind),mp_obj_new_int(i.flags),mp_obj_new_int(i.rights),
  mp_obj_new_int(f.capacity),mp_obj_new_int(f.queued),mp_obj_new_int(f.sub_dropped),
  mp_obj_new_int(f.fifo_closed),mp_obj_new_int(f.sub_closed)};
 return mp_obj_new_tuple(16,a);
}

static mp_obj_t p9_vobjinfo(mp_obj_t handle){
 return p9_infotuple(mp_obj_get_int(handle));
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_vobjinfo_obj,p9_vobjinfo);

static mp_obj_t p9_vobjclose(mp_obj_t h){check(obj_close(mp_obj_get_int(h)));return mp_const_none;}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_vobjclose_obj,p9_vobjclose);

static mp_obj_t p9_vobjfifocreate(size_t n,const mp_obj_t *a){
 long cap=n>0?mp_obj_get_int(a[0]):64;
 return check(obj_create(0,ObjKindFifo,NULL,(unsigned long long)cap,NULL));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_vobjfifocreate_obj,0,1,p9_vobjfifocreate);

static mp_obj_t p9_vobjfifosub(size_t n,const mp_obj_t *a){
 long cap=n>2?mp_obj_get_int(a[2]):0;
 unsigned long long rights=n>1?(unsigned long long)mp_obj_get_int(a[1]):(Vrecv|Vmeta);
 return check(obj_subscribe(mp_obj_get_int(a[0]),rights,(unsigned long long)cap));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_vobjfifosub_obj,1,3,p9_vobjfifosub);

static mp_obj_t p9_vobjfifosend(size_t n,const mp_obj_t *a){
 mp_buffer_info_t b;b.buf=NULL;b.len=0;
 if(n>2)mp_get_buffer_raise(a[2],&b,MP_BUFFER_READ);
 check(obj_publish(mp_obj_get_int(a[0]),mp_obj_get_int(a[1]),b.buf,(unsigned long long)b.len));
 return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_vobjfifosend_obj,2,3,p9_vobjfifosend);

static mp_obj_t p9_vobjfiforecv(size_t n,const mp_obj_t *a){
 int timeout=n>1?mp_obj_get_int(a[1]):-1,envcap=n>2?mp_obj_get_int(a[2]):64;
 long got=0,r;vstr_t v;mp_obj_t t[2];
 if(envcap<0||envcap>64)mp_raise_ValueError(MP_ERROR_TEXT("vobj envelope limit"));
 vstr_init_len(&v,envcap);
 r=obj_receive(mp_obj_get_int(a[0]),envcap?v.buf:NULL,(unsigned long long)envcap,(unsigned long long)timeout,(unsigned long long*)&got);
 if(r<0){vstr_clear(&v);raise_err();}
 if(got<v.len)v.len=got;else if(got>0&&got<=envcap)v.len=got;
 t[0]=mp_obj_new_int((mp_int_t)r);t[1]=mp_obj_new_bytes_from_vstr(&v);
 return mp_obj_new_tuple(2,t);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_vobjfiforecv_obj,1,3,p9_vobjfiforecv);

static mp_obj_t p9_vobjfifostat(mp_obj_t h){
 VobjFifostat s;if(obj_stat(mp_obj_get_int(h),&s)<0)raise_err();
 mp_obj_t a[7]={mp_obj_new_int_from_ull(s.sends),mp_obj_new_int_from_ull(s.drops),
  mp_obj_new_int(s.subscribers),mp_obj_new_int(s.queued),mp_obj_new_int(s.capacity),
  mp_obj_new_int(s.fifo_closed),mp_obj_new_int(s.sub_closed)};
 return mp_obj_new_tuple(7,a);
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_vobjfifostat_obj,p9_vobjfifostat);

static mp_obj_t p9_vobjgrant(mp_obj_t h,mp_obj_t rights){
 return check(obj_grant(mp_obj_get_int(h),(unsigned long long)mp_obj_get_int(rights)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_vobjgrant_obj,p9_vobjgrant);

static mp_obj_t p9_vobjrevoke(mp_obj_t h){
 check(obj_revoke(mp_obj_get_int(h)));return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_vobjrevoke_obj,p9_vobjrevoke);

/* Generic object I/O: vobjio(handle, op, size_or_data, timeout=0). */
static mp_obj_t p9_vobjio(size_t n,const mp_obj_t *a){
 int h=mp_obj_get_int(a[0]),op=mp_obj_get_int(a[1]);
 long timeout=n>3?mp_obj_get_int(a[3]):0,r;
 if(op==VioRead){
  long sz=mp_obj_get_int(a[2]);vstr_t v;
  if(sz<0)mp_raise_ValueError(MP_ERROR_TEXT("vobj read size"));
  vstr_init_len(&v,sz);
  r=obj_read(h,sz?v.buf:NULL,(unsigned long long)sz,(unsigned long long)timeout);
  if(r<0){vstr_clear(&v);raise_err();}
  v.len=r;return mp_obj_new_bytes_from_vstr(&v);
 }
 if(op==VioWrite){
  mp_buffer_info_t b;mp_get_buffer_raise(a[2],&b,MP_BUFFER_READ);
  r=obj_write(h,b.buf,(unsigned long long)b.len,(unsigned long long)timeout);
  return check(r);
 }
 mp_raise_ValueError(MP_ERROR_TEXT("vobj op"));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_vobjio_obj,3,4,p9_vobjio);

/* Enumerate this process's handles in slot order (used by an OSPAWN child). */
static mp_obj_t p9_vobjlist(void){
 VobjListRec rec[32];long n,i;mp_obj_t t[32];
 n=obj_list(rec,32);
 if(n<0)raise_err();
 if(n>32)n=32;
 for(i=0;i<n;i++)t[i]=mp_obj_new_int((mp_int_t)rec[i].handle);
 return mp_obj_new_tuple(n,t);
}
static MP_DEFINE_CONST_FUN_OBJ_0(p9_vobjlist_obj,p9_vobjlist);

/* Universal five-operation object ABI (runtime/abi/obj.h).  These are the
 * critical test bindings: they call libc obj_* wrappers over CREATE, OPEN,
 * CLOSE, BIND and INVOKE, not the legacy VOBJ* or TCP6* families. */
static mp_obj_t p9_objcreate(size_t n,const mp_obj_t *a){
 mp_buffer_info_t b;b.buf=NULL;b.len=0;
 unsigned long long space=0;
 if(n>0&&a[0]!=mp_const_none)mp_get_buffer_raise(a[0],&b,MP_BUFFER_READ);
 if(n>1)space=(unsigned long long)mp_obj_get_int(a[1]);
 return check(obj_create(space,ObjKindBytes,b.buf,(unsigned long long)b.len,NULL));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_objcreate_obj,0,2,p9_objcreate);

static mp_obj_t p9_objfifocreate(size_t n,const mp_obj_t *a){
 long cap=n>0?mp_obj_get_int(a[0]):64;
 return check(obj_create(0,ObjKindFifo,NULL,(unsigned long long)cap,NULL));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_objfifocreate_obj,0,1,p9_objfifocreate);

static mp_obj_t p9_objclose(mp_obj_t h){
 check(obj_close(mp_obj_get_int(h)));return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_objclose_obj,p9_objclose);

static mp_obj_t p9_objopenhandle(size_t n,const mp_obj_t *a){
 ObjOpenHandleReq req;unsigned long long rights=n>1?(unsigned long long)mp_obj_get_int(a[1]):0;
 memset(&req,0,sizeof req);req.handle=mp_obj_get_int(a[0]);
 return check(obj_open(ObjOpenHandle,&req,sizeof req,rights,NULL));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_objopenhandle_obj,1,2,p9_objopenhandle);

static mp_obj_t p9_objopenbinding(size_t n,const mp_obj_t *a){
 const char *role=mp_obj_str_get_str(a[0]);
 unsigned long long rights=n>1?(unsigned long long)mp_obj_get_int(a[1]):0;
 return check(obj_open(ObjOpenBinding,(void*)role,strlen(role),rights,NULL));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_objopenbinding_obj,1,2,p9_objopenbinding);

/* Persistent GE/O through the five-operation ABI only. */
static mp_obj_t p9_objopenspace(mp_obj_t name){
 const char *n=mp_obj_str_get_str(name);
 return check(obj_open(ObjOpenSpace,(void*)n,strlen(n),0,NULL));
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_objopenspace_obj,p9_objopenspace);

static mp_obj_t p9_objopennamespacecurrent(void){
 return check(obj_open(ObjOpenNamespaceCurrent,nil,0,0,NULL));
}
static MP_DEFINE_CONST_FUN_OBJ_0(p9_objopennamespacecurrent_obj,p9_objopennamespacecurrent);

static mp_obj_t p9_objcreatege(size_t n,const mp_obj_t *a){
 mp_buffer_info_t b;ObjId id;long h;
 b.buf=NULL;b.len=0;
 if(n>1)mp_get_buffer_raise(a[1],&b,MP_BUFFER_READ);
 h=obj_create((unsigned long long)mp_obj_get_int(a[0]),ObjKindGe,b.buf,
  (unsigned long long)b.len,&id);
 if(h<0)raise_err();
 mp_obj_t t[2]={mp_obj_new_int((mp_int_t)h),mp_obj_new_int_from_ull(id.oid)};
 return mp_obj_new_tuple(2,t);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_objcreatege_obj,1,2,p9_objcreatege);

static mp_obj_t p9_objopengeospace(mp_obj_t sh,mp_obj_t oid){
 ObjOpenGeSpaceReq req;
 memset(&req,0,sizeof req);
 req.space=mp_obj_get_int(sh);
 req.oid=(unsigned long long)mp_obj_get_uint(oid);
 return check(obj_open(ObjOpenGeSpace,&req,sizeof req,0,NULL));
}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_objopengeospace_obj,p9_objopengeospace);

static mp_obj_t p9_objcommit(mp_obj_t h){
 ObjCall c;
 memset(&c,0,sizeof c);c.version=OBJ_ABI_VERSION;c.op=ObjOpCommit;
 check(obj_invoke(mp_obj_get_int(h),&c,sizeof c));
 return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_objcommit_obj,p9_objcommit);

static mp_obj_t p9_objcommitversion(mp_obj_t h){
 ObjCall c;unsigned long long v=0;
 memset(&c,0,sizeof c);c.version=OBJ_ABI_VERSION;c.op=ObjOpCommit;
 c.out=&v;c.outlen=sizeof v;
 check(obj_invoke(mp_obj_get_int(h),&c,sizeof c));
 return mp_obj_new_int_from_ull(v);
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_objcommitversion_obj,p9_objcommitversion);

static mp_obj_t p9_objbind(size_t n,const mp_obj_t *a){
 const char *role=mp_obj_str_get_str(a[1]);
 unsigned long long rights=n>2?(unsigned long long)mp_obj_get_int(a[2]):0;
 unsigned int mode=n>3?(unsigned int)mp_obj_get_int(a[3]):0;
 check(obj_bind(mp_obj_get_int(a[0]),role,strlen(role),rights,mode));
 return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_objbind_obj,2,4,p9_objbind);

static mp_obj_t p9_objread(size_t n,const mp_obj_t *a){
 long sz=mp_obj_get_int(a[1]);
 long timeout=n>2?mp_obj_get_int(a[2]):0;
 long r;vstr_t v;
 if(sz<0)mp_raise_ValueError(MP_ERROR_TEXT("obj read size"));
 /* A zero-length read is empty, not a size query: use obj_info() for size
  * or the vobjread compatibility adapter, which does the libc/kernel
  * obj_read(h, NULL, 0) size query. */
 if(sz==0)return mp_const_empty_bytes;
 vstr_init_len(&v,sz);
 r=obj_read(mp_obj_get_int(a[0]),v.buf,(unsigned long long)sz,(unsigned long long)timeout);
 if(r<0){vstr_clear(&v);raise_err();}
 v.len=r;return mp_obj_new_bytes_from_vstr(&v);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_objread_obj,2,3,p9_objread);

/* obj_read_version(h, size, timeout=0) -> (bytes, version).  The version is
 * the published O version a persistent GE READ returned; it is zero for
 * other providers. */
static mp_obj_t p9_objreadver(size_t n,const mp_obj_t *a){
 long sz=mp_obj_get_int(a[1]);
 long timeout=n>2?mp_obj_get_int(a[2]):0;
 long r;unsigned long long ver=0;vstr_t v;mp_obj_t t[2];
 if(sz<0)mp_raise_ValueError(MP_ERROR_TEXT("obj read size"));
 if(sz==0){t[0]=mp_const_empty_bytes;t[1]=mp_obj_new_int_from_ull(0);return mp_obj_new_tuple(2,t);}
 vstr_init_len(&v,sz);
 r=obj_read_version(mp_obj_get_int(a[0]),v.buf,(unsigned long long)sz,(unsigned long long)timeout,&ver);
 if(r<0){vstr_clear(&v);raise_err();}
 v.len=r;t[0]=mp_obj_new_bytes_from_vstr(&v);t[1]=mp_obj_new_int_from_ull(ver);
 return mp_obj_new_tuple(2,t);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_objreadver_obj,2,3,p9_objreadver);

static mp_obj_t p9_objwrite(size_t n,const mp_obj_t *a){
 mp_buffer_info_t b;long timeout=n>2?mp_obj_get_int(a[2]):0;
 mp_get_buffer_raise(a[1],&b,MP_BUFFER_READ);
 return check(obj_write(mp_obj_get_int(a[0]),b.buf,(unsigned long long)b.len,(unsigned long long)timeout));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_objwrite_obj,2,3,p9_objwrite);

static mp_obj_t p9_objinfo(mp_obj_t h){
 return p9_infotuple(mp_obj_get_int(h));
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_objinfo_obj,p9_objinfo);

static mp_obj_t p9_objstat(mp_obj_t h){
 VobjFifostat s;if(obj_stat(mp_obj_get_int(h),&s)<0)raise_err();
 mp_obj_t a[7]={mp_obj_new_int_from_ull(s.sends),mp_obj_new_int_from_ull(s.drops),
  mp_obj_new_int(s.subscribers),mp_obj_new_int(s.queued),mp_obj_new_int(s.capacity),
  mp_obj_new_int(s.fifo_closed),mp_obj_new_int(s.sub_closed)};
 return mp_obj_new_tuple(7,a);
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_objstat_obj,p9_objstat);

static mp_obj_t p9_objsubscribe(size_t n,const mp_obj_t *a){
 unsigned long long rights=n>1?(unsigned long long)mp_obj_get_int(a[1]):(Vrecv|Vmeta);
 unsigned long long cap=n>2?(unsigned long long)mp_obj_get_int(a[2]):0;
 return check(obj_subscribe(mp_obj_get_int(a[0]),rights,cap));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_objsubscribe_obj,1,3,p9_objsubscribe);

static mp_obj_t p9_objpublish(size_t n,const mp_obj_t *a){
 mp_buffer_info_t b;b.buf=NULL;b.len=0;
 if(n>2)mp_get_buffer_raise(a[2],&b,MP_BUFFER_READ);
 check(obj_publish(mp_obj_get_int(a[0]),mp_obj_get_int(a[1]),b.buf,(unsigned long long)b.len));
 return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_objpublish_obj,2,3,p9_objpublish);

static mp_obj_t p9_objreceive(size_t n,const mp_obj_t *a){
 int timeout=n>1?mp_obj_get_int(a[1]):-1,envcap=n>2?mp_obj_get_int(a[2]):64;
 unsigned long long got=0;long r;vstr_t v;mp_obj_t t[2];
 if(envcap<0||envcap>64)mp_raise_ValueError(MP_ERROR_TEXT("obj envelope limit"));
 vstr_init_len(&v,envcap);
 r=obj_receive(mp_obj_get_int(a[0]),envcap?v.buf:NULL,(unsigned long long)envcap,
  (unsigned long long)timeout,&got);
 if(r<0){vstr_clear(&v);raise_err();}
 if(got<v.len)v.len=got;else if(got>0&&got<=(unsigned long long)envcap)v.len=got;
 t[0]=mp_obj_new_int((mp_int_t)r);t[1]=mp_obj_new_bytes_from_vstr(&v);
 return mp_obj_new_tuple(2,t);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_objreceive_obj,1,3,p9_objreceive);

static mp_obj_t p9_objgrant(mp_obj_t h,mp_obj_t rights){
 return check(obj_grant(mp_obj_get_int(h),(unsigned long long)mp_obj_get_int(rights)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_objgrant_obj,p9_objgrant);

/* Capability revocation only.  A FIFO is a transport, not a revoked grant;
 * use obj_fifo_shutdown() for FIFO shutdown. */
static mp_obj_t p9_objrevoke(mp_obj_t h){
 check(obj_revoke(mp_obj_get_int(h)));return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_objrevoke_obj,p9_objrevoke);

static mp_obj_t p9_objfifoshutdown(mp_obj_t h){
 check(obj_fifo_shutdown(mp_obj_get_int(h)));return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_objfifoshutdown_obj,p9_objfifoshutdown);

static mp_obj_t p9_objlist(void){
 VobjListRec rec[32];long n,i;mp_obj_t t[32];
 n=obj_list(rec,32);
 if(n<0)raise_err();
 if(n>32)n=32;
 for(i=0;i<n;i++)t[i]=mp_obj_new_int((mp_int_t)rec[i].handle);
 return mp_obj_new_tuple(n,t);
}
static MP_DEFINE_CONST_FUN_OBJ_0(p9_objlist_obj,p9_objlist);

/* Raw INVOKE for negative/version tests: no buffers are transferred. */
static mp_obj_t p9_objinvoke(size_t n,const mp_obj_t *a){
 ObjCall c;
 memset(&c,0,sizeof c);
 c.version=n>4?(unsigned int)mp_obj_get_int(a[4]):OBJ_ABI_VERSION;
 c.op=(unsigned int)mp_obj_get_int(a[1]);
 if(n>2)c.arg0=(unsigned long long)mp_obj_get_int(a[2]);
 if(n>3)c.arg1=(unsigned long long)mp_obj_get_int(a[3]);
 return check(obj_invoke(mp_obj_get_int(a[0]),&c,sizeof c));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_objinvoke_obj,2,5,p9_objinvoke);

/* Volatile capability regions.  These helpers exercise the region mapping:
 * create returns a writer-capable Handle, map returns a process-local virtual
 * address, and unmap affects only the caller's address space.  region_read
 * and region_write perform ordinary user-mode memory accesses, so writing an
 * SG_RONLY region fails in the MMU and delivers the normal fault note. */
static mp_obj_t p9_regioncreate(mp_obj_t size){
 return check(obj_region_create((unsigned long long)mp_obj_get_uint(size),NULL));
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_regioncreate_obj,p9_regioncreate);

static mp_obj_t p9_regionmap(size_t n,const mp_obj_t *a){
 unsigned long long addr=n>1?(unsigned long long)mp_obj_get_uint(a[1]):0;
 return check(obj_region_map(mp_obj_get_int(a[0]),addr));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_regionmap_obj,1,2,p9_regionmap);

static mp_obj_t p9_regionunmap(mp_obj_t addr){
 check(obj_region_unmap((unsigned long long)mp_obj_get_uint(addr)));
 return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_regionunmap_obj,p9_regionunmap);

static mp_obj_t p9_regionread(mp_obj_t addr,mp_obj_t count){
 long n=mp_obj_get_int(count);vstr_t v;
 if(n<0||n>65536)mp_raise_ValueError(MP_ERROR_TEXT("region read limit"));
 vstr_init_len(&v,n);
 if(n>0)memmove(v.buf,(void*)(uintptr_t)mp_obj_get_uint(addr),(size_t)n);
 return mp_obj_new_bytes_from_vstr(&v);
}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_regionread_obj,p9_regionread);

static mp_obj_t p9_regionwrite(mp_obj_t addr,mp_obj_t data){
 mp_buffer_info_t b;
 mp_get_buffer_raise(data,&b,MP_BUFFER_READ);
 if(b.len>65536)mp_raise_ValueError(MP_ERROR_TEXT("region write limit"));
 if(b.len>0)memmove((void*)(uintptr_t)mp_obj_get_uint(addr),b.buf,(size_t)b.len);
 return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_regionwrite_obj,p9_regionwrite);

/* Test-only callback address for ObjOpNotifyRegister: a real user text
 * address that accepts any note.  It is not installed by normal startup;
 * a script can pass it as arg0 of the new operation, remove it, and observe
 * delivery through the counter and last-note text.  Keeping the diagnostic
 * here adds no kernel entry point and changes no normal callback.
 *
 * It deliberately uses the normal GCC SysV parameters (RDI = Ureg*,
 * RSI = note text); it does not read the old Plan 9 stack frame.  The frame
 * words are retained only as supporting diagnostic output. */
static volatile unsigned p9_notify_probe_calls;
static uintptr p9_notify_probe_frame[4];
static char p9_notify_probe_last[ERRMAX];
static int p9_notify_probe(void *ureg, char *msg) {
    uintptr *fp;
    size_t n;

    fp = (uintptr*)__builtin_frame_address(0);
    if(fp != NULL){
        p9_notify_probe_frame[0] = fp[0];
        p9_notify_probe_frame[1] = fp[1];
        p9_notify_probe_frame[2] = fp[2];
        p9_notify_probe_frame[3] = (uintptr)ureg;
    }
    p9_notify_probe_calls++;
    if(msg != NULL){
        n = strlen(msg);
        if(n >= sizeof p9_notify_probe_last)
            n = sizeof p9_notify_probe_last - 1;
        memmove(p9_notify_probe_last, msg, n);
        p9_notify_probe_last[n] = 0;
    }else
        p9_notify_probe_last[0] = 0;
    noted(NCONT);
    return 1;
}
static mp_obj_t p9_notify_probe_addr(void) {
    return mp_obj_new_int_from_ull((unsigned long long)(unsigned long)p9_notify_probe);
}
static MP_DEFINE_CONST_FUN_OBJ_0(p9_notify_probe_addr_obj,p9_notify_probe_addr);
static mp_obj_t p9_notify_probe_count(void) {
    return mp_obj_new_int((mp_int_t)p9_notify_probe_calls);
}
static MP_DEFINE_CONST_FUN_OBJ_0(p9_notify_probe_count_obj,p9_notify_probe_count);
static mp_obj_t p9_notify_probe_reset(void) {
    p9_notify_probe_calls = 0;
    p9_notify_probe_last[0] = 0;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(p9_notify_probe_reset_obj,p9_notify_probe_reset);
static mp_obj_t p9_notify_probe_lastnote(void) {
    return mp_obj_new_str(p9_notify_probe_last, strlen(p9_notify_probe_last));
}
static MP_DEFINE_CONST_FUN_OBJ_0(p9_notify_probe_lastnote_obj,p9_notify_probe_lastnote);
static mp_obj_t p9_notify_probe_framevals(void) {
    mp_obj_t a[4];
    int i;
    for(i=0;i<4;i++)a[i]=mp_obj_new_int_from_ull((unsigned long long)p9_notify_probe_frame[i]);
    return mp_obj_new_tuple(4,a);
}
static MP_DEFINE_CONST_FUN_OBJ_0(p9_notify_probe_framevals_obj,p9_notify_probe_framevals);

/* Read-only system information (UJ1). */
extern long syssnapshot(void*);
static mp_obj_t p9_system(void){
 unsigned long long v[7];check(syssnapshot(v));mp_obj_t a[7];
 for(int i=0;i<7;i++)a[i]=mp_obj_new_int_from_ull(v[i]);return mp_obj_new_tuple(7,a);
}
static MP_DEFINE_CONST_FUN_OBJ_0(p9_system_obj,p9_system);

/* Native Pipe, UART and KBD: canonical OBJ* adapters and the historical
 * compatibility calls.  Both routes hit the same Vhand table. */
static mp_obj_t p9_objpipecreate(void){
 ObjId id;long h=obj_pipe_create(&id);
 if(h<0)raise_err();
 mp_obj_t t[2]={mp_obj_new_int((mp_int_t)h),mp_obj_new_int_from_ull(id.oid)};
 return mp_obj_new_tuple(2,t);
}
static MP_DEFINE_CONST_FUN_OBJ_0(p9_objpipecreate_obj,p9_objpipecreate);

static mp_obj_t p9_objpipepeer(mp_obj_t h){
 return check(obj_pipe_peer(mp_obj_get_int(h)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_objpipepeer_obj,p9_objpipepeer);

static mp_obj_t p9_objuartctl(size_t n,const mp_obj_t *a){
 Uartcfg c;
 memset(&c,0,sizeof c);
 c.set=Uartbaud|Uartbits|Uartstop|Uartparity;
 c.baud=mp_obj_get_int(a[1]);c.bits=mp_obj_get_int(a[2]);
 c.stop=mp_obj_get_int(a[3]);
 c.parity=n>4?mp_obj_get_int(a[4]):Uartparitynone;
 return check(obj_uart_ctl(mp_obj_get_int(a[0]),&c));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_objuartctl_obj,4,5,p9_objuartctl);

static mp_obj_t p9_objuartstatus(size_t n,const mp_obj_t *a){
 long sz=n>1?mp_obj_get_int(a[1]):4096,r;vstr_t v;
 if(sz<=0||sz>OBJ_UART_STATMAX)mp_raise_ValueError(MP_ERROR_TEXT("UART status size"));
 vstr_init_len(&v,sz);
 r=obj_uart_status(mp_obj_get_int(a[0]),v.buf,(unsigned long long)sz);
 if(r<0){vstr_clear(&v);raise_err();}
 v.len=r;return mp_obj_new_bytes_from_vstr(&v);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_objuartstatus_obj,1,2,p9_objuartstatus);

static mp_obj_t p9_objkbdleds(mp_obj_t h,mp_obj_t leds){
 return check(obj_kbd_leds(mp_obj_get_int(h),(unsigned int)mp_obj_get_int(leds)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_objkbdleds_obj,p9_objkbdleds);

/* Native common message-device ABI.  Payloads retain their device format. */
static mp_obj_t p9_devicesopen(size_t n, const mp_obj_t *a){
 return check(obj_devices_open(n ? (unsigned long long)mp_obj_get_int(a[0]) : 0));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_devicesopen_obj,0,1,p9_devicesopen);

static mp_obj_t p9_devicelist(size_t n,const mp_obj_t *a){
 int h=mp_obj_get_int(a[0]);
 long cap=n>1?mp_obj_get_int(a[1]):OBJ_DEVICE_LISTMAX;
 long r,i;ObjDeviceEntry *e;mp_obj_t *rows,t[9],result;
 if(cap<0||cap>OBJ_DEVICE_LISTMAX)mp_raise_ValueError(MP_ERROR_TEXT("device list capacity"));
 if(cap==0)return check(obj_device_list(h,NULL,0));
 e=m_new(ObjDeviceEntry,cap);
 r=obj_device_list(h,e,cap);
 if(r<0){m_del(ObjDeviceEntry,e,cap);raise_err();}
 rows=m_new(mp_obj_t,r?r:1);
 for(i=0;i<r;i++){
  t[0]=mp_obj_new_int(e[i].version);t[1]=mp_obj_new_int(e[i].family);
  t[2]=mp_obj_new_int(e[i].selector0);t[3]=mp_obj_new_int(e[i].selector1);
  t[4]=mp_obj_new_int(e[i].maxsend);t[5]=mp_obj_new_int(e[i].maxrecv);
  t[6]=mp_obj_new_int(e[i].flags);t[7]=mp_obj_new_int(e[i].kind);
  t[8]=mp_obj_new_int(e[i].reserved);
  rows[i]=mp_obj_new_tuple(9,t);
 }
 result=mp_obj_new_tuple(r,rows);
 m_del(mp_obj_t,rows,r?r:1);m_del(ObjDeviceEntry,e,cap);
 return result;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_devicelist_obj,1,2,p9_devicelist);

static mp_obj_t p9_deviceopen(size_t n,const mp_obj_t *a){
 unsigned long long rights=n>4?(unsigned long long)mp_obj_get_int(a[4]):0;
 mp_buffer_info_t b;
 if(n>5){
  mp_get_buffer_raise(a[5],&b,MP_BUFFER_READ);
  return check(obj_device_open(mp_obj_get_int(a[0]),mp_obj_get_int(a[1]),
   mp_obj_get_int(a[2]),mp_obj_get_int(a[3]),rights,b.buf,b.len));
 }
 return check(obj_device_open(mp_obj_get_int(a[0]),mp_obj_get_int(a[1]),
  mp_obj_get_int(a[2]),mp_obj_get_int(a[3]),rights,nil,0));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_deviceopen_obj,4,6,p9_deviceopen);

static mp_obj_t p9_devicesend(mp_obj_t h,mp_obj_t data){
 mp_buffer_info_t b;mp_get_buffer_raise(data,&b,MP_BUFFER_READ);
 return check(obj_device_send(mp_obj_get_int(h),b.buf,b.len));
}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_devicesend_obj,p9_devicesend);

static mp_obj_t p9_devicerecv(mp_obj_t h){
 ObjDeviceResult c;long r;mp_obj_t t[4];
 r=obj_device_recv(mp_obj_get_int(h),&c);
 if(r<0)raise_err();
 t[0]=mp_obj_new_int(r);t[1]=mp_obj_new_int_from_ull(c.sequence);
 t[2]=mp_obj_new_int(c.family);t[3]=mp_obj_new_int(c.length);
 return mp_obj_new_tuple(4,t);
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_devicerecv_obj,p9_devicerecv);

/* Ethernet receive-channel acquisition, distinct from USB completion recv. */
static mp_obj_t p9_devicerx(mp_obj_t h){
 return check(obj_device_rx(mp_obj_get_int(h)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_devicerx_obj,p9_devicerx);

/* Device control is IOCTL(145).  Each adapter names its own typed operation
 * and passes the real Python buffer length, so choosing a provider never
 * requires a metadata (Vmeta) lookup. */
static void devctlargs(size_t n,const mp_obj_t *a,void **buf,unsigned long long *len){
 mp_buffer_info_t b;
 *buf=nil;*len=0;
 if(n>2 && a[2]!=mp_const_none){
  mp_get_buffer_raise(a[2],&b,MP_BUFFER_WRITE);
  if(b.len>OBJ_DEVICE_MAXPAYLOAD)
   mp_raise_ValueError(MP_ERROR_TEXT("device control buffer limit"));
  *buf=b.buf;*len=b.len;
 }
}

static mp_obj_t p9_deviceioctl(size_t n,const mp_obj_t *a){
 void *buf;unsigned long long len,arg0=0;
 devctlargs(n,a,&buf,&len);
 if(n>3)arg0=(unsigned long long)mp_obj_get_int(a[3]);
 return check(obj_device_ioctl(mp_obj_get_int(a[0]),
   (unsigned)mp_obj_get_int(a[1]),buf,len,arg0));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_deviceioctl_obj,2,4,p9_deviceioctl);

static mp_obj_t p9_devusbglobal(size_t n,const mp_obj_t *a){
 void *buf;unsigned long long len;
 devctlargs(n,a,&buf,&len);
 return check(obj_device_usb_global_ctl(mp_obj_get_int(a[0]),
   (unsigned)mp_obj_get_int(a[1]),buf,len));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_devusbglobal_obj,2,3,p9_devusbglobal);

static mp_obj_t p9_devusbep(size_t n,const mp_obj_t *a){
 void *buf;unsigned long long len;
 devctlargs(n,a,&buf,&len);
 return check(obj_device_usb_ep_ctl(mp_obj_get_int(a[0]),
   (unsigned)mp_obj_get_int(a[1]),buf,len));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_devusbep_obj,2,3,p9_devusbep);

static mp_obj_t p9_devusbclearhalt(mp_obj_t h){
 return check(obj_device_usb_clear_halt(mp_obj_get_int(h)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_devusbclearhalt_obj,p9_devusbclearhalt);

static mp_obj_t p9_devedetach(mp_obj_t h){
 return check(obj_device_ether_detach(mp_obj_get_int(h)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_devedetach_obj,p9_devedetach);

static mp_obj_t p9_deviceetherinfo(mp_obj_t handle){
 ObjEtherStatus i;mp_obj_t a[15];
 if(obj_device_ether_status(mp_obj_get_int(handle),&i)<0)raise_err();
 a[0]=mp_obj_new_int(i.version);a[1]=mp_obj_new_int(i.controller);
 a[2]=mp_obj_new_int(i.minframe);a[3]=mp_obj_new_int(i.maxframe);
 a[4]=mp_obj_new_bytes(i.mac,6);a[5]=mp_obj_new_int(i.link);
 a[6]=mp_obj_new_int(i.closed);a[7]=mp_obj_new_int_from_ull(i.arrivals);
 a[8]=mp_obj_new_int_from_ull(i.published);a[9]=mp_obj_new_int_from_ull(i.handoff_drops);
 a[10]=mp_obj_new_int_from_ull(i.publish_drops);a[11]=mp_obj_new_int_from_ull(i.tx_accepted);
 a[12]=mp_obj_new_int_from_ull(i.highwater);a[13]=mp_obj_new_int(i.handoff_capacity);
 a[14]=mp_obj_new_int(i.handoff_queued);
 return mp_obj_new_tuple(15,a);
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_deviceetherinfo_obj,p9_deviceetherinfo);

extern long sysraw(long, unsigned long long, unsigned long long,
 unsigned long long, unsigned long long, unsigned long long);

/* Direct memory services operate on user segments and byte buffers. */
static mp_obj_t p9_segattach(mp_obj_t name, mp_obj_t size){
 const char *s=mp_obj_str_get_str(name);
 return check(segattach(0,(char*)s,0,mp_obj_get_int(size)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_segattach_obj,p9_segattach);
static mp_obj_t p9_segbrk(mp_obj_t addr,mp_obj_t top){
 return check(segbrk(mp_obj_get_int(addr),mp_obj_get_int(top)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_segbrk_obj,p9_segbrk);
static mp_obj_t p9_segfree(mp_obj_t addr,mp_obj_t size){check(segfree(mp_obj_get_int(addr),mp_obj_get_int(size)));return mp_const_none;}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_segfree_obj,p9_segfree);
static mp_obj_t p9_segflush(mp_obj_t addr,mp_obj_t size){check(segflush((void*)(uintptr_t)mp_obj_get_int(addr),mp_obj_get_int(size)));return mp_const_none;}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_segflush_obj,p9_segflush);
static mp_obj_t p9_segdetach(mp_obj_t addr){check(segdetach(mp_obj_get_int(addr)));return mp_const_none;}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_segdetach_obj,p9_segdetach);
static long* p9_semaddr(mp_obj_t word){
 mp_buffer_info_t b;mp_get_buffer_raise(word,&b,MP_BUFFER_WRITE);
 if(b.len<sizeof(long) || ((uintptr_t)b.buf & (sizeof(long)-1))) mp_raise_ValueError(MP_ERROR_TEXT("aligned semaphore word required"));
 return (long*)b.buf;
}
static mp_obj_t p9_semacquire(mp_obj_t word,mp_obj_t block){return check(semacquire(p9_semaddr(word),mp_obj_get_int(block)));}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_semacquire_obj,p9_semacquire);
static mp_obj_t p9_semrelease(mp_obj_t word,mp_obj_t delta){return check(semrelease(p9_semaddr(word),mp_obj_get_int(delta)));}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_semrelease_obj,p9_semrelease);
static mp_obj_t p9_tsemacquire(mp_obj_t word,mp_obj_t ms){return check(tsemacquire(p9_semaddr(word),mp_obj_get_int(ms)));}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_tsemacquire_obj,p9_tsemacquire);

/* Diagnostic raw-syscall trampoline for ABI retirement tests.  It adds no
 * kernel entry point: the number is chosen by the caller exactly as the
 * generated per-call stubs do.  Optional arguments let a test form a real
 * invalid-user-address syscall and thus receive a genuine kernel note. */
static mp_obj_t p9_sysraw(size_t n,const mp_obj_t *a){
 unsigned long long v[5]={0,0,0,0,0};
 size_t i;
 for(i=1;i<n && i<6;i++)v[i-1]=(unsigned long long)mp_obj_get_int(a[i]);
 return check(sysraw(mp_obj_get_int(a[0]),v[0],v[1],v[2],v[3],v[4]));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_sysraw_obj,1,6,p9_sysraw);

static const mp_rom_map_elem_t plan9_globals[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_plan9) },
    { MP_ROM_QSTR(MP_QSTR_sysraw), MP_ROM_PTR(&p9_sysraw_obj) },
    { MP_ROM_QSTR(MP_QSTR_seg_attach), MP_ROM_PTR(&p9_segattach_obj) },
    { MP_ROM_QSTR(MP_QSTR_seg_brk), MP_ROM_PTR(&p9_segbrk_obj) },
    { MP_ROM_QSTR(MP_QSTR_seg_free), MP_ROM_PTR(&p9_segfree_obj) },
    { MP_ROM_QSTR(MP_QSTR_seg_flush), MP_ROM_PTR(&p9_segflush_obj) },
    { MP_ROM_QSTR(MP_QSTR_seg_detach), MP_ROM_PTR(&p9_segdetach_obj) },
    { MP_ROM_QSTR(MP_QSTR_sem_acquire), MP_ROM_PTR(&p9_semacquire_obj) },
    { MP_ROM_QSTR(MP_QSTR_sem_release), MP_ROM_PTR(&p9_semrelease_obj) },
    { MP_ROM_QSTR(MP_QSTR_sem_timed_acquire), MP_ROM_PTR(&p9_tsemacquire_obj) },

    { MP_ROM_QSTR(MP_QSTR_pipenew), MP_ROM_PTR(&p9_pipenew_obj) },
    { MP_ROM_QSTR(MP_QSTR_piperead), MP_ROM_PTR(&p9_piperead_obj) },
    { MP_ROM_QSTR(MP_QSTR_pipewrite), MP_ROM_PTR(&p9_pipewrite_obj) },
    { MP_ROM_QSTR(MP_QSTR_pipeclose), MP_ROM_PTR(&p9_pipeclose_obj) },
    { MP_ROM_QSTR(MP_QSTR_alarm), MP_ROM_PTR(&p9_alarm_obj) },
    { MP_ROM_QSTR(MP_QSTR_wait), MP_ROM_PTR(&p9_wait_obj) },
    { MP_ROM_QSTR(MP_QSTR_rfork), MP_ROM_PTR(&p9_rfork_obj) },
    { MP_ROM_QSTR(MP_QSTR_rforkex), MP_ROM_PTR(&p9_rforkex_obj) },
    { MP_ROM_QSTR(MP_QSTR_envget), MP_ROM_PTR(&p9_envget_obj) },
    { MP_ROM_QSTR(MP_QSTR_envset), MP_ROM_PTR(&p9_envset_obj) },
    { MP_ROM_QSTR(MP_QSTR_envdelete), MP_ROM_PTR(&p9_envdelete_obj) },
    { MP_ROM_QSTR(MP_QSTR_envlist), MP_ROM_PTR(&p9_envlist_obj) },
    { MP_ROM_QSTR(MP_QSTR_proc_info), MP_ROM_PTR(&p9_procinfo_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_list_capacity), MP_ROM_PTR(&p9_objlistcap_obj) },
    { MP_ROM_QSTR(MP_QSTR_sleep), MP_ROM_PTR(&p9_sleep_obj) },
    { MP_ROM_QSTR(MP_QSTR_time_ns), MP_ROM_PTR(&p9_time_ns_obj) },
    { MP_ROM_QSTR(MP_QSTR_errstr), MP_ROM_PTR(&p9_errstr_get_obj) },
    { MP_ROM_QSTR(MP_QSTR_werrstr), MP_ROM_PTR(&p9_werrstr_obj) },
    { MP_ROM_QSTR(MP_QSTR_errstr_exchange), MP_ROM_PTR(&p9_errstr_exchange_obj) },
    { MP_ROM_QSTR(MP_QSTR_getpid), MP_ROM_PTR(&p9_getpid_obj) },
    { MP_ROM_QSTR(MP_QSTR_exits), MP_ROM_PTR(&p9_exits_obj) },
    { MP_ROM_QSTR(MP_QSTR_system), MP_ROM_PTR(&p9_system_obj) },
    { MP_ROM_QSTR(MP_QSTR_vobjcreate), MP_ROM_PTR(&p9_vobjcreate_obj) },
    { MP_ROM_QSTR(MP_QSTR_vobjread), MP_ROM_PTR(&p9_vobjread_obj) },
    { MP_ROM_QSTR(MP_QSTR_vobjinfo), MP_ROM_PTR(&p9_vobjinfo_obj) },
    { MP_ROM_QSTR(MP_QSTR_vobjclose), MP_ROM_PTR(&p9_vobjclose_obj) },
    { MP_ROM_QSTR(MP_QSTR_vobjfifocreate), MP_ROM_PTR(&p9_vobjfifocreate_obj) },
    { MP_ROM_QSTR(MP_QSTR_vobjfifosub), MP_ROM_PTR(&p9_vobjfifosub_obj) },
    { MP_ROM_QSTR(MP_QSTR_vobjfifosend), MP_ROM_PTR(&p9_vobjfifosend_obj) },
    { MP_ROM_QSTR(MP_QSTR_vobjfiforecv), MP_ROM_PTR(&p9_vobjfiforecv_obj) },
    { MP_ROM_QSTR(MP_QSTR_vobjfifostat), MP_ROM_PTR(&p9_vobjfifostat_obj) },
    { MP_ROM_QSTR(MP_QSTR_vobjgrant), MP_ROM_PTR(&p9_vobjgrant_obj) },
    { MP_ROM_QSTR(MP_QSTR_vobjrevoke), MP_ROM_PTR(&p9_vobjrevoke_obj) },
    { MP_ROM_QSTR(MP_QSTR_vobjio), MP_ROM_PTR(&p9_vobjio_obj) },
    { MP_ROM_QSTR(MP_QSTR_vobjlist), MP_ROM_PTR(&p9_vobjlist_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_create), MP_ROM_PTR(&p9_objcreate_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_fifocreate), MP_ROM_PTR(&p9_objfifocreate_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_close), MP_ROM_PTR(&p9_objclose_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_open_handle), MP_ROM_PTR(&p9_objopenhandle_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_open_binding), MP_ROM_PTR(&p9_objopenbinding_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_pipe_create), MP_ROM_PTR(&p9_objpipecreate_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_pipe_peer), MP_ROM_PTR(&p9_objpipepeer_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_uart_ctl), MP_ROM_PTR(&p9_objuartctl_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_uart_status), MP_ROM_PTR(&p9_objuartstatus_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_kbd_leds), MP_ROM_PTR(&p9_objkbdleds_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_device_ether_info), MP_ROM_PTR(&p9_deviceetherinfo_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_devices_open), MP_ROM_PTR(&p9_devicesopen_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_device_list), MP_ROM_PTR(&p9_devicelist_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_device_open), MP_ROM_PTR(&p9_deviceopen_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_device_send), MP_ROM_PTR(&p9_devicesend_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_device_recv), MP_ROM_PTR(&p9_devicerecv_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_device_rx), MP_ROM_PTR(&p9_devicerx_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_device_ioctl), MP_ROM_PTR(&p9_deviceioctl_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_device_usb_global_ctl), MP_ROM_PTR(&p9_devusbglobal_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_device_usb_ep_ctl), MP_ROM_PTR(&p9_devusbep_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_device_usb_clear_halt), MP_ROM_PTR(&p9_devusbclearhalt_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_device_ether_detach), MP_ROM_PTR(&p9_devedetach_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_open_space), MP_ROM_PTR(&p9_objopenspace_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_create_ge), MP_ROM_PTR(&p9_objcreatege_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_open_ge_space), MP_ROM_PTR(&p9_objopengeospace_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_open_namespace_current), MP_ROM_PTR(&p9_objopennamespacecurrent_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_commit), MP_ROM_PTR(&p9_objcommit_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_commit_version), MP_ROM_PTR(&p9_objcommitversion_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_bind), MP_ROM_PTR(&p9_objbind_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_read), MP_ROM_PTR(&p9_objread_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_read_version), MP_ROM_PTR(&p9_objreadver_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_write), MP_ROM_PTR(&p9_objwrite_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_info), MP_ROM_PTR(&p9_objinfo_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_stat), MP_ROM_PTR(&p9_objstat_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_subscribe), MP_ROM_PTR(&p9_objsubscribe_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_publish), MP_ROM_PTR(&p9_objpublish_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_receive), MP_ROM_PTR(&p9_objreceive_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_grant), MP_ROM_PTR(&p9_objgrant_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_revoke), MP_ROM_PTR(&p9_objrevoke_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_fifo_shutdown), MP_ROM_PTR(&p9_objfifoshutdown_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_list), MP_ROM_PTR(&p9_objlist_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_invoke), MP_ROM_PTR(&p9_objinvoke_obj) },
    { MP_ROM_QSTR(MP_QSTR_region_create), MP_ROM_PTR(&p9_regioncreate_obj) },
    { MP_ROM_QSTR(MP_QSTR_region_map), MP_ROM_PTR(&p9_regionmap_obj) },
    { MP_ROM_QSTR(MP_QSTR_region_unmap), MP_ROM_PTR(&p9_regionunmap_obj) },
    { MP_ROM_QSTR(MP_QSTR_region_read), MP_ROM_PTR(&p9_regionread_obj) },
    { MP_ROM_QSTR(MP_QSTR_region_write), MP_ROM_PTR(&p9_regionwrite_obj) },
    { MP_ROM_QSTR(MP_QSTR_notify_probe_addr), MP_ROM_PTR(&p9_notify_probe_addr_obj) },
    { MP_ROM_QSTR(MP_QSTR_notify_probe_count), MP_ROM_PTR(&p9_notify_probe_count_obj) },
    { MP_ROM_QSTR(MP_QSTR_notify_probe_reset), MP_ROM_PTR(&p9_notify_probe_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_notify_probe_lastnote), MP_ROM_PTR(&p9_notify_probe_lastnote_obj) },
    { MP_ROM_QSTR(MP_QSTR_notify_probe_framevals), MP_ROM_PTR(&p9_notify_probe_framevals_obj) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_ABI_VERSION), MP_ROM_INT(OBJ_ABI_VERSION) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OPEN_HANDLE), MP_ROM_INT(ObjOpenHandle) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OPEN_BINDING), MP_ROM_INT(ObjOpenBinding) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OPEN_SPACE), MP_ROM_INT(ObjOpenSpace) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OPEN_GE_SPACE), MP_ROM_INT(ObjOpenGeSpace) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OPEN_NAMESPACE_CURRENT), MP_ROM_INT(ObjOpenNamespaceCurrent) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_DEVICE_USB_CONTROL), MP_ROM_INT(ObjDeviceUsbControl) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_DEVICE_ETHERNET), MP_ROM_INT(ObjDeviceEthernet) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_DEVICE_UART), MP_ROM_INT(ObjDeviceUart) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_DEVICE_KBD), MP_ROM_INT(ObjDeviceKbd) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_DEVICE_USB_ENDPOINT), MP_ROM_INT(ObjDeviceUsbEndpoint) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_DEVICE_OPT_VERSION), MP_ROM_INT(OBJ_DEVICE_OPT_VERSION) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_DEVICE_KBD_EXCLUSIVE), MP_ROM_INT(OBJ_DEVICE_KBD_EXCLUSIVE) },
    { MP_ROM_QSTR(MP_QSTR_VKIND_NAMESPACE_CONTROL), MP_ROM_INT(VkindNamespaceControl) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_NAMESPACE_CREATE), MP_ROM_INT(ObjOpNamespaceCreate) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_NAMESPACE_ATTACH), MP_ROM_INT(ObjOpNamespaceAttach) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_NAMESPACE_DETACH), MP_ROM_INT(ObjOpNamespaceDetach) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_NAMESPACE_VISIBLE), MP_ROM_INT(ObjOpNamespaceVisible) },
    { MP_ROM_QSTR(MP_QSTR_NCONT), MP_ROM_INT(NCONT) },
    { MP_ROM_QSTR(MP_QSTR_NDFLT), MP_ROM_INT(NDFLT) },
    { MP_ROM_QSTR(MP_QSTR_NSAVE), MP_ROM_INT(NSAVE) },
    { MP_ROM_QSTR(MP_QSTR_NRSTR), MP_ROM_INT(NRSTR) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_USB_EP_DATA), MP_ROM_INT(OBJ_USB_EP_DATA) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_USB_EP_CTL), MP_ROM_INT(OBJ_USB_EP_CTL) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_KIND_PIPE), MP_ROM_INT(ObjKindPipe) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_PIPE_PEER), MP_ROM_INT(ObjOpPipePeer) },
    { MP_ROM_QSTR(MP_QSTR_VKIND_REGION), MP_ROM_INT(VkindRegion) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_KIND_REGION), MP_ROM_INT(ObjKindRegion) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_REGION_MAP), MP_ROM_INT(ObjOpRegionMap) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_REGION_UNMAP), MP_ROM_INT(ObjOpRegionUnmap) },
    { MP_ROM_QSTR(MP_QSTR_IOCTL_UART_CONFIG), MP_ROM_INT(IoctlUartConfig) },
    { MP_ROM_QSTR(MP_QSTR_IOCTL_UART_STATUS), MP_ROM_INT(IoctlUartStatus) },
    { MP_ROM_QSTR(MP_QSTR_IOCTL_KBD_LEDS), MP_ROM_INT(IoctlKbdLeds) },
    { MP_ROM_QSTR(MP_QSTR_IOCTL_ETHER_STATUS), MP_ROM_INT(IoctlEtherStatus) },
    { MP_ROM_QSTR(MP_QSTR_IOCTL_ETHER_DETACH), MP_ROM_INT(IoctlEtherDetach) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_STREAM_MAXIO), MP_ROM_INT(OBJ_STREAM_MAXIO) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_KBD_MAXREAD), MP_ROM_INT(OBJ_KBD_MAXREAD) },
    { MP_ROM_QSTR(MP_QSTR_UART_BAUD), MP_ROM_INT(Uartbaud) },
    { MP_ROM_QSTR(MP_QSTR_UART_BITS), MP_ROM_INT(Uartbits) },
    { MP_ROM_QSTR(MP_QSTR_UART_STOP), MP_ROM_INT(Uartstop) },
    { MP_ROM_QSTR(MP_QSTR_UART_PARITY), MP_ROM_INT(Uartparity) },
    { MP_ROM_QSTR(MP_QSTR_UART_OREAD), MP_ROM_INT(UARTOREAD) },
    { MP_ROM_QSTR(MP_QSTR_UART_OWRITE), MP_ROM_INT(UARTOWRITE) },
    { MP_ROM_QSTR(MP_QSTR_UART_ORDWR), MP_ROM_INT(UARTORDWR) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_KIND_GE), MP_ROM_INT(ObjKindGe) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_COMMIT), MP_ROM_INT(ObjOpCommit) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_PUT_OID), MP_ROM_INT(ObjOpPutOid) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_INFO), MP_ROM_INT(ObjOpInfo) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_READ), MP_ROM_INT(ObjOpRead) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_WRITE), MP_ROM_INT(ObjOpWrite) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_STAT), MP_ROM_INT(ObjOpStat) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_SEND), MP_ROM_INT(ObjOpSend) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_RECV), MP_ROM_INT(ObjOpRecv) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_SUBSCRIBE), MP_ROM_INT(ObjOpSubscribe) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_REVOKE), MP_ROM_INT(ObjOpRevoke) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_FIFO_SHUTDOWN), MP_ROM_INT(ObjOpFifoShutdown) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_DEVICE_RX), MP_ROM_INT(ObjOpDeviceRx) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_BIND_COPY), MP_ROM_INT(ObjBindCopy) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_BIND_EXCLUSIVE), MP_ROM_INT(ObjBindExclusive) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_BIND_SHARE), MP_ROM_INT(ObjBindShare) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_BIND_REMOVE), MP_ROM_INT(ObjBindRemove) },
    { MP_ROM_QSTR(MP_QSTR_VREAD), MP_ROM_INT(Vread) },
    { MP_ROM_QSTR(MP_QSTR_VSEND), MP_ROM_INT(Vsend) },
    { MP_ROM_QSTR(MP_QSTR_VRECV), MP_ROM_INT(Vrecv) },
    { MP_ROM_QSTR(MP_QSTR_VMETA), MP_ROM_INT(Vmeta) },
    { MP_ROM_QSTR(MP_QSTR_VSUBSCRIBE), MP_ROM_INT(Vsubscribe) },
    { MP_ROM_QSTR(MP_QSTR_VCONTROL), MP_ROM_INT(Vcontrol) },
    { MP_ROM_QSTR(MP_QSTR_VDELEGATE), MP_ROM_INT(Vdelegate) },
    { MP_ROM_QSTR(MP_QSTR_VWRITE), MP_ROM_INT(Vwrite) },
    { MP_ROM_QSTR(MP_QSTR_VACCEPT), MP_ROM_INT(Vaccept) },
    { MP_ROM_QSTR(MP_QSTR_VCONNECT), MP_ROM_INT(Vconnect) },
    { MP_ROM_QSTR(MP_QSTR_VCREATE), MP_ROM_INT(Vcreate) },
    { MP_ROM_QSTR(MP_QSTR_VCOMMIT), MP_ROM_INT(Vcommit) },
    { MP_ROM_QSTR(MP_QSTR_VKIND_BYTES), MP_ROM_INT(VkindBytes) },
    { MP_ROM_QSTR(MP_QSTR_VKIND_FIFO), MP_ROM_INT(VkindFifo) },
    { MP_ROM_QSTR(MP_QSTR_VKIND_GE), MP_ROM_INT(VkindGe) },
    { MP_ROM_QSTR(MP_QSTR_VKIND_SPACE), MP_ROM_INT(VkindSpace) },
    { MP_ROM_QSTR(MP_QSTR_VKIND_PIPE), MP_ROM_INT(VkindPipe) },
    { MP_ROM_QSTR(MP_QSTR_VKIND_UART), MP_ROM_INT(VkindUart) },
    { MP_ROM_QSTR(MP_QSTR_VKIND_KBD), MP_ROM_INT(VkindKbd) },
    { MP_ROM_QSTR(MP_QSTR_VKIND_USB_EP), MP_ROM_INT(VkindUsbEp) },
    { MP_ROM_QSTR(MP_QSTR_VIO_READ), MP_ROM_INT(VioRead) },
    { MP_ROM_QSTR(MP_QSTR_VIO_WRITE), MP_ROM_INT(VioWrite) },
    { MP_ROM_QSTR(MP_QSTR_VOPEN_SPACE), MP_ROM_INT(VopenSpace) },
    { MP_ROM_QSTR(MP_QSTR_VOPEN_GE_SPACE), MP_ROM_INT(VopenGeSpace) },
    { MP_ROM_QSTR(MP_QSTR_RFNAMEG), MP_ROM_INT(RFNAMEG) },
    { MP_ROM_QSTR(MP_QSTR_RFENVG), MP_ROM_INT(RFENVG) },
    { MP_ROM_QSTR(MP_QSTR_RFFDG), MP_ROM_INT(RFFDG) },
    { MP_ROM_QSTR(MP_QSTR_RFNOTEG), MP_ROM_INT(RFNOTEG) },
    { MP_ROM_QSTR(MP_QSTR_RFPROC), MP_ROM_INT(RFPROC) },
    { MP_ROM_QSTR(MP_QSTR_RFNOWAIT), MP_ROM_INT(RFNOWAIT) },
    { MP_ROM_QSTR(MP_QSTR_RFCNAMEG), MP_ROM_INT(RFCNAMEG) },
    { MP_ROM_QSTR(MP_QSTR_RFCENVG), MP_ROM_INT(RFCENVG) },
    { MP_ROM_QSTR(MP_QSTR_RFCFDG), MP_ROM_INT(RFCFDG) },
    { MP_ROM_QSTR(MP_QSTR_RFREND), MP_ROM_INT(RFREND) },
    { MP_ROM_QSTR(MP_QSTR_RFNOMNT), MP_ROM_INT(RFNOMNT) },
};
static MP_DEFINE_CONST_DICT(plan9_globals_dict, plan9_globals);

const mp_obj_module_t mp_module_plan9 = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&plan9_globals_dict,
};

MP_REGISTER_MODULE(MP_QSTR_plan9, mp_module_plan9);

/*
 * Plan 9 compatibility module for the remaining fd, process, timing and
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
#include <uartapi.h>
#include <usbctl.h>
#endif

// p9space.c: a rest before a call blocks (a Space's commit point 3)
void p9_space_rest(void);

extern long envget(int, char *, void *, long, long *);
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

static mp_obj_t p9_close(mp_obj_t fd) {
    check(close(mp_obj_get_int(fd)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_close_obj, p9_close);

// alarm(ms) -> the ms the one before had left; at its end the note
// "alarm" cuts a blocked call short (OSError, interrupted); 0 cancels
static mp_obj_t p9_alarm(mp_obj_t ms) {
    return mp_obj_new_int(alarm(mp_obj_get_int(ms)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_alarm_obj, p9_alarm);

// read(fd, n=8192) -> bytes, b'' at the end
static mp_obj_t p9_read(size_t n, const mp_obj_t *a) {
    vstr_t v;
    long r;

    p9_space_rest();
    vstr_init_len(&v, n > 1 ? mp_obj_get_int(a[1]) : 8192);
    r = read(mp_obj_get_int(a[0]), v.buf, v.len);
    if (r < 0) {
        vstr_clear(&v);
        raise_err();
    }
    v.len = r;
    return mp_obj_new_bytes_from_vstr(&v);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_read_obj, 1, 2, p9_read);

// write(fd, str or bytes) -> count
static mp_obj_t p9_write(mp_obj_t fd, mp_obj_t data) {
    mp_buffer_info_t b;

    mp_get_buffer_raise(data, &b, MP_BUFFER_READ);
    return check(write(mp_obj_get_int(fd), b.buf, b.len));
}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_write_obj, p9_write);

// seek(fd, offset, type=0) -> offset
static mp_obj_t p9_seek(size_t n, const mp_obj_t *a) {
    vlong r;

    r = seek(mp_obj_get_int(a[0]), mp_obj_get_int(a[1]), n > 2 ? mp_obj_get_int(a[2]) : 0);
    if (r < 0) {
        raise_err();
    }
    return mp_obj_new_int(r);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_seek_obj, 2, 3, p9_seek);

// dup(old, new=-1) -> fd
static mp_obj_t p9_dup(size_t n, const mp_obj_t *a) {
    return check(dup(mp_obj_get_int(a[0]), n > 1 ? mp_obj_get_int(a[1]) : -1));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_dup_obj, 1, 2, p9_dup);

// pipe() -> (fd, fd)
static mp_obj_t p9_pipe(void) {
    int p[2];
    mp_obj_t t[2];

    check(pipe(p));
    t[0] = mp_obj_new_int(p[0]);
    t[1] = mp_obj_new_int(p[1]);
    return mp_obj_new_tuple(2, t);
}
static MP_DEFINE_CONST_FUN_OBJ_0(p9_pipe_obj, p9_pipe);

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

/* Existing ENVGET syscall, bounded native bytes (no pathname getenv). */
static mp_obj_t p9_envget(mp_obj_t name) {
    const char *key=mp_obj_str_get_str(name);
    char data[128];long n=0;
    if(envget(strlen(key),(char*)key,data,sizeof data,&n)<0)raise_err();
    return mp_obj_new_bytes((const byte*)data,n);
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_envget_obj,p9_envget);

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
static mp_obj_t p9_pipeinherit(mp_obj_t h,mp_obj_t rights) {
    check(obj_pipe_inherit(mp_obj_get_int(h),(unsigned int)mp_obj_get_int(rights)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_pipeinherit_obj,p9_pipeinherit);
/* Historical net6_* Python names.  They are userland adapters over the
 * canonical five-operation OBJ* ABI; the NET6* kernel numbers are reserved
 * and dispatch to nothing. */
static mp_obj_t p9_net6addr(void) {
    vstr_t v;

    vstr_init_len(&v, 16);
    if (obj_udp6_addr((unsigned char*)v.buf) < 0) {
        vstr_clear(&v);
        raise_err();
    }
    return mp_obj_new_bytes_from_vstr(&v);
}
static MP_DEFINE_CONST_FUN_OBJ_0(p9_net6addr_obj, p9_net6addr);

static mp_obj_t p9_net6open(mp_obj_t local, mp_obj_t port_obj) {
    mp_buffer_info_t addr;
    mp_int_t port;
    long h;

    mp_get_buffer_raise(local, &addr, MP_BUFFER_READ);
    port = mp_obj_get_int(port_obj);
    if (addr.len != 16 || port < 1 || port > 65535) {
        mp_raise_ValueError(MP_ERROR_TEXT("expected a 16-byte IPv6 address and port 1..65535"));
    }
    h = obj_udp6_open((unsigned char*)addr.buf, (unsigned int)port);
    if (h < 0) {
        raise_err();
    }
    return mp_obj_new_int(h);
}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_net6open_obj, p9_net6open);

static mp_obj_t p9_net6sendto(size_t n_args, const mp_obj_t *args) {
    mp_buffer_info_t addr, data;
    mp_int_t port, h;
    long r;

    h = mp_obj_get_int(args[0]);
    mp_get_buffer_raise(args[1], &addr, MP_BUFFER_READ);
    port = mp_obj_get_int(args[2]);
    mp_get_buffer_raise(args[3], &data, MP_BUFFER_READ);
    if (addr.len != 16 || port < 1 || port > 65535 || data.len > 1232) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid IPv6 datagram address, port, or payload length"));
    }
    r = obj_udp6_sendto(h, (unsigned char*)addr.buf, (unsigned int)port,
        data.buf, (unsigned long long)data.len);
    if (r < 0) {
        raise_err();
    }
    (void)n_args;
    return mp_obj_new_int(r);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_net6sendto_obj, 4, 4, p9_net6sendto);

static mp_obj_t p9_net6recvfrom(size_t n_args, const mp_obj_t *args) {
    vstr_t data;
    uchar peersrc[18];
    ushort port;
    mp_int_t h, timeout;
    unsigned long long wait;
    long r;
    mp_obj_t out[3];

    h = mp_obj_get_int(args[0]);
    timeout = n_args > 1 ? mp_obj_get_int(args[1]) : 1000;
    if (timeout < -1 || (timeout >= 0 && (unsigned long long)timeout > 0xffffffffULL)) {
        mp_raise_ValueError(MP_ERROR_TEXT("timeout must be -1 or a 32-bit millisecond value"));
    }
    wait = timeout == -1 ? 0xffffffffULL : (unsigned long long)timeout;
    p9_space_rest();
    vstr_init_len(&data, 1232);
    r = obj_udp6_recvfrom(h, data.buf, (unsigned long long)data.len,
        peersrc, wait);
    if (r < 0) {
        vstr_clear(&data);
        raise_err();
    }
    data.len = r;
    out[0] = mp_obj_new_bytes_from_vstr(&data);
    out[1] = mp_obj_new_bytes(peersrc, 16);
    port = (peersrc[16] << 8) | peersrc[17];
    out[2] = mp_obj_new_int(port);
    return mp_obj_new_tuple(3, out);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_net6recvfrom_obj, 1, 2, p9_net6recvfrom);

static mp_obj_t p9_net6close(mp_obj_t handle) {
    if (obj_close(mp_obj_get_int(handle)) < 0) {
        raise_err();
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_net6close_obj, p9_net6close);

/* UDP6 through the canonical OBJ* five-operation ABI.  These are userland
 * adapters only; there is no separate NET6* entry point. */
static mp_obj_t p9_objopenudp6(size_t n,const mp_obj_t *a){
    mp_buffer_info_t b;unsigned long long rights=n>2?(unsigned long long)mp_obj_get_int(a[2]):0;
    mp_int_t port=mp_obj_get_int(a[1]);long h;
    mp_get_buffer_raise(a[0],&b,MP_BUFFER_READ);
    if(b.len!=16||port<1||port>65535)
        mp_raise_ValueError(MP_ERROR_TEXT("expected a 16-byte IPv6 address and port 1..65535"));
    h=obj_udp6_open_rights(b.buf,(unsigned int)port,rights);
    if(h<0)raise_err();
    return mp_obj_new_int((mp_int_t)h);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_objopenudp6_obj,2,3,p9_objopenudp6);

static mp_obj_t p9_objudp6sendto(size_t n,const mp_obj_t *a){
    mp_buffer_info_t addr,data;mp_int_t port;long r;
    mp_get_buffer_raise(a[1],&addr,MP_BUFFER_READ);
    port=mp_obj_get_int(a[2]);
    mp_get_buffer_raise(a[3],&data,MP_BUFFER_READ);
    if(addr.len!=16||port<1||port>65535||data.len>OBJ_UDP6_MAXPAYLOAD)
        mp_raise_ValueError(MP_ERROR_TEXT("invalid IPv6 datagram address, port, or payload length"));
    r=obj_udp6_sendto(mp_obj_get_int(a[0]),addr.buf,(unsigned int)port,
        data.buf,data.len);
    if(r<0)raise_err();
    (void)n;
    return mp_obj_new_int(r);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_objudp6sendto_obj,4,4,p9_objudp6sendto);

static mp_obj_t p9_objudp6recvfrom(size_t n,const mp_obj_t *a){
    vstr_t v;uchar peersrc[18];ushort port;mp_int_t timeout;long r;
    mp_obj_t out[3];
    timeout=n>1?mp_obj_get_int(a[1]):1000;
    if(timeout< -1||(timeout>=0&&(unsigned long long)timeout>0xffffffffULL))
        mp_raise_ValueError(MP_ERROR_TEXT("timeout must be -1 or a 32-bit millisecond value"));
    p9_space_rest();
    vstr_init_len(&v,OBJ_UDP6_MAXPAYLOAD);
    r=obj_udp6_recvfrom(mp_obj_get_int(a[0]),v.buf,(unsigned long long)v.len,
        peersrc,timeout<0?0xffffffffULL:(unsigned long long)timeout);
    if(r<0){vstr_clear(&v);raise_err();}
    v.len=r;
    out[0]=mp_obj_new_bytes_from_vstr(&v);
    out[1]=mp_obj_new_bytes(peersrc,16);
    port=(peersrc[16]<<8)|peersrc[17];
    out[2]=mp_obj_new_int(port);
    return mp_obj_new_tuple(3,out);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_objudp6recvfrom_obj,1,2,p9_objudp6recvfrom);

static mp_obj_t p9_objudp6addr(void){
    vstr_t v;
    vstr_init_len(&v,16);
    if(obj_udp6_addr((unsigned char*)v.buf)<0){vstr_clear(&v);raise_err();}
    return mp_obj_new_bytes_from_vstr(&v);
}
static MP_DEFINE_CONST_FUN_OBJ_0(p9_objudp6addr_obj,p9_objudp6addr);

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

static mp_obj_t p9_vobjinfo(mp_obj_t handle){
 VobjInfo i;if(obj_info(mp_obj_get_int(handle),&i)<0)raise_err();
 mp_obj_t a[16]={mp_obj_new_int_from_ull(i.incarnation),mp_obj_new_int_from_ull(i.space),
  mp_obj_new_int_from_ull(i.oid),mp_obj_new_int_from_ull(i.size),mp_obj_new_int_from_ull(i.holds),
  mp_obj_new_int_from_ull(i.subscribers),mp_obj_new_int_from_ull(i.sends),mp_obj_new_int_from_ull(i.drops),
  mp_obj_new_int(i.kind),mp_obj_new_int(i.flags),mp_obj_new_int(i.rights),
  mp_obj_new_int(i.sub_capacity),mp_obj_new_int(i.sub_queued),mp_obj_new_int(i.sub_dropped),
  mp_obj_new_int(i.fifo_closed),mp_obj_new_int(i.sub_closed)};
 return mp_obj_new_tuple(16,a);
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

static mp_obj_t p9_vobjrevoke(mp_obj_t h){check(obj_revoke(mp_obj_get_int(h)));return mp_const_none;}
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

static mp_obj_t p9_vobjtcpopen(size_t n,const mp_obj_t *a){
 mp_buffer_info_t b;VobjTcpReq req;long h;int listen=n>2?mp_obj_get_int(a[2]):1;
 mp_get_buffer_raise(a[0],&b,MP_BUFFER_READ);
 if(b.len!=16)mp_raise_ValueError(MP_ERROR_TEXT("IPv6 address must be 16 bytes"));
 memset(&req,0,sizeof req);
 memmove(req.addr,b.buf,16);
 req.port=(unsigned int)mp_obj_get_int(a[1]);
 h=obj_open(listen?ObjOpenTcpListen:ObjOpenTcpConnect,&req,sizeof req,0,NULL);
 if(h<0)raise_err();
 return mp_obj_new_int((mp_int_t)h);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_vobjtcpopen_obj,2,3,p9_vobjtcpopen);

static mp_obj_t p9_vobjaccept(mp_obj_t h,mp_obj_t ms){
 return check(obj_accept(mp_obj_get_int(h),(unsigned long long)mp_obj_get_int(ms)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_vobjaccept_obj,p9_vobjaccept);

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

static mp_obj_t p9_objopentcp(size_t n,const mp_obj_t *a){
 mp_buffer_info_t b;VobjTcpReq req;int listen=n>2?mp_obj_get_int(a[2]):1;
 mp_get_buffer_raise(a[0],&b,MP_BUFFER_READ);
 if(b.len!=16)mp_raise_ValueError(MP_ERROR_TEXT("IPv6 address must be 16 bytes"));
 memset(&req,0,sizeof req);memmove(req.addr,b.buf,16);
 req.port=(unsigned int)mp_obj_get_int(a[1]);
 return check(obj_open(listen?ObjOpenTcpListen:ObjOpenTcpConnect,&req,sizeof req,0,NULL));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_objopentcp_obj,2,3,p9_objopentcp);

static mp_obj_t p9_objopenge(size_t n,const mp_obj_t *a){
 VobjGeReq req;unsigned long long rights=n>2?(unsigned long long)mp_obj_get_int(a[2]):0;
 memset(&req,0,sizeof req);
 req.ospace=mp_obj_get_int(a[0]);
 req.oid=(unsigned long long)mp_obj_get_uint(a[1]);
 return check(obj_open(ObjOpenGe,&req,sizeof req,rights,NULL));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_objopenge_obj,2,3,p9_objopenge);

/* Persistent GE/O through the five-operation ABI only. */
static mp_obj_t p9_objopenspace(mp_obj_t name){
 const char *n=mp_obj_str_get_str(name);
 return check(obj_open(ObjOpenSpace,(void*)n,strlen(n),0,NULL));
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_objopenspace_obj,p9_objopenspace);

static mp_obj_t p9_objopenprocessself(size_t n,const mp_obj_t *a){
 unsigned long long rights=n>0?(unsigned long long)mp_obj_get_int(a[0]):0;
 return check(obj_open(ObjOpenProcessSelf,nil,0,rights,NULL));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_objopenprocessself_obj,0,1,p9_objopenprocessself);

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
 VobjInfo i;if(obj_info(mp_obj_get_int(h),&i)<0)raise_err();
 mp_obj_t a[16]={mp_obj_new_int_from_ull(i.incarnation),mp_obj_new_int_from_ull(i.space),
  mp_obj_new_int_from_ull(i.oid),mp_obj_new_int_from_ull(i.size),mp_obj_new_int_from_ull(i.holds),
  mp_obj_new_int_from_ull(i.subscribers),mp_obj_new_int_from_ull(i.sends),mp_obj_new_int_from_ull(i.drops),
  mp_obj_new_int(i.kind),mp_obj_new_int(i.flags),mp_obj_new_int(i.rights),
  mp_obj_new_int(i.sub_capacity),mp_obj_new_int(i.sub_queued),mp_obj_new_int(i.sub_dropped),
  mp_obj_new_int(i.fifo_closed),mp_obj_new_int(i.sub_closed)};
 return mp_obj_new_tuple(16,a);
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

static mp_obj_t p9_objaccept(mp_obj_t h,mp_obj_t ms){
 return check(obj_accept(mp_obj_get_int(h),(unsigned long long)mp_obj_get_int(ms)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_objaccept_obj,p9_objaccept);

static mp_obj_t p9_objgrant(mp_obj_t h,mp_obj_t rights){
 return check(obj_grant(mp_obj_get_int(h),(unsigned long long)mp_obj_get_int(rights)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_objgrant_obj,p9_objgrant);

static mp_obj_t p9_objrevoke(mp_obj_t h){
 check(obj_revoke(mp_obj_get_int(h)));return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_objrevoke_obj,p9_objrevoke);

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

/* Native TCP6 streams and read-only system information (UJ1). */
extern long syssnapshot(void*);
static mp_obj_t p9_system(void){
 unsigned long long v[7];check(syssnapshot(v));mp_obj_t a[7];
 for(int i=0;i<7;i++)a[i]=mp_obj_new_int_from_ull(v[i]);return mp_obj_new_tuple(7,a);
}
static MP_DEFINE_CONST_FUN_OBJ_0(p9_system_obj,p9_system);
static mp_obj_t p9_tcpopen(mp_obj_t address,mp_obj_t port,int listen){
 mp_buffer_info_t b;VobjTcpReq req;mp_get_buffer_raise(address,&b,MP_BUFFER_READ);
 if(b.len!=16)mp_raise_ValueError(MP_ERROR_TEXT("IPv6 address must be 16 bytes"));
 memset(&req,0,sizeof req);memmove(req.addr,b.buf,16);
 req.port=(unsigned int)mp_obj_get_int(port);
 return check(obj_open(listen?ObjOpenTcpListen:ObjOpenTcpConnect,&req,sizeof req,0,NULL));
}
static mp_obj_t p9_tcplisten(mp_obj_t a,mp_obj_t p){return p9_tcpopen(a,p,1);}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_tcplisten_obj,p9_tcplisten);
static mp_obj_t p9_tcpconnect(mp_obj_t a,mp_obj_t p){return p9_tcpopen(a,p,0);}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_tcpconnect_obj,p9_tcpconnect);
static mp_obj_t p9_tcpaccept(mp_obj_t h,mp_obj_t ms){return check(obj_accept(mp_obj_get_int(h),(unsigned long long)mp_obj_get_int(ms)));}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_tcpaccept_obj,p9_tcpaccept);
static mp_obj_t p9_tcpread(mp_obj_t h,mp_obj_t size,mp_obj_t ms){
 mp_int_t n=mp_obj_get_int(size);if(n<0||n>65536)mp_raise_ValueError(MP_ERROR_TEXT("TCP read limit"));
 vstr_t v;vstr_init_len(&v,n);long result=obj_read(mp_obj_get_int(h),v.buf,(unsigned long long)n,(unsigned long long)mp_obj_get_int(ms));
 if(result<0){vstr_clear(&v);raise_err();}v.len=result;return mp_obj_new_bytes_from_vstr(&v);
}
static MP_DEFINE_CONST_FUN_OBJ_3(p9_tcpread_obj,p9_tcpread);
static mp_obj_t p9_tcpwrite(mp_obj_t h,mp_obj_t data){
 mp_buffer_info_t b;mp_get_buffer_raise(data,&b,MP_BUFFER_READ);return check(obj_write(mp_obj_get_int(h),b.buf,(unsigned long long)b.len,0));
}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_tcpwrite_obj,p9_tcpwrite);
static mp_obj_t p9_tcpclose(mp_obj_t h){return check(obj_close(mp_obj_get_int(h)));}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_tcpclose_obj,p9_tcpclose);

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

static mp_obj_t p9_objuartopen(mp_obj_t port,mp_obj_t mode){
 return check(obj_uart_open(mp_obj_get_int(port),mp_obj_get_int(mode)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_objuartopen_obj,p9_objuartopen);

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

static mp_obj_t p9_objkbdopen(void){return check(obj_kbd_open());}
static MP_DEFINE_CONST_FUN_OBJ_0(p9_objkbdopen_obj,p9_objkbdopen);

static mp_obj_t p9_objkbdleds(mp_obj_t h,mp_obj_t leds){
 return check(obj_kbd_leds(mp_obj_get_int(h),(unsigned int)mp_obj_get_int(leds)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_objkbdleds_obj,p9_objkbdleds);

/* USB service metadata over OBJOPEN/OBJINVOKE/OBJCLOSE.  No endpoint is
 * opened and no USB data or control request crosses this binding. */
extern long usbopen(void);
extern long usblist(int,void*,long);
extern long usbepopen(int,int,int,int,int);
extern long usbclose(int);

static mp_obj_t usbentrytuple(const ObjUsbEntry *e){
 mp_obj_t a[10];int i;char name[129];
 for(i=0;i<128&&e->name[i];i++)name[i]=e->name[i];
 name[i]=0;
 a[0]=mp_obj_new_int(e->idx);a[1]=mp_obj_new_int(e->devno);
 a[2]=mp_obj_new_int(e->epno);a[3]=mp_obj_new_int(e->ttype);
 a[4]=mp_obj_new_int(e->mode);a[5]=mp_obj_new_int(e->speed);
 a[6]=mp_obj_new_int(e->state);a[7]=mp_obj_new_int(e->inuse);
 a[8]=mp_obj_new_int(e->maxpkt);a[9]=mp_obj_new_str(name,i);
 return mp_obj_new_tuple(10,a);
}

static mp_obj_t p9_objusbopen(size_t n,const mp_obj_t *a){
 unsigned long long rights=n>0?(unsigned long long)mp_obj_get_int(a[0]):0;
 return check(obj_usb_open(rights));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_objusbopen_obj,0,1,p9_objusbopen);

static mp_obj_t p9_objusblist(size_t n,const mp_obj_t *a){
 int h=mp_obj_get_int(a[0]);
 long max=n>1?mp_obj_get_int(a[1]):OBJ_USB_LISTMAX;
 long total,r,i;ObjUsbEntry *e;mp_obj_t *recs,t[2];
 if(max<0)mp_raise_ValueError(MP_ERROR_TEXT("USB list limit"));
 if(max>OBJ_USB_LISTMAX)max=OBJ_USB_LISTMAX;
 total=obj_usb_list(h,NULL,0);
 if(total<0)raise_err();
 if(max>total)max=total;
 e=m_new(ObjUsbEntry,max?max:1);
 r=max?obj_usb_list(h,e,max):0;
 if(r<0){m_del(ObjUsbEntry,e,max?max:1);raise_err();}
 recs=m_new(mp_obj_t,r?r:1);
 for(i=0;i<r;i++)recs[i]=usbentrytuple(&e[i]);
 m_del(ObjUsbEntry,e,max?max:1);
 t[1]=mp_obj_new_tuple(r,recs);
 m_del(mp_obj_t,recs,r?r:1);
 t[0]=mp_obj_new_int(total);
 return mp_obj_new_tuple(2,t);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_objusblist_obj,1,2,p9_objusblist);

/* Low-level byte-capacity form: it passes outlen through unchanged instead
 * of clamping the record count in userland, so the kernel's direct
 * OBJINVOKE(ObjOpUsbList) length rules are observable. */
static mp_obj_t p9_objusblistbytes(size_t n,const mp_obj_t *a){
 int h=mp_obj_get_int(a[0]);
 long nbytes=mp_obj_get_int(a[1]),total,r,i;
 unsigned char *buf;ObjUsbEntry *e;ObjCall c;mp_obj_t *recs,t[2];
 if(n<2)mp_raise_TypeError(MP_ERROR_TEXT("obj_usb_list_bytes needs 2 args"));
 if(nbytes<0||nbytes>1048576)mp_raise_ValueError(MP_ERROR_TEXT("USB list byte capacity"));
 total=obj_usb_list(h,NULL,0);
 if(total<0)raise_err();
 if(nbytes==0){t[0]=mp_obj_new_int(total);t[1]=mp_obj_new_tuple(0,NULL);return mp_obj_new_tuple(2,t);}
 buf=m_new(unsigned char,nbytes);
 memset(&c,0,sizeof c);
 c.version=OBJ_ABI_VERSION;c.op=ObjOpUsbList;
 c.out=buf;c.outlen=(unsigned long long)nbytes;
 r=obj_invoke(h,&c,sizeof c);
 if(r<0){m_del(unsigned char,buf,nbytes);raise_err();}
 if(r>nbytes/(long)sizeof(ObjUsbEntry))r=nbytes/sizeof(ObjUsbEntry);
 e=(ObjUsbEntry*)buf;
 recs=m_new(mp_obj_t,r?r:1);
 for(i=0;i<r;i++)recs[i]=usbentrytuple(&e[i]);
 m_del(unsigned char,buf,nbytes);
 t[1]=mp_obj_new_tuple(r,recs);
 m_del(mp_obj_t,recs,r?r:1);
 t[0]=mp_obj_new_int(total);
 return mp_obj_new_tuple(2,t);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_objusblistbytes_obj,2,2,p9_objusblistbytes);

static mp_obj_t p9_objusbepopen(size_t n,const mp_obj_t *a){
 if(n!=5)mp_raise_TypeError(MP_ERROR_TEXT("obj_usb_epopen needs 5 args"));
 return check(obj_usb_epopen(mp_obj_get_int(a[0]),mp_obj_get_int(a[1]),
  mp_obj_get_int(a[2]),mp_obj_get_int(a[3]),mp_obj_get_int(a[4])));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_objusbepopen_obj,5,5,p9_objusbepopen);

/* Native common message-device ABI.  Payloads retain their device format. */
static mp_obj_t p9_devicesopen(size_t n, const mp_obj_t *a){
 return check(obj_devices_open(n ? (unsigned long long)mp_obj_get_int(a[0]) : 0));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_devicesopen_obj,0,1,p9_devicesopen);

static mp_obj_t p9_devicelist(size_t n,const mp_obj_t *a){
 int h=mp_obj_get_int(a[0]);
 long cap=n>1?mp_obj_get_int(a[1]):OBJ_DEVICE_LISTMAX;
 long r,i;ObjDeviceEntry *e;mp_obj_t *rows,t[8],result;
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
  t[6]=mp_obj_new_int(e[i].flags);t[7]=mp_obj_new_int(e[i].reserved);
  rows[i]=mp_obj_new_tuple(8,t);
 }
 result=mp_obj_new_tuple(r,rows);
 m_del(mp_obj_t,rows,r?r:1);m_del(ObjDeviceEntry,e,cap);
 return result;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_devicelist_obj,1,2,p9_devicelist);

static mp_obj_t p9_deviceopen(size_t n,const mp_obj_t *a){
 return check(obj_device_open(mp_obj_get_int(a[0]),mp_obj_get_int(a[1]),
  mp_obj_get_int(a[2]),mp_obj_get_int(a[3]),n>4?(unsigned long long)mp_obj_get_int(a[4]):0));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_deviceopen_obj,4,5,p9_deviceopen);

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

static mp_obj_t p9_devicectl(mp_obj_t h,mp_obj_t op){
 return check(obj_device_ctl(mp_obj_get_int(h),mp_obj_get_int(op)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_devicectl_obj,p9_devicectl);

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

/* Legacy USBLIST path, kept as a thin compatibility adapter for tests. */
static mp_obj_t p9_usbopen(void){return check(usbopen());}
static MP_DEFINE_CONST_FUN_OBJ_0(p9_usbopen_obj,p9_usbopen);
static mp_obj_t p9_usblist(size_t n,const mp_obj_t *a){
 int h=mp_obj_get_int(a[0]);
 long max=n>1?mp_obj_get_int(a[1]):OBJ_USB_LISTMAX;
 long total,r,i;struct Usbentry *e;mp_obj_t *recs,t[2];
 if(max<0)mp_raise_ValueError(MP_ERROR_TEXT("USB list limit"));
 if(max>OBJ_USB_LISTMAX)max=OBJ_USB_LISTMAX;
 total=usblist(h,NULL,0);
 if(total<0)raise_err();
 if(max>total)max=total;
 e=m_new(struct Usbentry,max?max:1);
 r=max?usblist(h,e,max):0;
 if(r<0){m_del(struct Usbentry,e,max?max:1);raise_err();}
 recs=m_new(mp_obj_t,r?r:1);
 for(i=0;i<r;i++)recs[i]=usbentrytuple((ObjUsbEntry*)&e[i]);
 m_del(struct Usbentry,e,max?max:1);
 t[1]=mp_obj_new_tuple(r,recs);
 m_del(mp_obj_t,recs,r?r:1);
 t[0]=mp_obj_new_int(total);
 return mp_obj_new_tuple(2,t);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_usblist_obj,1,2,p9_usblist);
static mp_obj_t p9_usbepopen(size_t n,const mp_obj_t *a){
 if(n!=5)mp_raise_TypeError(MP_ERROR_TEXT("usb_epopen needs 5 args"));
 return check(usbepopen(mp_obj_get_int(a[0]),mp_obj_get_int(a[1]),
  mp_obj_get_int(a[2]),mp_obj_get_int(a[3]),mp_obj_get_int(a[4])));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_usbepopen_obj,5,5,p9_usbepopen);
static mp_obj_t p9_usbclose(mp_obj_t h){
 check(usbclose(mp_obj_get_int(h)));return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_usbclose_obj,p9_usbclose);

/* Pack the existing legacy USBIOCTL's bounded IN/OUT buffer.  This is
 * used for ordinary USB enumeration before opening a T12 session; it adds
 * no kernel operation, controller logic or alternative Handle table. */
extern long usbioctl(int, int, void*, long);
static mp_obj_t p9_usbioctl(mp_obj_t handle, mp_obj_t operation, mp_obj_t data){
 mp_buffer_info_t b;
 mp_get_buffer_raise(data,&b,MP_BUFFER_WRITE);
 if(b.len>4096)mp_raise_ValueError(MP_ERROR_TEXT("USB control buffer limit"));
 return check(usbioctl(mp_obj_get_int(handle),mp_obj_get_int(operation),b.buf,b.len));
}
static MP_DEFINE_CONST_FUN_OBJ_3(p9_usbioctl_obj,p9_usbioctl);

/* Historical native names are userland adapters over OBJOPEN/INVOKE/CLOSE;
 * they make no retired syscall. */
extern long sysraw(long, unsigned long long, unsigned long long,
 unsigned long long, unsigned long long, unsigned long long);
static mp_obj_t p9_uartcount(void){return check(obj_uart_count());}
static MP_DEFINE_CONST_FUN_OBJ_0(p9_uartcount_obj,p9_uartcount);
static mp_obj_t p9_uartopen(mp_obj_t port,mp_obj_t mode){
 return check(obj_uart_open(mp_obj_get_int(port),mp_obj_get_int(mode)));
}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_uartopen_obj,p9_uartopen);
static mp_obj_t p9_uartread(mp_obj_t h,mp_obj_t count){
 long n=mp_obj_get_int(count),r;vstr_t v;
 if(n<0||n>OBJ_STREAM_MAXIO)mp_raise_ValueError(MP_ERROR_TEXT("UART read limit"));
 vstr_init_len(&v,n);r=obj_read(mp_obj_get_int(h),v.buf,(unsigned long long)n,0);
 if(r<0){vstr_clear(&v);raise_err();}
 v.len=r;return mp_obj_new_bytes_from_vstr(&v);
}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_uartread_obj,p9_uartread);
static mp_obj_t p9_uartwrite(mp_obj_t h,mp_obj_t data){
 mp_buffer_info_t b;mp_get_buffer_raise(data,&b,MP_BUFFER_READ);
 return check(obj_write(mp_obj_get_int(h),b.buf,(unsigned long long)b.len,0));
}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_uartwrite_obj,p9_uartwrite);
static mp_obj_t p9_uartioctl(size_t n,const mp_obj_t *a){
 Uartcfg c;
 memset(&c,0,sizeof c);
 c.set=Uartbaud|Uartbits|Uartstop|Uartparity;
 c.baud=mp_obj_get_int(a[1]);c.bits=mp_obj_get_int(a[2]);
 c.stop=mp_obj_get_int(a[3]);
 c.parity=n>4?mp_obj_get_int(a[4]):Uartparitynone;
 return check(obj_uart_ctl(mp_obj_get_int(a[0]),&c));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_uartioctl_obj,4,5,p9_uartioctl);
static mp_obj_t p9_uartstatus(size_t n,const mp_obj_t *a){
 long sz=n>1?mp_obj_get_int(a[1]):4096,r;vstr_t v;
 if(sz<0||sz>OBJ_UART_STATMAX)mp_raise_ValueError(MP_ERROR_TEXT("UART status size"));
 vstr_init_len(&v,sz);r=obj_uart_status(mp_obj_get_int(a[0]),v.buf,(unsigned long long)sz);
 if(r<0){vstr_clear(&v);raise_err();}
 v.len=r;return mp_obj_new_bytes_from_vstr(&v);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_uartstatus_obj,1,2,p9_uartstatus);
static mp_obj_t p9_uartclose(mp_obj_t h){check(obj_close(mp_obj_get_int(h)));return mp_const_none;}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_uartclose_obj,p9_uartclose);
static mp_obj_t p9_kbdopen(void){return check(obj_kbd_open());}
static MP_DEFINE_CONST_FUN_OBJ_0(p9_kbdopen_obj,p9_kbdopen);
static mp_obj_t p9_kbdread(mp_obj_t h,mp_obj_t count){
 long n=mp_obj_get_int(count),r;vstr_t v;
 if(n<0||n>OBJ_KBD_MAXREAD)mp_raise_ValueError(MP_ERROR_TEXT("KBD read limit"));
 vstr_init_len(&v,n);r=obj_read(mp_obj_get_int(h),v.buf,(unsigned long long)n,0);
 if(r<0){vstr_clear(&v);raise_err();}
 v.len=r;return mp_obj_new_bytes_from_vstr(&v);
}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_kbdread_obj,p9_kbdread);
static mp_obj_t p9_kbdclose(mp_obj_t h){check(obj_close(mp_obj_get_int(h)));return mp_const_none;}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_kbdclose_obj,p9_kbdclose);
/* handle 0 is the eve-only global LED control; it does not claim KBD. */
static mp_obj_t p9_kbdleds(mp_obj_t leds){check(obj_kbd_leds(0,(unsigned int)mp_obj_get_int(leds)));return mp_const_none;}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_kbdleds_obj,p9_kbdleds);

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
    { MP_ROM_QSTR(MP_QSTR_pipenew), MP_ROM_PTR(&p9_pipenew_obj) },
    { MP_ROM_QSTR(MP_QSTR_piperead), MP_ROM_PTR(&p9_piperead_obj) },
    { MP_ROM_QSTR(MP_QSTR_pipewrite), MP_ROM_PTR(&p9_pipewrite_obj) },
    { MP_ROM_QSTR(MP_QSTR_pipeclose), MP_ROM_PTR(&p9_pipeclose_obj) },
    { MP_ROM_QSTR(MP_QSTR_pipeinherit), MP_ROM_PTR(&p9_pipeinherit_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&p9_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&p9_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&p9_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_seek), MP_ROM_PTR(&p9_seek_obj) },
    { MP_ROM_QSTR(MP_QSTR_alarm), MP_ROM_PTR(&p9_alarm_obj) },
    { MP_ROM_QSTR(MP_QSTR_dup), MP_ROM_PTR(&p9_dup_obj) },
    { MP_ROM_QSTR(MP_QSTR_pipe), MP_ROM_PTR(&p9_pipe_obj) },
    { MP_ROM_QSTR(MP_QSTR_wait), MP_ROM_PTR(&p9_wait_obj) },
    { MP_ROM_QSTR(MP_QSTR_rfork), MP_ROM_PTR(&p9_rfork_obj) },
    { MP_ROM_QSTR(MP_QSTR_envget), MP_ROM_PTR(&p9_envget_obj) },
    { MP_ROM_QSTR(MP_QSTR_sleep), MP_ROM_PTR(&p9_sleep_obj) },
    { MP_ROM_QSTR(MP_QSTR_time_ns), MP_ROM_PTR(&p9_time_ns_obj) },
    { MP_ROM_QSTR(MP_QSTR_errstr), MP_ROM_PTR(&p9_errstr_get_obj) },
    { MP_ROM_QSTR(MP_QSTR_werrstr), MP_ROM_PTR(&p9_werrstr_obj) },
    { MP_ROM_QSTR(MP_QSTR_errstr_exchange), MP_ROM_PTR(&p9_errstr_exchange_obj) },
    { MP_ROM_QSTR(MP_QSTR_getpid), MP_ROM_PTR(&p9_getpid_obj) },
    { MP_ROM_QSTR(MP_QSTR_exits), MP_ROM_PTR(&p9_exits_obj) },
    { MP_ROM_QSTR(MP_QSTR_system), MP_ROM_PTR(&p9_system_obj) },
    { MP_ROM_QSTR(MP_QSTR_tcp6_listen), MP_ROM_PTR(&p9_tcplisten_obj) },
    { MP_ROM_QSTR(MP_QSTR_tcp6_connect), MP_ROM_PTR(&p9_tcpconnect_obj) },
    { MP_ROM_QSTR(MP_QSTR_tcp6_accept), MP_ROM_PTR(&p9_tcpaccept_obj) },
    { MP_ROM_QSTR(MP_QSTR_tcp6_read), MP_ROM_PTR(&p9_tcpread_obj) },
    { MP_ROM_QSTR(MP_QSTR_tcp6_write), MP_ROM_PTR(&p9_tcpwrite_obj) },
    { MP_ROM_QSTR(MP_QSTR_tcp6_close), MP_ROM_PTR(&p9_tcpclose_obj) },
    { MP_ROM_QSTR(MP_QSTR_net6_addr), MP_ROM_PTR(&p9_net6addr_obj) },
    { MP_ROM_QSTR(MP_QSTR_net6_open), MP_ROM_PTR(&p9_net6open_obj) },
    { MP_ROM_QSTR(MP_QSTR_net6_recvfrom), MP_ROM_PTR(&p9_net6recvfrom_obj) },
    { MP_ROM_QSTR(MP_QSTR_net6_sendto), MP_ROM_PTR(&p9_net6sendto_obj) },
    { MP_ROM_QSTR(MP_QSTR_net6_close), MP_ROM_PTR(&p9_net6close_obj) },
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
    { MP_ROM_QSTR(MP_QSTR_vobjtcp_open), MP_ROM_PTR(&p9_vobjtcpopen_obj) },
    { MP_ROM_QSTR(MP_QSTR_vobjaccept), MP_ROM_PTR(&p9_vobjaccept_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_create), MP_ROM_PTR(&p9_objcreate_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_fifocreate), MP_ROM_PTR(&p9_objfifocreate_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_close), MP_ROM_PTR(&p9_objclose_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_open_handle), MP_ROM_PTR(&p9_objopenhandle_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_open_binding), MP_ROM_PTR(&p9_objopenbinding_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_open_tcp), MP_ROM_PTR(&p9_objopentcp_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_udp6_addr), MP_ROM_PTR(&p9_objudp6addr_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_open_udp6), MP_ROM_PTR(&p9_objopenudp6_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_udp6_sendto), MP_ROM_PTR(&p9_objudp6sendto_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_udp6_recvfrom), MP_ROM_PTR(&p9_objudp6recvfrom_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_pipe_create), MP_ROM_PTR(&p9_objpipecreate_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_pipe_peer), MP_ROM_PTR(&p9_objpipepeer_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_uart_open), MP_ROM_PTR(&p9_objuartopen_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_uart_ctl), MP_ROM_PTR(&p9_objuartctl_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_uart_status), MP_ROM_PTR(&p9_objuartstatus_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_kbd_open), MP_ROM_PTR(&p9_objkbdopen_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_kbd_leds), MP_ROM_PTR(&p9_objkbdleds_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_device_ether_info), MP_ROM_PTR(&p9_deviceetherinfo_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_devices_open), MP_ROM_PTR(&p9_devicesopen_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_device_list), MP_ROM_PTR(&p9_devicelist_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_device_open), MP_ROM_PTR(&p9_deviceopen_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_device_send), MP_ROM_PTR(&p9_devicesend_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_device_recv), MP_ROM_PTR(&p9_devicerecv_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_device_ctl), MP_ROM_PTR(&p9_devicectl_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_usb_open), MP_ROM_PTR(&p9_objusbopen_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_usb_list), MP_ROM_PTR(&p9_objusblist_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_usb_list_bytes), MP_ROM_PTR(&p9_objusblistbytes_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_usb_epopen), MP_ROM_PTR(&p9_objusbepopen_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_open), MP_ROM_PTR(&p9_usbopen_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_list), MP_ROM_PTR(&p9_usblist_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_epopen), MP_ROM_PTR(&p9_usbepopen_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_ioctl), MP_ROM_PTR(&p9_usbioctl_obj) },
    { MP_ROM_QSTR(MP_QSTR_usb_close), MP_ROM_PTR(&p9_usbclose_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_count), MP_ROM_PTR(&p9_uartcount_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_open), MP_ROM_PTR(&p9_uartopen_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_read), MP_ROM_PTR(&p9_uartread_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_write), MP_ROM_PTR(&p9_uartwrite_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_ioctl), MP_ROM_PTR(&p9_uartioctl_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_status), MP_ROM_PTR(&p9_uartstatus_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_close), MP_ROM_PTR(&p9_uartclose_obj) },
    { MP_ROM_QSTR(MP_QSTR_kbd_open), MP_ROM_PTR(&p9_kbdopen_obj) },
    { MP_ROM_QSTR(MP_QSTR_kbd_read), MP_ROM_PTR(&p9_kbdread_obj) },
    { MP_ROM_QSTR(MP_QSTR_kbd_close), MP_ROM_PTR(&p9_kbdclose_obj) },
    { MP_ROM_QSTR(MP_QSTR_kbd_leds), MP_ROM_PTR(&p9_kbdleds_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_open_ge), MP_ROM_PTR(&p9_objopenge_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_open_space), MP_ROM_PTR(&p9_objopenspace_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_create_ge), MP_ROM_PTR(&p9_objcreatege_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_open_ge_space), MP_ROM_PTR(&p9_objopengeospace_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_open_process_self), MP_ROM_PTR(&p9_objopenprocessself_obj) },
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
    { MP_ROM_QSTR(MP_QSTR_obj_accept), MP_ROM_PTR(&p9_objaccept_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_grant), MP_ROM_PTR(&p9_objgrant_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_revoke), MP_ROM_PTR(&p9_objrevoke_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_list), MP_ROM_PTR(&p9_objlist_obj) },
    { MP_ROM_QSTR(MP_QSTR_obj_invoke), MP_ROM_PTR(&p9_objinvoke_obj) },
    { MP_ROM_QSTR(MP_QSTR_notify_probe_addr), MP_ROM_PTR(&p9_notify_probe_addr_obj) },
    { MP_ROM_QSTR(MP_QSTR_notify_probe_count), MP_ROM_PTR(&p9_notify_probe_count_obj) },
    { MP_ROM_QSTR(MP_QSTR_notify_probe_reset), MP_ROM_PTR(&p9_notify_probe_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_notify_probe_lastnote), MP_ROM_PTR(&p9_notify_probe_lastnote_obj) },
    { MP_ROM_QSTR(MP_QSTR_notify_probe_framevals), MP_ROM_PTR(&p9_notify_probe_framevals_obj) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_ABI_VERSION), MP_ROM_INT(OBJ_ABI_VERSION) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OPEN_TCP_LISTEN), MP_ROM_INT(ObjOpenTcpListen) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OPEN_TCP_CONNECT), MP_ROM_INT(ObjOpenTcpConnect) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OPEN_GE), MP_ROM_INT(ObjOpenGe) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OPEN_HANDLE), MP_ROM_INT(ObjOpenHandle) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OPEN_BINDING), MP_ROM_INT(ObjOpenBinding) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OPEN_SPACE), MP_ROM_INT(ObjOpenSpace) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OPEN_GE_SPACE), MP_ROM_INT(ObjOpenGeSpace) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OPEN_PROCESS_SELF), MP_ROM_INT(ObjOpenProcessSelf) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_HEAP_BREAK), MP_ROM_INT(ObjOpHeapBreak) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_ERRSTR_EXCHANGE), MP_ROM_INT(ObjOpErrstrExchange) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_NOTIFY_REGISTER), MP_ROM_INT(ObjOpNotifyRegister) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_NOTED), MP_ROM_INT(ObjOpNoted) },
    { MP_ROM_QSTR(MP_QSTR_NCONT), MP_ROM_INT(NCONT) },
    { MP_ROM_QSTR(MP_QSTR_NDFLT), MP_ROM_INT(NDFLT) },
    { MP_ROM_QSTR(MP_QSTR_NSAVE), MP_ROM_INT(NSAVE) },
    { MP_ROM_QSTR(MP_QSTR_NRSTR), MP_ROM_INT(NRSTR) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OPEN_UDP6), MP_ROM_INT(ObjOpenUdp6) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_SENDTO), MP_ROM_INT(ObjOpSendTo) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_RECVFROM), MP_ROM_INT(ObjOpRecvFrom) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OPEN_UART), MP_ROM_INT(ObjOpenUart) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OPEN_KBD), MP_ROM_INT(ObjOpenKbd) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OPEN_USB), MP_ROM_INT(ObjOpenUsb) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_USB_LIST), MP_ROM_INT(ObjOpUsbList) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_USB_EP_OPEN), MP_ROM_INT(ObjOpUsbEpOpen) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_USB_LISTMAX), MP_ROM_INT(OBJ_USB_LISTMAX) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_USB_ENTRY_SIZE), MP_ROM_INT(OBJ_USB_ENTRY_SIZE) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_USB_EP_DATA), MP_ROM_INT(OBJ_USB_EP_DATA) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_USB_EP_CTL), MP_ROM_INT(OBJ_USB_EP_CTL) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_USB_MODE_READ), MP_ROM_INT(OBJ_USB_MODE_READ) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_USB_MODE_WRITE), MP_ROM_INT(OBJ_USB_MODE_WRITE) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_USB_MODE_RDWR), MP_ROM_INT(OBJ_USB_MODE_RDWR) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_KIND_PIPE), MP_ROM_INT(ObjKindPipe) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_PIPE_PEER), MP_ROM_INT(ObjOpPipePeer) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_PIPE_INHERIT), MP_ROM_INT(ObjOpPipeInherit) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_UART_COUNT), MP_ROM_INT(ObjOpUartCount) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_UART_CTL), MP_ROM_INT(ObjOpUartCtl) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_UART_STATUS), MP_ROM_INT(ObjOpUartStatus) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_KBD_LEDS), MP_ROM_INT(ObjOpKbdLeds) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_STREAM_MAXIO), MP_ROM_INT(OBJ_STREAM_MAXIO) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_KBD_MAXREAD), MP_ROM_INT(OBJ_KBD_MAXREAD) },
    { MP_ROM_QSTR(MP_QSTR_UART_BAUD), MP_ROM_INT(Uartbaud) },
    { MP_ROM_QSTR(MP_QSTR_UART_BITS), MP_ROM_INT(Uartbits) },
    { MP_ROM_QSTR(MP_QSTR_UART_STOP), MP_ROM_INT(Uartstop) },
    { MP_ROM_QSTR(MP_QSTR_UART_PARITY), MP_ROM_INT(Uartparity) },
    { MP_ROM_QSTR(MP_QSTR_UART_OREAD), MP_ROM_INT(UARTOREAD) },
    { MP_ROM_QSTR(MP_QSTR_UART_OWRITE), MP_ROM_INT(UARTOWRITE) },
    { MP_ROM_QSTR(MP_QSTR_UART_ORDWR), MP_ROM_INT(UARTORDWR) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_UDP6_MAXPAYLOAD), MP_ROM_INT(OBJ_UDP6_MAXPAYLOAD) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_KIND_GE), MP_ROM_INT(ObjKindGe) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_CREATE_PERSIST), MP_ROM_INT(ObjOpCreatePersist) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_COMMIT), MP_ROM_INT(ObjOpCommit) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_PUT_OID), MP_ROM_INT(ObjOpPutOid) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_INFO), MP_ROM_INT(ObjOpInfo) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_READ), MP_ROM_INT(ObjOpRead) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_WRITE), MP_ROM_INT(ObjOpWrite) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_STAT), MP_ROM_INT(ObjOpStat) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_SEND), MP_ROM_INT(ObjOpSend) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_RECV), MP_ROM_INT(ObjOpRecv) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_SUBSCRIBE), MP_ROM_INT(ObjOpSubscribe) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_ACCEPT), MP_ROM_INT(ObjOpAccept) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_GRANT), MP_ROM_INT(ObjOpGrant) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_REVOKE), MP_ROM_INT(ObjOpRevoke) },
    { MP_ROM_QSTR(MP_QSTR_OBJ_OP_LIST), MP_ROM_INT(ObjOpList) },
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
    { MP_ROM_QSTR(MP_QSTR_VKIND_TCP), MP_ROM_INT(VkindTcp) },
    { MP_ROM_QSTR(MP_QSTR_VKIND_GE), MP_ROM_INT(VkindGe) },
    { MP_ROM_QSTR(MP_QSTR_VKIND_SPACE), MP_ROM_INT(VkindSpace) },
    { MP_ROM_QSTR(MP_QSTR_VKIND_UDP6), MP_ROM_INT(VkindUdp6) },
    { MP_ROM_QSTR(MP_QSTR_VKIND_PIPE), MP_ROM_INT(VkindPipe) },
    { MP_ROM_QSTR(MP_QSTR_VKIND_UART), MP_ROM_INT(VkindUart) },
    { MP_ROM_QSTR(MP_QSTR_VKIND_KBD), MP_ROM_INT(VkindKbd) },
    { MP_ROM_QSTR(MP_QSTR_VKIND_USB), MP_ROM_INT(VkindUsb) },
    { MP_ROM_QSTR(MP_QSTR_VKIND_USB_EP), MP_ROM_INT(VkindUsbEp) },
    { MP_ROM_QSTR(MP_QSTR_VIO_READ), MP_ROM_INT(VioRead) },
    { MP_ROM_QSTR(MP_QSTR_VIO_WRITE), MP_ROM_INT(VioWrite) },
    { MP_ROM_QSTR(MP_QSTR_VOPEN_TCP_LISTEN), MP_ROM_INT(VopenTcpListen) },
    { MP_ROM_QSTR(MP_QSTR_VOPEN_TCP_CONNECT), MP_ROM_INT(VopenTcpConnect) },
    { MP_ROM_QSTR(MP_QSTR_VOPEN_GE), MP_ROM_INT(VopenGe) },
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

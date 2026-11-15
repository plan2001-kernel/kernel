/* File-service single-owner request dispatcher. No privileged syscall or
 * third-party runtime. Only the server sees UxService and its GE/O store.
 * The client has a per-request capability stream and per-session FD state.
 */
#include "uxrpc.h"

static void clear(void *p,unsigned int n)
{
    unsigned char *b=(unsigned char *)p;
    while(n--) *b++=0;
}
static void copy(void *dest,const void *src,unsigned int n)
{
    unsigned char *d=(unsigned char *)dest;
    const unsigned char *s=(const unsigned char *)src;
    while(n--) *d++=*s++;
}
static unsigned int size(const char *s)
{
    unsigned int n=0;
    if(!s) return PVD_PATH_MAX;
    while(n<PVD_PATH_MAX && s[n]) ++n;
    return n;
}
static int exact_write(UxPipe *p,const void *v,unsigned int n)
{
    const unsigned char *s=(const unsigned char *)v;
    unsigned int done=0;
    while(done<n) {
        long rc=uxpipe_write(p,s+done,n-done,-1);
        if(rc<=0 || (unsigned long)rc>n-done) return -1;
        done+=(unsigned int)rc;
    }
    return 0;
}
static int exact_read(UxPipe *p,void *v,unsigned int n)
{
    unsigned char *d=(unsigned char *)v;
    unsigned int done=0;
    while(done<n) {
        long rc=uxpipe_read(p,d+done,n-done,-1);
        if(rc<=0 || (unsigned long)rc>n-done) return -1;
        done+=(unsigned int)rc;
    }
    return 0;
}
static int permitted(UxRpcServer *s,const char *path)
{
    char canonical[PVD_PATH_MAX];
    unsigned int n=size(s->prefix);
    if(!path || path[0]!='/' || size(path)==PVD_PATH_MAX ||
       uxfs_path(&s->service->fs,path,canonical)!=PVD_OK) return 0;
    if(n==1 && s->prefix[0]=='/') return 1;
    if(n<2 || n>=PVD_PATH_MAX) return 0;
    for(unsigned int i=0;i<n;++i)
        if(canonical[i]!=s->prefix[i]) return 0;
    return canonical[n]==0 || canonical[n]=='/';
}
int uxrpc_server_init(UxRpcServer *s, UxService *service,
                      const char *prefix, const UxPipe *request,
                      const UxPipe *response)
{
    unsigned int n=size(prefix);
    if(!s || !service || !service->fs.view || !prefix ||
       prefix[0]!='/' || n>=PVD_PATH_MAX || !n ||
       !request || request->role!=UXPIPE_READER ||
       !response || response->role!=UXPIPE_WRITER)
        return PVD_EINVAL;
    clear(s,sizeof(*s));
    s->service=service;
    s->request=*request;
    s->response=*response;
    copy(s->prefix,prefix,n+1);
    uxfd_init(&s->fds,service);
    s->next_seq=1;
    return PVD_OK;
}
int uxrpc_server_once(UxRpcServer *s)
{
    UxRpcRequest q;
    UxRpcReply r;
    long value;
    unsigned int kind=0,len=0;
    int rc;
    if(!s || !s->service || exact_read(&s->request,&q,sizeof(q)))
        return -1;
    if(q.magic!=UXRPC_MAGIC || q.version!=UXRPC_VERSION ||
       q.seq!=s->next_seq || q.count>UXFS_DATA_MAX ||
       size(q.path)==PVD_PATH_MAX) return -1;
    clear(&r,sizeof(r));
    r.magic=UXRPC_MAGIC;
    r.version=UXRPC_VERSION;
    r.seq=q.seq;
    r.result=PVD_EINVAL;
    switch(q.op) {
    case UXRPC_OPEN:
        if(permitted(s,q.path))
            r.result=uxfd_open(&s->fds,q.path,q.flags);
        else r.result=PVD_EACCES;
        break;
    case UXRPC_READ:
        value=uxfd_read(&s->fds,(int)q.fd,r.data,q.count);
        r.result=value;
        break;
    case UXRPC_WRITE:
        r.result=uxfd_write(&s->fds,(int)q.fd,q.data,q.count);
        break;
    case UXRPC_CLOSE:
        r.result=uxfd_close(&s->fds,(int)q.fd);
        break;
    case UXRPC_SEEK:
        r.result=uxfd_seek(&s->fds,(int)q.fd,q.offset,q.whence);
        break;
    case UXRPC_STAT:
        if(!permitted(s,q.path)) {r.result=PVD_EACCES;break;}
        rc=uxfs_info(&s->service->fs,q.path,&kind,&len);
        r.result=rc;
        r.kind=kind; r.length=len;
        break;
    case UXRPC_UNLINK:
        r.result=permitted(s,q.path) ?
            uxfd_unlink(&s->fds,q.path) : PVD_EACCES;
        break;
    case UXRPC_EMIT:
        if(q.count>UXFS_DATA_MAX-s->output_size)
            r.result=PVD_ENOSPC;
        else {
            copy(s->output+s->output_size,q.data,q.count);
            s->output_size+=q.count;
            r.result=q.count;
        }
        break;
    case UXRPC_QUIT:
        r.result=PVD_OK;
        break;
    default:
        return -1; /* unexpected opcode is a protocol failure */
    }
    if(exact_write(&s->response,&r,sizeof(r))) return -1;
    if(++s->next_seq==0) return -1;
    if(q.op==UXRPC_QUIT) {
        (void)uxpipe_finish(&s->response);
        return 1;
    }
    return 0;
}
int uxrpc_client_init(UxRpcClient *c, const UxPipe *request,
                      const UxPipe *response)
{
    if(!c || !request || !response ||
       request->role!=UXPIPE_WRITER ||
       response->role!=UXPIPE_READER) return PVD_EINVAL;
    clear(c,sizeof(*c));
    c->request=*request; c->response=*response;
    c->next_seq=1;
    return PVD_OK;
}
int uxrpc_client_call(UxRpcClient *c,UxRpcRequest *q,UxRpcReply *r)
{
    if(!c || !q || !r || !c->next_seq) return PVD_EINVAL;
    q->magic=UXRPC_MAGIC; q->version=UXRPC_VERSION;
    q->seq=c->next_seq;
    if(exact_write(&c->request,q,sizeof(*q)) ||
       exact_read(&c->response,r,sizeof(*r)) ||
       r->magic!=UXRPC_MAGIC || r->version!=UXRPC_VERSION ||
       r->seq!=q->seq) return PVD_EIO;
    if(++c->next_seq==0) return PVD_EIO;
    return PVD_OK;
}
static int path_request(UxRpcRequest *q,const char *path)
{
    unsigned int n=size(path);
    if(n==0 || n>=PVD_PATH_MAX) return PVD_ENAMETOOLONG;
    copy(q->path,path,n+1);
    return PVD_OK;
}
int uxrpc_open(UxRpcClient *c,const char *path,unsigned int rights)
{
    UxRpcRequest q;
    UxRpcReply r;
    int rc;
    clear(&q,sizeof(q));q.op=UXRPC_OPEN; q.flags=rights;
    rc=path_request(&q,path);
    if(rc) return rc;
    return uxrpc_client_call(c,&q,&r) ? PVD_EIO : (int)r.result;
}
long uxrpc_read(UxRpcClient *c,int fd,void *buf,unsigned int n)
{
    UxRpcRequest q;
    UxRpcReply r;
    if(n>UXFS_DATA_MAX || (n&&!buf)) return PVD_EINVAL;
    clear(&q,sizeof(q));q.op=UXRPC_READ; q.fd=fd;q.count=n;
    if(uxrpc_client_call(c,&q,&r) || r.result>(long long)n)
        return PVD_EIO;
    if(r.result>0) copy(buf,r.data,(unsigned int)r.result);
    return (long)r.result;
}
long uxrpc_write(UxRpcClient *c,int fd,const void *buf,unsigned int n)
{
    UxRpcRequest q;
    UxRpcReply r;
    if(n>UXFS_DATA_MAX || (n&&!buf)) return PVD_EINVAL;
    clear(&q,sizeof(q));q.op=UXRPC_WRITE;q.fd=fd;q.count=n;
    if(n) copy(q.data,buf,n);
    if(uxrpc_client_call(c,&q,&r) || r.result>(long long)n)
        return PVD_EIO;
    return (long)r.result;
}
int uxrpc_close(UxRpcClient *c,int fd)
{
    UxRpcRequest q;UxRpcReply r;
    clear(&q,sizeof(q));q.op=UXRPC_CLOSE;q.fd=fd;
    return uxrpc_client_call(c,&q,&r)?PVD_EIO:(int)r.result;
}
long long uxrpc_seek(UxRpcClient *c,int fd,long long off,int whence)
{
    UxRpcRequest q;UxRpcReply r;
    clear(&q,sizeof(q));q.op=UXRPC_SEEK;q.fd=fd;
    q.offset=off;q.whence=whence;
    return uxrpc_client_call(c,&q,&r)?PVD_EIO:r.result;
}
int uxrpc_stat(UxRpcClient *c,const char *path,unsigned int *kind,
               unsigned int *length)
{
    UxRpcRequest q;UxRpcReply r;
    int rc;
    if(!kind || !length) return PVD_EINVAL;
    clear(&q,sizeof(q));q.op=UXRPC_STAT;
    rc=path_request(&q,path);
    if(rc) return rc;
    if(uxrpc_client_call(c,&q,&r)) return PVD_EIO;
    if(!r.result) {*kind=r.kind;*length=r.length;}
    return (int)r.result;
}
int uxrpc_unlink(UxRpcClient *c,const char *path)
{
    UxRpcRequest q;UxRpcReply r;
    int rc;
    clear(&q,sizeof(q));q.op=UXRPC_UNLINK;
    rc=path_request(&q,path);
    if(rc) return rc;
    return uxrpc_client_call(c,&q,&r)?PVD_EIO:(int)r.result;
}
int uxrpc_emit(UxRpcClient *c,const void *buf,unsigned int n)
{
    UxRpcRequest q;UxRpcReply r;
    if(n>UXFS_DATA_MAX || (n&&!buf)) return PVD_EINVAL;
    clear(&q,sizeof(q));q.op=UXRPC_EMIT;q.count=n;
    if(n) copy(q.data,buf,n);
    return uxrpc_client_call(c,&q,&r)?PVD_EIO:(int)r.result==(int)n?0:PVD_EIO;
}
int uxrpc_quit(UxRpcClient *c)
{
    UxRpcRequest q;UxRpcReply r;
    int rc;
    clear(&q,sizeof(q));q.op=UXRPC_QUIT;
    rc=uxrpc_client_call(c,&q,&r);
    if(rc) return rc;
    (void)uxpipe_finish(&c->request);
    (void)uxpipe_finish(&c->response);
    return (int)r.result;
}

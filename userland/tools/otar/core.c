#include "gefs.h"
extern void hostfmtinstall(void);
extern void **privalloc(void);
extern int fsync(int);
Proc *up=(Proc*)1;
int permissive,noneid,nogroupid,admid;
static Gefs *volume;
static void *tx;
void splhi(void) {sysfatal("host fault injection unavailable");}
void yield(void) {}
int rwakeupall(Rendez *r){USED(r);return 0;}
void rlock(RWLock *r){USED(r);} void runlock(RWLock*r){USED(r);}
void wlock(RWLock *r){USED(r);} void wunlock(RWLock*r){USED(r);}
int canwlock(RWLock*r){USED(r);return 1;} int canrlock(RWLock*r){USED(r);return 1;}
void *agetp(Aptr *a){return a->v;} void *aswapp(Aptr *a,void *p){void *v=a->v;a->v=p;return v;}
vlong aswapv(Avlong*a,vlong v){vlong o=a->v;a->v=v;return o;}
vlong aincv(Avlong*a,vlong v){return a->v+=v;}
int acasv(Avlong*a,vlong o,vlong n){if(a->v!=o)return 0;a->v=n;return 1;}
int acasp(Aptr*a,void*o,void*n){if(a->v!=o)return 0;a->v=n;return 1;}
void gflushdev(Gefs *fs){if(fs->oblkflush(fs->oblk)<0)error("host flush failed");}
static long diskio(void *arg,void *buf,long n,uvlong off,int wr)
{
 Gefs *fs=volume;int fd=(int)(uintptr)arg;
 if(off>(uvlong)fs->oblksize || (uvlong)n>(uvlong)fs->oblksize-off)error("host disk bounds");
 return wr?pwrite(fd,buf,n,off):pread(fd,buf,n,off);
}
static int diskflush(void *p){return fsync((int)(uintptr)p);}
int core_open(int fd,unsigned long long size,int write)
{
 errctx=(Errctx**)privalloc();*errctx=mallocz(sizeof(Errctx),1);hostfmtinstall();
 if(waserror()){fprint(2,"otar-ge: %s\n",errmsg());return -1;}
 Gefs *fs=emalloc(sizeof(Gefs),1);volume=fs;
 fs->snap.limbo.fs=fs;fs->snapdl.limbo.fs=fs;fs->dropdl.limbo.fs=fs;
 fs->dropdl.hd=Zb;fs->dropdl.tl=Zb;
 fs->lrurz.l=&fs->lrulk;fs->syncrz.l=&fs->synclk;fs->bfreerz.l=&fs->bfreelk;
 fs->cmax=8192;fs->dlcmax=512;
 fs->bcache=emalloc(fs->cmax*sizeof(Bucket),1);fs->dlcache=emalloc(fs->dlcmax*sizeof(Dlist*),1);
 for(unsigned i=0;i<fs->cmax;i++){
  Bfree *f=emalloc(sizeof(Bfree),1);f->bp=Zb;f->limbo.fs=fs;f->limbo.next=fs->bfree?&fs->bfree->limbo:nil;fs->bfree=f;
  Blk *b=emalloc(sizeof(Blk),1);b->bp=Zb;b->limbo.fs=fs;b->magic=Magic;lrutop(b);
 }
 fs->oblk=(void*)(uintptr)fd;fs->oblksize=size;fs->oblkrw=diskio;fs->oblkflush=diskflush;fs->rdonly.v=!write;
 loadge(fs,"host-fd");
 if(!(fs->flag&Ovolflag) || (fs->flag&Ovolvermask)!=Ovolvers)error("unsupported GE/O volume");
 fs->nsyncers=1;fs->syncq[0].fs=fs;
 for(int i=0;i<fs->narena;i++)fs->arenas[i].sync=&fs->syncq[0];
 tx=oobjopen(fs);poperror();return 0;
}
int core_get(unsigned long long oid,void **buf,unsigned long long *size)
{
 if(waserror()){fprint(2,"otar-ge: %s\n",errmsg());return -1;}
 long n=oobjget(tx,oid,nil,0,nil);if(n>=0 && (uvlong)n>64*MiB)error("object exceeds host transfer bound");if(n<0){poperror();return 1;}
 *buf=emalloc(n?n:1,0);*size=n;
 if(oobjget(tx,oid,*buf,n,nil)!=n)error("short object read");poperror();return 0;
}
int core_owner(unsigned long long oid,unsigned long long *owner)
{
 if(waserror()){fprint(2,"otar-ge: %s\n",errmsg());return -1;}
 int required;oobjownerget(tx,oid,owner,&required);poperror();return 0;
}
int core_create(unsigned long long owner,void *buf,unsigned long long size,unsigned long long *oid)
{
 if(waserror()){fprint(2,"otar-ge: %s\n",errmsg());return -1;}
 oobjcreate(tx,buf,size,oid,owner);poperror();return 0;
}
int core_put(unsigned long long oid,unsigned long long owner,void *buf,unsigned long long size)
{
 if(waserror()){fprint(2,"otar-ge: %s\n",errmsg());return -1;}
 oobjput(tx,oid,buf,size,owner);poperror();return 0;
}
int core_commit(void)
{
 if(waserror()){fprint(2,"otar-ge: %s\n",errmsg());return -1;}
 oobjcommit(tx);poperror();return 0;
}
char Eio[]="I/O error",Enomem[]="out of memory";
int canlock(Lock *l){if(l->val)return 0;l->val=1;return 1;}

/* F0 experimental flat on-disk file store. Linux-host benchmark, not a kernel FS. */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <inttypes.h>

#define SLOT 256
#define SLOTS 65536u
#define DATA_START (4096ULL + (uint64_t)SLOT*SLOTS)
#define HTSIZE 131072u
#define MAGIC 0x4630504152543031ULL /* F0PART01 */
#define FMAGIC 0x4630464f4f544552ULL /* F0FOOTER */
#define ACTIVE 1u
#define DELETED 2u
#define NAMELEN 208

typedef struct {
    uint64_t magic;
    uint32_t version;
    uint32_t clean;
    uint64_t data_start;
    uint64_t unused;
} Super;

typedef struct {
    uint64_t hash, pos, size, digest;
    uint32_t flags, name_len;
    char name[NAMELEN];
    uint64_t checksum;
} Slot;
_Static_assert(sizeof(Slot) == SLOT, "slot must be exactly 256 bytes");
typedef struct { uint64_t magic, hash, size, digest; } Footer;
_Static_assert(sizeof(Footer)==32,"footer size");
typedef struct {
    int fd, rw, dirty;
    uint64_t next_data;
    uint32_t used;
    Slot *slots;
    uint32_t *table;
    int invalid;
} Store;
static void die(const char *where) { perror(where); exit(2); }
static void fail(const char *s) { fprintf(stderr,"ERROR: %s\n",s); exit(2); }
static uint64_t hash64(const void *ptr, size_t len) {
    uint64_t h=UINT64_C(14695981039346656037);
    const unsigned char *p=ptr;
    for(size_t i=0;i<len;i++) { h^=p[i];h*=UINT64_C(1099511628211); }
    return h;
}
static uint64_t digest_update(uint64_t h,const void *ptr,size_t len){
    const unsigned char *p=ptr;
    for(size_t i=0;i<len;i++){h^=p[i];h*=UINT64_C(1099511628211);}
    return h;
}
static uint64_t slot_checksum(Slot s){s.checksum=0;return hash64(&s,sizeof s);}
static uint64_t round_up(uint64_t x,uint64_t n){return (x+n-1)&~(n-1);}
static void rd(int fd,void *dst,size_t n,uint64_t off){
    unsigned char *p=dst;
    while(n){ssize_t r=pread(fd,p,n,(off_t)off);if(r<0&&errno==EINTR)continue;if(r<=0)die("pread / short data");p+=r;off+=r;n-=r;}
}
static void wr(int fd,const void *src,size_t n,uint64_t off){
    const unsigned char *p=src;
    while(n){ssize_t r=pwrite(fd,p,n,(off_t)off);if(r<0&&errno==EINTR)continue;if(r<=0)die("pwrite");p+=r;off+=r;n-=r;}
}
static void index_insert(Store *s,uint32_t slot){
    Slot *r=&s->slots[slot];uint32_t k=(uint32_t)r->hash&(HTSIZE-1);
    for(unsigned tries=0;tries<HTSIZE;tries++,k=(k+1)&(HTSIZE-1)){
        uint32_t prev=s->table[k];
        if(!prev){s->table[k]=slot+1;return;}
        Slot *old=&s->slots[prev-1];
        if(old->hash==r->hash&&old->name_len==r->name_len&&memcmp(old->name,r->name,r->name_len)==0){s->table[k]=slot+1;return;}
    }
    fail("index full");
}
static Slot *find(Store *s,const char *name){
    size_t n=strlen(name);if(n>=NAMELEN) return NULL;
    uint64_t h=hash64(name,n);uint32_t k=(uint32_t)h&(HTSIZE-1);
    for(unsigned tries=0;tries<HTSIZE;tries++,k=(k+1)&(HTSIZE-1)){
        uint32_t v=s->table[k];if(!v)return NULL;
        Slot *r=&s->slots[v-1];if(r->hash==h&&r->name_len==n&&memcmp(r->name,name,n)==0)
            return r->flags==ACTIVE?r:NULL;
    }
    return NULL;
}
static void init_image(const char *filename){
    int fd=open(filename,O_CREAT|O_EXCL|O_RDWR|O_CLOEXEC,0600);if(fd<0)die("create image");
    Super b={.magic=MAGIC,.version=1,.clean=1,.data_start=DATA_START};
    wr(fd,&b,sizeof b,0);
    if(ftruncate(fd,DATA_START))die("truncate index");
    if(fsync(fd))die("fsync initialized volume");
    close(fd);
}
static Store *mount_image(const char *name,int rw){
    Store *s=calloc(1,sizeof *s);if(!s)die("calloc store");
    s->fd=open(name,rw?O_RDWR|O_CLOEXEC:O_RDONLY|O_CLOEXEC);if(s->fd<0)die("open image");s->rw=rw;
    Super b;rd(s->fd,&b,sizeof b,0);
    if(b.magic!=MAGIC||b.version!=1||b.data_start!=DATA_START)fail("unsupported or damaged F0 superblock");
    s->slots=calloc(SLOTS,sizeof(Slot));s->table=calloc(HTSIZE,sizeof(uint32_t));
    if(!s->slots||!s->table)die("alloc index");
    s->next_data=DATA_START;
    struct stat st;if(fstat(s->fd,&st))die("fstat image");
    for(uint32_t i=0;i<SLOTS;i++){
        Slot *r=&s->slots[i];rd(s->fd,r,sizeof *r,4096ULL+(uint64_t)i*SLOT);
        if(!r->checksum)break;
        if(r->checksum!=slot_checksum(*r)||r->name_len==0||r->name_len>=NAMELEN||
           (r->flags!=ACTIVE&&r->flags!=DELETED)){
           s->invalid++;break; // torn log tail
        }
        if(r->flags==ACTIVE){
            uint64_t end=r->pos+r->size+sizeof(Footer);
            if(r->pos<DATA_START||end<r->pos||end>(uint64_t)st.st_size){s->invalid++;break;}
            if(!b.clean){
                Footer ft;rd(s->fd,&ft,sizeof ft,r->pos+r->size);
                if(ft.magic!=FMAGIC||ft.size!=r->size||ft.hash!=r->hash||ft.digest!=r->digest){
                    s->invalid++;break; // discard torn tail and any following entries
                }
            }
            if(round_up(end,4096)>s->next_data)s->next_data=round_up(end,4096);
        }
        s->used=i+1;
        index_insert(s,i);
    }
    if(s->invalid&&rw){
        unsigned char zero[SLOT]={0};wr(s->fd,zero,sizeof zero,4096ULL+(uint64_t)s->used*SLOT);
    }
    return s;
}
static void finish(Store *s,int durable){
    if(durable&&s->rw){
        if(fdatasync(s->fd))die("sync image");
        Super b;rd(s->fd,&b,sizeof b,0);b.clean=1;wr(s->fd,&b,sizeof b,0);
        if(fsync(s->fd))die("mark clean");
    }
    close(s->fd);free(s->slots);free(s->table);free(s);
}
static void mark_dirty(Store *s){
    if(s->dirty)return;
    if(!s->rw)fail("read-only volume");
    Super b;rd(s->fd,&b,sizeof b,0);
    b.clean=0;wr(s->fd,&b,sizeof b,0);
    if(fsync(s->fd))die("dirty marker sync");
    s->dirty=1;
}
static void save(Store *s,const char *name,const unsigned char *data,size_t size){
    size_t n=strlen(name);if(n==0||n>=NAMELEN)fail("name length");
    if(s->used==SLOTS)fail("index log exhausted");
    mark_dirty(s);
    uint64_t pos=s->next_data;
    uint64_t h=hash64(name,n);
    uint64_t d=hash64(data,size);
    if(size)wr(s->fd,data,size,pos);
    Footer f={FMAGIC,h,size,d};wr(s->fd,&f,sizeof f,pos+size);
    s->next_data=round_up(pos+size+sizeof f,4096);
    Slot r={.hash=h,.pos=pos,.size=size,.digest=d,.flags=ACTIVE,.name_len=n};
    memcpy(r.name,name,n);r.checksum=slot_checksum(r);
    wr(s->fd,&r,sizeof r,4096ULL+(uint64_t)s->used*SLOT);
    s->slots[s->used]=r;index_insert(s,s->used);s->used++;
}
static uint64_t load(Store *s,const char *name, int verify, size_t *size_out){
    Slot *r=find(s,name);if(!r){fprintf(stderr,"missing F0 name: %s\n",name);exit(3);}
    unsigned char buf[65536];uint64_t left=r->size,off=r->pos;
    uint64_t d=UINT64_C(14695981039346656037);
    while(left){size_t n=left>sizeof buf?sizeof buf:left;rd(s->fd,buf,n,off);
      d=digest_update(d,buf,n);off+=n;left-=n;}
    if(verify&&d!=r->digest)fail("F0 content mismatch");
    *size_out=(size_t)r->size;return d;
}
static uint64_t read_ext(const char *path,size_t *size_out){
    int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)die(path);
    unsigned char b[65536];size_t total=0;uint64_t d=UINT64_C(14695981039346656037);
    for(;;){ssize_t n=read(fd,b,sizeof b);if(n<0&&errno==EINTR)continue;if(n<0)die("read ext4");if(n==0)break;total+=(size_t)n;d=digest_update(d,b,n);}
    close(fd);*size_out=total;return d;
}
static void write_ext(const char *path,const unsigned char *data,size_t n){
    int fd=open(path,O_CREAT|O_TRUNC|O_WRONLY|O_CLOEXEC,0600);if(fd<0)die(path);
    while(n){ssize_t k=write(fd,data,n);if(k<0&&errno==EINTR)continue;if(k<=0)die("write ext4");data+=k;n-=k;}
    if(close(fd))die("close output");
}
static double monotonic(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC_RAW,&t))die("clock");return t.tv_sec+t.tv_nsec*1e-9;}
static void proc_io(void){
    FILE *f=fopen("/proc/self/io","r");if(!f)return;
    char line[256];while(fgets(line,sizeof line,f))
      if(!strncmp(line,"read_bytes:",11)||!strncmp(line,"write_bytes:",12))fputs(line,stdout);
    fclose(f);
}
static void replay(const char *root,const char *image,const char *manifest,int f0){
    double start=monotonic();
    Store *s=NULL; if(f0)s=mount_image(image,1);
    FILE *m=fopen(manifest,"r");if(!m)die(manifest);
    uint64_t files=0,reads=0,writes=0,checksum=0,bytes_read=0,bytes_written=0;
    char line[1024],path[1024];
    while(fgets(line,sizeof line,m)){
        char op;char name[256];unsigned long long amount=0;
        int fields=sscanf(line,"%c %207s %llu",&op,name,&amount);
        if(fields<2||name[0]=='/'||strstr(name,".."))fail("manifest entry malformed");
        if(op=='R'){
            size_t n=0;uint64_t d;
            if(f0)d=load(s,name,1,&n);
            else{snprintf(path,sizeof path,"%s/%s",root,name);d=read_ext(path,&n);}
            reads++;files++;bytes_read+=n;checksum^=(d+(files*UINT64_C(0x9e3779b97f4a7c15)));
        } else if(op=='W'&&fields==3&&amount<=1048576){
            size_t n=(size_t)amount;
            unsigned char *buffer=malloc(n?n:1);if(!buffer)die("malloc output");
            uint64_t seed=hash64(name,strlen(name));for(size_t j=0;j<n;j++)buffer[j]=(unsigned char)((seed+j*131)>>((j%8)*8));
            if(f0)save(s,name,buffer,n);
            else{snprintf(path,sizeof path,"%s/%s",root,name);write_ext(path,buffer,n);}
            free(buffer);writes++;bytes_written+=n;
        } else fail("unknown manifest operation");
    }
    double elapsed=monotonic()-start;
    fclose(m);
    printf("backend=%s elapsed_ms=%.3f reads=%"PRIu64" writes=%"PRIu64" bytes_read=%"PRIu64" bytes_written=%"PRIu64" digest=%016"PRIx64"\n", f0?"f0":"ext4",elapsed*1000,reads,writes,bytes_read,bytes_written,checksum);
    proc_io();fflush(stdout);
    if(s)finish(s,0); // do not charge a durable close to one backend; recovery scans dirty footer
}
static void import_from_manifest(const char *root,const char *manifest,Store *s){
    FILE *m=fopen(manifest,"r");if(!m)die("manifest");
    char line[1024],name[208],path[1024];int count=0;
    while(fgets(line,sizeof line,m)){
       if(line[0]!='R')continue;
       if(sscanf(line,"R %207s",name)!=1)fail("manifest read");
       if(find(s,name))continue;
       snprintf(path,sizeof path,"%s/%s",root,name);
       int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)die(path);
       struct stat st;if(fstat(fd,&st))die("stat source");
       if(st.st_size<0||st.st_size>16*1024*1024)fail("unsupported source size");
       size_t n=(size_t)st.st_size;
       unsigned char *b=malloc(n?n:1);if(!b)die("malloc import");
       size_t done=0;while(done<n){ssize_t k=read(fd,b+done,n-done);if(k<0&&errno==EINTR)continue;if(k<=0)die("source read");done+=k;}
       close(fd);save(s,name,b,n);free(b);count++;
    }
    fclose(m);printf("imported=%d slots=%u\n",count,s->used);
}
int main(int argc,char **argv){
    if(argc==3&&!strcmp(argv[1],"init")){init_image(argv[2]);return 0;}
    if(argc==5&&!strcmp(argv[1],"import")){
        Store *s=mount_image(argv[3],1);import_from_manifest(argv[2],argv[4],s);finish(s,1);return 0;
    }
    if(argc==5&&!strcmp(argv[1],"verify")){
        Store *s=mount_image(argv[3],0);FILE *m=fopen(argv[4],"r");if(!m)die(argv[4]);
        char line[1024],name[208],path[1024];size_t checked=0;
        while(fgets(line,sizeof line,m)){
           if(line[0]!='R')continue;
           if(sscanf(line,"R %207s",name)!=1)fail("manifest");
           snprintf(path,sizeof path,"%s/%s",argv[2],name);
           size_t a,b;uint64_t x=load(s,name,1,&a),y=read_ext(path,&b);
           if(a!=b||x!=y)fail("verification mismatch");
           checked++;
        }
        fclose(m);printf("verified=%zu recovery_tail_errors=%d\n",checked,s->invalid);finish(s,0);return 0;
    }
    if(argc==6&&!strcmp(argv[1],"replay")){
        if(!strcmp(argv[2],"ext4"))replay(argv[3],argv[4],argv[5],0);
        else if(!strcmp(argv[2],"f0"))replay(argv[3],argv[4],argv[5],1);
        else fail("unknown backend");
        return 0;
    }
    if(argc==3&&!strcmp(argv[1],"damage-last-footer")){
        Store *s=mount_image(argv[2],1);
        if(!s->used)fail("no entries to damage");
        Slot *r=&s->slots[s->used-1];Footer f;rd(s->fd,&f,sizeof f,r->pos+r->size);
        f.magic=0;wr(s->fd,&f,sizeof f,r->pos+r->size);
        Super b;rd(s->fd,&b,sizeof b,0);b.clean=0;wr(s->fd,&b,sizeof b,0);
        if(fsync(s->fd))die("damage sync");
        finish(s,0);return 0;
    }
    if(argc==3&&!strcmp(argv[1],"info")){
        Store *s=mount_image(argv[2],0);printf("slots=%u next_data=%"PRIu64" invalid_tail=%d\n",s->used,s->next_data,s->invalid);
        finish(s,0);return 0;
    }
    fprintf(stderr,"usage: f0bench init IMAGE | import ROOT IMAGE MANIFEST | verify ROOT IMAGE MANIFEST | replay ext4|f0 ROOT IMAGE MANIFEST | info IMAGE\n");return 2;
}
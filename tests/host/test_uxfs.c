#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "uxfs.h"

#define MOCK_OBJECTS 40
#define MOCK_BYTES (2*1024*1024)
typedef struct {
    UxOid oid;
    unsigned int n;
    unsigned char data[MOCK_BYTES];
} MockObject;
typedef struct {
    MockObject committed[MOCK_OBJECTS], pending[MOCK_OBJECTS];
    unsigned int count, commits;
    int denied;
    UxOid unreadable;
    int reject_commit;
} MockDisk;
static MockDisk disk;
static Pvd view;
static UxFs fs;

static int create(void *ctx, UxOid *oid)
{
    MockDisk *d = ctx;
    assert(d->count < MOCK_OBJECTS);
    *oid = 100 + ++d->count;
    return 0;
}
static int put(void *ctx, UxOid oid, const void *data, unsigned int n)
{
    MockDisk *d = ctx;
    unsigned int i = (unsigned int)(oid-100);
    if (!oid || i >= MOCK_OBJECTS || n > MOCK_BYTES || d->denied)
        return -1;
    d->pending[i].oid = oid;
    d->pending[i].n = n;
    memcpy(d->pending[i].data, data, n);
    return 0;
}
static int get(void *ctx, UxOid oid, void *data,
               unsigned int capacity, unsigned int *size)
{
    MockDisk *d = ctx;
    unsigned int i = (unsigned int)(oid-100);
    if (!oid || i >= MOCK_OBJECTS ||
        d->committed[i].oid != oid || d->committed[i].n > capacity ||
        (d->denied || oid == d->unreadable)) return -1;
    *size = d->committed[i].n;
    memcpy(data, d->committed[i].data, *size);
    return 0;
}
static int commit(void *ctx)
{
    MockDisk *d = ctx;
    unsigned int i;
    if (d->denied || d->reject_commit) return -1;
    for (i = 0; i < MOCK_OBJECTS; ++i)
        if (d->pending[i].oid) d->committed[i] = d->pending[i];
    ++d->commits;
    return 0;
}
static char listing[512];
static unsigned int used;
static int emit(void *ctx, const void *name, unsigned int n)
{
    (void)ctx;
    assert(used+n+1 < sizeof(listing));
    memcpy(listing+used,name,n);
    used+=n;
    listing[used++]='\n';
    listing[used]=0;
    return 0;
}
int main(void)
{
    UxStore store = {&disk, create, put, get, commit};
    static unsigned char bytes[UXFS_DATA_MAX];
    unsigned int n;
    char path[PVD_PATH_MAX];
    static unsigned char oversized[UXFS_DATA_MAX+1];
    UxOid previous_oid;

    assert(pvd_bootstrap(&view) == 0);
    uxfs_init(&fs, &view);
    assert(uxfs_path(&fs,"/../../usr//bin/../.",path) == 0);
    assert(strcmp(path,"/usr")==0);
    assert(uxfs_path(&fs,"../../../../../",path)==0);
    assert(strcmp(path,"/")==0);
    memset(oversized, 'z', sizeof(oversized));
    assert(uxfs_write(&fs,"/tmp/oversize",oversized,sizeof(oversized),0)==PVD_ENOSPC);
    assert(uxfs_read(&fs,"/tmp/oversize",bytes,sizeof(bytes),&n)==PVD_ENOENT);
    assert(uxfs_mkdir(&fs, "/home/notes") == 0);
    assert(uxfs_mkdir(&fs, "/home/notes") == PVD_EEXIST);
    assert(uxfs_write(&fs, "/home/notes/a", "alpha", 5, 0) == 0);
    assert(uxfs_write(&fs, "/home/notes/a", "beta", 4, 1) == 0);
    assert(uxfs_copy(&fs, "/home/notes/a", "/home/notes/b") == 0);
    assert(uxfs_move(&fs, "/home/notes/b", "/home/notes/c") == 0);
    assert(uxfs_remove(&fs, "/home/notes/a") == 0);
    assert(uxfs_read(&fs, "/home/notes/c", bytes, sizeof(bytes), &n)==0);
    assert(n==9 && memcmp(bytes,"alphabeta",n)==0);
    assert(uxfs_read(&fs, "/home/notes/a", bytes, sizeof(bytes), &n)==PVD_ENOENT);
    assert(uxfs_remove(&fs, "/home/notes") == PVD_EINVAL);

    /* The trusted caller allocates the manifest OID through a volume grant.
     * A guessed OID or denied backend must not result in a fake success.
     */
    assert(create(&disk, &((UxOid){0})) == 0);
    assert(uxfs_save(&fs, &store, 101) == 0);
    assert(disk.commits == 1 && !fs.dirty);
    /* Empty inode capacity must not consume storage on every metadata update. */
    assert(disk.committed[1].n < 32u*1024u);
    previous_oid = fs.nodes[11].oid;
    disk.denied = 1;
    assert(uxfs_save(&fs, &store, 101) == PVD_OK);
    assert(disk.commits == 1); /* clean save is a no-op */
    disk.denied = 0;
    assert(uxfs_write(&fs, "/home/notes/c", "changed", 7, 0) == 0);
    assert(fs.dirty);
    disk.reject_commit = 1;
    assert(uxfs_save(&fs, &store, 101) == PVD_EIO);
    disk.reject_commit = 0;
    assert(fs.dirty);
    assert(uxfs_read(&fs, "/home/notes/c", bytes, sizeof(bytes), &n)==0);
    assert(n==7 && memcmp(bytes,"changed",n)==0);
    /* File identity must still be that of the prior committed snapshot. */
    assert(fs.nodes[11].oid == previous_oid);

    /* Cold restart loses fs RAM but reuses the same committed GE/O mock. */
    assert(pvd_bootstrap(&view)==0);
    uxfs_init(&fs, &view);
    assert(uxfs_read(&fs, "/home/notes/c",bytes,sizeof(bytes),&n)==PVD_ENOENT);
    assert(uxfs_restore(&fs,&store,101)==0);
    assert(uxfs_read(&fs,"/home/notes/c",bytes,sizeof(bytes),&n)==0);
    assert(n==9 && memcmp(bytes,"alphabeta",n)==0);
    assert(pvd_chdir(&view,"/home/notes")==0);
    disk.unreadable = previous_oid;
    assert(uxfs_restore(&fs,&store,101)==PVD_EIO);
    assert(pvd_getcwd(&view,path,sizeof(path))==PVD_OK);
    assert(strcmp(path,"/home/notes")==0);
    assert(uxfs_read(&fs, "c",bytes,sizeof(bytes),&n)==0);
    assert(n==9 && memcmp(bytes,"alphabeta",n)==0);
    disk.unreadable = 0;
    used=0;
    assert(uxfs_list(&fs,".",emit,0)==0);
    assert(strcmp(listing,"c\n")==0);
    assert(uxfs_restore(&fs,&store,500000) == PVD_EIO);
    disk.committed[1].data[16] ^= 0x80; /* corrupt manifest */
    assert(uxfs_restore(&fs,&store,101) == PVD_EIO);
    /* Persist an expanded inventory and a file larger than the old 1 KiB. */
    memset(&disk,0,sizeof disk);
    assert(pvd_bootstrap(&view)==0);uxfs_init(&fs,&view);
    for(unsigned i=0;i<128;i++){snprintf(path,sizeof path,"/tmp/persist%u",i);assert(uxfs_mkdir(&fs,path)==0);}
    memset(bytes,'p',128*1024);assert(uxfs_write(&fs,"/tmp/persist-data",bytes,128*1024,0)==0);
    assert(create(&disk,&((UxOid){0}))==0 && uxfs_save(&fs,&store,101)==0);
    assert(pvd_bootstrap(&view)==0);uxfs_init(&fs,&view);
    assert(uxfs_restore(&fs,&store,101)==0);
    assert(pvd_chdir(&view,"/tmp/persist127")==0);
    UxMeta before,after;
    assert(uxfs_metadata(&fs,"/tmp/persist-data",&before)==0);
    assert(!strcmp(before.name,"persist-data"));
    assert(uxfs_move(&fs,"/tmp/persist-data","/tmp/renamed-data")==0);
    assert(uxfs_save(&fs,&store,101)==0);
    assert(uxfs_restore(&fs,&store,101)==0);
    assert(uxfs_metadata(&fs,"/tmp/renamed-data",&after)==0 && !strcmp(after.name,before.name));
    assert(after.birth_ns==before.birth_ns && after.mode==before.mode);

    assert(uxfs_read(&fs,"/tmp/renamed-data",bytes,sizeof bytes,&n)==0 && n==128*1024);
    for(unsigned i=0;i<n;i++)assert(bytes[i]=='p');
    /* Directory deletion updates both inventories and reclaims PVD capacity.
     * Keep IDs of live siblings stable while holes are reused. */
    assert(pvd_bootstrap(&view)==0); uxfs_init(&fs,&view);
    unsigned baseline=view.count;
    assert(uxfs_mkdir(&fs,"/tmp/a")==0 && uxfs_mkdir(&fs,"/tmp/b")==0);
    unsigned high=view.count;
    assert(uxfs_rmdir(&fs,"/tmp/a")==0);
    assert(uxfs_mkdir(&fs,"/tmp/c")==0 && view.count==high);
    assert(pvd_chdir(&view,"/tmp/b")==0);
    assert(uxfs_rmdir(&fs,"/tmp/b")==PVD_EBUSY);
    assert(pvd_chdir(&view,"/")==0);
    assert(uxfs_rmdir(&fs,"/tmp/c")==0 && uxfs_rmdir(&fs,"/tmp/b")==0);
    assert(view.count==baseline);
    for(unsigned round=0;round<2*PVD_MAX_NODES;++round) {
        assert(uxfs_mkdir(&fs,"/tmp/reuse")==0);
        assert(uxfs_write(&fs,"/tmp/reuse/item","x",1,0)==0);
        assert(uxfs_rmdir(&fs,"/tmp/reuse")==PVD_ENOTEMPTY);
        assert(uxfs_rmdir(&fs,"/tmp/reuse/item")==PVD_ENOTDIR);
        assert(uxfs_remove(&fs,"/tmp/reuse/item")==0);
        assert(uxfs_rmdir(&fs,"/tmp/reuse")==0);
        assert(view.count==baseline);
        assert(pvd_chdir(&view,"/tmp/reuse")==PVD_ENOENT);
    }
    assert(uxfs_rmdir(&fs,"/")==PVD_EBUSY);
    assert(uxfs_rmdir(&fs,"/tmp")==PVD_EBUSY);
    assert(uxfs_rmdir(&fs,"/dev")==PVD_EBUSY);
    puts("PASS: directory removal, nonempty/cwd denial and repeated PVD slot reuse");
    assert(pvd_bootstrap(&view)==0);uxfs_init(&fs,&view);
    for(unsigned i=0;i<128;i++){
        snprintf(path,sizeof path,"/tmp/d%u",i);assert(uxfs_mkdir(&fs,path)==0);
        snprintf(path,sizeof path,"/tmp/f%u",i);assert(uxfs_write(&fs,path,"ok",2,0)==0);
    }
    memset(bytes,'x',256*1024);
    assert(uxfs_write(&fs,"/tmp/large",bytes,256*1024,0)==0);
    assert(uxfs_write(&fs,"/tmp/early","tail",4,0)==0);
    assert(uxfs_pwrite(&fs,"/tmp/f0","after-gap",9,8192,0)==9);
    assert(uxfs_read(&fs,"/tmp/early",bytes,sizeof bytes,&n)==0 && n==4 && !memcmp(bytes,"tail",4));
    assert(uxfs_remove(&fs,"/tmp/large")==0);
    assert(uxfs_read(&fs,"/tmp/early",bytes,sizeof bytes,&n)==0 && n==4 && !memcmp(bytes,"tail",4));
    assert(uxfs_read(&fs,"/tmp/f0",bytes,sizeof bytes,&n)==0 && n==8201);
    for(unsigned i=2;i<8192;i++)assert(bytes[i]==0);
    assert(!memcmp(bytes+8192,"after-gap",9));
    assert(pvd_bootstrap(&view)==0);uxfs_init(&fs,&view);
    memset(bytes,'z',sizeof bytes);
    assert(uxfs_write(&fs,"/tmp/full",bytes,sizeof bytes,0)==0);
    assert(uxfs_write(&fs,"/tmp/fail","x",1,0)==PVD_ENOSPC);
    unsigned kind;assert(uxfs_info(&fs,"/tmp/fail",&kind,&n)==PVD_ENOENT);
    assert(uxfs_write(&fs,"/tmp/full","x",1,1)==PVD_ENOSPC);
    assert(uxfs_copy(&fs,"/tmp/full","/tmp/copy-fail")==PVD_ENOSPC);
    assert(uxfs_info(&fs,"/tmp/copy-fail",&kind,&n)==PVD_ENOENT);
    assert(uxfs_remove(&fs,"/tmp/full")==0 && fs.used==0);
    assert(uxfs_write(&fs,"/tmp/reuse",bytes,sizeof bytes,0)==0);
    puts("PASS: 256 nodes, 256 KiB files, sparse writes, compaction, full arena rollback and reuse");
    puts("PASS: bounded files, GE/O OIDs, transactional manifest, restart, denial, corruption");
    return 0;
}

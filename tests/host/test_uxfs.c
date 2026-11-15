#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "uxfs.h"

#define MOCK_OBJECTS 40
#define MOCK_BYTES 20000
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
    unsigned char bytes[UXFS_DATA_MAX];
    unsigned int n;
    char path[PVD_PATH_MAX];
    unsigned char oversized[UXFS_DATA_MAX+1];
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
    puts("PASS: bounded files, GE/O OIDs, transactional manifest, restart, denial, corruption");
    return 0;
}

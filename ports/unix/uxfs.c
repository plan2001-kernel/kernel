/* Memory cache and explicit GE/O persistence transaction for Unix files.
 * The durable manifest is versioned and checksummed. Per-file data objects
 * use independent GE/O OIDs; metadata publishes only on final commit.
 * Files share a packed content arena; node metadata remains pointer-free.
 */
#include "uxfs.h"

static void zero(void *v, unsigned long n)
{
    unsigned char *p = (unsigned char *)v;
    while (n--) *p++ = 0;
}
static void bytes(void *v, const void *s, unsigned long n)
{
    unsigned char *p = (unsigned char *)v;
    const unsigned char *q = (const unsigned char *)s;
    while (n--) *p++ = *q++;
}
static void movebytes(void *dst,const void *src,unsigned long n)
{
    unsigned char *d=dst; const unsigned char *s=src;
    if(d>s){while(n){--n;d[n]=s[n];}}
    else {while(n--)*d++=*s++;}
}
static unsigned int length(const char *p)
{
    unsigned int n = 0;
    while (p[n] && n < PVD_PATH_MAX) ++n;
    return n;
}
static int same(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static int find(const UxFs *fs, const char *path)
{
    unsigned int i;
    for (i = 0; i < UXFS_NODES; ++i)
        if (fs->nodes[i].kind && same(fs->nodes[i].path, path))
            return (int)i;
    return -1;
}
static int free_node(const UxFs *fs)
{
    unsigned int i;
    for (i = 0; i < UXFS_NODES; ++i)
        if (!fs->nodes[i].kind) return (int)i;
    return -1;
}
/* Normalise against the selected PVD stack and confine .. to the view root.
 * Reject overflowing input BEFORE inspecting beyond PVD_PATH_MAX.
 */
int uxfs_path(const UxFs *fs, const char *path, char out[PVD_PATH_MAX])
{
    char source[PVD_PATH_MAX];
    unsigned int i = 0, used = 1, n, start, base = 0;
    if (!fs || !fs->view || !path || !*path || !out)
        return PVD_EINVAL;
    if (length(path) >= PVD_PATH_MAX) return PVD_ENAMETOOLONG;
    if (path[0] == '/') {
        n = length(path);
        bytes(source, path, n + 1);
    } else {
        if (pvd_getcwd(fs->view, source, sizeof(source)) != PVD_OK)
            return PVD_EINVAL;
        base = length(source);
        n = length(path);
        if (base + 1 + n >= PVD_PATH_MAX) return PVD_ENAMETOOLONG;
        if (base > 1) source[base++] = '/';
        bytes(source + base, path, n + 1);
    }
    out[0] = '/'; out[1] = 0;
    while (source[i]) {
        while (source[i] == '/') ++i;
        if (!source[i]) break;
        start = i;
        while (source[i] && source[i] != '/') ++i;
        n = i - start;
        if (n == 1 && source[start] == '.') continue;
        if (n == 2 && source[start] == '.' && source[start+1] == '.') {
            if (used > 1) {
                while (used > 1 && out[used-1] != '/') --used;
                if (used > 1) --used;
                out[used] = 0;
            }
            continue;
        }
        if (n > PVD_LABEL_MAX) return PVD_ENAMETOOLONG;
        if (used + (used > 1) + n >= PVD_PATH_MAX)
            return PVD_ENAMETOOLONG;
        if (used > 1) out[used++] = '/';
        bytes(out + used, source + start, n);
        used += n;
        out[used] = 0;
    }
    return PVD_OK;
}
static int parent_ok(const UxFs *fs, const char *path)
{
    char parent[PVD_PATH_MAX];
    unsigned int n = length(path);
    int id;
    if (n < 2 || n >= PVD_PATH_MAX) return PVD_EINVAL;
    bytes(parent, path, n+1);
    while (n > 1 && parent[n-1] != '/') --n;
    if (n == 1) parent[n] = 0;
    else parent[n-1] = 0;
    id = find(fs, parent);
    return id < 0 ? PVD_ENOENT :
           fs->nodes[id].kind == UXFS_DIR ? PVD_OK : PVD_EINVAL;
}
static int seed(UxFs *fs, const char *path)
{
    int id = free_node(fs);
    unsigned int n = length(path);
    if (id < 0 || n >= PVD_PATH_MAX) return PVD_ENOSPC;
    bytes(fs->nodes[id].path, path, n + 1);
    fs->nodes[id].kind = UXFS_DIR;
    return PVD_OK;
}
void uxfs_init(UxFs *fs, Pvd *view)
{
    static const char *const boot[] = {
        "/", "/bin", "/dev", "/etc", "/home", "/tmp",
        "/usr", "/usr/bin", "/var"
    };
    unsigned int i;
    if (!fs) return;
    zero(fs, sizeof(*fs));
    fs->view = view;
    for (i = 0; i < sizeof(boot)/sizeof(boot[0]); ++i)
        (void)seed(fs, boot[i]);
}
int uxfs_mkdir(UxFs *fs, const char *path)
{
    char resolved[PVD_PATH_MAX];
    int id, rc = uxfs_path(fs, path, resolved);
    if (rc) return rc;
    if (find(fs, resolved) >= 0) return PVD_EEXIST;
    rc = parent_ok(fs, resolved);
    if (rc) return rc;
    id = free_node(fs);
    if (id < 0) return PVD_ENOSPC;
    rc = pvd_mkdir_memory(fs->view, resolved);
    if (rc) return rc;
    bytes(fs->nodes[id].path, resolved, length(resolved)+1);
    fs->nodes[id].kind = UXFS_DIR;
    fs->dirty = 1;
    return PVD_OK;
}
int uxfs_rmdir(UxFs *fs,const char *path)
{
    char resolved[PVD_PATH_MAX];
    static const char *const anchors[]={
        "/","/bin","/dev","/etc","/home","/tmp","/usr","/usr/bin","/var"
    };
    unsigned i,n;
    int id,rc=uxfs_path(fs,path,resolved);
    if(rc) return rc;
    id=find(fs,resolved);
    if(id<0) return PVD_ENOENT;
    if(fs->nodes[id].kind!=UXFS_DIR) return PVD_ENOTDIR;
    /* These fixed view anchors are required by the v1 restore contract.
     * Treat them like mount points; deleting their children remains allowed. */
    for(i=0;i<sizeof(anchors)/sizeof(anchors[0]);++i)
        if(same(resolved,anchors[i])) return PVD_EBUSY;
    n=length(resolved);
    if(n==1) return PVD_EBUSY;
    for(i=0;i<UXFS_NODES;++i) {
        unsigned j;
        if(!fs->nodes[i].kind || i==(unsigned)id) continue;
        for(j=0;j<n && fs->nodes[i].path[j]==resolved[j];++j) {}
        if(j==n && fs->nodes[i].path[n]=='/') return PVD_ENOTEMPTY;
    }
    rc=pvd_rmdir_memory(fs->view,resolved);
    if(rc) return rc;
    zero(&fs->nodes[id],sizeof(fs->nodes[id]));
    fs->modified[id]=0;
    fs->dirty=1;
    return PVD_OK;
}
/* Resize atomically after checking capacity. Offsets, never pointers, cross
 * the process snapshot boundary. memmove permits overlapping arena tails. */
static int resize(UxFs *fs,unsigned id,unsigned size)
{
    unsigned old=fs->nodes[id].length, off=fs->offsets[id], i;
    if(size>old && size-old>UXFS_DATA_MAX-fs->used) return PVD_ENOSPC;
    movebytes(fs->data+off+size,fs->data+off+old,fs->used-off-old);
    for(i=0;i<UXFS_NODES;i++)
        if(i!=id && fs->nodes[i].kind==UXFS_FILE &&
           (fs->offsets[i]>off || (fs->offsets[i]==off && old==0)))
            fs->offsets[i]=fs->offsets[i]-old+size;
    if(size>old) zero(fs->data+off+old,size-old);
    fs->used=fs->used-old+size;
    fs->nodes[id].length=size;
    return 0;
}
void uxfs_snapshot(UxFs *dst,const UxFs *src)
{
    bytes(dst,src,__builtin_offsetof(UxFs,data));
    bytes(dst->data,src->data,src->used);
}
long uxfs_pwrite(UxFs *fs,const char *path,const void *buf,unsigned count,
                 unsigned offset,int truncate)
{
    char resolved[PVD_PATH_MAX];
    int id,rc=uxfs_path(fs,path,resolved),fresh=0;
    unsigned target;
    if(rc)return rc;
    if(count && !buf)return PVD_EINVAL;
    if(offset>UXFS_DATA_MAX || count>UXFS_DATA_MAX-offset)return PVD_ENOSPC;
    id=find(fs,resolved);
    if(id<0){
        if((rc=parent_ok(fs,resolved)))return rc;
        id=free_node(fs);if(id<0)return PVD_ENOSPC;
        fresh=1;
    }else if(fs->nodes[id].kind!=UXFS_FILE)return PVD_EINVAL;
    target=offset+count;
    if(!truncate && target<fs->nodes[id].length)target=fs->nodes[id].length;
    if(target>fs->nodes[id].length && target-fs->nodes[id].length>UXFS_DATA_MAX-fs->used)
        return PVD_ENOSPC;
    if(fresh){
        fs->offsets[id]=fs->used;
        fs->nodes[id].kind=UXFS_FILE;
        bytes(fs->nodes[id].path,resolved,length(resolved)+1);
    }
    rc=resize(fs,(unsigned)id,target);if(rc)return rc;
    if(count)bytes(fs->data+fs->offsets[id]+offset,buf,count);
    fs->modified[id]=1;fs->dirty=1;
    return count;
}
int uxfs_write(UxFs *fs,const char *path,const void *buf,unsigned size,int append)
{
    unsigned offset=0,kind;
    int rc;
    if(append){rc=uxfs_info(fs,path,&kind,&offset);if(rc && rc!=PVD_ENOENT)return rc;}
    long result=uxfs_pwrite(fs,path,buf,size,offset,!append);
    return result<0 ? (int)result : 0;
}
long uxfs_pread(UxFs *fs,const char *path,void *buf,unsigned count,unsigned offset)
{
    char resolved[PVD_PATH_MAX];int id,rc=uxfs_path(fs,path,resolved);
    if(rc)return rc;
    if(count && !buf)return PVD_EINVAL;
    id=find(fs,resolved);if(id<0)return PVD_ENOENT;
    if(fs->nodes[id].kind!=UXFS_FILE)return PVD_EINVAL;
    if(offset>=fs->nodes[id].length)return 0;
    if(count>fs->nodes[id].length-offset)count=fs->nodes[id].length-offset;
    if(count)bytes(buf,fs->data+fs->offsets[id]+offset,count);
    return count;
}
int uxfs_read(UxFs *fs,const char *path,void *buf,unsigned capacity,unsigned *size)
{
    unsigned kind,n;int rc=uxfs_info(fs,path,&kind,&n);
    if(rc)return rc;
    if(!size || kind!=UXFS_FILE)return PVD_EINVAL;
    if(capacity<n)return PVD_ENOSPC;
    long got=uxfs_pread(fs,path,buf,n,0);
    if(got<0)return (int)got;
    *size=(unsigned)got;return 0;
}
int uxfs_info(UxFs *fs, const char *path,
              unsigned int *kind, unsigned int *size)
{
    char resolved[PVD_PATH_MAX];
    int id, rc;
    if(!fs || !kind || !size) return PVD_EINVAL;
    rc=uxfs_path(fs,path,resolved);
    if(rc) return rc;
    id=find(fs,resolved);
    if(id<0) return PVD_ENOENT;
    *kind=fs->nodes[id].kind;
    *size=fs->nodes[id].length;
    return PVD_OK;
}
int uxfs_copy(UxFs *fs,const char *src,const char *dest)
{
    unsigned char chunk[UXFS_IO_CHUNK];unsigned off=0;char a[PVD_PATH_MAX],b[PVD_PATH_MAX];
    int rc=uxfs_path(fs,src,a);if(rc)return rc;
    rc=uxfs_path(fs,dest,b);if(rc)return rc;
    unsigned kind,size;rc=uxfs_info(fs,a,&kind,&size);if(rc)return rc;
    if(kind!=UXFS_FILE)return PVD_EINVAL;
    if(same(a,b))return 0;
    int destid=find(fs,b);
    unsigned reclaim=destid>=0 && fs->nodes[destid].kind==UXFS_FILE ? fs->nodes[destid].length : 0;
    if(size>UXFS_DATA_MAX-fs->used+reclaim)return PVD_ENOSPC;
    rc=uxfs_write(fs,b,0,0,0);if(rc)return rc;
    while(off<size){long n=uxfs_pread(fs,a,chunk,sizeof(chunk),off);if(n<=0)return PVD_EIO;
        long put=uxfs_pwrite(fs,b,chunk,(unsigned)n,off,0);if(put<0)return (int)put;off+=(unsigned)n;}
    return 0;
}
int uxfs_remove(UxFs *fs, const char *path)
{
    char resolved[PVD_PATH_MAX];
    int id, rc = uxfs_path(fs, path, resolved);
    if (rc) return rc;
    id = find(fs, resolved);
    if (id < 0) return PVD_ENOENT;
    if (fs->nodes[id].kind != UXFS_FILE) return PVD_EINVAL;
    (void)resize(fs,(unsigned)id,0);
    zero(&fs->nodes[id], sizeof(UxNode));
    fs->offsets[id]=0;
    fs->modified[id] = 0;
    fs->dirty = 1;
    return PVD_OK;
}
int uxfs_move(UxFs *fs, const char *src, const char *dest)
{
    char a[PVD_PATH_MAX], b[PVD_PATH_MAX];
    int id, other, rc = uxfs_path(fs, src, a);
    if (rc) return rc;
    rc = uxfs_path(fs, dest, b);
    if (rc) return rc;
    id = find(fs, a);
    if (id < 0) return PVD_ENOENT;
    if (fs->nodes[id].kind != UXFS_FILE) return PVD_EINVAL;
    if (same(a, b)) return PVD_OK;
    rc = parent_ok(fs, b);
    if (rc) return rc;
    other = find(fs, b);
    if (other >= 0 && fs->nodes[other].kind != UXFS_FILE)
        return PVD_EINVAL;
    if (other >= 0) {
        (void)resize(fs,(unsigned)other,0);
        zero(&fs->nodes[other], sizeof(UxNode));
        fs->offsets[other]=0;
        fs->modified[other] = 0;
    }
    bytes(fs->nodes[id].path, b, length(b)+1);
    fs->dirty = 1;
    return PVD_OK;
}
int uxfs_list(UxFs *fs, const char *path, UxEmit emit, void *ctx)
{
    char base[PVD_PATH_MAX];
    unsigned int i, n, tail;
    int id, rc = uxfs_path(fs, path && *path ? path : ".", base);
    if (rc) return rc;
    id = find(fs, base);
    if (id < 0) return PVD_ENOENT;
    if (fs->nodes[id].kind == UXFS_FILE)
        return emit(ctx, base, length(base));
    n = length(base);
    for (i = 0; i < UXFS_NODES; ++i) {
        const char *p = fs->nodes[i].path;
        if (!fs->nodes[i].kind || (int)i == id) continue;
        if (n == 1) {
            if (p[0] != '/' || !p[1]) continue;
            tail = 1;
        } else {
            unsigned int j;
            for (j = 0; j < n && p[j] == base[j]; ++j) {}
            if (j != n || p[n] != '/') continue;
            tail = n + 1;
        }
        for (unsigned int j = tail; p[j]; ++j)
            if (p[j] == '/') { tail = 0; break; }
        if (tail && emit(ctx, p+tail, length(p+tail)))
            return PVD_EIO;
    }
    return PVD_OK;
}

/* Disk metadata is a stable record with fixed-size entries. Never deserialize
 * Handles or rights: reopening every OID requires a valid volume capability.
 */
typedef struct {
    UxOid magic;
    unsigned int version;
    unsigned int reserved;
    UxNode nodes[UXFS_NODES];
    UxOid checksum;
} UxManifest;
static UxOid hash(const void *data, unsigned long n)
{
    const unsigned char *p = (const unsigned char *)data;
    UxOid h = 14695981039346656037ULL;
    while (n--) { h ^= *p++; h *= 1099511628211ULL; }
    return h;
}
/* Copy-on-write persistence: never replace the object named by a published
 * manifest until its successor has committed. GE/O create may commit an
 * orphan OID early; orphan reclamation is separate from publish atomicity.
 * The caller's dirty flags and OIDs change only after final commit.
 */
int uxfs_save(UxFs *fs, const UxStore *s, UxOid manifest)
{
    static UxManifest disk;
    unsigned int i, j;
    if (!fs || !s || !manifest || !s->create || !s->put || !s->commit)
        return PVD_EINVAL;
    if (!fs->dirty) return PVD_OK;
    zero(&disk, sizeof(disk));
    disk.magic = UXFS_MAGIC;
    disk.version = UXFS_VERSION;
    bytes(disk.nodes, fs->nodes, sizeof(disk.nodes));
    for (i = 0; i < UXFS_NODES; ++i) {
        UxNode *n = &disk.nodes[i];
        UxOid fresh = 0;
        if (n->kind != UXFS_FILE) continue;
        if (!n->oid || fs->modified[i]) {
            if (s->create(s->context, &fresh) || !fresh || fresh == manifest)
                return PVD_EIO;
            /* OIDs are identity only. Reject backend allocation collisions
             * before staging writes to another file or the manifest. */
            for (j = 0; j < UXFS_NODES; ++j)
                if (j != i && disk.nodes[j].kind == UXFS_FILE &&
                    disk.nodes[j].oid == fresh)
                    return PVD_EIO;
            n->oid = fresh;
            if (s->put(s->context, fresh, fs->data+fs->offsets[i], n->length))
                return PVD_EIO;
        }
    }
    disk.checksum = hash(&disk, sizeof(disk)-sizeof(disk.checksum));
    if (s->put(s->context, manifest, &disk, sizeof(disk)) ||
        s->commit(s->context))
        return PVD_EIO;
    for (i = 0; i < UXFS_NODES; ++i) {
        fs->nodes[i].oid = disk.nodes[i].oid;
        fs->modified[i] = 0;
    }
    fs->dirty = 0;
    return PVD_OK;
}

/* Deserialization is failure-atomic w.r.t. the visible PVD and file view.
 * Stage every directory and file independently; only publish after all
 * metadata, parent links and explicitly authorized GE/O reads validate.
 * Scratch space is static to avoid an >80 KiB userspace stack frame.
 * The initial F5 owner is single threaded; this function is NOT reentrant.
 */
int uxfs_restore(UxFs *fs, const UxStore *s, UxOid manifest)
{
    static UxFs staged;
    static Pvd staged_view;
    static const char *const boot[] = {
        "/", "/bin", "/dev", "/etc", "/home", "/tmp",
        "/usr", "/usr/bin", "/var"
    };
    static UxManifest disk;
    char canonical[PVD_PATH_MAX];
    unsigned int n, i, j, round, built;
    int rc;
    if (!fs || !fs->view || !s || !s->get || !manifest)
        return PVD_EINVAL;
    /* Version 1 held 64 fixed metadata records. Its payload checksum must
     * be verified before zero-extending into the current metadata table. */
    zero(&disk,sizeof(disk));
    if(s->get(s->context,manifest,&disk,sizeof(disk),&n) || n<24 ||
       disk.magic!=UXFS_MAGIC || disk.reserved!=0)return PVD_EIO;
    if(disk.version==1){
        UxOid stored;unsigned legacy=16+64*sizeof(UxNode)+sizeof(UxOid);
        if(n!=legacy)return PVD_EIO;
        bytes(&stored,(unsigned char*)&disk+n-sizeof(UxOid),sizeof stored);
        if(stored!=hash(&disk,n-sizeof(UxOid)))return PVD_EIO;
        zero((unsigned char*)&disk+16+64*sizeof(UxNode),sizeof(disk)-16-64*sizeof(UxNode));
    }else if(disk.version!=UXFS_VERSION || n!=sizeof(disk) ||
       disk.checksum!=hash(&disk,sizeof(disk)-sizeof(disk.checksum)))return PVD_EIO;
    if (disk.nodes[0].kind != UXFS_DIR ||
        !same(disk.nodes[0].path, "/")) return PVD_EIO;
    if (pvd_bootstrap(&staged_view) != PVD_OK) return PVD_EIO;
    uxfs_init(&staged, &staged_view);
    bytes(staged.nodes, disk.nodes, sizeof(disk.nodes));
    for (i = 0; i < UXFS_NODES; ++i) {
        UxNode *p = &staged.nodes[i];
        if (!p->kind) continue;
        if (p->kind != UXFS_DIR && p->kind != UXFS_FILE) return PVD_EIO;
        if (length(p->path) >= PVD_PATH_MAX || p->path[0] != '/' ||
            uxfs_path(&staged, p->path, canonical) != PVD_OK ||
            !same(p->path, canonical)) return PVD_EIO;
        if (p->kind == UXFS_DIR) {
            if (p->oid || p->length) return PVD_EIO;
        } else if (p->length > UXFS_DATA_MAX || !p->oid ||
                   p->oid == manifest) return PVD_EIO;
        if (i && !same(p->path, "/") &&
            parent_ok(&staged, p->path) != PVD_OK) return PVD_EIO;
        for (j = 0; j < i; ++j) {
            if (staged.nodes[j].kind &&
                same(p->path, staged.nodes[j].path)) return PVD_EAMBIG;
            if (p->kind == UXFS_FILE &&
                staged.nodes[j].kind == UXFS_FILE &&
                p->oid == staged.nodes[j].oid) return PVD_EAMBIG;
        }
    }
    /* Preserve the fixed Unix init directories, including /usr/bin. */
    for (i = 0; i < sizeof(boot)/sizeof(boot[0]); ++i)
        if ((rc = find(&staged, boot[i])) < 0 ||
            staged.nodes[rc].kind != UXFS_DIR)
            return PVD_EIO;
    /* The serialized directory order is arbitrary, so build parents first.
     * No partial PVD reaches the caller on failure.
     */
    for (round = 0; round < PVD_MAX_DEPTH; ++round) {
        built = 0;
        for (i = 1; i < UXFS_NODES; ++i) {
            UxNode *p = &staged.nodes[i];
            if (p->kind != UXFS_DIR) continue;
            rc = pvd_mkdir_memory(&staged_view, p->path);
            if (rc == PVD_OK) ++built;
            else if (rc != PVD_EEXIST && rc != PVD_ENOENT)
                return PVD_EIO;
        }
        if (!built) break;
    }
    for (i = 0; i < UXFS_NODES; ++i) {
        UxNode *p = &staged.nodes[i];
        if (p->kind == UXFS_DIR) {
            /* Directory must be reachable through PVD, not merely named
             * in untrusted metadata. chdir on scratch provides validation.
             */
            if (pvd_chdir(&staged_view, p->path) != PVD_OK)
                return PVD_EIO;
            if (pvd_chdir(&staged_view, "/") != PVD_OK)
                return PVD_EIO;
        } else if (p->kind == UXFS_FILE) {
            if(p->length>UXFS_DATA_MAX-staged.used)return PVD_EIO;
            staged.offsets[i]=staged.used;
            if (s->get(s->context, p->oid, staged.data+staged.used,
                       p->length, &n) || n != p->length)
                return PVD_EIO;
            staged.used+=n;
        }
    }
    Pvd *original_view=fs->view;
    uxfs_snapshot(fs,&staged);
    fs->view=original_view;
    zero(fs->modified, sizeof(fs->modified));
    *fs->view = staged_view;
    fs->dirty = 0;
    return PVD_OK;
}

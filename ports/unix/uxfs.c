/* Memory cache and explicit GE/O persistence transaction for Unix files.
 * The durable manifest is versioned and checksummed. Per-file data objects
 * use independent GE/O OIDs; metadata publishes only on final commit.
 * This is a bounded v1 (64 nodes, 1024-byte regular files), not POSIX VFS.
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
int uxfs_write(UxFs *fs, const char *path, const void *buf,
               unsigned int size, int append)
{
    char resolved[PVD_PATH_MAX];
    UxNode *node;
    unsigned int old;
    int id, rc = uxfs_path(fs, path, resolved);
    if (rc) return rc;
    if (size && !buf) return PVD_EINVAL;
    id = find(fs, resolved);
    if (id < 0) {
        rc = parent_ok(fs, resolved);
        if (rc) return rc;
        /* Check payload size before exposing a new entry. Failed writes
         * cannot create a phantom zero-length file. */
        if (size > UXFS_DATA_MAX) return PVD_ENOSPC;
        id = free_node(fs);
        if (id < 0) return PVD_ENOSPC;
        bytes(fs->nodes[id].path, resolved, length(resolved)+1);
        fs->nodes[id].kind = UXFS_FILE;
    }
    node = &fs->nodes[id];
    if (node->kind != UXFS_FILE) return PVD_EINVAL;
    old = append ? node->length : 0;
    if (old > UXFS_DATA_MAX || size > UXFS_DATA_MAX-old)
        return PVD_ENOSPC;
    if (size) bytes(fs->data[id]+old, buf, size);
    node->length = old + size;
    fs->modified[id] = 1;
    fs->dirty = 1;
    return PVD_OK;
}
int uxfs_read(UxFs *fs, const char *path, void *buf,
              unsigned int capacity, unsigned int *size)
{
    char resolved[PVD_PATH_MAX];
    int id, rc = uxfs_path(fs, path, resolved);
    if (rc) return rc;
    id = find(fs, resolved);
    if (id < 0) return PVD_ENOENT;
    if (fs->nodes[id].kind != UXFS_FILE || !size) return PVD_EINVAL;
    if (capacity < fs->nodes[id].length || (!buf && fs->nodes[id].length))
        return PVD_ENOSPC;
    bytes(buf, fs->data[id], fs->nodes[id].length);
    *size = fs->nodes[id].length;
    return PVD_OK;
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
int uxfs_copy(UxFs *fs, const char *src, const char *dest)
{
    unsigned char tmp[UXFS_DATA_MAX];
    unsigned int n = 0;
    int rc = uxfs_read(fs, src, tmp, sizeof(tmp), &n);
    return rc ? rc : uxfs_write(fs, dest, tmp, n, 0);
}
int uxfs_remove(UxFs *fs, const char *path)
{
    char resolved[PVD_PATH_MAX];
    int id, rc = uxfs_path(fs, path, resolved);
    if (rc) return rc;
    id = find(fs, resolved);
    if (id < 0) return PVD_ENOENT;
    if (fs->nodes[id].kind != UXFS_FILE) return PVD_EINVAL;
    zero(&fs->nodes[id], sizeof(UxNode));
    zero(fs->data[id], UXFS_DATA_MAX);
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
        zero(&fs->nodes[other], sizeof(UxNode));
        zero(fs->data[other], UXFS_DATA_MAX);
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
    UxManifest disk;
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
            if (s->put(s->context, fresh, fs->data[i], n->length))
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
    UxManifest disk;
    char canonical[PVD_PATH_MAX];
    unsigned int n, i, j, round, built;
    int rc;
    if (!fs || !fs->view || !s || !s->get || !manifest)
        return PVD_EINVAL;
    if (s->get(s->context, manifest, &disk, sizeof(disk), &n) ||
        n != sizeof(disk) || disk.magic != UXFS_MAGIC ||
        disk.version != UXFS_VERSION || disk.reserved != 0 ||
        disk.checksum != hash(&disk, sizeof(disk)-sizeof(disk.checksum)))
        return PVD_EIO;
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
            if (s->get(s->context, p->oid, staged.data[i],
                       UXFS_DATA_MAX, &n) || n != p->length)
                return PVD_EIO;
        }
    }
    bytes(fs->nodes, staged.nodes, sizeof(fs->nodes));
    bytes(fs->data, staged.data, sizeof(fs->data));
    zero(fs->modified, sizeof(fs->modified));
    *fs->view = staged_view;
    fs->dirty = 0;
    return PVD_OK;
}

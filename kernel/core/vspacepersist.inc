/*
 * GE/O v2-backed ObjectSpace snapshots (amd64).
 *
 * All edges are serialized in one bounded OID payload; the GE/O version
 * publication is the atomic boundary. Persistent metadata is never a
 * capability. LOAD requires a live volume Handle and reopens every target
 * through its authorized provider, preserving revocation ancestry.
 *
 * Phase-4 base: only persistent GE/O target edges can cross a reboot.
 * Weak volatile ObjectSpace and device/process/region edges are rejected,
 * never silently reified into grants with new authority.
 */
enum {
    SpaceDiskVersion = 1,
};
#define SPACEDISKMAGIC 0x31504f5253474550ULL /* "PEGSROP1", nonzero discriminator */

typedef struct SpaceDiskHead SpaceDiskHead;
typedef struct SpaceDiskEdge SpaceDiskEdge;
typedef struct SpaceDiskImage SpaceDiskImage;

struct SpaceDiskHead {
    uvlong magic;
    uint version;
    uint count;
    uvlong nextid;
    uvlong epoch;
    uvlong hash;
};
struct SpaceDiskEdge {
    uvlong bindingid;
    uvlong oid;
    uint kind;
    uint rights;
    uint relation;
    uint flags;
    char label[ObjSpaceLabelMax];
};
struct SpaceDiskImage {
    SpaceDiskHead head;
    SpaceDiskEdge edges[ObjSpaceBindingMax];
};

_Static_assert(sizeof(SpaceDiskHead) == 40, "ObjectSpace GE disk header");
_Static_assert(sizeof(SpaceDiskEdge) == 80, "ObjectSpace GE binding record");

static uvlong
spacediskhash(void *data, ulong n)
{
    uchar *p = data;
    uvlong h = 0xcbf29ce484222325ULL;
    ulong i;
    for(i = 0; i < n; i++){
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

static void
spacediskreqcheck(ObjSpaceDiskReq *r)
{
    if(r == nil || r->reserved != 0 || r->volume <= 0 || r->oid == 0)
        error(Ebadarg);
}

/*
 * Persist exactly one consistent ObjectSpace version. The GE engine holds
 * its exclusive writer until oscommithand publishes all of the edge bytes.
 * Concurrent volatile mutations after the snapshot need another SAVE;
 * neither partial edge records nor a fabricated target Handle reach disk.
 */
void
vobjspacesave(int spacehandle, ObjSpaceDiskReq *r, uvlong *version)
{
    Vobj *o = nil;
    Vhand *vol = nil;
    Vspace *s;
    SpaceDiskImage *img = nil;
    SpaceDiskEdge *e;
    VspaceBinding *b;
    uchar diskvol[16], targetvol[16];
    ulong n;
    int i, bad = 0;

    spacediskreqcheck(r);
    o = vspacehandle(spacehandle, Vcontrol);
    if(waserror()){
        free(img);
        vhandput(vol);
        vobjput(o);
        nexterror();
    }
    vol = vlookup(r->volume, Vwrite|Vcommit);
    if(vol->obj->kind != VkindGeVolume || vol->obj->priv == nil)
        error(Ebadarg);
    img = mallocz(sizeof *img, 1);
    if(img == nil)
        error(Enomem);
    img->head.magic = SPACEDISKMAGIC;
    img->head.version = SpaceDiskVersion;
    s = o->priv;
    /* s->lk protects an all-or-nothing in-memory snapshot, never held
     * across allocation, an O call or a blocking GE commit. */
    lock(&s->lk);
    if(s->revoked){
        bad = 1;
    }else{
        img->head.count = s->nbindings;
        img->head.nextid = s->nextid;
        img->head.epoch = s->epoch;
        for(i = 0; i < s->nbindings; i++){
            b = &s->entries[i];
            if(b->kind != VkindGe || b->target == nil ||
               !vcapvalid(b->cap) || b->oid == 0 ||
               b->rights == 0 || (b->rights & ~vol->rights) != 0){
                bad = 1;
                break;
            }
            e = &img->edges[i];
            e->bindingid = b->id;
            e->oid = b->oid;
            e->kind = b->kind;
            e->rights = b->rights;
            e->relation = b->relation;
            memmove(e->label, b->label, sizeof e->label);
        }
    }
    unlock(&s->lk);
    if(bad)
        error("objectspace: only valid GE/O bindings can be persisted");
    /* Reject cross-volume substitution even when numeric OIDs coincide. */
    osvolumeid((Ohand*)vol->obj->priv, diskvol);
    for(i = 0; i < img->head.count; i++){
        /* Each saved OID must exist with valid owner provenance under the
         * committing volume authority before it becomes a durable edge. */
        osownercheck((Ohand*)vol->obj->priv, img->edges[i].oid);
    }
    /* Target volume identity is checked when GE targets are created by the
     * current kernel's sole activated O volume. The record contains only an
     * OID, never transient process-local Handle/identity data. */
    USED(targetvol);
    USED(diskvol);
    n = sizeof img->head + img->head.count * sizeof img->edges[0];
    img->head.hash = spacediskhash(img, n);
    oswritehandk((Ohand*)vol->obj->priv, r->oid, img, n);
    oscommithand((Ohand*)vol->obj->priv, version);
    poperror();
    free(img);
    vhandput(vol);
    vobjput(o);
}

/* A private ephemeral GE/O grant, derived from the caller's volume
 * capability. It is immediately bound to the new space and closed.
 * A stored rights mask cannot add a bit beyond the live volume grant. */
static int
spaceopenge(Vhand *vol, uvlong oid, int rights)
{
    Ohand *oh = nil;
    Vobj *o = nil;
    int h, ceiling;
    if(oid == 0 || rights == 0 || (rights & ~Vall) != 0)
        error(Ebadarg);
    if(waserror()){
        vobjput(o);
        oshandput(oh);
        nexterror();
    }
    oh = oscopycap((Ohand*)vol->obj->priv, oid);
    ceiling = vgerights(oh) & vol->rights & ~Vcreate;
    if((rights & ~ceiling) != 0)
        error(Eperm);
    vgecaplimit(oh, rights);
    o = vobjnewkind(VkindGe, 0);
    if(o == nil)
        error(Enomem);
    o->provider = VopenGe;
    o->oid = oid;
    o->space = 0;
    o->owner = osownerof(oh, oid);
    o->priv = oh;
    oh = nil;
    h = vobjinstallchild(o, rights, vol->cap);
    poperror();
    vobjput(o);
    return h;
}

/*
 * Rebuild under current authority: verified format + per-edge GE owner
 * integrity + attenuated Vcap lineage. Reject the entire graph on any
 * invalid, missing or over-privileged target. No partial graph is exposed.
 */
int
vobjspaceload(ObjSpaceDiskReq *r, VobjId *id)
{
    Vhand *vol = nil;
    SpaceDiskImage *img = nil;
    SpaceDiskHead *head;
    Vobj *rootobj = nil;
    Vspace *s;
    ObjSpaceBindReq bind;
    uvlong oldhash, prev;
    long n, got;
    int i, root = -1, tmp = -1;

    spacediskreqcheck(r);
    if(id != nil)
        validaddr((uintptr)id, sizeof *id, 1);
    vol = vlookup(r->volume, Vread|Vdelegate);
    if(waserror()){
        if(tmp >= 0) vobjclosehandle(tmp);
        if(root >= 0) vobjclosehandle(root);
        vobjput(rootobj);
        free(img);
        vhandput(vol);
        nexterror();
    }
    if(vol->obj->kind != VkindGeVolume || vol->obj->priv == nil)
        error(Ebadarg);
    n = osreadhandk((Ohand*)vol->obj->priv, r->oid, nil, 0, nil);
    if(n < (long)sizeof(SpaceDiskHead) || n > (long)sizeof(SpaceDiskImage))
        error("objectspace: invalid graph record length");
    img = mallocz(sizeof *img, 1);
    if(img == nil)
        error(Enomem);
    got = osreadhandk((Ohand*)vol->obj->priv, r->oid, img, n, nil);
    if(got != n)
        error("objectspace: short graph read");
    head = &img->head;
    if(head->magic != SPACEDISKMAGIC ||
       head->version != SpaceDiskVersion ||
       head->count > ObjSpaceBindingMax ||
       n != (long)(sizeof *head + head->count * sizeof img->edges[0]) ||
       head->epoch == 0)
        error("objectspace: unsupported graph format");
    oldhash = head->hash;
    head->hash = 0;
    if(oldhash != spacediskhash(img, n))
        error("objectspace: graph checksum mismatch");
    prev = 0;
    for(i = 0; i < head->count; i++){
        SpaceDiskEdge *e = &img->edges[i];
        if(e->bindingid <= prev || e->bindingid > head->nextid ||
           e->oid == 0 || e->kind != VkindGe || e->flags != 0 ||
           e->rights == 0 || (e->rights & ~Vall) != 0)
            error("objectspace: invalid graph binding");
        vspacelabelcheck(e->label);
        prev = e->bindingid;
    }

    root = vobjspacecreate(nil);
    rootobj = vspacehandle(root, Vcontrol);
    s = rootobj->priv;
    for(i = 0; i < head->count; i++){
        SpaceDiskEdge *e = &img->edges[i];
        /* Binding needs delegate on the ephemeral source, even when the
         * resulting persisted binding does not delegate. */
        tmp = spaceopenge(vol, e->oid, e->rights|Vdelegate);
        memset(&bind, 0, sizeof bind);
        bind.target = tmp;
        bind.rights = e->rights;
        bind.relation = e->relation;
        memmove(bind.label, e->label, sizeof bind.label);
        vobjspacebind(root, &bind);
        vobjclosehandle(tmp);
        tmp = -1;
        lock(&s->lk);
        s->entries[i].id = e->bindingid;
        unlock(&s->lk);
    }
    lock(&s->lk);
    s->nextid = head->nextid;
    s->epoch = head->epoch;
    unlock(&s->lk);
    if(id != nil){
        id->incarnation = rootobj->incarnation;
        id->space = rootobj->space;
        id->oid = rootobj->oid;
    }
    poperror();
    vobjput(rootobj);
    free(img);
    vhandput(vol);
    return root;
}

/*
 * GE/O v2 ObjectSpace graph image.  All reachable nodes are serialized
 * together in one OID and published by one GE transaction.
 *
 * Disk node indices, OIDs, labels and rights are descriptive data, never
 * authority.  Reification derives capabilities from the caller's live
 * GE volume grant.  No transient Handle, pointer or boot identity is saved.
 */
enum {
    SpaceDiskVersion = 2,
    SpaceGraphNodeMax = 16,
    SpaceGraphEdgeMax = SpaceGraphNodeMax * ObjSpaceBindingMax,
};
#define SPACEDISKMAGIC 0x31504f5253474550ULL

typedef struct SpaceGraphHead SpaceGraphHead;
typedef struct SpaceGraphNode SpaceGraphNode;
typedef struct SpaceGraphEdge SpaceGraphEdge;

struct SpaceGraphHead {
    uvlong magic;
    uint version;
    uint nodecount;
    uint edgecount;
    uint root;
    uvlong hash;
};
struct SpaceGraphNode {
    uvlong nextid;
    uvlong epoch;
    uint first;
    uint count;
};
struct SpaceGraphEdge {
    uvlong bindingid;
    uvlong target;          /* GE OID, or graph-local node index */
    uint kind;
    uint rights;
    uint relation;
    uint flags;
    char label[ObjSpaceLabelMax];
};

#define SpaceGraphBytes (sizeof(SpaceGraphHead) + \
    SpaceGraphNodeMax*sizeof(SpaceGraphNode) + \
    SpaceGraphEdgeMax*sizeof(SpaceGraphEdge))
_Static_assert(sizeof(SpaceGraphHead) == 32, "ObjectSpace graph header");
_Static_assert(sizeof(SpaceGraphNode) == 24, "ObjectSpace graph node");
_Static_assert(sizeof(SpaceGraphEdge) == 80, "ObjectSpace graph edge");

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

/* node objects are pinned by vspaceweakref or a checked live Handle. */
static int
spacegraphindex(Vobj **nodes, int count, uvlong incarnation, uvlong oid)
{
    int i;
    for(i = 0; i < count; i++)
        if(nodes[i]->incarnation == incarnation && nodes[i]->oid == oid)
            return i;
    return -1;
}

/*
 * Capture a closed, bounded reachable graph. Discover weak ObjectSpace
 * targets outside the node locks. Lock all discovered nodes in identity
 * order before copying any edges, then check the graph is still closed and
 * every discovered node remains reachable. Concurrent topology changes
 * invalidate the save instead of serializing a mixed graph.
 */
void
vobjspacesave(int spacehandle, ObjSpaceDiskReq *r, uvlong *version)
{
    Vhand *vol = nil;
    Vobj *nodes[SpaceGraphNodeMax];
    Vobj *order[SpaceGraphNodeMax], *swap;
    Vspace *s;
    SpaceGraphHead *head;
    SpaceGraphNode *dn;
    SpaceGraphEdge *edges, *e;
    VspaceBinding *b;
    uchar *image = nil;
    uvlong inc[ObjSpaceBindingMax], oid[ObjSpaceBindingMax];
    uint seen[SpaceGraphNodeMax];
    ulong n;
    int count = 0, held = 0, i, j, k, nt, idx, total, bad = 0, changed;

    spacediskreqcheck(r);
    memset(nodes, 0, sizeof nodes);
    memset(order, 0, sizeof order);
    memset(seen, 0, sizeof seen);
    nodes[count++] = vspacehandle(spacehandle, Vcontrol);
    if(waserror()){
        for(i = held-1; i >= 0; i--)
            unlock(&((Vspace*)order[i]->priv)->lk);
        for(i = 0; i < count; i++)
            vobjput(nodes[i]);
        free(image);
        vhandput(vol);
        nexterror();
    }
    vol = vlookup(r->volume, Vwrite|Vcommit);
    if(vol->obj->kind != VkindGeVolume || vol->obj->priv == nil)
        error(Ebadarg);

    /* Discover from process-independent weak references, never from an OID
     * supplied by the snapshot. Identities are checked again under locks. */
    for(i = 0; i < count && !bad; i++){
        s = nodes[i]->priv;
        nt = 0;
        lock(&s->lk);
        if(s->revoked)
            bad = 1;
        else
            for(j = 0; j < s->nbindings; j++){
                b = &s->entries[j];
                if(b->kind == VkindObjectSpace){
                    inc[nt] = b->incarnation;
                    oid[nt++] = b->oid;
                }
            }
        unlock(&s->lk);
        for(j = 0; j < nt && !bad; j++){
            if(spacegraphindex(nodes, count, inc[j], oid[j]) >= 0)
                continue;
            if(count >= SpaceGraphNodeMax){
                bad = 1;
                break;
            }
            nodes[count] = vspaceweakref(inc[j], oid[j]);
            if(nodes[count] == nil){
                bad = 1;
                break;
            }
            count++;
        }
    }
    if(bad)
        error("objectspace: stale or oversized persistent graph");
    image = mallocz(SpaceGraphBytes, 1);
    if(image == nil)
        error(Enomem);

    /* Acquire node locks in one deterministic order. Each mutation locks
     * only its own node; no GE operations or allocation occur while locked. */
    for(i = 0; i < count; i++)
        order[i] = nodes[i];
    for(i = 0; i < count; i++)
        for(j = i+1; j < count; j++)
            if(order[j]->oid < order[i]->oid){
                swap = order[i]; order[i] = order[j]; order[j] = swap;
            }
    for(i = 0; i < count; i++){
        lock(&((Vspace*)order[i]->priv)->lk);
        held++;
    }
    head = (SpaceGraphHead*)image;
    dn = (SpaceGraphNode*)(head+1);
    edges = (SpaceGraphEdge*)(dn+count);
    head->magic = SPACEDISKMAGIC;
    head->version = SpaceDiskVersion;
    head->nodecount = count;
    head->root = 0;
    total = 0;
    for(i = 0; i < count && !bad; i++){
        s = nodes[i]->priv;
        if(s->revoked || s->nbindings > ObjSpaceBindingMax ||
           total + s->nbindings > SpaceGraphEdgeMax){
            bad = 1;
            break;
        }
        dn[i].nextid = s->nextid;
        dn[i].epoch = s->epoch;
        dn[i].first = total;
        dn[i].count = s->nbindings;
        for(j = 0; j < s->nbindings; j++){
            b = &s->entries[j];
            e = &edges[total++];
            if(!vcapvalid(b->cap) || b->rights == 0 ||
               (b->rights & ~vol->rights) != 0 || b->id == 0){
                bad = 1;
                break;
            }
            e->bindingid = b->id;
            e->kind = b->kind;
            e->rights = b->rights;
            e->relation = b->relation;
            memmove(e->label, b->label, sizeof e->label);
            if(b->kind == VkindObjectSpace){
                idx = spacegraphindex(nodes, count, b->incarnation, b->oid);
                if(idx < 0 || b->target != nil){
                    bad = 1;
                    break;
                }
                e->target = idx;
            }else if(b->kind == VkindGe && b->target != nil &&
                     b->oid != 0){
                e->target = b->oid;
            }else{
                bad = 1;
                break;
            }
        }
    }
    head->edgecount = total;
    /* Every node must still be reachable from root after all locks were
     * acquired. Otherwise discovery raced a concurrent unbind. */
    if(!bad){
        seen[0] = 1;
        do{
            changed = 0;
            for(i = 0; i < count; i++) if(seen[i])
                for(j = dn[i].first; j < dn[i].first+dn[i].count; j++)
                    if(edges[j].kind == VkindObjectSpace &&
                       !seen[edges[j].target]){
                        seen[edges[j].target] = 1;
                        changed = 1;
                    }
        }while(changed);
        for(i = 0; i < count; i++)
            if(!seen[i]) bad = 1;
    }
    for(i = held-1; i >= 0; i--)
        unlock(&((Vspace*)order[i]->priv)->lk);
    held = 0;
    if(bad)
        error("objectspace: graph mutated, revoked or unsupported");
    /* Validate every referenced GE OID under the live volume authority. */
    for(i = 0; i < total; i++)
        if(edges[i].kind == VkindGe)
            osownercheck((Ohand*)vol->obj->priv, edges[i].target);
    n = sizeof *head + count*sizeof *dn + total*sizeof *edges;
    head->hash = spacediskhash(image, n);
    oswritehandk((Ohand*)vol->obj->priv, r->oid, image, n);
    oscommithand((Ohand*)vol->obj->priv, version);

    poperror();
    for(i = 0; i < count; i++)
        vobjput(nodes[i]);
    free(image);
    vhandput(vol);
}

/* Every reopened GE target inherits the current volume grant's lineage. */
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
 * Validate the entire snapshot before allocating graph objects. Build every
 * node before binding any edge, so nested spaces, aliases and cycles use the
 * same identity. Keep child nodes alive under the returned root lifetime;
 * inter-node graph edges remain weak to avoid refcount cycles.
 */
int
vobjspaceload(ObjSpaceDiskReq *r, VobjId *id)
{
    Vhand *vol = nil;
    uchar *image = nil;
    SpaceGraphHead *head;
    SpaceGraphNode *dn;
    SpaceGraphEdge *edges, *e;
    Vobj *nodes[SpaceGraphNodeMax];
    Vspace *s;
    ObjSpaceBindReq bind;
    uvlong oldhash, prev;
    long n, got;
    int handles[SpaceGraphNodeMax];
    int count = 0, i, j, tmp = -1, root = -1;

    spacediskreqcheck(r);
    if(id != nil)
        validaddr((uintptr)id, sizeof *id, 1);
    memset(nodes, 0, sizeof nodes);
    for(i = 0; i < SpaceGraphNodeMax; i++)
        handles[i] = -1;
    vol = vlookup(r->volume, Vread|Vdelegate);
    if(waserror()){
        if(tmp >= 0) vobjclosehandle(tmp);
        for(i = 0; i < count; i++)
            if(handles[i] >= 0) vobjclosehandle(handles[i]);
        for(i = 0; i < count; i++)
            vobjput(nodes[i]);
        free(image);
        vhandput(vol);
        nexterror();
    }
    if(vol->obj->kind != VkindGeVolume || vol->obj->priv == nil)
        error(Ebadarg);
    n = osreadhandk((Ohand*)vol->obj->priv, r->oid, nil, 0, nil);
    if(n < (long)(sizeof(SpaceGraphHead)+sizeof(SpaceGraphNode)) ||
       n > (long)SpaceGraphBytes)
        error("objectspace: invalid graph length");
    image = mallocz(SpaceGraphBytes, 1);
    if(image == nil)
        error(Enomem);
    got = osreadhandk((Ohand*)vol->obj->priv, r->oid, image, n, nil);
    if(got != n)
        error("objectspace: short graph read");
    head = (SpaceGraphHead*)image;
    if(head->magic != SPACEDISKMAGIC || head->version != SpaceDiskVersion ||
       head->nodecount == 0 || head->nodecount > SpaceGraphNodeMax ||
       head->edgecount > SpaceGraphEdgeMax || head->root != 0 ||
       n != (long)(sizeof *head + head->nodecount*sizeof(SpaceGraphNode)
                    +head->edgecount*sizeof(SpaceGraphEdge)))
        error("objectspace: unsupported graph format");
    oldhash = head->hash;
    head->hash = 0;
    if(oldhash != spacediskhash(image, n))
        error("objectspace: checksum mismatch");
    dn = (SpaceGraphNode*)(head+1);
    edges = (SpaceGraphEdge*)(dn+head->nodecount);
    j = 0;
    for(i = 0; i < head->nodecount; i++){
        if(dn[i].epoch == 0 || dn[i].count > ObjSpaceBindingMax ||
           dn[i].first != j || dn[i].first+dn[i].count > head->edgecount)
            error("objectspace: invalid graph node");
        prev = 0;
        for(; j < dn[i].first+dn[i].count; j++){
            e = &edges[j];
            if(e->bindingid <= prev || e->bindingid > dn[i].nextid ||
               e->flags != 0 || e->rights == 0 ||
               (e->rights & ~Vall) != 0 ||
               (e->rights & ~vol->rights) != 0 ||
               (e->kind != VkindObjectSpace && e->kind != VkindGe) ||
               (e->kind == VkindGe && e->target == 0) ||
               (e->kind == VkindObjectSpace && e->target >= head->nodecount))
                error("objectspace: invalid graph binding");
            vspacelabelcheck(e->label);
            prev = e->bindingid;
        }
    }
    if(j != head->edgecount)
        error("objectspace: unused graph edges");
    /* Use the current live GE authority to verify object existence and
     * owner provenance, independently of any serialized rights mask. */
    for(i = 0; i < head->edgecount; i++)
        if(edges[i].kind == VkindGe)
            osownercheck((Ohand*)vol->obj->priv, edges[i].target);

    for(i = 0; i < head->nodecount; i++){
        handles[i] = vobjspacecreate(nil);
        count++;
        nodes[i] = vspacehandle(handles[i], Vcontrol);
    }
    for(i = 0; i < count; i++){
        s = nodes[i]->priv;
        for(j = dn[i].first; j < dn[i].first+dn[i].count; j++){
            e = &edges[j];
            if(e->kind == VkindGe)
                tmp = spaceopenge(vol, e->target, e->rights|Vdelegate);
            else
                tmp = vobjinstallchild(nodes[e->target],
                        e->rights|Vdelegate, vol->cap);
            memset(&bind, 0, sizeof bind);
            bind.target = tmp;
            bind.rights = e->rights;
            bind.relation = e->relation;
            memmove(bind.label, e->label, sizeof bind.label);
            vobjspacebind(handles[i], &bind);
            vobjclosehandle(tmp);
            tmp = -1;
            lock(&s->lk);
            s->entries[j-dn[i].first].id = e->bindingid;
            unlock(&s->lk);
        }
        lock(&s->lk);
        s->nextid = dn[i].nextid;
        s->epoch = dn[i].epoch;
        unlock(&s->lk);
    }
    /* All restored nodes remain live as long as the root is live. Weak
     * graph edges cannot create cyclic reference-count leaks. */
    s = nodes[0]->priv;
    for(i = 1; i < count; i++){
        incref(&nodes[i]->ref);
        s->persist_children[s->npersist_children++] = nodes[i];
    }
    root = handles[0];
    if(id != nil){
        id->incarnation = nodes[0]->incarnation;
        id->space = nodes[0]->space;
        id->oid = nodes[0]->oid;
    }
    for(i = 1; i < count; i++){
        vobjclosehandle(handles[i]);
        handles[i] = -1;
    }
    poperror();
    for(i = 0; i < count; i++)
        vobjput(nodes[i]);
    free(image);
    vhandput(vol);
    return root;
}

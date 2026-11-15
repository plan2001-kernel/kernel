#include "uxgeo.h"
#include "obj.h"
#include "vobj.h"
static int create_file(void *p, UxOid *oid)
{
    UxGeo *geo = (UxGeo *)p;
    return obj_create_persist(geo->volume, 0, 0, oid) < 0 ? -1 : 0;
}
static int put_file(void *p, UxOid oid, const void *data, unsigned int n)
{
    UxGeo *geo = (UxGeo *)p;
    return obj_put_oid(geo->volume, oid, (void *)data, n) < 0 ? -1 : 0;
}
static int get_file(void *p, UxOid oid, void *data,
                    unsigned int limit, unsigned int *size)
{
    UxGeo *geo = (UxGeo *)p;
    ObjOpenGeOidReq req = { geo->volume, 0, oid };
    long h, len;
    if (!size) return -1;
    h = obj_open(ObjOpenGeOid, &req, sizeof(req), Vread, 0);
    if (h < 0) return -1;
    /* Detect truncation and never silently accept partial files. */
    len = obj_read((int)h, 0, 0, 0);
    if (len < 0 || (unsigned long)len > limit) {
        (void)obj_close((int)h);
        return -1;
    }
    if (len) {
        long actual = obj_read((int)h, data, (unsigned long)len, 0);
        if (actual != len) {
            (void)obj_close((int)h);
            return -1;
        }
    }
    (void)obj_close((int)h);
    *size = (unsigned int)len;
    return 0;
}
static int commit_files(void *p)
{
    UxGeo *geo = (UxGeo *)p;
    return obj_commit(geo->volume, 0) < 0 ? -1 : 0;
}
int uxgeo_bind(UxGeo *geo, int volume, UxStore *store)
{
    if (!geo || !store || volume <= 0) return -1;
    geo->volume = volume;
    store->context = geo;
    store->create = create_file;
    store->put = put_file;
    store->get = get_file;
    store->commit = commit_files;
    return 0;
}

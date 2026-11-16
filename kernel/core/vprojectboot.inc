/*
 * Trusted boot root policy.
 *
 * The selector is a trusted boot configuration value (plan9.ini
 * obootgraph=OID): one durable graph OID on the already authorized boot
 * GE/O volume.  It is deliberately separate from the graph payload and is
 * never derived from an OID, label or OwnerID in that payload.
 *
 * The boot process restores exactly once, after OEXECUTE has cleared the
 * loader's bootstrap Handles.  restore keeps its own temporary volume
 * Handle, derives every target from the current boot volume grant, and
 * leaves the returned VkindObjectSpace Handle installed in the bootstrap
 * process.  Other processes receive zero or more roots only through
 * explicit delegation.  A configured but unusable selector fails closed;
 * no substitute OID, silent bypass or storage mutation is attempted.
 */
static uvlong bootrootoid;
static int bootrootdone;

void
osbootrootselect(uvlong oid)
{
    bootrootoid = oid;
}

void
osbootrootrestore(void)
{
    ObjProjectOpen r;
    VobjId id;
    Ohand *oh;
    char err[128];
    int vol = -1, root;

    if(bootrootdone || up == nil || !up->boot)
        return;
    bootrootdone = 1;
    if(bootrootoid == 0){
        print("objectspace: no boot root selector configured\n");
        return;
    }
    print("objectspace: boot root restore oid=%#llux\n", bootrootoid);
    if(waserror()){
        snprint(err, sizeof err, "%s", up->errstr);
        if(vol >= 0)
            vobjclosehandle(vol);
        print("objectspace: boot root restore failed oid=%#llux: %s\n",
            bootrootoid, err);
        panic("objectspace: boot root restore failed");
    }
    oh = osopenvolume();
    vol = vobjinstallohand(oh, VkindGeVolume);
    memset(&r, 0, sizeof r);
    r.volume = vol;
    r.oid = bootrootoid;
    r.rights=Vread|Vmeta|Vdelegate|Vexecute;
    root = vobjprojectopen(&r, nil);
    vobjidentity(root, &id);
    vobjclosehandle(vol);
    vol = -1;
    poperror();
    print("objectspace: boot root installed oid=%#llux handle=%d "
        "incarnation=%#llux\n", bootrootoid, root, id.incarnation);
}

#!/usr/bin/env python3
"""Fail closed on stale owner-equality checks and missing capability gates."""
from pathlib import Path
root=Path(__file__).resolve().parents[3]
core=(root/"kernel/core/sysvobj.c").read_text()
storage=(root/"ge/storage/sysgefs.c").read_text()
objects=(root/"ge/storage/obj.c").read_text()
owner=(root/"kernel/core/owner.c").read_text()
assert "ownercanmanage" not in core
assert "up->owner == o->owner" not in core
assert "up->owner != owner" not in storage
assert "up->owner != ownerroot()" not in storage
for needle in ("vobjholdobj(handle, Vcontrol)","vlookup(handle, Vread)",
               "vobjholdobj(handle, Vcommit)","vobjholdobj(handle, Vwrite)",
               "vlookup(handle, Vdelegate)","if(write && !ownercanmanage"):
    if needle.startswith("if(write"):
        assert needle not in core
    else:
        assert needle in core,needle
for needle in ("oauthorize(oh, oid, Oread)","oauthorize(oh, oid, Owrite)",
               "oauthorize(oh, oh->oid, Ocreate)",
               "oauthorize(oh, oh->oid, Ocommit)",
               "if(!oboot()) error(Eperm)","osownercheckcore"):
    assert needle in storage,needle
assert "if(up == nil || !up->boot)" in owner
assert "if(stored != owner)" not in objects, "Kowner must remain immutable without matching writer"
assert "ownerassign(tx, &h, oid, owner)" in objects
print("PASS: OwnerID provenance isolated from Handle and Ohand authorization")

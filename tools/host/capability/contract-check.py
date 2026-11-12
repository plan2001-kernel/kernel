#!/usr/bin/env python3
"""Static closure checks; full behavioral validation is native QEMU."""
from pathlib import Path
root=Path(__file__).resolve().parents[3]
kernel=(root/"kernel/core/sysvobj.c").read_text()
dispatch=(root/"kernel/core/sysobj.c").read_text()
abi=(root/"runtime/abi/obj.h").read_text()
mpy=(root/"micropython/modplan9.c").read_text()
graph=(root/"runtime/abi/objectspace.h").read_text()
for token in ("struct Vcap","vcapvalidlocked(","vcapnew(","vcaprevokechildren(","vhandderive(","vobjinstallchild("):
    assert token in kernel,token
assert "ObjOpCapRevoke = 57" in abi
assert "case ObjOpCapRevoke:" in dispatch
assert "vobjcaprevoke(handle)" in dispatch
assert "MP_QSTR_cap_revoke" in mpy
assert "ObjSpaceFlagRevoked = 2" in graph
assert "b->cap = grant" in kernel
assert "vobjinstallchild(target, allowed, grant)" in kernel
assert "vhandderive(vi->e[i].src, vi->e[i].rights)" in kernel
assert "vhandallocchild(o, vi->e[i].rights, vi->e[i].src->cap)" in kernel
print("PASS: capability lineage ABI/provider/graph closure")

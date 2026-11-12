#!/usr/bin/env python3
"""Static ABI closure and public ObjectSpace contract audit (not a QEMU test)."""
from pathlib import Path

root = Path(__file__).resolve().parents[3]
abi = (root / "runtime/abi/obj.h").read_text()
records = (root / "runtime/abi/objectspace.h").read_text()
dispatch = (root / "kernel/core/sysobj.c").read_text()
provider = (root / "kernel/core/sysvobj.c").read_text()
userspace = (root / "micropython/modplan9.c").read_text()
vobj = (root / "runtime/abi/vobj.h").read_text()
for name, number in [
    ("ObjOpSpaceBind", 53),
    ("ObjOpSpaceUnbind", 54),
    ("ObjOpSpaceResolve", 55),
    ("ObjOpSpaceEnumerate", 56),
]:
    assert f"{name} = {number}" in abi, name
    assert f"case {name}:" in dispatch, name
for name in ("vobjspacecreate", "vobjspacebind", "vobjspaceunbind",
             "vobjspaceresolve", "vobjspaceenumerate"):
    assert name + "(" in provider, name
for name in ("space_new", "space_bind", "space_unbind", "space_resolve",
             "space_list"):
    assert "MP_QSTR_" + name in userspace, name
assert "#define OBJ_ABI_VERSION 2" in abi
assert "VkindObjectSpace = 19" in vobj
assert "ObjKindObjectSpace = 5" in abi
assert "ObjSpaceBindingMax = 64" in records
assert "ObjSpaceFlagStale" in records
assert "vspaceweakref" in provider and "vspaceregistryremove" in provider
assert "vobjspacecreate(&vid)" in dispatch
boot = (root / "kernel/amd64/ouser.c").read_text()
proc = (root / "kernel/core/sysproc.c").read_text()
assert 'getconf("obootgraph")' in boot
assert "osbootrootselect(" in boot
persist = (root / "kernel/core/vspacepersist.inc").read_text()
assert "osbootrootrestore" in persist and "osbootrootselect" in persist
assert "osbootrootrestore();" in proc
assert "obootgraph=<OID>" in (root / "docs/OBJECTSPACE.md").read_text()
assert (root / "tools/host/objectspace/run-boot-root.sh").is_file()
print("PASS: static ObjectSpace ABI and kernel/userspace closure")

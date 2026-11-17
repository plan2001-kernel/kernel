"""Host-only contract test for the MicroPython settheory module.

Simulates the documented plan9 GE/O capability calls. It is not a substitute
for native QEMU/GE/O acceptance.
"""
import importlib.util
from pathlib import Path
import sys
import types

source = Path(__file__).with_name("settheory.py")

class FakePlan9(types.ModuleType):
    def __init__(self):
        super().__init__("plan9")
        self.objects = {}
        self.handles = {}
        self.pending = {}
        self.next_oid = 100
        self.next_handle = 10
        self.volume = 1

    def _handle(self, oid):
        h = self.next_handle
        self.next_handle += 1
        self.handles[h] = oid
        return h

    def obj_create_ge(self, volume, data):
        assert volume == self.volume
        oid = self.next_oid
        self.next_oid += 1
        self.objects[oid] = bytes(data)
        return self._handle(oid), oid

    def obj_commit(self, volume):
        assert volume == self.volume
        self.objects.update(self.pending)
        self.pending.clear()

    def obj_open_ge_oid(self, volume, oid):
        assert volume == self.volume
        if oid not in self.objects:
            raise OSError("no such authorized GE/O object")
        return self._handle(oid)

    def vobjread(self, handle):
        return self.objects[self.handles[handle]]

    def obj_put_oid(self, volume, oid, data):
        assert volume == self.volume
        if oid not in self.objects:
            raise OSError("no such authorized GE/O object")
        self.pending[oid] = bytes(data)
        return len(data)

    def obj_close(self, handle):
        self.handles.pop(handle)


fake = FakePlan9()
sys.modules["plan9"] = fake
spec = importlib.util.spec_from_file_location("settheory", source)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def rejects(func):
    try:
        func()
    except (ValueError, OSError):
        return
    raise AssertionError("invalid operation accepted")


s = module.Store(fake.volume)
a = s.new((1, 2))
b = s.new((2, 3))
assert s.members(a) == (1, 2)
assert s.members(b) == (2, 3)
assert s.intersection(a, b) == (2,)
assert s.union(a, b) == (1, 2, 3)
assert s.difference(a, b) == (1,)
assert s.difference(b, a) == (3,)
assert s.contains(a, 2) and not s.contains(a, 3)
assert s.add(a, 3) and not s.add(a, 3)
assert s.members(a) == (1, 2, 3)
assert s.remove(a, 3) and not s.remove(a, 3)
assert s.members(a) == (1, 2)
c = s.materialize(s.intersection(a, b))
assert s.members(c) == (2,)
assert module.decode(module.encode((8, 1, 8))) == (1, 8)
assert module.union((), (2,)) == (2,)
assert module.intersection((), (2,)) == ()
assert module.difference((2,), ()) == (2,)
for invalid in (0, -1, 1 << 64, "x", True):
    rejects(lambda x=invalid: module.encode((x,)))
rejects(lambda: module.decode(b"not-a-set"))
rejects(lambda: module.decode(module.MAGIC + b"\x00" * 8))
rejects(lambda: module.decode(module.encode((1,)) + b"\x00" * 8))
rejects(lambda: s.members(9999))
fake.objects[a] = b"not-a-set"
rejects(lambda: s.add(a, 4))
assert not fake.pending
assert not fake.handles, "all GE/O object handles must be closed"
print("HOST-SETTHEORY-PASS")

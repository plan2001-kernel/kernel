"""Set algebra over persistent Plan2001 GE/O objects (experimental).

A set is one ordinary GE/O object whose payload is an ordered sequence of
unique 64-bit ObjectIDs. No directory, traversal, implicit volume or new
kernel API is required. Set membership is data, never access authority.

REPL:
    import plan9 as p, settheory
    volume = p.obj_open_volume()       # trusted/authorized REPL only
    sets = settheory.Store(volume)
    a = sets.new([101, 102])
    b = sets.new([102, 103])
    sets.intersection(a, b)            # (102,)
"""
import plan9 as _p

MAGIC = b"P2SET\x01"
MAX_MEMBERS = 65536
MAX_OID = (1 << 64) - 1


def _oid(value):
    if type(value) is not int or not 0 < value <= MAX_OID:
        raise ValueError("OID must be a positive unsigned 64-bit integer")
    return value


def _ordered(values):
    result = sorted(set(_oid(v) for v in values))
    if len(result) > MAX_MEMBERS:
        raise ValueError("set is too large for this prototype")
    return tuple(result)


def encode(values):
    """Versioned, canonical GE/O payload for one finite OID set."""
    members = _ordered(values)
    payload = bytearray(MAGIC)
    for oid in members:
        for shift in (56, 48, 40, 32, 24, 16, 8, 0):
            payload.append((oid >> shift) & 255)
    return bytes(payload)


def decode(data):
    """Reject corrupt, unsorted and duplicate member records."""
    if not data.startswith(MAGIC) or (len(data) - len(MAGIC)) % 8:
        raise ValueError("not a valid settheory payload")
    count = (len(data) - len(MAGIC)) // 8
    if count > MAX_MEMBERS:
        raise ValueError("set is too large for this prototype")
    values = []
    previous = 0
    for offset in range(len(MAGIC), len(data), 8):
        value = 0
        for octet in data[offset:offset + 8]:
            value = (value << 8) | octet
        if value <= previous:
            raise ValueError("set members must be positive, unique and sorted")
        values.append(value)
        previous = value
    return tuple(values)


def union(a, b):
    """Pure set union, independent of GE/O."""
    return _ordered(set(a) | set(b))


def intersection(a, b):
    """Pure set intersection, independent of GE/O."""
    return _ordered(set(a) & set(b))


def difference(a, b):
    """Pure set difference, independent of GE/O."""
    return _ordered(set(a) - set(b))


class Store:
    """Persistent sets addressed by their SetID (a GE/O OID).

    The caller explicitly supplies an authorized GE/O volume Handle.
    Store never opens a volume implicitly and never derives authority from
    a SetID or member OID. Mutations commit immediately for REPL usability.
    """

    def __init__(self, volume):
        self.volume = volume

    def new(self, members=()):
        """Create a durable set and return its SetID."""
        handle, oid = _p.obj_create_ge(self.volume, encode(members))
        try:
            _p.obj_commit(self.volume)
        finally:
            _p.obj_close(handle)
        return oid

    def members(self, set_id):
        """Read a durable set. Opening a SetID still requires volume rights."""
        handle = _p.obj_open_ge_oid(self.volume, _oid(set_id))
        try:
            data = _p.vobjread(handle)
        finally:
            _p.obj_close(handle)
        return decode(data)

    def _replace(self, set_id, values):
        # Read first to reject an unrelated GE/O object; otherwise a
        # mistyped, writable OID could be silently overwritten.
        self.members(set_id)
        _p.obj_put_oid(self.volume, _oid(set_id), encode(values))
        _p.obj_commit(self.volume)

    def add(self, set_id, object_id):
        value = _oid(object_id)
        values = self.members(set_id)
        if value in values:
            return False
        self._replace(set_id, values + (value,))
        return True

    def remove(self, set_id, object_id):
        value = _oid(object_id)
        values = self.members(set_id)
        if value not in values:
            return False
        self._replace(set_id, (v for v in values if v != value))
        return True

    def contains(self, set_id, object_id):
        return _oid(object_id) in self.members(set_id)

    def union(self, a, b):
        return union(self.members(a), self.members(b))

    def intersection(self, a, b):
        return intersection(self.members(a), self.members(b))

    def difference(self, a, b):
        return difference(self.members(a), self.members(b))

    def materialize(self, members):
        """Store a computed selection as a new independent durable set."""
        return self.new(members)

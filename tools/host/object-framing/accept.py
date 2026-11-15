"""Execute real native loader admission checks from a framed MPY object."""
class Counter:
    def __init__(self,n=0):
        self.n=n
shared=Counter()
alias=shared

def init():
    shared.n=0

def run():
    import space
    import cbor
    import plan9
    original=space.oget(0x4f45584543000003)
    raw=space.oraw(0x4f45584543000003)
    meta=cbor.loads(space.ometa(0x4f45584543000003))
    assert raw.startswith(b'\x7fELF') and meta['name']=='micropython.elf'
    broken=bytearray(original)
    broken[-20]=1
    for payload in (raw, original[:-1], bytes(broken), b'not an executable'):
        oid=space.oregister(payload)
        try:
            child=space.spawn(oid,argv=('micropython','-c','import plan9; plan9.exits(\"unexpected\")'))
        except OSError:
            pass
        else:
            plan9.obj_close(child)
            raise AssertionError('native loader admitted invalid footer')
    print('FRAMED-NATIVE-LOADER-NEGATIVE-PASS')
    shared.n+=1
    return shared.n

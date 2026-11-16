#!/usr/bin/env python3
import hashlib
import unittest
from object_format import pack,unpack,cbor,decode,MAGIC
class Framing(unittest.TestCase):
    def test_wire(self):
        raw=b'Hello';meta=cbor({'name':'kernel_trap.c','created':None,'unix':{'mode':0o100640},'xattr':b'\x00\xff'})
        obj=pack(raw,meta);self.assertEqual(unpack(obj),(raw,meta));self.assertEqual(obj[-32:-24],MAGIC)
        self.assertEqual(obj[-16:-8],len(raw).to_bytes(8,'big'));self.assertEqual(decode(meta)['xattr'],b'\x00\xff')
    def test_corruption(self):
        obj=pack(b'x',b'opaque')
        for i in [len(obj)-32,len(obj)-24,len(obj)-20,len(obj)-1]:
            bad=bytearray(obj);bad[i]^=0x80
            with self.assertRaises(ValueError):unpack(bad)
        for legacy in [b'',b'legacy',obj[:-1]]:
            with self.assertRaises(ValueError):unpack(legacy)
    def test_cbor(self):
        values=[None,True,False,-2**63,2**64-1,0,24,65536,b'\0\xff','ää',[1,2],{'x':{'y':3}}]
        for v in values:self.assertEqual(decode(cbor(v)),v)
        for bad in [b'\x5a\xff\xff\xff\xff',b'\xa2aa\x01aa\x02',b'\x9f\xff']:
            with self.assertRaises(ValueError):decode(bad)
if __name__=='__main__':unittest.main()

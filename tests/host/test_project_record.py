"""Exercise the durable namespace decoder without granting any authority."""
import ctypes as c
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
class Record(c.Structure):
    _fields_=[('epoch',c.c_uint64),('next_binding',c.c_uint64),('count',c.c_size_t),('records',c.c_void_p),('records_length',c.c_size_t)]
def frame(entries, epoch=1, next_binding=1):
    data=b'P2PROJ01'+struct.pack('>QQQ',epoch,next_binding,len(entries))
    for binding,oid,name in entries:
        data+=struct.pack('>QQIIII',binding,oid,1,0,1,len(name))+name
    h=14695981039346656037
    for b in data:h=((h^b)*1099511628211)&((1<<64)-1)
    return data+struct.pack('>Q',h)
class Decode(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory()
        lib=Path(cls.tmp.name)/'record.so'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-shared','-fPIC',str(ROOT/'ports/project/record.c'),'-o',str(lib)],check=True)
        cls.lib=c.CDLL(str(lib));cls.lib.project_record_open.argtypes=[c.POINTER(Record),c.c_void_p,c.c_size_t]
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def valid(self,data):
        buf=c.create_string_buffer(data);r=Record()
        return self.lib.project_record_open(c.byref(r),buf,len(data))==0
    def test_large_namespace(self):
        self.assertTrue(self.valid(frame([(i,i+100,b'x'*255) for i in range(1,130)],next_binding=129)))
    def test_aliases_and_duplicate_labels(self):
        self.assertTrue(self.valid(frame([(1,100,b'a'),(2,100,b'b'),(3,101,b'a')],next_binding=3)))
    def test_corruption_and_truncation(self):
        data=frame([(1,100,b'module.py')])
        for n in range(len(data)):self.assertFalse(self.valid(data[:n]))
        for n in range(len(data)):
            bad=bytearray(data);bad[n]^=1;self.assertFalse(self.valid(bytes(bad)))
    def test_invalid_records(self):
        for entries in [[(0,100,b'a')],[(1,0,b'a')],[(1,100,b'')],[(1,100,b'a\0b')],[(1,100,b'a'*256)],[(1,100,b'a'),(1,101,b'b')]]:
            self.assertFalse(self.valid(frame(entries)))
        self.assertFalse(self.valid(frame([(2,100,b'a')],next_binding=1)))
        self.assertFalse(self.valid(frame([],epoch=0)))
if __name__=='__main__':unittest.main()

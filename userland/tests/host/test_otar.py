"""Offline import contracts, with no physical device access."""
import importlib.machinery
import importlib.util
import io
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
loader=importlib.machinery.SourceFileLoader('otar',str(ROOT/'tools/otar/otar'))
spec=importlib.util.spec_from_loader(loader.name,loader)
o=importlib.util.module_from_spec(spec);loader.exec_module(o)
sys.path.insert(0,str(ROOT.parent/'kernel/tools/host'))
from object_format import pack,unpack,cbor,decode
for name in ('pack','unpack','cbor','decode'):setattr(o,name,globals()[name])

class Store:
    def __init__(self):
        self.values={7:o.encode_record({'epoch':1,'high':0,'entries':[],'meta':cbor(o.origin(b'root',True,None))})}
        self.owners={7:123};self.next=64;self.staged={};self.commits=0
    def get(self,oid):return self.values.get(oid)
    def owner(self,oid):return self.owners.get(oid,0)
    def call(self,op,oid=0,owner=0,data=b''):
        if op=='C':
            oid=self.next;self.next+=1;self.values[oid]=data;self.owners[oid]=owner;return oid
        if op=='P':self.staged[oid]=data;self.owners.setdefault(oid,owner)
        elif op=='K':self.values.update(self.staged);self.staged={};self.commits+=1
        else:raise AssertionError(op)

class ImportTests(unittest.TestCase):
    def meta(self,label=b'a'):return cbor(o.origin(label,False,None))
    def test_publish_reopen_and_idempotent(self):
        b=Store();t=o.Tree(b,7);t.store([b'home',b'a'],b'hello',self.meta());t.publish()
        before=dict(b.values);t=o.Tree(b,7);d,_=t.walk([b'home']);oid=t.find(d,b'a')[1]
        self.assertEqual(unpack(t.blob(oid))[0],b'hello')
        t.store([b'home',b'a'],b'hello',self.meta());self.assertEqual(t.publish(),0)
        self.assertEqual(b.values,before);self.assertEqual(b.commits,1)
        t=o.Tree(b,7);t.store([b'home',b'a'],b'changed',self.meta());t.publish()
        t=o.Tree(b,7);self.assertEqual(t.find(d,b'a')[1],oid)
        self.assertEqual(unpack(t.blob(oid))[0],b'changed')
    def test_dry_run_never_allocates(self):
        b=Store();t=o.Tree(b,7);t.store([b'd',b'a'],b'x',self.meta());t.publish(True)
        self.assertEqual(b.next,64);self.assertEqual(b.commits,0)
    def test_failure_keeps_published_tree(self):
        b=Store();before=dict(b.values);t=o.Tree(b,7);t.store([b'a'],b'x',self.meta())
        old=b.call
        def fail(op,*args,**kw):
            if op=='K':raise OSError('injected commit failure')
            return old(op,*args,**kw)
        b.call=fail
        with self.assertRaises(OSError):t.publish()
        self.assertEqual(b.values[7],before[7]);self.assertEqual(o.Tree(b,7).dir(7)['entries'],[])
    def test_metadata_provenance_and_unknown_fields(self):
        old={'name':'original','created':None,'custom':b'opaque','unix':{'mtime_ns':1}}
        new={'name':'renamed','created':4,'unix':{'mtime_ns':2}}
        m=decode(o.merge_metadata(cbor(old),cbor(new)))
        self.assertEqual(m['name'],'original');self.assertIsNone(m['created']);self.assertEqual(m['custom'],b'opaque');self.assertEqual(m['unix']['mtime_ns'],2)
        self.assertEqual(o.merge_metadata(b'opaque',cbor(new)),b'opaque')
    def test_paths(self):
        for name in ['/abs','../escape','a/../b','a//b','a/./b','\0']:
            with self.subTest(name=name),self.assertRaises(ValueError):o.components(name)
        self.assertEqual(o.components('./a/b/'),[b'a',b'b'])
    def test_type_conflict_and_catalog_protection(self):
        b=Store();t=o.Tree(b,7);t.store([b'a'],b'x',self.meta());t.publish();t=o.Tree(b,7)
        with self.assertRaises(ValueError):t.store([b'a',b'b'],b'x',self.meta())
        with self.assertRaises(ValueError):t.store([b'.programs',b'a'],b'x',self.meta())
    def test_reuse_requires_exact_bytes(self):
        b=Store();b.values[12]=pack(b'tcc',self.meta());b.owners[12]=123
        t=o.Tree(b,7)
        with self.assertRaises(ValueError):t.store([b'tcc'],b'wrong',self.meta(),reuse=12)
        t=o.Tree(b,7);t.store([b'tcc'],b'tcc',self.meta(),reuse=12);t.publish()
        self.assertEqual(o.Tree(b,7).find(7,b'tcc')[1],12)
    def test_archive_validation_and_raw_export(self):
        with tempfile.TemporaryDirectory() as d:
            d=Path(d);a=d/'a.tar'
            with tarfile.open(a,'w') as tar:
                t=tarfile.TarInfo('a');t.size=3;tar.addfile(t,io.BytesIO(b'raw'))
            b=Store();tree=o.Tree(b,7)
            for row in o.read_archive(a):tree.store(*row)
            tree.publish();o.tar_export(o.Tree(b,7),[],d/'out.tar')
            with tarfile.open(d/'out.tar') as tar:self.assertEqual(tar.extractfile('a').read(),b'raw')
            with tarfile.open(a,'w') as tar:
                t=tarfile.TarInfo('link');t.type=tarfile.SYMTYPE;t.linkname='/bad';tar.addfile(t)
            with self.assertRaises(ValueError):o.read_archive(a)
    def test_backup_roundtrip_metadata(self):
        with tempfile.TemporaryDirectory() as d:
            a=Path(d)/'backup.otar';b=Store();t=o.Tree(b,7)
            t.store([b'd',b'a'],b'raw',cbor({'name':'a','custom':b'\x00\xff'}));t.publish()
            o.backup(o.Tree(b,7),a);target=Store();tree=o.Tree(target,7)
            o.restore_graph(tree,o.restore_members(a),[])
            tree.publish();tree=o.Tree(target,7);parent,_=tree.walk([b'd']);oid=tree.find(parent,b'a')[1]
            self.assertEqual(tree.blob(oid),o.Tree(b,7).blob(o.Tree(b,7).find(o.Tree(b,7).walk([b'd'])[0],b'a')[1]))
    def test_backup_preserves_aliases_without_admitting_programs(self):
        with tempfile.TemporaryDirectory() as d:
            a=Path(d)/'aliases.otar';b=Store();tree=o.Tree(b,7)
            tree.store([b'a'],b'raw',self.meta());tree.publish()
            tree=o.Tree(b,7);oid=tree.find(7,b'a')[1]
            tree.bind(7,b'alias',oid,o.GE,65535)
            catalog,_=tree.walk([b'.programs'],True)
            tree.bind(catalog,b'program',oid,o.GE,o.ADMISSION);tree.publish()
            o.backup(o.Tree(b,7),a);dest=Store();tree=o.Tree(dest,7)
            o.restore_graph(tree,o.restore_members(a),[]);tree.publish()
            tree=o.Tree(dest,7)
            self.assertEqual(tree.find(7,b'a')[1],tree.find(7,b'alias')[1])
            self.assertIsNone(tree.find(7,b'.programs'))
    def test_oversized_transaction_fails_before_allocation(self):
        b=Store();tree=o.Tree(b,7);tree.store([b'a'],b'raw',self.meta())
        maximum=o.MAX
        try:
            o.MAX=1
            with self.assertRaises(ValueError):tree.publish()
        finally:o.MAX=maximum
        self.assertEqual(b.next,64);self.assertEqual(b.commits,0)
    def test_ambiguous_labels_are_rejected(self):
        b=Store();tree=o.Tree(b,7)
        tree.bind(7,b'a',7,o.SPACE,65535);tree.bind(7,b'a',7,o.SPACE,65535)
        with self.assertRaises(ValueError):tree.find(7,b'a')
    def test_corrupt_records_rejected(self):
        blob=Store().values[7]
        with self.assertRaises(ValueError):o.record(blob[:-1])
        raw,meta=unpack(blob)
        with self.assertRaises(ValueError):o.record(pack(raw[:-1]+b'x',meta))

if __name__=='__main__':unittest.main()

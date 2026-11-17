"""Real native GE/O integration on a fresh sparse image; no device access.

Build tools/otar and the kernel formatter first. Set OTAR_BACKEND to enable.
"""
import hashlib
import io
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
KERNEL=Path(os.environ.get('PLAN2001_KERNEL',ROOT.parent/'kernel'))
BACKEND=os.environ.get('OTAR_BACKEND')

@unittest.skipUnless(BACKEND,'set OTAR_BACKEND to run the real GE/O volume test')
class NativeVolumeTests(unittest.TestCase):
    def test_native_import_export_restore_and_reopen(self):
        sys.path.insert(0,str(KERNEL/'tools/host'))
        from object_format import pack,cbor
        with tempfile.TemporaryDirectory(prefix='otar-volume-') as temp:
            temp=Path(temp);seed=temp/'seed';(seed/'mod').mkdir(parents=True)
            (seed/'env').write_bytes(b'');(seed/'envhex').write_text(hashlib.sha256(b'').hexdigest())
            raw=b'P2PROJ01'+struct.pack('>QQQ',1,0,0)
            value=14695981039346656037
            for b in raw:value=((value^b)*1099511628211)&((1<<64)-1)
            (seed/'bad').write_bytes(pack(raw+struct.pack('>Q',value),cbor({'name':'root'})))
            (seed/'badoid').write_text('0000000000000007')
            (seed/'alloc').write_bytes(b'OALLOC01'+struct.pack('>Q',64)+b'OOWNMODE'+struct.pack('>Q',1))
            records=b'RAWREC01'
            for oid in (7,0xfffffffffffffffc):
                key=b'\x08'+struct.pack('>QQ',oid,(1<<64)-2)
                data=b'OOWNER01'+struct.pack('>Q',123)
                records+=struct.pack('>II',len(key),len(data))+key+data
            (seed/'raw').write_bytes(records)
            volume=temp/'volume.img'
            subprocess.run([str(KERNEL/'build/format-o/format-o'),'--seed',str(seed),'--size','256',str(volume)],check=True,stdout=subprocess.DEVNULL)
            def cli(*args,success=True):
                r=subprocess.run([sys.executable,str(ROOT/'tools/otar/otar'),'--kernel',str(KERNEL),'--backend',BACKEND,*map(str,args)],capture_output=True,text=True)
                self.assertEqual(r.returncode,0 if success else 1,r.stdout+r.stderr)
                return r.stdout
            def inspect():return json.loads(cli('inspect',volume))
            archive=temp/'input.tar';payload=b'native-object-bytes\0\xff'*50000
            def make_archive(content):
                with tarfile.open(archive,'w',format=tarfile.USTAR_FORMAT) as a:
                    t=tarfile.TarInfo('nested/file.bin');t.size=len(content);t.mtime=123;t.mode=0o640;a.addfile(t,io.BytesIO(content))
            make_archive(payload);before=inspect()['volume']
            cli('-xf',archive,volume,'--root','7','--dry-run')
            self.assertEqual(inspect()['volume'],before)
            cli('-xf',archive,volume,'--root','7')
            listing=cli('list',volume,'--root','7');oid=next(line.split()[0] for line in listing.splitlines() if line.endswith('/nested/file.bin'))
            before=inspect()['volume'];cli('-xf',archive,volume,'--root','7')
            self.assertEqual(inspect()['volume'],before)
            make_archive(b'updated');cli('-xf',archive,volume,'--root','7')
            self.assertIn(oid+' file /nested/file.bin',cli('list',volume,'--root','7'))
            out=temp/'out.tar';cli('-cf',out,volume,'--root','7')
            with tarfile.open(out) as a:self.assertEqual(a.extractfile('nested/file.bin').read(),b'updated')
            backup=temp/'objects.otar';cli('backup',volume,backup,'--root','7','-C','/nested')
            cli('restore',volume,backup,'--root','7','-C','/restored')
            out=temp/'restored.tar';cli('-cf',out,volume,'--root','7','-C','/restored')
            with tarfile.open(out) as a:self.assertEqual(a.extractfile('file.bin').read(),b'updated')
            # Invalid archive must leave the published version unchanged.
            with tarfile.open(archive,'w') as a:
                t=tarfile.TarInfo('../escape');t.size=1;a.addfile(t,io.BytesIO(b'x'))
            before=inspect()['volume'];cli('-xf',archive,volume,'--root','7',success=False)
            self.assertEqual(inspect()['volume'],before)
            # Independent reader checks native superblocks, logs, tree and owners.
            d=json.loads(subprocess.check_output([sys.executable,str(KERNEL/'tools/host/oinspect.py'),str(volume)]))
            self.assertTrue(d['owner_mode']);self.assertTrue(d['owners'])

if __name__=='__main__':unittest.main()

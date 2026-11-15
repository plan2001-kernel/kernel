#!/usr/bin/env python3
"""Portable raw-first GE/O object framing and opaque metadata access.

The footer is mandatory for this tool. Metadata is opaque to framing; the
producer profile below uses deterministic CBOR and records unknown birth time
as null rather than manufacturing one from import time.
"""
from __future__ import annotations
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import stat
import struct

FOOTER = struct.Struct('>8sIIQQ')
MAGIC = b'P2OTAIL1'

def cbor(value):
    def head(kind,n):
        if n<24:return bytes([kind*32+n])
        for size,tag in [(1,24),(2,25),(4,26),(8,27)]:
            if n<1<<(size*8):return bytes([kind*32+tag])+n.to_bytes(size,'big')
        raise ValueError('CBOR integer overflow')
    if value is None:return b'\xf6'
    if isinstance(value,bool):return b'\xf5' if value else b'\xf4'
    if isinstance(value,int):return head(0,value) if value>=0 else head(1,-1-value)
    if isinstance(value,bytes):return head(2,len(value))+value
    if isinstance(value,str):
        data=value.encode('utf-8');return head(3,len(data))+data
    if isinstance(value,(tuple,list)):return head(4,len(value))+b''.join(map(cbor,value))
    if isinstance(value,dict):
        entries=sorted((cbor(k),cbor(v)) for k,v in value.items())
        entries.sort(key=lambda p:(len(p[0]),p[0]))
        return head(5,len(entries))+b''.join(k+v for k,v in entries)
    raise ValueError('unsupported CBOR value')

def decode(data):
    pos=0
    def item(depth=0):
        nonlocal pos
        if depth>64 or pos>=len(data):raise ValueError('invalid CBOR')
        b=data[pos];pos+=1;kind=b>>5;n=b&31
        if kind==7 and n in (20,21,22):return {20:False,21:True,22:None}[n]
        if n>=24:
            width={24:1,25:2,26:4,27:8}.get(n)
            if width is None or width>len(data)-pos:raise ValueError('invalid CBOR length')
            n=int.from_bytes(data[pos:pos+width],'big');pos+=width
        if kind in (0,1):return n if kind==0 else -1-n
        if kind in (2,3):
            if n>len(data)-pos:raise ValueError('truncated CBOR')
            s=data[pos:pos+n];pos+=n;return s if kind==2 else s.decode('utf-8')
        if kind in (4,5) and n>len(data)-pos:raise ValueError('invalid CBOR count')
        if kind==4:return [item(depth+1) for _ in range(n)]
        if kind==5:
            d={}
            for _ in range(n):
                k=item(depth+1)
                if k in d:raise ValueError('duplicate CBOR key')
                d[k]=item(depth+1)
            return d
        raise ValueError('unsupported CBOR type')
    result=item()
    if pos!=len(data):raise ValueError('trailing CBOR bytes')
    return result

def pack(raw,metadata=b''):
    if len(raw)+len(metadata)>2**64-33:raise ValueError('object length overflow')
    return raw+metadata+FOOTER.pack(MAGIC,1,0,len(raw),len(metadata))

def unpack(data):
    if len(data)<FOOTER.size:raise ValueError('missing object footer')
    magic,version,flags,r,m=FOOTER.unpack(data[-32:])
    if magic!=MAGIC or version!=1 or flags or r>len(data)-32 or m!=len(data)-32-r:
        raise ValueError('invalid object footer')
    return data[:r],data[r:-32]

def metadata(raw,name,media_type='application/octet-stream',source=None):
    result={'profile':'plan2001.unix.v1','name':name,'created':None,
            'media_type':media_type,'raw_sha256':hashlib.sha256(raw).digest()}
    if source is not None:
        s=Path(source).stat()
        result['name_bytes']=os.fsencode(name)
        result['unix']={'mode':s.st_mode,'uid':s.st_uid,'gid':s.st_gid,
            'atime_ns':None,'mtime_ns':s.st_mtime_ns,'ctime_ns':s.st_ctime_ns,
            'birthtime_ns':getattr(s,'st_birthtime_ns',None),'rdev':s.st_rdev}
        result['origin']={'dev':s.st_dev,'ino':s.st_ino,'nlink':s.st_nlink}
    return result

def frame_file(path,media_type='application/octet-stream',name=None):
    path=Path(path);raw=path.read_bytes()
    return pack(raw,cbor(metadata(raw,name or path.name,media_type,path)))

def json_value(value):
    if isinstance(value,bytes):return {'base64':base64.b64encode(value).decode()}
    if isinstance(value,dict):return {str(k):json_value(v) for k,v in value.items()}
    if isinstance(value,list):return list(map(json_value,value))
    return value

def main():
    p=argparse.ArgumentParser(description=__doc__);s=p.add_subparsers(dest='op',required=True)
    q=s.add_parser('pack');q.add_argument('input',type=Path);q.add_argument('output',type=Path);q.add_argument('--name');q.add_argument('--media-type',default='application/octet-stream')
    for op in ['inspect','raw','metadata']:
        q=s.add_parser(op);q.add_argument('input',type=Path)
        if op!='inspect':q.add_argument('output',type=Path)
    a=p.parse_args()
    if a.op=='pack':
        with a.output.open('xb') as out:out.write(frame_file(a.input,a.media_type,a.name))
        return
    raw,meta=unpack(a.input.read_bytes())
    if a.op in ('raw','metadata'):
        with a.output.open('xb') as out:out.write(raw if a.op=='raw' else meta)
    else:
        result={'raw_length':len(raw),'metadata_length':len(meta),'raw_sha256':hashlib.sha256(raw).hexdigest(),'metadata_bytes':json_value(meta)}
        try:result['metadata']=json_value(decode(meta)) if meta else None
        except (ValueError,UnicodeError,TypeError) as e:result['metadata_decode_error']=str(e)
        print(json.dumps(result,indent=2))
if __name__=='__main__':main()

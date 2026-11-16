"""Capability-scoped live projects; no ambient filesystem or volume lookup."""
import plan9 as _p
import cbor as _cbor
import sys as _sys

_READ=1
_META=8
_CONTROL=64
_DELEGATE=128
_WRITE=256
_CREATE=2048
_COMMIT=4096
_EXECUTE=8192
_READONLY=_READ|_META|_DELEGATE|_EXECUTE
_ALL=_READONLY|_CONTROL|_WRITE|_CREATE|_COMMIT
_active=None

def _u(n,size=8): return n.to_bytes(size,'big')
def _hash(data):
    h=14695981039346656037
    for c in data:h=((h^c)*1099511628211)&0xffffffffffffffff
    return h

def _split(data):
    if len(data)<32 or data[-32:-24]!=b'P2OTAIL1': raise ValueError('invalid footer')
    if int.from_bytes(data[-24:-20],'big')!=1 or int.from_bytes(data[-20:-16],'big'):raise ValueError('invalid footer version')
    n=int.from_bytes(data[-16:-8],'big');m=int.from_bytes(data[-8:],'big')
    if n+m+32!=len(data):raise ValueError('invalid footer lengths')
    return data[:n],data[n:n+m]

def _frame(raw,meta):
    metadata=_cbor.dumps(meta)
    return raw+metadata+b'P2OTAIL1'+_u(1,4)+_u(0,4)+_u(len(raw))+_u(len(metadata))

def _empty(meta):
    raw=b'P2PROJ01'+_u(1)+_u(0)+_u(0)
    return _frame(raw+_u(_hash(raw)),meta)

def _meta(name,directory=False):
    return {'profile':'plan2001.unix.v1','name':name,'name_bytes':name.encode(),
            'created':None,'media_type':'inode/directory' if directory else 'application/octet-stream',
            'unix':{'mode':0o40700 if directory else 0o100600,'uid':0,'gid':0}}

class Object:
    def __init__(self,handle):self.handle=handle
    def close(self):
        if self.handle:_p.obj_close(self.handle);self.handle=0
    def identity(self):
        return _p.project_identity(self.handle)
    def raw(self):return _split(_p.project_call(self.handle,1))[0]
    def metadata(self):return _cbor.loads(_split(_p.project_call(self.handle,1))[1])
    def write(self,data):
        if not isinstance(data,bytes):raise TypeError('bytes required')
        meta=self.metadata();meta['raw_sha256']=None
        _p.project_call(self.handle,6,None,0,0,_frame(data,meta))
    def text(self):return self.raw().decode('utf-8')
    def __enter__(self):return self
    def __exit__(self,*args):self.close()

class Namespace(Object):
    def __init__(self,handle):
        Object.__init__(self,handle);self._modules={};self._loading={}
    def _entries(self):
        raw=self.raw()
        if raw[:8]!=b'P2PROJ01' or _hash(raw[:-8])!=int.from_bytes(raw[-8:],'big'):raise ValueError('invalid namespace')
        result=[];off=32
        for i in range(int.from_bytes(raw[24:32],'big')):
            b=int.from_bytes(raw[off:off+8],'big');oid=int.from_bytes(raw[off+8:off+16],'big')
            rights=int.from_bytes(raw[off+16:off+20],'big');kind=int.from_bytes(raw[off+24:off+28],'big')
            n=int.from_bytes(raw[off+28:off+32],'big');label=raw[off+32:off+32+n].decode('utf-8')
            result.append((label,oid,kind,rights,b));off+=32+n
        return result
    def _native(self,name=None,binding=0):
        h=_p.project_call(self.handle,7,name,0,binding)
        kind=_p.obj_info(h)[8]
        return Namespace(h) if kind==19 else Object(h)
    def _view(self):return self.metadata().get('project_view')
    def _sources(self):return [self._native(binding=e[4]) for e in self._entries()]
    def compose(self,name,sources,operation='union',write_source=None):
        if operation not in ('union','intersection') or not sources:raise ValueError('invalid composition')
        if write_source is not None and not 0<=write_source<len(sources):raise ValueError('invalid write source')
        meta=_meta(name,True);meta['project_view']=operation;meta['write_source']=write_source
        h=_p.project_call(self.handle,10,None,0,0,_empty(meta),19,_p.obj_info(self.handle)[10])
        view=Namespace(h)
        try:
            for i,source in enumerate(sources):view.link(str(i),source)
            self.link(name,view)
            return view
        except:
            view.close();raise
    def _writer(self):
        if not self._view():return self
        index=self.metadata().get('write_source')
        if index is None:raise OSError('read-only composed namespace')
        return self._native(str(index))._writer()
    def entries(self,_seen=()):
        mode=self._view()
        if not mode:return self._entries()
        identity=self.identity()
        if identity in _seen:raise OSError('cyclic namespace composition')
        sources=self._sources()
        try:
            rows=[s.entries(_seen+(identity,)) for s in sources];table={}
            for row in rows:
                for e in row:
                    key=(e[0],e[1]);table[key]=e
            if mode=='intersection':
                table={k:e for k,e in table.items() if all(any((r[0],r[1])==k for r in row) for row in rows)}
            return list(table.values())
        finally:
            for s in sources:s.close()
    def names(self):return [e[0] for e in self.entries()]
    def resolve(self,name,_seen=()):
        if '/' in name:return self.open(name)
        mode=self._view()
        if not mode:return self._native(name)
        identity=self.identity()
        if identity in _seen:raise OSError('cyclic namespace composition')
        sources=self._sources();matches=[]
        try:
            for source in sources:
                if name not in source.names():
                    if mode=='intersection':raise OSError('not found in intersection')
                    continue
                matches.append(source.resolve(name,_seen+(identity,)))
            if not matches:raise OSError('not found')
            if any(o.identity()!=matches[0].identity() for o in matches):raise OSError('ambiguous name')
            rights=_READONLY
            for o in matches:rights&=_p.obj_info(o.handle)[10]
            grant=matches[0];selected=None
            index=self.metadata().get('write_source')
            try:
                if index is not None:
                    writer=self._native(str(index))
                    try:
                        if name in writer.names():selected=writer.resolve(name)
                    finally:writer.close()
                    if selected is not None and selected.identity()==grant.identity():
                        grant=selected;rights=_p.obj_info(grant.handle)[10]
                h=_p.project_call(grant.handle,8,None,0,0,None,0,rights)
                return Namespace(h) if isinstance(matches[0],Namespace) else Object(h)
            finally:
                if selected is not None:selected.close()
        finally:
            for o in matches:o.close()
            for source in sources:source.close()
    def open(self,path):
        components=[]
        for part in path.split('/'):
            if not part or part=='.':continue
            if part=='..':
                if components:components.pop()
            else:components.append(part)
        current=Namespace(_p.project_call(self.handle,8,None,0,0,None,0,_p.obj_info(self.handle)[10]))
        try:
            for part in components:
                nxt=current.resolve(part);current.close();current=nxt
            return current
        except:
            current.close();raise
    def link(self,name,target):
        # Source links during composition are native bindings in the descriptor.
        return _p.project_call(self.handle,3,name,target.handle,0,None,0,_p.obj_info(target.handle)[10]&_p.obj_info(self.handle)[10])
    def create(self,name,data=b'',directory=False):
        if self._view():
            writer=self._writer()
            try:return writer.create(name,data,directory)
            finally:writer.close()
        meta=_meta(name,directory)
        payload=_empty(meta) if directory else _frame(data,meta)
        _p.project_call(self.handle,2,name,0,0,payload,19 if directory else 3,_p.obj_info(self.handle)[10])
        return self.resolve(name)
    def mkdir(self,name):return self.create(name,directory=True)
    def remove(self,name):
        if self._view():
            writer=self._writer()
            try:return writer.remove(name)
            finally:writer.close()
        matches=[e for e in self._entries() if e[0]==name]
        if len(matches)!=1:raise OSError('not found or ambiguous')
        return _p.project_call(self.handle,4,None,0,matches[0][4])
    def rename(self,name,destination,newname,replace=False):
        if self._view() or destination._view():
            src=self._writer();dst=destination._writer()
            try:return src.rename(name,dst,newname,replace)
            finally:
                if src is not self:src.close()
                if dst is not destination:dst.close()
        matches=[e for e in self._entries() if e[0]==name]
        if len(matches)!=1:raise OSError('not found or ambiguous')
        return _p.project_call(self.handle,5,newname,destination.handle,matches[0][4],None,0,0,0,replace)
    def activate(self):
        global _active
        _active=self
        return self
    def reload(self,name):
        self._modules.pop(name,None)
        return self.import_module(name)
    def import_module(self,name):
        if name in self._modules:return self._modules[name]
        if name in self._loading:return self._loading[name]
        parts=name.split('.');parent=self
        package=''
        if len(parts)>1:
            ancestor=self.import_module('.'.join(parts[:-1]));parent=ancestor.__project_directory__;package='.'.join(parts[:-1])
        leaf=parts[-1];names=parent.names();options=[n for n in (leaf+'.py',leaf+'.mpy',leaf) if n in names]
        if len(options)!=1:raise ImportError('ambiguous or missing project module: '+name)
        target=parent.resolve(options[0]);directory=None
        try:
            if isinstance(target,Namespace):
                directory=target;names=directory.names();options=[n for n in ('__init__.py','__init__.mpy') if n in names]
                if len(options)!=1:raise ImportError('package requires one __init__: '+name)
                code=directory.resolve(options[0]);package=name
            else:code=target
            if not (_p.obj_info(code.handle)[10]&_EXECUTE):raise OSError('module load denied')
            module=_p.project_module();g=module.__dict__
            g.update({'__name__':name,'__package__':package,'__project_context__':self,
                      '__project_directory__':directory,'__file__':'<project:'+str(code.identity())+'>'})
            if directory:g['__path__']=[g['__file__']]
            self._loading[name]=module
            try:_p.project_exec(module,code.raw(),options[0].endswith('.mpy'))
            except:
                self._loading.pop(name,None)
                raise
            self._loading.pop(name,None);self._modules[name]=module
            if len(parts)>1:setattr(ancestor,leaf,module)
            return module
        finally:
            if directory:code.close()
            else:target.close()

def from_handle(handle):return Namespace(handle)
def _import(name,globals=None,locals=None,fromlist=(),level=0):
    context=globals.get('__project_context__') if globals else None
    if context is None:context=_active
    if context is None:return None
    if level:
        package=globals.get('__package__','') if globals else ''
        parts=package.split('.') if package else []
        if level>len(parts):raise ImportError('relative import outside project package')
        prefix='.'.join(parts[:len(parts)-level+1]);name=prefix+('.'+name if name else '')
    module=context.import_module(name)
    if fromlist:
        for item in fromlist:
            if item!='*' and not hasattr(module,item):setattr(module,item,context.import_module(name+'.'+item))
        return module
    return context.import_module(name.split('.')[0])

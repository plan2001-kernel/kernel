"""Unix path interpretation over an explicitly supplied native namespace."""
import plan9 as _p
import namespace as _namespace

class Filesystem:
    def __init__(self,namespace):
        if not isinstance(namespace,_namespace.Namespace):raise TypeError('namespace required')
        self._root=_namespace.Namespace(_p.project_call(namespace.handle,8,None,0,0,None,0,_p.obj_info(namespace.handle)[10]))
    def close(self):self._root.close()
    def __enter__(self):return self
    def __exit__(self,*args):self.close()
    def open(self,path):
        if not isinstance(path,str):raise TypeError('path required')
        if not path:raise OSError('empty path')
        root=self._root
        stack=[_namespace.Namespace(_p.project_call(root.handle,8,None,0,0,None,0,_p.obj_info(root.handle)[10]))]
        try:
            for part in path.split('/'):
                if not part:continue
                if not isinstance(stack[-1],_namespace.Namespace):raise OSError('not a directory')
                if part=='.':continue
                if part=='..':
                    if len(stack)>1:stack.pop().close()
                else:stack.append(stack[-1].resolve(part))
            if path.endswith('/') and not isinstance(stack[-1],_namespace.Namespace):raise OSError('not a directory')
            return stack.pop()
        finally:
            for item in stack:item.close()

def open_filesystem(namespace):return Filesystem(namespace)

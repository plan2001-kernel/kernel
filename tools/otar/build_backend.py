#!/usr/bin/env python3
"""Build the native GE/O engine for one synchronous Linux host process."""
import argparse
from pathlib import Path
import subprocess
p=argparse.ArgumentParser();p.add_argument('--kernel',type=Path,default=Path(__file__).resolve().parents[3]/'kernel');p.add_argument('--output',type=Path,required=True);a=p.parse_args()
k=a.kernel.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
here=Path(__file__).resolve().parent
# Generated translation units use host glue while compiling actual GE storage
# sources. No formatter, FS/9P server or alternate disk implementation is linked.
for name in ['gdat.h','gfns.h','objint.h']:(out/name).write_bytes((k/'ge/storage'/name).read_bytes())
(out/'gefs.h').write_bytes((here/'host.h').read_bytes())
names=['blk','cache','hash','pack','tree','error','load','snap','o','obj','fs']
for name in names:
 s=(k/'ge/storage'/f'{name}.c').read_text()
 if name=='fs':s=s[:s.index('/* Plan2001: the queue')]
 if name=='blk':
  s=s.replace('qput(Syncq *q, Qent qe)','queued_put(Syncq *q, Qent qe)')
  s+='''\n/* Offline host: ordered synchronous I/O, no competing workers. */
void qput(Syncq *q,Qent e) {
 Gefs *fs=q->fs;
 if(e.b){setflag(e.b,0,Bqueued);e.b->queued=0;}
 switch(e.op){
 case Qwrite: if(!checkflag(e.b,Bfreed,Bstatic))syncblk(e.b);dropblk(e.b);break;
 case Qfree: blkdealloc_lk(getarena(fs,e.bp.addr),e.bp.addr);break;
 case Qfence: --fs->syncing;break;
 default: sysfatal("invalid synchronous queue operation");
 }
}
'''
 (out/f'{name}.c').write_text(s)
s=(k/'tools/host/ge-compat/lib9compat.c').read_text()
s='#include "gefs.h"\n'+s[s.index('Gefs\t*fs;'):]
s=s.replace('broke(char *fmt, ...)','broke(Gefs *fs, char *fmt, ...)')
s=s.replace('_trace(char *msg, Bptr bp, vlong v0, vlong v1)','_trace(Gefs *fs, char *msg, Bptr bp, vlong v0, vlong v1)')
s=s.replace('_babort(vlong addr, char *msg)','_babort(Gefs *fs, vlong addr, char *msg)')
s=s.replace('void\n_assert(char *s)\n{','extern void otar_backtrace(void);\nvoid\n_assert(char *s)\n{\n otar_backtrace();')
s=s.replace('void lock(Lock *l) { USED(l); }','void lock(Lock *l) { assert(!l->val);l->val=1; }').replace('void unlock(Lock *l) { USED(l); }','void unlock(Lock *l) { assert(l->val);l->val=0; }')
s=s.replace('void qlock(QLock *l) { USED(l); }','void qlock(QLock *l) { assert(!l->locked);l->locked=1; }').replace('void qunlock(QLock *l) { USED(l); }','void qunlock(QLock *l) { assert(l->locked);l->locked=0; }').replace('int canqlock(QLock *l) { USED(l); return 1; }','int canqlock(QLock *l) { if(l->locked)return 0;l->locked=1;return 1; }')
s=s.replace('void rsleep(Rendez *r) { USED(r); }','void rsleep(Rendez *r) { USED(r); error("synchronous host resource exhausted"); }').replace('format-o:', 'otar-ge:')
(out/'compat.c').write_text(s)
flags=['gcc','-std=gnu11','-m64','-O2','-g','-fno-builtin','-fno-strict-aliasing','-ffunction-sections','-fdata-sections','-DPLAN2001_GCC','-DPLAN2001_GCC_USER','-include',str(k/'kernel/compat/plan2001-gcc.h'),'-I'+str(k/'include/amd64'),'-I'+str(k/'include'),'-I'+str(out),'-I'+str(k/'runtime/libc/fmt')]
sources=[out/f'{n}.c' for n in names]+[out/'compat.c',here/'core.c',k/'tools/host/ge-compat/avl.c']+[k/'runtime/libc/port'/f'{n}.c' for n in ['rune','utflen','ctype']]+[k/'runtime/libc/fmt'/f'{n}.c' for n in ['dofmt','errfmt','fltfmt','fmt','fmtfd','fmtlock','fmtprint','fprint','print','snprint','sprint','vfprint','vsnprint']]
objects=[]
for i,src in enumerate(sources):
 obj=out/f'{i}.o';subprocess.run(flags+['-c',str(src),'-o',str(obj)],check=True);objects.append(str(obj))
for name,src in [('setjmp',k/'tools/host/ge-compat/setjmp.S'),('main',here/'backend.c')]:
 obj=out/f'{name}.o';subprocess.run(['gcc','-g','-O2','-c',str(src),'-o',str(obj)],check=True);objects.append(str(obj))
subprocess.run(['gcc','-m64','-Wl,--gc-sections','-o',str(out/'otar-ge'),*objects,'-lm'],check=True)
print(out/'otar-ge')

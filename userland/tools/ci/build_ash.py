#!/usr/bin/env python3
"""Build a pinned, reduced upstream ash with genuine native Newlib adapters."""
import argparse,json,subprocess,hashlib
from pathlib import Path
from newlib_full_link import audit
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser()
for name in ('kernel','busybox','reports'): p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args(); b=a.busybox.resolve(); k=a.kernel.resolve(); reports=a.reports.resolve()
o=reports/'ash-native';o.mkdir(parents=True,exist_ok=True)
log=(o/'build.log').open('w')
def run(args,**kw):
 log.write('$ '+' '.join(map(str,args))+'\n');log.flush()
 subprocess.run(list(map(str,args)),stdout=log,stderr=subprocess.STDOUT,check=True,**kw)
lock=json.loads((ROOT/'config/sources.json').read_text())
if subprocess.check_output(['git','-C',str(b),'rev-parse','HEAD'],text=True).strip()!=lock['busybox']['commit']: raise SystemExit('BusyBox source pin mismatch')
run(['make','-C',b,'allnoconfig'])
c=b/'.config';s=c.read_text()
for flag in ('ASH','SH_IS_ASH','ASH_OPTIMIZE_FOR_SIZE','ASH_INTERNAL_GLOB','ASH_ALIAS','ASH_ECHO','ASH_PRINTF','ASH_TEST','ASH_GETOPTS','FEATURE_SH_MATH','STATIC'):
 s=s.replace('# CONFIG_'+flag+' is not set','CONFIG_'+flag+'=y')
c.write_text(s);run(['make','-C',b,'oldconfig'],input='\n'*10000,text=True);run(['make','-C',b,'prepare'])
c=b/'include/platform.h';s=c.read_text()
for flag in ('HAVE_MNTENT_H','HAVE_SYS_STATFS_H','HAVE_NET_ETHERNET_H','HAVE_DEV_FD','HAVE_UNLOCKED_LINE_OPS','HAVE_UNLOCKED_STDIO'):
 s=s.replace('#define '+flag+' 1','/* Native Newlib: '+flag+' unavailable */')
c.write_text(s)
nl=reports/'newlib-full';rt=reports/'newlib-target'
flags=['gcc-14','-m64','-Os','-std=gnu11','-ffreestanding','-nostdinc','-fno-builtin','-fno-pie','-fno-pic','-fno-stack-protector','-ffunction-sections','-fdata-sections','-D_GNU_SOURCE','-DSA_RESTART=2','-DBB_VER="1.36.1"','-DBB_EXTRA_VERSION=""','-Werror=implicit-function-declaration','-Werror=int-conversion','-I'+str(ROOT/'ports/newlib/ash/include'),'-I'+str(b/'include'),'-I'+str(b/'shell'),'-isystem',str(nl/'include'),'-isystem',subprocess.check_output(['gcc-14','-print-file-name=include'],text=True).strip(),'-include',str(b/'include/autoconf.h')]
units=[b/x for x in json.loads((ROOT/'config/ash-sources.json').read_text())]+[ROOT/'ports/newlib/ash/libbb_compat.c',ROOT/'ports/unix/fnmatch.c']
ash_source=o/'ash.c'
ash_text=(b/'shell/ash.c').read_text()
needle='/* Keep these in proper order since it is searched via bsearch() */'
if ash_text.count(needle)!=1: raise SystemExit('BusyBox ash builtin table layout changed')
ash_text=ash_text.replace(needle,
    'extern int p9_ash_ls_builtin(int,char **);\n'+needle)
needle='\t{ BUILTIN_REGULAR       "printf"  , printfcmd  },'
if ash_text.count(needle)!=1: raise SystemExit('BusyBox ash printf builtin layout changed')
ash_text=ash_text.replace(needle,
    '\t{ BUILTIN_REGULAR       "ls"      , p9_ash_ls_builtin },\n'+needle)
ash_source.write_text(ash_text)
ash_index=units.index(b/'shell/ash.c')
units[ash_index]=ash_source
units.append(ROOT/'ports/newlib/ash/ls_builtin.c')
objects=[]
for i,source in enumerate(units):
 obj=o/(str(i)+'.o');run(flags+['-c',source,'-o',obj]);objects.append(obj)
main=o/'main.o';run(flags+['-c',ROOT/'ports/newlib/ash/main.c','-o',main])
archive=o/'ash-libbb.a'
if archive.exists(): archive.unlink()
run(['ar','rcs',archive,*objects])
elf=o/'ash.elf'
run(['gcc-14','-m64','-nostdlib','-static','-no-pie','-Wl,--wrap=longjmp','-Wl,--gc-sections','-Wl,--build-id=none','-Wl,-z,noexecstack','-Wl,-T,'+str(ROOT/'ports/newlib/full/user-newlib.ld'),'-o',elf,k/'build/gcc/mpy/mpyobj/rt_startup.o',rt/'startup.o',main,*[rt/(x+'.o') for x in ('syscalls','process','inherit','file_state','fd_table','posix','dirent','ash_posix','pipe','pvd','uxfs','uxshell','uxservice','uxfd','rt_stubs-newlib')],'-Wl,--start-group',archive,nl/'libc.a',nl/'libm.a','-lgcc','-Wl,--end-group'])
result=audit(elf,o,['_start','ash_main','main','fork','execve','waitpid'])
(o/'manifest.json').write_text(json.dumps({'artifact':result,'busybox':lock['busybox'],'kernel':subprocess.check_output(['git','-C',str(k),'rev-parse','HEAD'],text=True).strip()},indent=2)+'\n')
(o/'LICENSE.busybox').write_bytes((b/'LICENSE').read_bytes())
(o/'busybox.config').write_bytes((b/'.config').read_bytes())
print(elf)

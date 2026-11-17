#!/usr/bin/env python3
"""Build a native file-to-file TinyCC and an installable Newlib sysroot."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from newlib_full_link import audit

ROOT = Path(__file__).resolve().parents[2]
RUNTIME_UNITS = ('syscalls','process','inherit','file_state','fd_table','posix',
    'stdio_process','dirent','ash_posix','pipe','pvd','uxfs','uxobject','objtail',
    'uxshell','uxservice','uxfd','project_native','project_record','rt_stubs-newlib')


def main():
    p=argparse.ArgumentParser()
    for name in ('kernel','tinycc','newlib-source','reports','output'):
        p.add_argument('--'+name,required=True,type=Path)
    a=p.parse_args()
    kernel,tcc,newlib,reports,out=(x.resolve() for x in
        (a.kernel,a.tinycc,a.newlib_source,a.reports,a.output))
    policy=json.loads((ROOT/'config/toolchain-sources.json').read_text())
    for name,path in (('tinycc',tcc),('newlib',newlib)):
        sha=subprocess.check_output(['git','-C',path,'rev-parse','HEAD'],text=True).strip()
        if sha!=policy[name]['commit']: raise SystemExit(name+': source commit mismatch')
    if out.exists(): raise SystemExit('refusing existing output '+str(out))
    out.mkdir(parents=True)
    src=out/'source'
    shutil.copytree(tcc,src,ignore=shutil.ignore_patterns('.git'))
    with (out/'build.log').open('w') as log:
        def run(cmd):
            log.write(' '.join(map(str,cmd))+'\n');log.flush()
            subprocess.run(list(map(str,cmd)),check=True,stdout=log,stderr=subprocess.STDOUT)
        run(['patch','--batch','-d',src,'-p1','-i',ROOT/'ports/tinycc/plan2001.patch'])
        shutil.copy(ROOT/'ports/tinycc/config.h',src/'config.h')
        gccinc=subprocess.check_output(['gcc-14','-print-file-name=include'],text=True).strip()
        flags=['gcc-14','-m64','-O2','-std=gnu11','-ffreestanding','-nostdinc',
            '-fno-builtin','-fno-stack-protector','-fno-pie','-fno-pic','-fno-strict-aliasing',
            '-ffunction-sections','-fdata-sections',
            '-I'+str(ROOT/'ports/newlib/ash/include'),'-I'+str(ROOT/'ports/newlib/full'),
            '-I'+str(kernel/'runtime/abi'),'-I'+str(kernel/'runtime'),
            '-isystem',str(reports/'newlib-full/include'),'-isystem',gccinc,'-I'+str(src)]
        objects=[]
        for name,source in [('tcc',src/'tcc.c'),('native',ROOT/'ports/tinycc/native.c')]:
            obj=out/(name+'.o');run(flags+['-c',source,'-o',obj]);objects.append(obj)
        rt=reports/'newlib-target'
        native=[rt/(x+'.o') for x in RUNTIME_UNITS]
        if (rt/'project_stage.o').exists(): native.append(rt/'project_stage.o')
        startup=kernel/'build/gcc/mpy/mpyobj/rt_startup.o'
        link=['gcc-14','-m64','-nostdlib','-static','-no-pie','-Wl,--wrap=longjmp',
            '-Wl,--gc-sections','-Wl,--build-id=none','-Wl,-z,noexecstack',
            '-Wl,-T,'+str(ROOT/'ports/newlib/full/user-newlib.ld'),
            startup,rt/'startup.o',*objects,*native,'-Wl,--start-group',
            reports/'newlib-full/libc.a',reports/'newlib-full/libm.a','-lgcc','-Wl,--end-group',
            '-o',out/'tcc.elf']
        run(link)
        result=audit(out/'tcc.elf',out,['_start','main','tcc_compile','tcc_output_file'])
        driver=[]
        for name,source in [('bootstrap',ROOT/'tests/guest/tinycc_bootstrap.c'),
                            ('install',ROOT/'ports/tinycc/install.c')]:
            obj=out/(name+'.o')
            run(flags+['-I'+str(ROOT/'ports/tinycc'),'-c',source,'-o',obj]);driver.append(obj)
        driver_link=[str(x) for x in link]
        first=driver_link.index(str(objects[0]))
        driver_link[first:first+len(objects)]=[str(x) for x in driver]
        driver_link[-1]=str(out/'bootstrap.elf')
        run(driver_link)
        audit(out/'bootstrap.elf',out,['_start','main','p2001_toolchain_install'])
        sysroot=out/'sysroot'
        (sysroot/'usr/lib/tcc/include').mkdir(parents=True)
        (sysroot/'usr/bin').mkdir()
        (sysroot/'usr/share/licenses/tinycc').mkdir(parents=True)
        (sysroot/'usr/share/licenses/newlib').mkdir(parents=True)
        shutil.copytree(reports/'newlib-full/include',sysroot/'usr/include')
        for header in (tcc/'include').glob('*.h'):
            shutil.copy(header,sysroot/'usr/lib/tcc/include'/header.name)
        shutil.copy(out/'tcc.elf',sysroot/'usr/bin/tcc')
        for lib in ('libc.a','libm.a'): shutil.copy(reports/'newlib-full'/lib,sysroot/'usr/lib'/lib)
        # Preserve the Newlib longjmp behind the native signal-aware wrapper.
        run(['objcopy','--redefine-sym','longjmp=__real_longjmp',sysroot/'usr/lib/libc.a'])
        native_dir=out/'runtime';native_dir.mkdir()
        members=[]
        for obj in native:
            target=native_dir/obj.name;shutil.copy(obj,target);members.append(target)
        run(['objcopy','--redefine-sym','__wrap_longjmp=longjmp',native_dir/'process.o'])
        run(['ar','rcs',sysroot/'usr/lib/libplan2001.a',*members])
        run(['ld','-r',startup,rt/'startup.o','-o',sysroot/'usr/lib/crt1.o'])
        support=[]
        for unit in ('libtcc1.c','stdatomic.c','atomic.S','builtin.c','alloca.S','alloca-bt.S','va_list.c','dsohandle.c'):
            source=tcc/'lib'/unit
            if not source.exists(): source=tcc/'lib'/('x86_64-'+unit)
            obj=out/(unit.replace('.','-')+'.o')
            run(flags+['-I'+str(tcc/'include'),'-c',source,'-o',obj]);support.append(obj)
        run(['ar','rcs',sysroot/'usr/lib/tcc/libtcc1.a',*support])
        for name in ('COPYING','RELICENSING'): shutil.copy(tcc/name,sysroot/'usr/share/licenses/tinycc'/name)
        shutil.copy(newlib/'COPYING.NEWLIB',sysroot/'usr/share/licenses/newlib/COPYING.NEWLIB')
        # Relinkable compiler object and native runtime are kept with the bundle
        # to support LGPL relinking, alongside the exact upstream source archive.
        shutil.copy(out/'tcc.o',sysroot/'usr/lib/tcc/tcc.o')
        shutil.copy(out/'native.o',sysroot/'usr/lib/tcc/native.o')
        result['sources']=policy
        result['kernel_commit']=subprocess.check_output(['git','-C',kernel,'rev-parse','HEAD'],text=True).strip()
        result['userland_commit']=subprocess.check_output(['git','-C',ROOT,'rev-parse','HEAD'],text=True).strip()
        result['userland_worktree_dirty']=bool(subprocess.check_output(['git','-C',ROOT,'status','--porcelain'],text=True).strip())
        result['files']={str(f.relative_to(sysroot)):{'bytes':f.stat().st_size,
            'sha256':hashlib.sha256(f.read_bytes()).hexdigest()}
            for f in sorted(sysroot.rglob('*')) if f.is_file()}
        result['guest_execution']='NOT RUN'
        (out/'manifest.json').write_text(json.dumps(result,indent=2)+'\n')
        run(['git','-C',tcc,'archive','--format=tar.gz','--output='+str(out/'tinycc-source.tar.gz'),'HEAD'])
        print(out/'tcc.elf')


if __name__=='__main__': main()

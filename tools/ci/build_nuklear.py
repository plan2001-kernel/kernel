#!/usr/bin/env python3
"""Build the native Nuklear software renderer and install its actual ELF."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from newlib_full_link import audit
ROOT=Path(__file__).resolve().parents[2]

def main():
    p=argparse.ArgumentParser()
    for name in ('kernel','nuklear','reports','toolchain'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();kernel,nuklear,reports,toolchain=(x.resolve() for x in (a.kernel,a.nuklear,a.reports,a.toolchain))
    policy=json.loads((ROOT/'config/gui-sources.json').read_text())['nuklear']
    sha=subprocess.check_output(['git','-C',nuklear,'rev-parse','HEAD'],text=True).strip()
    if sha!=policy['commit']:raise SystemExit('Nuklear source revision mismatch')
    out=toolchain/'gui';out.mkdir(exist_ok=True)
    api=out/'include/plan2001';api.mkdir(parents=True,exist_ok=True)
    for header in (kernel/'runtime/abi').glob('*.h'):shutil.copy(header,api/header.name)
    shutil.copy(ROOT/'ports/gui/plan2001/surface.h',api/'surface.h')
    with (out/'build.log').open('w') as log:
        def run(cmd):subprocess.run(list(map(str,cmd)),check=True,stdout=log,stderr=subprocess.STDOUT)
        gccinc=subprocess.check_output(['gcc-14','-print-file-name=include'],text=True).strip()
        flags=['gcc-14','-m64','-O2','-std=gnu11','-ffreestanding','-nostdinc','-fno-builtin','-fno-stack-protector',
            '-fno-pie','-fno-pic','-fno-strict-aliasing','-ffunction-sections','-fdata-sections',
            '-I'+str(nuklear),'-I'+str(out/'include'),'-I'+str(ROOT/'ports/newlib/full'),'-I'+str(ROOT/'ports/newlib/ash/include'),
            '-I'+str(kernel/'runtime/abi'),'-isystem',str(reports/'newlib-full/include'),'-isystem',gccinc]
        run(flags+['-c',ROOT/'ports/gui/nuklear_demo.c','-o',out/'demo.o'])
        for unit in ('native_runtime','native_session'):
            run(flags+['-c',ROOT/'ports/gui'/(unit+'.c'),'-o',out/(unit+'.o')])
        rt=reports/'newlib-target';elf=out/'nuklear-demo.elf'
        run(['gcc-14','-m64','-nostdlib','-static','-no-pie','-Wl,--gc-sections',
            '-Wl,--build-id=none','-Wl,-z,noexecstack','-Wl,-T,'+str(ROOT/'ports/newlib/full/user-newlib.ld'),
            kernel/'build/gcc/mpy/mpyobj/rt_startup.o',out/'native_runtime.o',out/'native_session.o',out/'demo.o',rt/'rt_stubs-newlib.o',
            '-Wl,--start-group',reports/'newlib-full/libc.a',reports/'newlib-full/libm.a','-lgcc','-Wl,--end-group','-o',elf])
        result=audit(elf,out,['_start','main','p9_surface_map','p9_surface_present','nk_begin_titled','nk_button_label'])
        symbols=subprocess.check_output(['nm','--defined-only',elf],text=True)
        for forbidden in ('p9_process_startup','p9_full_auto_bind_uxpipe','uxpipe_bind','p9_file_attach'):
            if any(line.split()[-1:]==[forbidden] for line in symbols.splitlines()):
                raise SystemExit('Unix runtime unexpectedly linked: '+forbidden)
        sysroot=toolchain/'sysroot';shutil.copy(elf,sysroot/'usr/bin/nuklear-demo')
        license_dir=sysroot/'usr/share/licenses/nuklear';license_dir.mkdir(parents=True,exist_ok=True)
        shutil.copy(nuklear/'LICENSE',license_dir/'LICENSE')
        # Also make the toolkit and native graphics API available to guest C code.
        shutil.copy(nuklear/'nuklear.h',sysroot/'usr/include/nuklear.h')
        headers=sysroot/'usr/include/plan2001';headers.mkdir(exist_ok=True)
        for header in api.glob('*.h'):shutil.copy(header,headers/header.name)
        examples=sysroot/'usr/share/examples';examples.mkdir(exist_ok=True)
        shutil.copy(ROOT/'ports/gui/nuklear_demo.c',examples/'nuklear_demo.c')
        run(['git','-C',nuklear,'archive','--format=tar.gz','--output='+str(toolchain/'nuklear-source.tar.gz'),'HEAD'])
        manifest=json.loads((toolchain/'manifest.json').read_text());manifest['nuklear']={'source':policy,'artifact':result}
        manifest['files']={str(f.relative_to(sysroot)):{'bytes':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()}
            for f in sorted(sysroot.rglob('*')) if f.is_file()}
        (toolchain/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        print(elf)
if __name__=='__main__':main()

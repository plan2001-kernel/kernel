#!/usr/bin/env python3
import sys

orch_path, out_path = sys.argv[1], sys.argv[2]
code = open(orch_path).read().splitlines()

lines = []
lines.append("import plan9,space")
lines.append("nl=chr(10).encode()")
lines.append("sh=plan9.obj_open_space(b'init')")
lines.append("cr,cw=plan9.pipenew()")
lines.append("orx,ow=plan9.pipenew()")
lines.append("wr=chr(10).join(('import plan9','sh,cr,ow=plan9.obj_list()','plan9.obj_write(ow,bytes((87,48))+chr(10).encode())','b=bytes()','while 1:','    try:','        c=plan9.obj_read(cr,512,0)','    except OSError:','        break','    if len(c)==0:','        break','    b=b+c','plan9.obj_write(ow,(chr(87)+chr(49)+chr(32)+str(len(b))+chr(10)).encode())','try:','    exec(b.decode())','except Exception as e:','    plan9.obj_write(ow,(chr(69)+chr(82)+chr(82)+chr(32)+str(e)+chr(10)).encode())'))")
lines.append("pid=space.spawn(0x4f45584543000003,argv=('micropython','-c',wr),vhandles=((sh,0),(cr,0),(ow,0)))")
lines.append("plan9.obj_close(cr)")
lines.append("plan9.obj_close(sh)")
lines.append("plan9.obj_close(ow)")
for ln in code:
    if '"' in ln or '\\' in ln:
        raise SystemExit("unsupported character in orchestrator line: %r" % ln)
    lines.append('plan9.obj_write(cw,b"%s"+nl)' % ln)
lines.append("plan9.obj_close(cw)")
lines.append("out=bytes()")
lines.append("while not (b'-RESULT' in out):")
lines.append("    try:")
lines.append("        c=plan9.obj_read(orx,4096,0)")
lines.append("    except OSError:")
lines.append("        break")
lines.append("    if len(c)==0:")
lines.append("        break")
lines.append("    out=out+c")
lines.append("    print(c)")
lines.append("")
open(out_path, "w").write("\n".join(lines) + "\n\n")
print("wrote %s (%d typed lines)" % (out_path, len(lines)))

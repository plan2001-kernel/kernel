#!/usr/bin/env bash
# Disposable QEMU power-loss tests for cross-namespace atomic rename.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
export QEMU_O_BOOT_LIB=1
source "$ROOT/tools/qemu-o-boot.sh"
qob_new_o_image() {
 local img=$1 logdir=$2
 mkdir -p "$logdir"
 MPEXEC="$QOB_MPY" MPYCROSS="$QOB_MPYCROSS" FORMATO="$QOB_FORMAT" \
 "$QOB_ROOT/tools/mkovol" "$img" "$QOB_MIB" "$ROOT/tools/host/project/native.py" > "$logdir/mkovol.log" 2>&1
}
qob_main --no-build --accel kvm:tcg --obootexec 0x4f45584543000003 --timeout 240 --expect PROJECT-NATIVE-PASS "$@"
root_oid=$(sed -n 's/.*PROJECT-ROOT-OID \([0-9]*\).*/\1/p' "$QOB_OUT/boot-boot.log" | head -1)
[ -n "$root_oid" ] || qob_die "missing fixture root identity"
for point in 105 106; do
 cat > "$QOB_OUT/crash-$point.py" <<PY
import plan9 as p
import project
r=project.open_project($root_oid,14793)
a=r.open('left')
b=r.open('right')
f=a.create('crash$point',b'atomic')
p.ownerid_fault($point)
a.rename('crash$point',b,'done$point')
PY
 python3 - "$QOB_OUT/crash-$point.py" <<'PYWRAP'
import pathlib,sys
p=pathlib.Path(sys.argv[1]);lines=p.read_text().splitlines();p.write_text('exec('+ '+chr(10)+'.join(repr(line) for line in lines)+',dict())'+chr(10))
PYWRAP
 QOB_CRASH_AFTER_EXPECT=1 QOB_REPL_SYNC_LINES=0 qob_boot_once "$QOB_OUT/disk.img" "crash-$point" 'PLAN2001-O: REPL READY' repl "$QOB_OUT/crash-$point.py" 'OWNERID-CRASH' || qob_die "crash injection failed: $QOB_BOOT_REASON"
 cat > "$QOB_OUT/verify-$point.py" <<PY
import project
r=project.open_project($root_oid,14793)
a=r.open('left')
b=r.open('right')
assert ('crash$point' in a.names()) != ('done$point' in b.names())
f=a.open('crash$point') if 'crash$point' in a.names() else b.open('done$point')
assert f.raw()==b'atomic'
assert r.links(f)==1
print('PROJECT-RECOVERY-$point-PASS')
PY
 python3 - "$QOB_OUT/verify-$point.py" <<'PYWRAP'
import pathlib,sys
p=pathlib.Path(sys.argv[1]);lines=p.read_text().splitlines();p.write_text('exec('+ '+chr(10)+'.join(repr(line) for line in lines)+',dict())'+chr(10))
PYWRAP
 QOB_REPL_SYNC_LINES=1 qob_boot_once "$QOB_OUT/disk.img" "verify-$point" 'PLAN2001-O: REPL READY' repl "$QOB_OUT/verify-$point.py" "PROJECT-RECOVERY-$point-PASS" || qob_die "crash recovery failed: $QOB_BOOT_REASON"
done
qob_say 'PASS: unpublished rename and torn-superblock recovery preserve atomic bindings and link counts'

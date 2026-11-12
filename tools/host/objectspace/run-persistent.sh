#!/usr/bin/env bash
# Real AMD64 QEMU/OVMF acceptance on ONE disposable GE/O v2 disk.
# Boots: save, restore+replace, SIGKILL with unpublished write, recover.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
OUT=${OBJECTSPACE_PERSIST_OUT:-$ROOT/build/objectspace/persistent}
export QEMU_O_BOOT_LIB=1 QOB_REPL_SYNC_LINES=1
source "$ROOT/tools/qemu-o-boot.sh"
QOB_OUT=$OUT
QOB_MIB=129
QOB_TIMEOUT=420
QOB_CONTROLLER=virtio
QOB_ACCEL=${QOB_ACCEL:-kvm:tcg}
QOB_CPUS=2
QOB_INI_NCPU=2
QOB_INI_BOOTCPU=0
QOB_INI_BOOTEXEC=0x4f45584543000003

while [ "$#" -gt 0 ]; do
  case "$1" in
    --no-build) QOB_BUILD=never ;;
    --rebuild) QOB_BUILD=always ;;
    --output-dir)
      shift
      [ "$#" -gt 0 ] || qob_die "--output-dir needs a path"
      QOB_OUT=$1 ;;
    *) qob_die "usage: $0 [--no-build|--rebuild] [--output-dir DIR]" ;;
  esac
  shift
done

trap qob_cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
qob_prepare_outdir
qob_preflight
qob_build_artifacts
puuid=$(python3 -c 'import uuid; print(uuid.uuid4())')
printf '%s\n' "$puuid" > "$QOB_OUT/disk.partuuid"
qob_new_o_image "$QOB_OUT/o.img" "$QOB_OUT/ream"
qob_write_ini "$QOB_OUT/plan9.ini" "$puuid" /9pc64.elf 1
qob_make_esp "$QOB_OUT/plan9.ini" "$QOB_OUT/esp.img"
qob_make_disk "$QOB_OUT/o.img" "$QOB_OUT/disk.img" \
  "$QOB_OUT/esp.img" "$puuid" "$QOB_OUT/disk.layout"
success='PLAN2001-O: REPL READY'

run_boot() {
  local tag=$1 input=$2 expect=$3
  if ! qob_boot_once "$QOB_OUT/disk.img" "$tag" "$success" repl "$input" "$expect"; then
    qob_show_tail "$QOB_OUT/boot-$tag.log"
    qob_die "$tag failed: $QOB_BOOT_REASON"
  fi
}
run_boot boot1 "$ROOT/tools/host/objectspace/graph-boot1.txt" '^PERSISTENT-BOOT1 [0-9]+ [0-9]+ [0-9]+'
python3 - "$ROOT/tools/host/objectspace" "$QOB_OUT" <<'PY'
from pathlib import Path
import re
import sys
fixtures, out = map(Path, sys.argv[1:])
log = (out / "boot-boot1.log").read_text(errors="replace")
m = re.search(r"^PERSISTENT-BOOT1 (\d+) (\d+) (\d+)", log, re.M)
assert m, "boot1 did not publish a durable graph"
graph, obj, binding = m.groups()
assert int(graph) and int(obj) and int(binding)
(out / "graph-ids.txt").write_text(f"{graph} {obj} {binding}\n")
for src, dest, fields in (
    ("graph-boot2.txt.in", "graph-boot2.txt", {"@GRAPH@": graph, "@BINDING@": binding}),
    ("graph-crash.txt.in", "graph-crash.txt", {"@GRAPH@": graph}),
):
    data = (fixtures / src).read_text()
    for key, value in fields.items():
        data = data.replace(key, value)
    (out / dest).write_text(data)
PY

run_boot boot2 "$QOB_OUT/graph-boot2.txt" '^PERSISTENT-BOOT2 [0-9]+ [0-9]+ [0-9]+'
python3 - "$ROOT/tools/host/objectspace" "$QOB_OUT" <<'PY'
from pathlib import Path
import re
import sys
fixtures, out = map(Path, sys.argv[1:])
log = (out / "boot-boot2.log").read_text(errors="replace")
m = re.search(r"^PERSISTENT-BOOT2 (\d+) (\d+) (\d+)", log, re.M)
assert m, "boot2 did not restore and replace graph"
graph, obj, binding = m.groups()
assert int(graph) and int(obj) and int(binding)
data = (fixtures / "graph-boot4.txt.in").read_text()
data = data.replace("@GRAPH@", graph).replace("@NEWBINDING@", binding)
(out / "graph-boot4.txt").write_text(data)
mid = (fixtures / "graph-midcommit.txt.in").read_text()
mid = mid.replace("@GRAPH@", graph).replace("@NEWBINDING@", binding)
(out / "graph-midcommit.txt").write_text(mid)
PY

# SIGKILL is intentionally abrupt, with no guest shutdown. An unpublished
# replacement must never become the graph read by the next boot.
QOB_REPL_SYNC_LINES=0 QOB_CRASH_AFTER_EXPECT=1 run_boot crash "$QOB_OUT/graph-crash.txt" '^PERSISTENT-UNCOMMITTED'
run_boot boot4 "$QOB_OUT/graph-boot4.txt" '^PERSISTENT-RECOVERY-PASS'

# The earlier crash was before COMMIT was entered. This second fault is
# inside objtxcommit, after staged records were flushed but before the
# published-version header changes. Kill the hung guest, reboot the SAME disk
# and assert the entire previously published snapshot is still visible.
QOB_REPL_SYNC_LINES=0 QOB_CRASH_AFTER_EXPECT=1 run_boot midcommit "$QOB_OUT/graph-midcommit.txt" '^OWNERID-CRASH graph-before-publication'
run_boot after-midcommit "$QOB_OUT/graph-boot4.txt" '^PERSISTENT-RECOVERY-PASS'

# Nested-ObjectSpace acceptance uses the same persistent GE/O volume across
# two more real boots. A saved graph has three nodes, two space edges and a
# persistent leaf, not merely an embedded flat list of GE object bindings.
run_boot nested1 "$ROOT/tools/host/objectspace/graph-nested-boot1.txt" '^NESTED-GRAPH-SAVED [0-9]+ [0-9]+ [0-9]+ [0-9]+'
python3 - "$ROOT/tools/host/objectspace" "$QOB_OUT" <<'PY'
from pathlib import Path
import re
import sys
fixtures, out = map(Path, sys.argv[1:])
log = (out / "boot-nested1.log").read_text(errors="replace")
m = re.search(r"^NESTED-GRAPH-SAVED (\d+) (\d+) (\d+) (\d+)", log, re.M)
assert m, "nested graph was not published"
data = (fixtures / "graph-nested-boot2.txt.in").read_text()
for key, val in zip(("@NESTED_GRAPH@", "@CHILD_BINDING@", "@GRAND_BINDING@", "@LEAF_BINDING@"), m.groups()):
    assert int(val) > 0
    data = data.replace(key, val)
(out / "graph-nested-boot2.txt").write_text(data)
PY
run_boot nested2 "$QOB_OUT/graph-nested-boot2.txt" '^NESTED-GRAPH-REBOOT-PASS'


# QEMU two-boot alias/cycle acceptance on the SAME GE/O disk.
run_boot cyclic1 "$ROOT/tools/host/objectspace/graph-cyclic-boot1.txt" '^CYCLIC-GRAPH-SAVED [0-9]+'
python3 - "$ROOT/tools/host/objectspace" "$QOB_OUT" <<'PY'
from pathlib import Path
import re
import sys
fixtures, out = map(Path, sys.argv[1:])
log = (out / "boot-cyclic1.log").read_text(errors="replace")
m = re.search(r"^CYCLIC-GRAPH-SAVED (\d+) (\d+) (\d+) (\d+) (\d+) (\d+) (\d+)", log, re.M)
assert m, "cyclic graph was not published"
tokens = ("@GRAPH@", "@LEFT@", "@DIRECT@", "@ALIAS@", "@BACK@", "@CYCLE@", "@SELF@")
data = (fixtures / "graph-cyclic-boot2.txt.in").read_text()
for key, val in zip(tokens, m.groups()):
    assert int(val) > 0
    data = data.replace(key, val)
assert "@" not in data, "unreplaced graph ID"
(out / "graph-cyclic-boot2.txt").write_text(data)
PY
run_boot cyclic2 "$QOB_OUT/graph-cyclic-boot2.txt" '^CYCLIC-GRAPH-REBOOT-PASS'


python3 - "$QOB_OUT" <<'PY'
from pathlib import Path
import re
import sys
out = Path(sys.argv[1])
for tag in ("boot1", "boot2", "crash", "boot4", "midcommit", "after-midcommit", "nested1", "nested2", "cyclic1", "cyclic2"):
    log = (out / ("boot-" + tag + ".log")).read_text(errors="replace")
    assert not re.search(r"^(?:Traceback|AssertionError|panic:)", log, re.M), f"guest exception in {tag}"
assert "PERSISTENT-UNCOMMITTED" in (out / "boot-crash.log").read_text()
assert "PERSISTENT-RECOVERY-PASS" in (out / "boot-boot4.log").read_text()
assert "OWNERID-CRASH graph-before-publication" in (out / "boot-midcommit.log").read_text()
assert "PERSISTENT-RECOVERY-PASS" in (out / "boot-after-midcommit.log").read_text()
assert "NESTED-GRAPH-REBOOT-PASS" in (out / "boot-nested2.log").read_text()
assert "CYCLIC-GRAPH-REBOOT-PASS" in (out / "boot-cyclic2.log").read_text()
print("PASS: GE/O ObjectSpace snapshots / nested and cyclic aliasing reboot / pre-publication crash recovery")
PY

#!/usr/bin/env bash
# Trusted persistent ObjectSpace boot root acceptance on real AMD64 QEMU/OVMF.
#
# One disposable GE/O disk across repeated boots:
#   no selector -> save root -> selector -> automatic restore ->
#   committed replacement -> uncommitted crash -> old committed root ->
#   negative selector / missing record / corrupt record / excess grant.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
OUT=${OBJECTSPACE_BOOTROOT_OUT:-$ROOT/build/objectspace/bootroot}
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

# Guest fixtures are sent as one hex-encoded exec() line so the constrained
# PS/2 REPL never has to type Python block syntax. The fixture source stays
# readable and may use ordinary indentation and indexing.
make_repl() {
  local src=$1 dest=$2
  python3 - "$src" "$dest" <<'PY'
from pathlib import Path
import sys
src, dest = map(Path, sys.argv[1:])
data = src.read_bytes()
dest.write_text("exec(bytes.fromhex('" + data.hex() + "').decode())\n")
PY
}

make_repl "$ROOT/tools/host/objectspace/boot-root-save.py.in" \
  "$QOB_OUT/boot-root-save.repl.txt"

run_boot() {
  local tag=$1 input=$2 expect=$3
  if ! qob_boot_once "$QOB_OUT/disk.img" "$tag" "$success" repl "$input" "$expect"; then
    qob_show_tail "$QOB_OUT/boot-$tag.log"
    qob_die "$tag failed: $QOB_BOOT_REASON"
  fi
}

set_boot_graph() {
  QOB_INI_BOOTGRAPH=$1
  qob_write_ini "$QOB_OUT/plan9.ini" "$puuid" /9pc64.elf 1
  qob_update_disk_ini "$QOB_OUT/disk.img" "$QOB_OUT/disk.layout" \
    "$QOB_OUT/plan9.ini"
}

run_fail_boot() {
  local tag=$1 pattern=$2
  if qob_boot_once "$QOB_OUT/disk.img" "$tag" "$success" repl "" ""; then
    qob_show_tail "$QOB_OUT/boot-$tag.log"
    qob_die "$tag unexpectedly reached the REPL"
  fi
  if ! qob_has_ere "$pattern" "$QOB_OUT/boot-$tag.log"; then
    qob_show_tail "$QOB_OUT/boot-$tag.log"
    qob_die "$tag did not report the expected failure: $pattern"
  fi
}

# Boot one: no selector means no automatic root. The guest itself proves the
# empty root set before it saves the graph for the next boot.
run_boot save1 "$QOB_OUT/boot-root-save.repl.txt" \
  '^BOOT-ROOT-SAVED [0-9]+ [0-9]+ [0-9]+'

python3 - "$ROOT/tools/host/objectspace" "$QOB_OUT" <<'PY'
from pathlib import Path
import re
import sys
fixtures, out = map(Path, sys.argv[1:])
log = (out / "boot-save1.log").read_text(errors="replace")
m = re.search(r"^BOOT-ROOT-SAVED (\d+) (\d+) (\d+)", log, re.M)
assert m, "boot1 did not publish the persistent root graph"
graph, obj, binding = m.groups()
assert int(graph) and int(obj) and int(binding)
(out / "graph-id.txt").write_text(graph + "\n")
assert "o-server: BOOTROOT none" in log
assert "objectspace: no boot root selector configured" in log
assert "objectspace: boot root installed" not in log
PY
graph=$(cat "$QOB_OUT/graph-id.txt")
graphhex=$(printf '0x%x' "$graph")
set_boot_graph "$graph"

python3 - "$ROOT/tools/host/objectspace" "$QOB_OUT" "$graph" <<'PY'
from pathlib import Path
import sys
fixtures, out, graph = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
for name in ("boot-root-restore", "boot-root-replace", "boot-root-updated",
             "boot-root-crash-recovery", "boot-root-negative-setup"):
    data = (fixtures / (name + ".py.in")).read_text().replace("@GRAPH@", graph)
    (out / (name + ".py")).write_text(data)
    (out / (name + ".repl.txt")).write_text(
        "exec(bytes.fromhex('" + data.encode().hex() + "').decode())\n")
PY

# Boot two: automatic restoration, without space_load. The fixture proves the
# nested graph, cycle, alias, attenuated rights and leaf bytes, then commits a
# replacement of the same graph OID.
run_boot restore2 "$QOB_OUT/boot-root-restore.repl.txt" \
  '^BOOT-ROOT-RESTORED [0-9]+ [0-9]+ [0-9]+'
grep -aq "o-server: BOOTROOT oid=$graphhex" "$QOB_OUT/boot-restore2.log" ||
  qob_die "boot did not report the selected root OID"
grep -aq "objectspace: boot root installed oid=$graphhex" \
  "$QOB_OUT/boot-restore2.log" ||
  qob_die "boot did not install the restored root"
! grep -aq 'space_load' "$QOB_OUT/boot-restore2.log" ||
  qob_die "boot fixture called space_load manually"

run_boot replace2 "$QOB_OUT/boot-root-replace.repl.txt" \
  '^BOOT-ROOT-REPLACED [0-9]+ [0-9]+ [0-9]+'

# Boot three: the committed replacement is the restored root. It then stages an
# uncommitted replacement of the same OID and is destroyed before publication.
QOB_REPL_SYNC_LINES=0 QOB_CRASH_AFTER_EXPECT=1 \
  run_boot updated3 "$QOB_OUT/boot-root-updated.repl.txt" '^BOOT-ROOT-UNCOMMITTED'
grep -aq '^BOOT-ROOT-UPDATED' "$QOB_OUT/boot-updated3.log" ||
  qob_die "committed replacement was not visible after reboot"

# Boot four: only the last committed root is installed; the staged bytes are
# never treated as a graph and there is no silent substitute.
run_boot crash4 "$QOB_OUT/boot-root-crash-recovery.repl.txt" \
  '^BOOT-ROOT-CRASH-RECOVERY-PASS'

# Prepare malformed and excess-rights records while the valid root is active.
run_boot negsetup "$QOB_OUT/boot-root-negative-setup.repl.txt" \
  '^BOOT-ROOT-NEGATIVE-IDS [0-9]+ [0-9]+'
python3 - "$QOB_OUT" <<'PY'
from pathlib import Path
import re
import sys
out = Path(sys.argv[1])
log = (out / "boot-negsetup.log").read_text(errors="replace")
m = re.search(r"^BOOT-ROOT-NEGATIVE-IDS (\d+) (\d+)", log, re.M)
assert m, "negative setup did not publish its test roots"
(out / "negative-ids.txt").write_text(m.group(1) + " " + m.group(2) + "\n")
PY
read -r badrecord badgrant < "$QOB_OUT/negative-ids.txt"

# Invalid selector text fails before any executable bootstrap starts.
set_boot_graph 'not-an-oid'
run_fail_boot badselector \
  'o-server: bad obootgraph|panic: o-server: obootgraph must be'

# A configured but missing graph OID fails closed.
set_boot_graph '0x123456789abcdef0'
run_fail_boot missingroot \
  'objectspace: boot root restore failed oid=0x123456789abcdef0|panic: objectspace: boot root restore failed'

# A short/corrupt record fails closed.
set_boot_graph "$badrecord"
run_fail_boot corruptroot \
  'objectspace: boot root restore failed|panic: objectspace: boot root restore failed'

# A structurally valid record with an impossible saved rights ceiling fails
# capability validation before any root is installed.
set_boot_graph "$badgrant"
run_fail_boot badgrant \
  'objectspace: boot root restore failed.*invalid graph binding|panic: objectspace: boot root restore failed'

python3 - "$QOB_OUT" <<'PY'
from pathlib import Path
import re
import sys
out = Path(sys.argv[1])
for tag in ("save1", "restore2", "replace2", "updated3", "crash4", "negsetup"):
    log = (out / ("boot-" + tag + ".log")).read_text(errors="replace")
    assert not re.search(r"^(?:Traceback|AssertionError)", log, re.M), f"guest exception in {tag}"
assert "BOOT-ROOT-SAVED" in (out / "boot-save1.log").read_text()
assert "BOOT-ROOT-RESTORED" in (out / "boot-restore2.log").read_text()
assert "BOOT-ROOT-REPLACED" in (out / "boot-replace2.log").read_text()
assert "BOOT-ROOT-UPDATED" in (out / "boot-updated3.log").read_text()
assert "BOOT-ROOT-UNCOMMITTED" in (out / "boot-updated3.log").read_text()
assert "BOOT-ROOT-CRASH-RECOVERY-PASS" in (out / "boot-crash4.log").read_text()
assert "BOOT-ROOT-NEGATIVE-IDS" in (out / "boot-negsetup.log").read_text()
for tag in ("badselector", "missingroot", "corruptroot", "badgrant"):
    log = (out / ("boot-" + tag + ".log")).read_text(errors="replace")
    assert "panic:" in log, f"{tag} did not fail closed"
print("PASS: persistent ObjectSpace boot root save / automatic restore / "
      "replacement / uncommitted crash / negative selector, record and grant")
PY

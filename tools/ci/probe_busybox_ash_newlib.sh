#!/usr/bin/env bash
# Real ash source, generated upstream BusyBox config and actual Newlib
# 4.6.0 target sysroot. NO glibc header or Linux CRT in the target compiler.
set -euo pipefail
[ "$#" -eq 3 ] || {
  echo "usage: probe_busybox_ash_newlib.sh BUSYBOX_SOURCE NEWLIB_REPORTS OUTPUT" >&2
  exit 2
}
BB=$(realpath "$1")
NL=$(realpath "$2")
OUT=$(realpath -m "$3")
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mkdir -p "$OUT"
LOG="$OUT/ash-newlib-build.log"
SUMMARY="$OUT/ash-newlib-summary.md"
INC="$NL/newlib-full/include"
test -f "$INC/sys/times.h" || {
  echo "Genuine Newlib sys/times.h missing" >&2
  exit 1
}
test -f "$INC/unistd.h" || {
  echo "Genuine Newlib unistd.h missing" >&2
  exit 1
}
test -f "$BB/shell/ash.c" || {
  echo "Pinned upstream BusyBox ash.c missing" >&2
  exit 1
}
: > "$LOG"
echo '$ make upstream BusyBox defconfig and generated header preparation' >> "$LOG"
make -C "$BB" defconfig >> "$LOG" 2>&1
# Disable unavailable job-control/fork/signal/TTY promises rather than
# building a shell that silently reports success for missing semantics.
python3 - "$BB/.config" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1])
s=p.read_text()
for setting in ("ASH_JOB_CONTROL", "ASH_BASH_COMPAT",
                "FEATURE_SH_STANDALONE", "FEATURE_SH_NOFORK",
                "ASH_MAIL", "ASH_IDLE_TIMEOUT",
                "FEATURE_UTMP", "FEATURE_WTMP"):
    s=s.replace(f"CONFIG_{setting}=y",f"# CONFIG_{setting} is not set")
p.write_text(s)
PY
yes "" | make -C "$BB" oldconfig >> "$LOG" 2>&1 || true
make -C "$BB" prepare >> "$LOG" 2>&1
# BusyBox's generic platform.h defaults to glibc-like optional APIs,
# including /etc/mtab and statfs, that this freestanding Newlib target
# truly does not have. Edit only the THROWAWAY BusyBox checkout's
# platform-feature assumptions; the original shell/ash.c stays immutable.
python3 - "$BB/include/platform.h" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1])
s=p.read_text()
for name in ("HAVE_MNTENT_H", "HAVE_SYS_STATFS_H",
             "HAVE_NET_ETHERNET_H", "HAVE_DEV_FD",
             "HAVE_UNLOCKED_LINE_OPS", "HAVE_UNLOCKED_STDIO"):
    old=f"#define {name} 1"
    if old not in s:
        raise SystemExit(f"unexpected BusyBox upstream platform.h: {name}")
    s=s.replace(old,f"/* Plan2001/Newlib: {name} unavailable */")
p.write_text(s)
PY
CC=${CC:-gcc-14}
GCCHDR=$("$CC" -print-file-name=include)
test -d "$GCCHDR" || exit 1
test -f "$BB/include/autoconf.h" || {
  echo "BusyBox include/autoconf.h not generated" >&2; exit 1;
}
echo '$ compile original ash.c, ash_ptr_hack.c, shell_common.c using Newlib' >> "$LOG"
flags=(
  -m64 -std=gnu11 -O1 -ffreestanding -nostdinc
  -fno-pie -fno-pic -fno-builtin -fno-stack-protector
  -Werror=implicit-function-declaration -Werror=int-conversion
  -Wno-unused-function -Wno-unused-variable
  -I"$ROOT/ports/newlib/ash/include"
  -I"$BB/include" -I"$BB/shell"
  -isystem "$INC" -isystem "$GCCHDR"
  -include "$BB/include/autoconf.h"
)
rc=0
for unit in ash ash_ptr_hack shell_common; do
  if ! "$CC" "${flags[@]}" \
      -c "$BB/shell/$unit.c" -o "$OUT/$unit-newlib.o" \
      >> "$LOG" 2>&1; then
    rc=1
    break
  fi
done
{
  echo "### Upstream BusyBox ash 1.36.1 / Newlib 4.6.0 target compile"
  echo
  echo "- Original pinned ash.c, real upstream BusyBox generated headers."
  echo "- Real Newlib x86_64-unknown-elf installed headers, not Debian glibc."
  echo "- Newlib sys/times.h present: **PASS**."
  echo "- Original ash.c + two upstream companion modules compiler exit: **$rc**."
  if [ "$rc" -eq 0 ]; then
    echo "- ash.c, ash_ptr_hack.c and shell_common.c: **PASS**."
    nm -u "$OUT/"*-newlib.o | awk '{print $2}' | sort -u \
      >"$OUT/ash-undefined-symbols.txt"
    echo "- Static ash/libbb link: **NOT RUN**; no claimed full shell yet."
    {
      echo "# Still unresolved process/descriptor interface requirements"
      grep -E '^(fork|execve|waitpid|pipe|dup2|fcntl|sigaction|sigprocmask|sigsuspend)
  else
    echo "- Compilation: **FAIL**; inspect attached exact diagnostics."
    echo "- Static ash/libbb link / real QEMU ash: **NOT RUN**."
  fi
} > "$SUMMARY"
tail -80 "$LOG"
exit "$rc"
 \
        "$OUT/ash-undefined-symbols.txt" || true
    } >"$OUT/ash-process-blockers.txt"
    echo "- Missing native process semantics (fork/execve/waitpid/pipe/dup2): see ash-process-blockers.txt."
  else
    echo "- Compilation: **FAIL**; inspect attached exact diagnostics."
    echo "- Static ash/libbb link / real QEMU ash: **NOT RUN**."
  fi
} > "$SUMMARY"
tail -80 "$LOG"
exit "$rc"

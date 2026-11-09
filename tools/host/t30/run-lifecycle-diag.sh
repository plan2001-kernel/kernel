#!/usr/bin/env bash
# Build-time diagnostic run for issue #30: live Pgrp/OContext accounting.
#
# The kernel build adds non-ABI T30LIFE counter lines.  Every T30-LIFE marker
# from process-lifecycle-checks.py must report the same live totals as the
# first marker; a nonzero delta after a scenario is a leaked temporary group
# or context.  The canonical kernel is rebuilt on exit.
set -euo pipefail
T30D_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
T30D_OUT=${T30D_OUT:-$T30D_ROOT/build/t30/diag}
mkdir -p "$T30D_OUT"
T30D_OUT=$(realpath "$T30D_OUT")

restore()
{
  env -u PLAN2001_LIFECYCLE_DIAG "$T30D_ROOT/tools/build-gcc.sh" --kernel \
    >"$T30D_OUT/canonical-build.log" 2>&1 || true
}
trap restore EXIT

PLAN2001_LIFECYCLE_DIAG=1 "$T30D_ROOT/tools/build-gcc.sh" --kernel \
  >"$T30D_OUT/diag-build.log" 2>&1

T30_OUT="$T30D_OUT/run" "$T30D_ROOT/tools/host/t30/run-process-lifecycle-checks.sh" "$@"

python3 - "$T30D_OUT/run/boot-boot.log" <<'PY'
import re
import sys
from pathlib import Path

text = Path(sys.argv[1]).read_text()
event = re.compile(r'T30LIFE ([goc])([+-]) id=(\d+) pgrp=(-?\d+) octx=(-?\d+)\n')
clean = []
rawpos = []
events = []
palloc = pfree = oalloc = ofree = 0
negative = False
last = 0
for m in event.finditer(text):
    clean.append(text[last:m.start()])
    rawpos.extend(range(last, m.start()))
    kind, sign, ident, pgrp, octx = m.groups()
    pgrp, octx = int(pgrp), int(octx)
    events.append((m.start(), (pgrp, octx)))
    if pgrp < 0 or octx < 0:
        negative = True
    if kind == 'g':
        palloc += sign == '+'
        pfree += sign == '-'
    elif kind == 'o':
        oalloc += sign == '+'
        ofree += sign == '-'
    last = m.end()
clean.append(text[last:])
rawpos.extend(range(last, len(text)))
clean = ''.join(clean)


def snapshot(clean_end):
    raw = rawpos[clean_end - 1] if clean_end > 0 else -1
    counts = None
    for at, live in events:
        if at < raw:
            counts = live
        else:
            break
    return counts


markers = []
for m in re.finditer(r'^T30-LIFE (\S+)$', clean, re.M):
    markers.append((m.group(1), snapshot(m.end())))

assert markers, 'no T30-LIFE markers'
assert markers[0][0] == 'base', 'first marker is not base: %r' % (markers[0],)
assert markers[-1][0] == 'end', 'last marker is not end: %r' % (markers[-1],)
base = markers[0][1]
assert base is not None, 'base marker has no lifecycle event'
for name, counts in markers:
    assert counts is not None, 'marker %s has no lifecycle event' % name
    assert counts == base, \
        'live Pgrp/OContext delta at %s: %r != baseline %r' % (name, counts, base)
assert not negative, 'negative live Pgrp/OContext count'
assert palloc + oalloc > 0, 'no lifecycle events observed'
assert palloc - pfree == base[0], \
    'Pgrp allocation/release imbalance: +%d/-%d baseline %d' % (palloc, pfree, base[0])
assert oalloc - ofree == base[1], \
    'OContext allocation/release imbalance: +%d/-%d baseline %d' % (oalloc, ofree, base[1])
print('Verified %d T30-LIFE markers at baseline pgrp=%d octx=%d '
      '(pgrp +%d/-%d, octx +%d/-%d)' %
      (len(markers), base[0], base[1], palloc, pfree, oalloc, ofree))
PY
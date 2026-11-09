"""Parse and verify the opt-in PLAN2001_LIFECYCLE_DIAG log.

The diag kernel emits complete T30LIFE event lines.  A blocking user console
write can interleave with such a line, so the parser removes complete event
lines before matching the user T30-LIFE markers.  Event order is checked with
the diagnostic sequence number captured under the same lock as the counters;
aggregate counts alone are never trusted to identify a leaked or double-freed
Pgrp.
"""

import re
import sys
from pathlib import Path

EVENT = re.compile(r'T30LIFE ([gocdsp])([+-]) seq=(\d+) id=(\d+) '
                   r'pgrp=(-?\d+) octx=(-?\d+)\n')
MARKER = re.compile(r'^T30-LIFE (\S+)$', re.M)

RESOURCE_KIND = {'d': 'delegated Ohand', 's': 'owned Ospace',
                 'p': 'pinned origin'}


def parse(text):
    """Return (markers, summary) or raise ValueError on any violation.

    markers is [(name, (pgrp, octx), live_pgrp_ids, live_delegated_ids,
    live_ospace_ids, live_origin_ids)].  The three delegated-capability
    resource sets must be empty at every marker, so an internal Ohand,
    Ospace or origin reference leaked by an error path is a failure even
    when VobjInfo.holds is unchanged.
    """
    clean = []
    rawpos = []
    events = []
    last = 0
    seen = set()
    live = set()
    dres = {'d': set(), 's': set(), 'p': set()}
    pgrp_count = octx_count = 0
    palloc = pfree = oalloc = ofree = 0
    ralloc = {'d': 0, 's': 0, 'p': 0}
    rfree = {'d': 0, 's': 0, 'p': 0}
    last_seq = -1
    for m in EVENT.finditer(text):
        clean.append(text[last:m.start()])
        rawpos.extend(range(last, m.start()))
        kind, sign, seq, ident, pgrp, octx = m.groups()
        seq, ident, pgrp, octx = int(seq), int(ident), int(pgrp), int(octx)
        if seq <= last_seq:
            raise ValueError('event sequence not monotonic: %d after %d'
                             % (seq, last_seq))
        last_seq = seq
        if pgrp < 0 or octx < 0:
            raise ValueError('negative counter in event seq=%d' % seq)
        if kind == 'g':
            if sign == '+':
                if ident <= 0 or ident in seen:
                    raise ValueError('duplicate/invalid Pgrp allocation id=%d'
                                     % ident)
                seen.add(ident)
                live.add(ident)
                palloc += 1
            else:
                if ident not in live:
                    raise ValueError('Pgrp free of non-live id=%d' % ident)
                live.remove(ident)
                pfree += 1
        elif kind == 'c':
            if ident not in live:
                raise ValueError('NamespaceControl event for non-live id=%d'
                                 % ident)
        elif kind == 'o':
            if sign == '+':
                oalloc += 1
            else:
                ofree += 1
        else:
            target = dres[kind]
            if sign == '+':
                if ident <= 0 or ident in target:
                    raise ValueError('duplicate/invalid %s allocation id=%d'
                                     % (RESOURCE_KIND[kind], ident))
                target.add(ident)
                ralloc[kind] += 1
            else:
                if ident not in target:
                    raise ValueError('%s free of non-live id=%d'
                                     % (RESOURCE_KIND[kind], ident))
                target.remove(ident)
                rfree[kind] += 1
        pgrp_count = pgrp
        octx_count = octx
        if len(live) != pgrp_count:
            raise ValueError('event seq=%d reports pgrp=%d but %d live IDs %r'
                             % (seq, pgrp_count, len(live), sorted(live)))
        events.append((m.start(), (pgrp_count, octx_count),
                       frozenset(live), frozenset(dres['d']),
                       frozenset(dres['s']), frozenset(dres['p'])))
        last = m.end()
    clean.append(text[last:])
    rawpos.extend(range(last, len(text)))
    clean = ''.join(clean)

    if not events:
        raise ValueError('no T30LIFE events')
    if 'T30-FINISH' not in clean:
        raise ValueError('missing T30-FINISH')

    markers = []
    ei = 0
    for m in MARKER.finditer(clean):
        end = m.end()
        raw = rawpos[end - 1] if end > 0 else -1
        while ei < len(events) and events[ei][0] < raw:
            ei += 1
        if ei == 0:
            raise ValueError('marker %s has no lifecycle event' % m.group(1))
        (_pos, counts, live, dmiss, smiss, pmiss) = events[ei - 1]
        markers.append((m.group(1), counts, live, dmiss, smiss, pmiss))

    if not markers:
        raise ValueError('no T30-LIFE markers')
    if markers[0][0] != 'base':
        raise ValueError('first marker is not base')
    if markers[-1][0] != 'end':
        raise ValueError('last marker is not end')
    base_counts, base_live = markers[0][1], markers[0][2]
    if len(base_live) != 1:
        raise ValueError('baseline must contain exactly the permanent anchor '
                         'group, got %r' % (sorted(base_live),))
    for name, counts, live, dmiss, smiss, pmiss in markers:
        if counts != base_counts:
            raise ValueError('live Pgrp/OContext delta at %s: %r != %r'
                             % (name, counts, base_counts))
        if live != base_live:
            raise ValueError('live Pgrp id set changed at %s: %r != %r'
                             % (name, sorted(live), sorted(base_live)))
        if dmiss or smiss or pmiss:
            raise ValueError('live delegated capability resource(s) at %s: '
                             'Ohand=%r Ospace=%r origin=%r'
                             % (name, sorted(dmiss), sorted(smiss),
                                sorted(pmiss)))
    if palloc - pfree != base_counts[0]:
        raise ValueError('Pgrp allocation/release imbalance: +%d/-%d baseline '
                         '%d' % (palloc, pfree, base_counts[0]))
    if oalloc - ofree != base_counts[1]:
        raise ValueError('OContext allocation/release imbalance: +%d/-%d '
                         'baseline %d' % (oalloc, ofree, base_counts[1]))
    for kind in 'dsp':
        if ralloc[kind] != rfree[kind]:
            raise ValueError('%s allocation/release imbalance: +%d/-%d'
                             % (RESOURCE_KIND[kind], ralloc[kind],
                                rfree[kind]))
    return markers, (base_counts, base_live, palloc, pfree, oalloc, ofree,
                     ralloc, rfree)


def main(argv):
    if len(argv) != 2:
        print('usage: lifecycle_log.py LOG', file=sys.stderr)
        return 2
    try:
        markers, summary = parse(Path(argv[1]).read_text())
    except ValueError as exc:
        print('lifecycle diag FAIL:', exc, file=sys.stderr)
        return 1
    (base_counts, base_live, palloc, pfree, oalloc, ofree,
     ralloc, rfree) = summary
    print('Verified %d T30-LIFE markers at baseline pgrp=%d octx=%d '
          '(anchor id=%s; pgrp +%d/-%d, octx +%d/-%d, '
          'delegated Ohand +%d/-%d, Ospace +%d/-%d, origin +%d/-%d)' %
          (len(markers), base_counts[0], base_counts[1],
           ','.join(map(str, sorted(base_live))),
           palloc, pfree, oalloc, ofree,
           ralloc['d'], rfree['d'], ralloc['s'], rfree['s'],
           ralloc['p'], rfree['p']))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

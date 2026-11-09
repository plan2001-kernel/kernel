"""Synthetic fixtures for lifecycle_log.parse (no VM required)."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lifecycle_log


def check(text, should_pass, label):
    try:
        lifecycle_log.parse(text)
    except ValueError as exc:
        if should_pass:
            raise AssertionError('%s unexpectedly failed: %s' % (label, exc))
        return str(exc)
    if not should_pass:
        raise AssertionError('%s unexpectedly passed' % label)
    return ''


VALID = """\
T30LIFE o+ seq=1 id=0 pgrp=0 octx=1
T30LIFE g+ seq=2 id=1 pgrp=1 octx=1
T30-LIFE base
T30LIFE c+ seq=3 id=1 pgrp=1 octx=1
T30LIFE c- seq=4 id=1 pgrp=1 octx=1
T30LIFE o+ seq=5 id=0 pgrp=1 octx=2
T30LIFE g+ seq=6 id=2 pgrp=2 octx=2
T30LIFE g- seq=7 id=2 pgrp=1 octx=2
T30LIFE o- seq=8 id=0 pgrp=1 octx=1
T30-LIFE end
T30-FINISH 1 PASS
"""

UNMATCHED_FREE = """\
T30LIFE o+ seq=1 id=0 pgrp=0 octx=1
T30LIFE g+ seq=2 id=1 pgrp=1 octx=1
T30-LIFE base
T30LIFE g- seq=3 id=2 pgrp=0 octx=1
T30-LIFE end
T30-FINISH 1 PASS
"""

DOUBLE_FREE = """\
T30LIFE o+ seq=1 id=0 pgrp=0 octx=1
T30LIFE g+ seq=2 id=1 pgrp=1 octx=1
T30-LIFE base
T30LIFE g- seq=3 id=1 pgrp=0 octx=1
T30LIFE g- seq=4 id=1 pgrp=0 octx=1
T30-LIFE end
T30-FINISH 1 PASS
"""

DUPLICATE_ALLOC = """\
T30LIFE o+ seq=1 id=0 pgrp=0 octx=1
T30LIFE g+ seq=2 id=1 pgrp=1 octx=1
T30-LIFE base
T30LIFE g+ seq=3 id=1 pgrp=2 octx=1
T30-LIFE end
T30-FINISH 1 PASS
"""

SAME_COUNT_DIFFERENT_SET = """\
T30LIFE o+ seq=1 id=0 pgrp=0 octx=1
T30LIFE g+ seq=2 id=1 pgrp=1 octx=1
T30-LIFE base
T30LIFE g- seq=3 id=1 pgrp=0 octx=1
T30LIFE g+ seq=4 id=2 pgrp=1 octx=1
T30-LIFE end
T30-FINISH 1 PASS
"""

VALID_DELEGATED = """\
T30LIFE o+ seq=1 id=0 pgrp=0 octx=1
T30LIFE g+ seq=2 id=1 pgrp=1 octx=1
T30-LIFE base
T30LIFE s+ seq=3 id=1 pgrp=1 octx=1
T30LIFE p+ seq=4 id=1 pgrp=1 octx=1
T30LIFE d+ seq=5 id=1 pgrp=1 octx=1
T30LIFE s- seq=6 id=1 pgrp=1 octx=1
T30LIFE p- seq=7 id=1 pgrp=1 octx=1
T30LIFE d- seq=8 id=1 pgrp=1 octx=1
T30-LIFE end
T30-FINISH 1 PASS
"""

DELEGATE_LEAK = """\
T30LIFE o+ seq=1 id=0 pgrp=0 octx=1
T30LIFE g+ seq=2 id=1 pgrp=1 octx=1
T30-LIFE base
T30LIFE s+ seq=3 id=1 pgrp=1 octx=1
T30LIFE p+ seq=4 id=1 pgrp=1 octx=1
T30LIFE d+ seq=5 id=1 pgrp=1 octx=1
T30-LIFE end
T30-FINISH 1 PASS
"""

OSPACE_LEAK = """\
T30LIFE o+ seq=1 id=0 pgrp=0 octx=1
T30LIFE g+ seq=2 id=1 pgrp=1 octx=1
T30-LIFE base
T30LIFE s+ seq=3 id=1 pgrp=1 octx=1
T30LIFE p+ seq=4 id=1 pgrp=1 octx=1
T30LIFE d+ seq=5 id=1 pgrp=1 octx=1
T30LIFE p- seq=6 id=1 pgrp=1 octx=1
T30LIFE d- seq=7 id=1 pgrp=1 octx=1
T30-LIFE end
T30-FINISH 1 PASS
"""

ORIGIN_LEAK = """\
T30LIFE o+ seq=1 id=0 pgrp=0 octx=1
T30LIFE g+ seq=2 id=1 pgrp=1 octx=1
T30-LIFE base
T30LIFE s+ seq=3 id=1 pgrp=1 octx=1
T30LIFE p+ seq=4 id=1 pgrp=1 octx=1
T30LIFE d+ seq=5 id=1 pgrp=1 octx=1
T30LIFE s- seq=6 id=1 pgrp=1 octx=1
T30LIFE d- seq=7 id=1 pgrp=1 octx=1
T30-LIFE end
T30-FINISH 1 PASS
"""

UNMATCHED_DELEGATE_FREE = """\
T30LIFE o+ seq=1 id=0 pgrp=0 octx=1
T30LIFE g+ seq=2 id=1 pgrp=1 octx=1
T30-LIFE base
T30LIFE d- seq=3 id=1 pgrp=1 octx=1
T30-LIFE end
T30-FINISH 1 PASS
"""


def main():
    check(VALID, True, 'valid log')
    check(UNMATCHED_FREE, False, 'unmatched free id')
    check(DOUBLE_FREE, False, 'double free')
    check(DUPLICATE_ALLOC, False, 'duplicate allocation id')
    check(SAME_COUNT_DIFFERENT_SET, False, 'same count, different live set')
    check(VALID_DELEGATED, True, 'valid delegated capability events')
    check(DELEGATE_LEAK, False, 'live delegated Ohand at marker')
    check(OSPACE_LEAK, False, 'live delegated Ospace at marker')
    check(ORIGIN_LEAK, False, 'live pinned origin at marker')
    check(UNMATCHED_DELEGATE_FREE, False,
          'unmatched delegated resource free')
    print('PASS lifecycle_log parser fixtures')
    return 0


if __name__ == '__main__':
    sys.exit(main())

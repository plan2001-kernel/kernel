"""Negative ownership fixture for the lifecycle diagnostic checker.

This run deliberately leaks one delegated caps=2 Ohand: the diag-only
`inherit-space-leak-ohand` fault detaches the provider before the Vobj is
released, reproducing the old inherit-space-vhand cleanup bug.  The guest
run itself completes, but lifecycle_log.parse must reject the T30LIFE
ownership log at the `end` marker.  The positive fault suite is not expected
to contain this token.
"""

OID = 0x4f45584543000003


class Counter:
    def __init__(self):
        self.n = 0


shared = Counter()
alias = shared


def init():
    shared.n = 0


def run():
    import plan9 as p

    sh = p.obj_open_space('init')
    print('T30-LIFE base')
    p.envset('PLAN2001_FAULT', b'inherit-space-leak-ohand')
    try:
        p.rforkex(p.RFPROC, ((sh, 8192 | p.VMETA, 1),))
    except OSError:
        pass
    else:
        print('T30-LEAK-NOT-INJECTED')
    print('T30-LIFE end')
    p.obj_close(sh)
    print('T30-FINISH 0 PASS')
    return 1

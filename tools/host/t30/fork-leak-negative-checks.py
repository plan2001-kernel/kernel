"""Negative fixture for the unpublished-New-Proc ownership checker.

The diag-only `fork-child-leak-proc` fault fails at the first prepublication
boundary but deliberately omits procdiscard(), leaving one reserved New Proc
behind.  The guest still reports T30-FINISH, so lifecycle_log.parse alone
must reject the log at the `end` marker.
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

    print('T30-LIFE base')
    p.envset('PLAN2001_FAULT', b'fork-child-leak-proc')
    try:
        p.rforkex(p.RFPROC, ())
    except OSError as e:
        msg = str(e.args[0]) if e.args else ''
        if msg != 'fork: injected New Proc leak':
            print('T30-LEAK-WRONG-ERROR', msg)
    else:
        print('T30-LEAK-NOT-INJECTED')
    print('T30-LIFE end')
    print('T30-FINISH 0 PASS')
    return 1

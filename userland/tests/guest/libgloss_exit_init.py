# Native MicroPython boot process exercises _exit from an ordinary
# Plan2001 child program with no inherited console capabilities.
EXIT_CHILD_OID = 0x4f45584543000005

class Counter:
    def __init__(self, n=0):
        self.n = n

shared = Counter(41)
alias = shared

def init():
    shared.n = 0

def check_exit(status, expected_empty):
    import plan9
    import space
    child = None
    try:
        child = space.spawn(EXIT_CHILD_OID, argv=("exit", status))
        state = plan9.process_wait(child, 30000)
        assert state[3] == plan9.OBJ_PROC_COMPLETED, state
        assert state[2] == plan9.OBJ_PROC_CAUSE_EXIT, state
        assert (state[1] == "") == expected_empty, state
    finally:
        if child is not None:
            plan9.obj_close(child)

def run():
    check_exit("0", True)
    print("PLAN2001-LIBGLOSS-EXIT-ZERO-PASS")
    check_exit("7", False)
    print("PLAN2001-LIBGLOSS-EXIT-NONZERO-PASS")
    print("PLAN2001-LIBGLOSS-EXIT-PASS")
    shared.n += 1
    return 0

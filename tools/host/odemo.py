# Plan2001 Stage 4/5 demo: the module the host preparation tool compiles
# into an O code object.  Its top level runs only on the first boot; a
# restored Space must see the same objects without running it again.
# Stage 5 calls init() once on a fresh image, then run() on every boot.

class Counter:
    def __init__(self, n=0):
        self.n = n


def bump(c):
    c.n += 1
    return c.n


shared = Counter(41)
alias = shared


def init():
    # Stage 5: run() starts the persistent counter at zero on the one
    # boot that initializes a fresh O image.
    shared.n = 0


def run():
    return bump(shared)

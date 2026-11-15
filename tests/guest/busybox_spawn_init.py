# Boot-time MicroPython code: run ordinary BusyBox children via SPAWN.
# Original native MicroPython owns the boot; the BusyBox ELF's normal main()
# executes inside new processes. No diagnostic C launcher and no kernel edit.

BUSYBOX_OID = 0x4f45584543000005


class Counter:
    def __init__(self, n=0):
        self.n = n


# Current pinned MicroPython persistent graph report requires these names.
shared = Counter(41)
alias = shared


def init():
    shared.n = 0


def busybox(*argv):
    import plan9
    import space

    # Spawned children have no ambient stdout grant. Pass ONLY a writable
    # shared region capability: their libc adapter publishes its bytes.
    # This process maps the same region read-only and forwards verified data
    # using its own, already-authorized MicroPython output.
    region = plan9.region_create(4096)
    view = None
    child = None
    try:
        view = plan9.region_map(region, 0, 1)
        child = space.spawn(BUSYBOX_OID, argv=argv,
                            vhandles=[(region, 256)])
        state = plan9.process_wait(child, 30000)
        if state[3] != plan9.OBJ_PROC_COMPLETED:
            raise RuntimeError("BusyBox child did not complete")
        if state[2] != plan9.OBJ_PROC_CAUSE_EXIT:
            raise RuntimeError("BusyBox child did not exit normally")

        frame = plan9.region_read(view, 4096)
        n = 0
        for i in range(8):
            n |= frame[i] << (8 * i)
        if n > 4088:
            raise RuntimeError("stdout region length out of bounds")
        if n > 0:
            print(frame[8:8+n].decode(), end="")
        return 0 if state[1] == "" else 1
    finally:
        if child is not None:
            plan9.obj_close(child)
        if view is not None:
            plan9.region_unmap(view)
        plan9.obj_close(region)


def external_ls():
    """SPAWN a distinct native ls process with RO path-view input and RW output.

    The view contains only names, not GE/O object authority. The caller owns
    both source and destination regions; the child gets attenuated grants.
    """
    import plan9
    import space

    expected = b"bin\ndev\netc\nhome\ntmp\nusr\nvar\n"
    source = plan9.region_create(4096)
    stdout = plan9.region_create(4096)
    writer = None
    observer = None
    child = None
    try:
        writer = plan9.region_map(source)
        frame = bytes([(len(expected) >> (8 * i)) & 255 for i in range(8)]) + expected
        plan9.region_write(writer, frame)
        # The child gets only read access to this region, no writer right.
        observer = plan9.region_map(stdout, 0, 1)
        child = space.spawn(BUSYBOX_OID, argv=("busybox", "ls", "/"),
                            vhandles=[(source, 1), (stdout, 256)])
        state = plan9.process_wait(child, 30000)
        if state[3] != plan9.OBJ_PROC_COMPLETED:
            raise RuntimeError("external ls child still running")
        if state[2] != plan9.OBJ_PROC_CAUSE_EXIT or state[1] != "":
            raise RuntimeError("external ls process exit failure")
        result = plan9.region_read(observer, 4096)
        n = 0
        for i in range(8):
            n |= result[i] << (8 * i)
        if n > 4088 or result[8:8+n] != expected:
            raise RuntimeError("external ls delegated stdout mismatch")
        print("PLAN2001-EXTERNAL-LS-OUTPUT-START")
        print(result[8:8+n].decode(), end="")
        print("PLAN2001-EXTERNAL-LS-PASS")
        return 0
    finally:
        if child is not None:
            plan9.obj_close(child)
        if observer is not None:
            plan9.region_unmap(observer)
        if writer is not None:
            plan9.region_unmap(writer)
        plan9.obj_close(stdout)
        plan9.obj_close(source)


def two_process_pipe():
    """Two independent native SPAWN applets transfer exactly 10,000 bytes.

    Two separately authorized capability regions carry bytes and ACKs.
    Writer gets data Vwrite + ACK Vread; reader gets data Vread + ACK Vwrite.
    No Unix kernel pipe, fd inheritance or implicit volume open is used.
    """
    import plan9
    import space

    data = plan9.region_create(4096)
    ack = plan9.region_create(4096)
    data_rw = ack_rw = data_ro = ack_ro = None
    sending = receiving = None

    def le64(v):
        return bytes([(v >> (8 * i)) & 255 for i in range(8)])

    def number(frame, offset):
        total = 0
        for i in range(8):
            total |= frame[offset + i] << (8 * i)
        return total

    try:
        # Both pages are zeroed by kernel region creation. Format the
        # initial protocol headers while this trusted bootstrap owns RW.
        data_rw = plan9.region_map(data)
        ack_rw = plan9.region_map(ack)
        plan9.region_write(data_rw,
            le64(0x3154414450505855) + le64(1) + le64(0) + le64(0))
        plan9.region_write(ack_rw,
            le64(0x314b434150505855) + le64(1) + le64(0) +
            le64(0) + le64(0))
        plan9.region_unmap(ack_rw)
        ack_rw = None
        plan9.region_unmap(data_rw)
        data_rw = None

        # The parent only observes and never owns either active writer map.
        data_ro = plan9.region_map(data, 0, 1)
        ack_ro = plan9.region_map(ack, 0, 1)

        sending = space.spawn(
            BUSYBOX_OID, argv=("busybox", "uxpipe-send"),
            vhandles=[(data, 256), (ack, 1)])
        receiving = space.spawn(
            BUSYBOX_OID, argv=("busybox", "uxpipe-recv"),
            vhandles=[(data, 1), (ack, 256)])

        a = plan9.process_wait(sending, 30000)
        b = plan9.process_wait(receiving, 30000)
        for state in (a, b):
            if state[3] != plan9.OBJ_PROC_COMPLETED:
                raise RuntimeError("native pipe child did not complete")
            if state[2] != plan9.OBJ_PROC_CAUSE_EXIT or state[1] != "":
                raise RuntimeError("native pipe child failed")

        published = plan9.region_read(data_ro, 40)
        observed = plan9.region_read(ack_ro, 40)
        if (number(published, 16) != 10000 or
            number(observed, 16) != 10000 or
            number(published, 24) != 1 or
            number(observed, 24) != 1 or
            number(observed, 32) != 1):
            raise RuntimeError("native pipe lost data or EOF/ACK")
        print("PLAN2001-UXPIPE-QEMU-PASS")
        return 0
    finally:
        if receiving is not None:
            plan9.obj_close(receiving)
        if sending is not None:
            plan9.obj_close(sending)
        if ack_ro is not None:
            plan9.region_unmap(ack_ro)
        if data_ro is not None:
            plan9.region_unmap(data_ro)
        if ack_rw is not None:
            plan9.region_unmap(ack_rw)
        if data_rw is not None:
            plan9.region_unmap(data_rw)
        plan9.obj_close(ack)
        plan9.obj_close(data)


def run():
    assert busybox("busybox", "echo", "PLAN2001-BUSYBOX-SPAWN-ECHO") == 0
    assert busybox("/bin/true") == 0
    assert busybox("busybox", "false") == 1
    assert external_ls() == 0
    assert two_process_pipe() == 0
    print("PLAN2001-BUSYBOX-SPAWN-PASS")
    shared.n += 1
    return 0

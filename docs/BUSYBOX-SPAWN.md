# Launch native BusyBox as a MicroPython child process

GitHub Actions: [busybox-spawn.yml](../.github/workflows/busybox-spawn.yml).

The boot executable remains the **normal native MicroPython** process.
MicroPython calls its existing **space.spawn(oid, argv=..., vhandles=...)**
API to run the normal three-applet BusyBox ELF in a separate process, then
waits on the returned Process Handle with **plan9.process_wait()**. No
BusyBox-specific kernel changes or special BusyBox test launcher are used.

The latest kernel `main` is fetched for each run and its SHA recorded; BusyBox stays commit-pinned by [sources.json](../config/sources.json).
The host fixture uses the kernel's normal mpy-cross, oseed.py and format-o
tools to create a **fresh disposable, regular-file** GE/O volume.

| Program | GE/O OID |
| --- | --- |
| MicroPython boot owner (normal executable) | `0x4f45584543000003` |
| BusyBox (normal native three-applet executable) | `0x4f45584543000005` |

Both ELF images are verified byte-for-byte with SHA-256 via the native GE/O
inspector. The raw OID alone does **not** authorize execution.

## Explicit stdout delegation

Under this kernel ABI, SPAWN children intentionally start with **no ambient
console iomask**. A console Handle opened by the boot owner has only
`Vwrite` and cannot be delegated because it lacks `Vdelegate`.
Therefore granting arbitrary console access to a child is not a valid fix.

The caller instead creates a 4096-byte `VkindRegion` capability, maps it
read-only, and delegates only `Vwrite` (256) to the child using
`vhandles=[(region, 256)]`. The small BusyBox libc shim detects that
inherited region, maps it writable and publishes the output bytes to an
8-byte length header followed by a bounded payload. After the child exits,
MicroPython reads the region and forwards the data through **its own**
authorized console output. The existing direct-boot BusyBox smoke still
uses its own console Handle when permitted.

This 4088-byte single-publication scheme is an **initial bounded stdout
adapter**, not a general POSIX descriptor, terminal, or streaming protocol.
Overflow and missing grants return errors, not false success. Broader
streaming, backpressure, stderr and pipes require later userland work.

## Direct use in the MicroPython REPL

Once the separate BusyBox ELF is installed and the caller has the authorized
GE/O volume capability, the API can be called directly:

```python
import space, plan9

busybox_oid = 0x4f45584543000005
region = plan9.region_create(4096)
view = plan9.region_map(region, 0, 1)  # read-only mapping
child = None
try:
    child = space.spawn(busybox_oid,
                        argv=["busybox", "echo", "hello"],
                        vhandles=[(region, 256)])  # child only Vwrite
    result = plan9.process_wait(child, 30000)
    # (pid, message, cause, state, generation, user_ms, sys_ms,
    #  real_ms, wait_result, waiters)
    assert result[3] == plan9.OBJ_PROC_COMPLETED
    assert result[2] == plan9.OBJ_PROC_CAUSE_EXIT
    assert result[1] == ""
    frame = plan9.region_read(view, 4096)
    n = 0
    for i in range(8):
        n |= frame[i] << (8 * i)
    assert n <= 4088
    print(frame[8:8+n].decode(), end="")
finally:
    if child is not None:
        plan9.obj_close(child)
    plan9.region_unmap(view)
    plan9.obj_close(region)
```

The Handle returned by `space.spawn` is observation/control authority,
not a PID or a Unix file descriptor. The current native CRT calls EXITS with
an empty message for return code zero and `error` for any nonzero return.
The demonstration exposes only success versus failure, not a full POSIX
numeric exit status.

## QEMU acceptance

The workflow builds the kernel, UEFI loader, MicroPython, native libc and
BusyBox subset, seeds both executables into one temporary GE/O image and
boots the **real kernel in QEMU/OVMF** using `kvm:tcg`.

The MicroPython init script
[`tests/guest/busybox_spawn_init.py`](../tests/guest/busybox_spawn_init.py)
starts `busybox echo`, `/bin/true` and `busybox false` as separate
children, waits for each, checks exit cause/state and success/failure, and
prints the final PASS marker only after all checks. The CI requires the
forwarded original `echo_main` output, the parent PASS marker and a
successful `init: run() -> 0` in the serial log.

A green QEMU run demonstrates a **real child-process launch**, an explicit
output capability, process completion and normal applet execution. It does
not demonstrate a full upstream BusyBox build, a POSIX shell, arbitrary
filesystem tools or physical hardware. The CI never reformats an existing
GE/O volume.

# Native slot-bound graphics and Nuklear

The graphical profile delegates a native console surface from the trusted F6
supervisor to its Unix child. `CHILD_OID project ROOT_OID surface` explicitly
requests this grant. The original kernel console rings remain with the
supervisor, which continues to proxy stdin/stdout/stderr through UXPIPE.
The surface is a separately validated region capability. Plain F5 sessions
receive no surface and keep their own persistent root.

The kernel exposes immutable pixel geometry through `ObjOpSurfaceInfo`.
Pixels are packed uint32 `0x00RRGGBB`. The client maps the ordinary region as
its single writer, draws within the reported byte count and invokes
`ObjOpSurfacePresent`. The kernel copies only the matching focused slot,
preserves its boot marker strip and retains inactive snapshots for focus
restoration. There is no physical framebuffer address or caller-selected
slot in this interface. A child cannot open an ambient session surface;
read-only capabilities cannot present. Releasing the writer restores text.

Newlib propagates the explicitly delegated surface through process context
ABI 5. `p9_surface_map`, `p9_surface_present` and `p9_surface_unmap` are declared
in the installed `<plan2001/surface.h>`. The region must be unmapped before
fork or exec; an attempted operation while mapped fails with EBUSY and leaves
the original mapping intact. The old context layout is rejected.

The Nuklear source revision and MIT license choice are recorded in
`config/gui-sources.json`. Source is fetched externally, never vendored into
the repository. `build_nuklear.py` creates a native static demo and installs
Nuklear, native ABI headers, license text and the example C source as actual
ObjectSpace-backed files. The software renderer supports the command types
used by the demo: scissoring, filled rectangles, outlines, lines and atlas
text. Rounded rectangle corners are approximated by square corners;
unsupported command types fail explicitly. This is a bounded renderer,
not a claim that every Nuklear widget or pointer device is implemented.

In Win+F6, compile, register and launch the installed example:

```sh
tcc /usr/share/examples/nuklear_demo.c -o /home/nuklear
tcc --register /home/nuklear
/home/nuklear
```

Enter activates the focused Approve button. The status turns green and shows
Approved. Type `q` and Enter to return to ash. Win+F1 and Win+F5 select the
independent kernel and Unix text consoles; returning to Win+F6 restores the
GUI while it is running. The prebuilt `/usr/bin/nuklear-demo` is also a real
file and requires explicit registration before execution.

Pass an optional seventh Nuklear source argument to
`build_toolchain_image.sh` to build the graphical image. The acceptance gate
uses PS/2 input and framebuffer screenshots: it compiles the GUI with the
guest's own TinyCC, registers and runs that ELF, checks the pending/approved
pixels, switches among F1/F5/F6, verifies denied surface operations, releases
the surface back to text and repeats after a cold reboot. Source archives,
licenses, the native relinking files and checksums accompany the disk image.
Pending-kernel integration is distinct from moving-main compatibility.
Physical hardware, ARM64, pointer input and broad GUI conformance require
separate validation.

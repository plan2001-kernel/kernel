# Native boot graphics and Nuklear

The graphical image starts `guiF6` directly from the kernel boot manifest,
parallel to the other independent boot processes. Its executable is the
GE/O object `0x4f4558454300000c`. F6 has no Unix supervisor or shell. The GUI
uses its three native console-region grants and opens its own session-bound
surface. `native_runtime.c` supplies the minimal Newlib memory and console
hooks; `native_session.c` uses publication regions and WAIT/WAKE directly.
The ELF does not link the Unix process, descriptor, filesystem or UXPIPE
runtime. Newlib supplies C allocation, formatting and math only.

`bootview=F6` selects the first completed graphical frame automatically.
A manual view selection cancels the pending boot selection. Win+F1, Win+F5
and Win+F6 switch views without stopping their independent processes.
The kernel retains keyboard focus and framebuffer routing; the GUI receives
no physical framebuffer pointer or global focus authority.

The kernel exposes immutable geometry through `ObjOpSurfaceInfo`. Pixels
are packed uint32 `0x00RRGGBB`. The GUI maps the region as its single writer
and publishes using `ObjOpSurfacePresent`. Only its bound slot can be updated;
read-only capabilities cannot present and callers cannot select another slot.
Inactive snapshots are retained for focus restoration. Startup failure exits
only the GUI session and leaves SystemConsole available.

Nuklear's source revision and MIT license choice are recorded in
`config/gui-sources.json`. Source is fetched externally, never vendored.
The bounded software renderer implements scissoring, rectangles, outlines,
lines and atlas text. Rounded rectangle corners are drawn square;
unsupported command types fail explicitly. Pointer input and general
multiwindow desktop behavior are not implemented.

Enter activates the Approve button and changes the status to green.
The application runs for the machine's lifetime; `q` does not exit to a shell.

Pass the seventh Nuklear source argument to `build_toolchain_image.sh` to
build this profile. GUI acceptance requires the first frame before injecting
any keyboard events, Enter activation, persistent lifetime and F1/F5/F6
isolation on two cold boots. Compiler acceptance uses a separate shell
fixture; compiling or registering an ELF is not part of GUI boot.
`seed_native_gui.py` provisions a separate fixture containing no Unix ELF,
allowing the same GUI acceptance with `UX_GUI_NATIVE_ONLY=1`.
Source archives, licenses, native build recipes and checksums accompany the
image. Pending-kernel integration is distinct from moving-main compatibility.
Physical hardware, ARM64 and pointer input require separate validation.

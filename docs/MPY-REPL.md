# Native MicroPython boot REPL initialization

The manifest's MicroPython argument is one mode string. `args=-R` starts an
independent kernel-console REPL without restoring a Python graph, running
`init.run`, acquiring a keyboard device or automatically committing GE/O data.

An explicitly configured initializer uses one argument:

```ini
args=--repl-init=0xSOURCE_OID
```

This selects the same REPL mode and reads a framed Python source object from
the already activated native GE/O volume. The existing boot-volume authority
check applies; an OID does not authorize volume access. The loader accepts a
nonzero unsigned decimal or 0x-prefixed hexadecimal OID, bounds the complete
object to 64 KiB, validates the standard object footer and executes only RAW
source bytes. Metadata remains opaque. The temporary volume/object Handles
close before Python source executes. No Environment or graph format changes
are involved.

The initializer executes once in the REPL's globals before its ready marker.
It can define short operator commands or install an application import context.
A command-definition initializer should not run the operator's workload at
boot. Initialization errors are reported and the interactive REPL remains
available; a broken initializer must not remove the diagnostic console.
Other REPL sessions remain independent and keep their own explicitly selected
mode. General child processes gain no new volume-opening authority.

Build the native interpreter with `tools/host/build-mpy-gcc.py`. Deploy its
framed ELF using the existing trusted offline installation process and configure
only the intended boot-manifest session. The kernel executable and native ABI
are unchanged by this userspace option.

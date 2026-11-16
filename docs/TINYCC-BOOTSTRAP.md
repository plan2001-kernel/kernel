# Native TinyCC and Newlib toolchain

The AMD64 bootstrap builds TinyCC as a native static Plan2001 ELF using
Newlib and the capability-backed Unix adapters. The immutable source
revisions and TinyCC's LGPL license are recorded in
`config/toolchain-sources.json`; kernel main is resolved per CI run under
the existing source policy. Upstream compiler source is fetched externally,
and the small target patch lives in `ports/tinycc/plan2001.patch`.

The trusted bootstrap installs the compiler at `/usr/bin/tcc`, full Newlib
headers at `/usr/include`, `libc.a`, `libm.a`, `libplan2001.a` and `crt1.o` at
`/usr/lib`, and TinyCC's support archive and headers at `/usr/lib/tcc`.
These are durable ObjectSpace-backed files. The installed compiler binding
points to the exact, read-only seeded executable object. Its bytes can be
read through ordinary file descriptors. Other files are atomically created,
verified against the installation bundle, and never silently overwritten.
Malformed bundle framing or unsafe paths are rejected before installation.
An installation interrupted by a resource failure may leave complete files
and can be retried against the same bundle.

The `--toolchain` multicall profile explicitly registers only the seeded
compiler when it is installed in the delegated root. The F6 root contains
the toolchain; the F5 root remains independent. An installed path or raw OID
never grants execution authority. Newly generated ELF output remains
unregistered; admission and execution of those programs belongs to P4.

TinyCC defines `__plan2001__`, uses native startup and Newlib archives, and
produces static AMD64 ELF by default. It does not select Linux startup
objects, host libraries or a dynamic loader. `-run` and forwarding to other
architecture compilers are unavailable. Input files are snapshotted once per
compiler open to support efficient random archive reads; ordinary file
operations retain the live native backend. Sequential ELF output is buffered
to avoid repeated whole-object publication and reports final flush failures.

With a provisioned F6 root, compile and link using the installed files:

```sh
tcc -c /home/hello.c -o /home/hello.o
tcc /home/hello.o -o /home/hello
```

`tools/ci/build_toolchain_image.sh KERNEL BUSYBOX TINYCC NEWLIB_SOURCE REPORTS
FRESH_OUTPUT` builds and verifies the image. Newlib must first be built with
`build_newlib_libc.sh`; the kernel and image tools must also be available.
`fetch_toolchain.py` fetches the exact TinyCC revision. Only fresh disposable
images are formatted. The build requires guest compilation and linking from
installed files, malformed-source rejection, ELF layout checks, cold-reboot
persistence and PS/2/VGA acceptance of compiler commands in the genuine F6
ash process. The image is bundled with pinned upstream sources, port source,
relinking recipes, runtime objects, license files and checksums. Passing this
AMD64 fixture does not establish physical hardware or ARM64 support, general
POSIX completeness, compiler self-hosting, or execution of newly linked ELF.

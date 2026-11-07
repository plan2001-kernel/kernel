# Named OID collection graph: native TinyCC VM test

> Test harnesses and userspace programs named below are retained in the
> separate `Plan2001-O-weekend-tests/plan2001` companion bundle. Relative
> `tools/...` and `user/...` paths refer to that bundle layout; they are not
> build or boot prerequisites of this canonical kernel tree.

The TinyCC port now supports a userspace OID graph in addition to its
earlier flat name registry. Run the same real GE/O compile/link/execute
test with the graph seed:

```sh
CC=gcc-14 tools/build-tcc.sh
bash tools/qemu-tcc-space2.sh --graph
```

The normal prerequisites and tool/firmware selectors are described in
[TINYCC-SPACE2.md](TINYCC-SPACE2.md). Without `--graph`, the previous flat
registry test remains available. One compiler binary recognizes both modes
from the root object's bytes. Existing images are not automatically migrated.

## Persistent objects

SpaceID 1 remains `init`; SpaceID 2 remains the independently named `/`
store. Inside Space 2, OID 64 is a root **collection object**:

```text
64 root
  usr     -> 65
    opt   -> 66
      src -> 67
        build   -> 68
        include -> 69
          detail -> 70
        lib     -> 71
        main.c  -> 77 (raw C bytes)
  project -> 67 (same src object, second name)
```

The seed also includes the unchanged C/assembler/header files from the
previous test. Each collection is its own native GE/O byte object. An
example root value is:

```text
OIDPATH1
tag	path
tag	root
entry	D	0000000000000043	project
entry	D	0000000000000041	usr
```

The displayed `\t` characters represent literal tab bytes in storage.
`D` identifies an edge to a collection, `F` an edge to a raw-content object.
Names belong to edges; identity belongs to the referenced OID. Collection
objects carry `path` and a descriptive tag (`root`, `usr`, etc.). References
in this format are local to Space 2. No complete absolute path is stored
in collection records, and there is no central table of all file paths.

The resolver in `user/tcc/port/oidpath.c` starts from OID 64 and reads each
collection's named edge using the existing OSPACEGET syscall. For example:

```text
/usr/opt/src/main.c
64 --usr--> 65 --opt--> 66 --src--> 67 --main.c--> 77
```

This walk is entirely in userspace. The kernel and GE/O disk layout are
unchanged by this experiment; they store and retrieve opaque byte objects.
The source list in the C test driver remains explicit. This change tests
object-based path resolution, not automatic project discovery or a make parser.

## Writes and identity

Reading a collection as a byte-content file returns EISDIR. A file cannot
be traversed as a collection. Missing names and malformed collection values
are rejected. The resolver bounds path length/depth and detects cycles in
the visited collection chain. Dot/dotdot and empty path components are
rejected; this is not a complete POSIX path compatibility layer.

Overwriting an existing file writes its original OID. A new compiler output
allocates a native content object and adds a named edge to the existing
`build` collection. Root and unrelated collections need not be rewritten.
`/project/build/demo` and `/usr/opt/src/build/demo` consequently name the
same compiled object, including outputs created after seeding.

The current creation protocol uses the allocator's create commit followed
by the parent-edge commit, so interruption may leave an unnamed object.
The format and writer are experimental and assume a single writer. Limits
are 64 edges and 16 tags per collection, 63-byte components, paths shorter
than 256 bytes, 32 traversed edges, and 8 MiB per content object. There is no
cross-machine reference, general metadata/tag query index, access-rights
inheritance, snapshot-view API, rename API or concurrent graph transaction
protocol in this test. Tags are stored as metadata; AND/OR search is future work.

## Checked results

The in-guest checks verify the alias identity, collection/file distinction,
missing names and invalid path components. TinyCC then assembles the startup
runtime, compiles the same two C files, links and runs the executable twice:

```text
SPACE2 APP: answer=42 bss=zero data=7
SPACE2 APP: answer=43 bss=zero data=7
```

The second build follows an update to the header object, preserving its
OID. The previous missing-include and malformed-ELF rejection tests remain.
The serial log records the number of edges followed for real compiler
reads, for example `OIDPATH READ hops=6 ... /include/detail/value.h`.

After QEMU stops, an independent host reader validates the real GE/O tree,
follows the stored collection edges, rejects dangling/cyclic test graphs,
checks original OIDs and source bytes, verifies aliases to newly created
outputs, and validates the compiled ELF segments. The run directory contains
`report.json`, `graph-seed.json`, `space2-inspect.json`, `serial-tcc.log`,
`after.img` and the extracted `objects/`. `report.json` includes the full
collection graph and `namespace_mode: "oid-graph-v1"`.

The exact same resolver is also checked with mocked object syscalls on the
host under AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
gcc-14 -std=gnu11 -O1 -g -fsanitize=address,undefined \
  -fno-omit-frame-pointer -Wall -Wextra -Werror -Iuser/tcc/port \
  tools/host/test-oidpath.c user/tcc/port/oidpath.c \
  -o build/gcc/tcc/test-oidpath
ASAN_OPTIONS=detect_leaks=1 build/gcc/tcc/test-oidpath
```

Those checks cover writes through aliases, parent-only edge updates,
duplicate names, missing path tags, dangling edges, cycles, excessive
depth, and 5,000 mutated collection payloads.

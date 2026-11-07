# SpaceID 2: flat root-store porting experiment

> Test harnesses and userspace programs named below are retained in the
> separate `Plan2001-O-weekend-tests/plan2001` companion bundle. Relative
> `tools/...` and `user/...` paths refer to that bundle layout; they are not
> build or boot prerequisites of this canonical kernel tree.

Run from the project root with Python 3 and a host C compiler:

```sh
python3 tools/host/space2-port-demo.py
```

Each invocation creates a fresh `build/space2-port-demo/run-*` directory,
containing `objects.sqlite`, `import.json` and a verified `report.json`.
An assertion failure or compiler failure outside the intentional error test
exits unsuccessfully. `--cc gcc-14` selects another installed host compiler.

This is a host-only simulation of the proposed object naming and build
workflow. SQLite stands in for the catalog, opaque byte objects and build
metadata. It does not modify the GE/O disk format, seed an actual O volume,
reserve an ID on an existing volume, change the kernel, or run in the VM.

## Model

The simulated catalog contains `init -> 1` and `/ -> 2`. The slash is the
exact name of the second store, not a directory object. All fixture names
belong to that store:

```text
SpaceID 2 (name: "/")
    "/usr/opt/src/Makefile"               -> byte object
    "/usr/opt/src/main.c"                 -> byte object
    "/usr/opt/src/lib/answer.c"           -> byte object
    "/usr/opt/src/lib/banner.c"           -> byte object
    "/usr/opt/src/include/api.h"          -> byte object
    "/usr/opt/src/include/detail/value.h" -> byte object
```

There are no `/usr`, `/usr/opt`, `lib` or `include` directory objects.
The store performs exact `(SpaceID, name)` lookups, with no parent lookup,
prefix inheritance, path normalization or implicit creation of other Spaces.
Each name binds to a stable OID; changing bytes increments its version.
This simulation adds a name-to-OID table: the current native GE/O object
API does not already provide that per-object naming service.

## Python make

The object named `Makefile` contains a small JSON rule manifest, not Unix
Makefile syntax. Rules specify targets, source/object inputs, available
headers and include search names. The Python builder traverses dependencies,
compiles three translation units and links a host executable. Compiler-generated
dependency files identify which headers each translation unit actually uses.
Build state records input content hashes, the recipe/compiler identity and
the output content hash; filesystem timestamps are not used for freshness.

The compiler adapter exports selected byte objects into an ephemeral host
directory tree for the unmodified C compiler. It imports successful outputs
back as objects and then removes the tree. Include search and relative include
resolution happen in this adapter/compiler, not in the store. System headers,
the compiler, linker and C runtime come from the host, outside SpaceID 2.
This tests the storage/build model, not a complete self-hosted toolchain port.

## Verified behavior

- Both initial catalog entries resolve to the intended numeric IDs.
- No directory objects or implicit prefix Spaces are created.
- Initial build compiles three C files and links the executable: `space2 42`.
- An unchanged build runs no compiler or linker commands.
- Editing `include/detail/value.h` preserves its OID and advances its version.
- Only `answer.c` recompiles after that edit; relinking produces `space2 43`.
- A deliberate C preprocessing error leaves the old object and executable intact.
- Restoring the previous header bytes reuses the existing valid outputs.
- Identically named objects in Spaces 1 and 2 remain independent.

Limits: this builder has explicit header availability, simple ASCII fixture
names, no shell recipes, no Unix make parser, no parallel execution and no
whole-build transaction. Each successful rule publishes independently. The
simulation's version counter is not a GE/O history implementation.

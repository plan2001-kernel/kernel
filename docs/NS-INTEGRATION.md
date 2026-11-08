# NS2–NS4 integration and verification

> Test harnesses and userspace programs named below are retained in the
> separate `Plan2001-O-weekend-tests/plan2001` companion bundle. Relative
> `tools/...` and `user/...` paths refer to that bundle layout; they are not
> build or boot prerequisites of this canonical kernel tree.

Plan 2001-kernel retains Plan 9 process namespace ownership while replacing namespace
contents with native GE object capabilities. Pgrp owns OContext; its bounded
ordered entries retain Ohand grants. GE objects and R1 relations remain
persistent and independent of these temporary views.

Implemented interfaces and rules are documented in:

- NS2-NAMESPACE-SEMANTICS.md: inheritance, restrictions and ownership.
- NS3-OBJECT-MOUNTS.md: sealed providers, union ordering and lifetime.
- NS4-NAMESPACE-RELATIONS.md: native full identities and kernel query checks.

No directory hierarchy, Dev/Chan/Qid restoration, VFS, SQL, second storage
engine, remote transport or namespace persistence was added. Existing syscall
numbers remain stable. New slots 101–106 extend the amd64 ABI. The native ABI
header is independent of MicroPython. The existing object context remains the
only binding structure and still holds at most 32 entries, including mounts.

Run the canonical sequence:

```sh
CC=gcc-14 bash tools/test-ns-all.sh
```

The runner builds the kernel and MicroPython once, runs actual-C host tests
with address/undefined sanitizers, then serializes the combined two-boot
four-CPU TCG demonstration, original NS1, original N13, and R1 REPL persistence
regressions. Tests use newly created disposable regular-file GPT images only.
Keyboard-driven R1 testing runs alone to avoid input losses under VM load.

`tools/qemu-ns-complete.sh` can run the integrated proof separately. It builds
a fresh native test executable and seeds it alongside the real MicroPython ELF.
The guest checks shared/copied/empty groups, replacement, restriction, mount
precedence, aliasing, hidden handles, lifetime, revocation, explicit OSPAWN
sharing/delegation, query authorization and two-Space identity collisions.
On boot two it restores persistent state, reconstructs the application views,
repeats native unprivileged queries, and checks the same content and identities.
Only then does the guest emit `NS-COMPLETE PASS`.

The final machine report `NS-VERIFICATION.json` records actual results and
artifact hashes. `tools/host/report-ns-all.py` verifies markers, at-rest GE data,
original regression output, and that every test ESP used the same current
kernel. Source-level tests alone do not confer an integration PASS.

## Audit and limits

NS1 allocation-before-publication, reference counting and kernel-group anchor
fixes remain. NS3 centralizes atomic context composition and releases removed
grants outside the spinlock. Copies pin their ordered entries under that lock.
Providers contain grants, never process/context links, eliminating cycles by
construction. SHARE validates every retained layer's delegation rights.
All old native object read/write/exec operations still call oauthorize, now
using effective union membership; a raw OID alone does not authorize access.

The MicroPython incremental build previously keyed only on source timestamps
and flags. QSTR/header changes could leave incompatible cached object files;
its cache signature now includes generated and relevant API headers. New
namespace kernel units reserve kernel registers and avoid SSE instructions
that assume SysV stack alignment at Plan 9 entry points.

Remaining bounded PoC limits: one active native volume; 32 total binding
entries; eight process-local O handles (including provider handles); sealed
snapshot mounts rather than live source mirroring; R1's existing 256-object,
64-tag and 1024-edge bounds; no restricted arbitrary-object creation; no
MCREATE; no cross-process provider-handle transfer beyond exporting views from
already delegated bindings. Existing explicit object/Pipe delegation remains
the transfer mechanism. There is no constant-time metadata query guarantee.
Content already copied into a process's memory cannot be revoked retroactively.
Host failure injection tests actual C routines; no new kernel debug syscall
was introduced for allocator faults or kproc stress. Hardware and KVM tests
are not claimed when unavailable; see the machine report for execution status.

Bootstrap and ORELATE clients use the same kernel R1 operation lock. The
concurrent-mutation proof checks that all four bootstrap tags and all four
child tags survive overlapping updates, then removes their relations. Tag
identifier objects remain persistent and are reused after reboot. Raw metadata
editing by the trusted bootstrap remains outside this operation protocol.

GE retains its existing one-pending-writer-per-Space rule. A concurrent Python
graph transaction or another authorized native writer can cause a busy error;
callers must retry rather than assume success. The concurrent mutation test
uses bounded retries and asserts every addition before cleanup. Kernel R1
operations share one lock, but this does not replace GE's transaction policy
or serialize arbitrary trusted raw writes across separate syscalls.

## Recorded guest output

The proof's actual allocated identities and assertions (the machine report
links the complete final logs):

```text
NS OBJECT IDS [64, 65, 66, 67, 68, 69]
NS2 SHARE PASS
NS3 UNION BEFORE PASS
NS3 UNION AFTER PASS
NS3 UNION REPLACE PASS
NS4 NAMESPACE FILTER PASS
NS4 DIRECT READ DENIED PASS
NS4 CONCURRENT MUTATION PASS
NS4 COMPLETE IDENTITY PASS
NS4 REBOOT PERSISTENCE PASS
NS-COMPLETE PASS
```

The separate original R1 native REPL regression reports `GE R1 BEFORE [64]`
and `GE R1 AFTER [64]`, with unchanged `b'JPEG DATA1'`. The combined application
proof uses image 67 in Space 1; its four-tag result is asserted before and
after reboot. Source 64 in Space 1 and object 64 in Space 2 have distinct
content and relations and remain distinct in the native query ABI.

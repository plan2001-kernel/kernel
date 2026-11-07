# N13 native object views and Pipe delegation

This is the experimental implementation contract for
`CDX-20261029-N13-OCTX`. It defines narrow object and Pipe interfaces,
not a general resource registry or a complete container security model.
GE/O v1 and its native `(volume, SpaceID, OID)` identities are unchanged.
Only the one currently opened native volume is supported. The Python
`OID_GRAPH` remains a serialized Python graph, not a native relation index.

## Object views

A `Pgrp` owns a reference-counted `OContext`. The context contains at most
32 bindings. A role contains 1–31 ASCII letters, digits, `_` or `-`; no
slashes, directory search, implicit root, pathname or OID translation exists.
Each binding references a kernel grant containing the actual native Space,
validated SpaceID, OID, rights and revocation state. Binding a new object
checks that its published bytes exist. Values are live object references,
not snapshots of object contents.

- `rfork(RFPROC)` shares the `Pgrp` and its view. Binding edits are visible
  to both processes.
- `rfork(RFPROC | RFNAMEG)` copies the binding map. Bind/unbind edits are
  independent; grants and the underlying object identities remain shared.
- `RFCNAMEG` creates an empty object view and preserves the inherited device
  mask. The equivalent no-`RFPROC` operations replace the caller's group.
- Existing `Rgrp` and `Egrp` behavior is unchanged.
- No O handle table is inherited. Children resolve their view bindings into
  fresh process-local handles. Successful exec closes local O and Pipe
  handles; the view survives with its `Pgrp`.

A copied map is a snapshot of **bindings**, not an independent grant or an
object-content snapshot. Revoke affects every copied/shared reference to
that grant. Unbind removes one map entry; an old handle loses access when
its grant is no longer present under any role in the caller's current view.
An operation already admitted before unbind may finish. Grant revocation
serializes with operations: after revoke returns, no new operation using
that grant can succeed. Revoke also aborts its uncommitted transaction.
Closing a handle invalidates that local handle, while other local handles
and view references remain valid. Unbinding/closing the final reference
releases the native Space and aborts an unpublished transaction.

## Authorization and bootstrap

Every `OSPACEGET`, `OSPACEPUT`, `OSPACECREATE`, `OSPACECOMMIT` and
`OEXECUTE` resolves the caller's handle table and checks the grant under a
sleeping lock. A restricted grant requires membership in the current view,
an exact OID match, no revocation and the required rights. The native Space
pointer fixes the volume and SpaceID; the user cannot supply another SID
or open another Space to bypass this check.

| Bit | Right | Relevant operation |
| --- | --- | --- |
| 1 | read | `OSPACEGET`, including size queries |
| 2 | write | `OSPACEPUT` on the bound OID |
| 4 | create | `OSPACECREATE`; bootstrap only in this slice |
| 8 | commit | `OSPACECOMMIT` on this grant's transaction |
| 16 | execute | `OEXECUTE` on the bound OID |
| 32 | delegate | rebind this same grant; revoke it |

The first successful `OVOLOPEN`, made by the single embedded boot program
before any other userspace exists, records the process pointer and PID as
the bootstrap holder. Only that process may subsequently use `OVOLOPEN`
or `OSPACEOPEN`, or obtain a whole-Space grant. This explicit initial-runtime
trust continues across its O exec and is cleared on process exit; it is
never inherited by `rfork`, derived from a username, or granted through
`eve`/`iseve`. The trusted initial MicroPython runtime can therefore keep
loading its Environment, allocating code objects and committing the Python
graph. This is a bootstrap exception, not complete isolation of arbitrary
code run inside that initial interpreter.

The bootstrap holder can issue single-object grants with any subset except
create, and revoke grants in its own view even without delegating bit 32.
Other holders may rebind only the **same** grant, OID and rights, with bit
32. They cannot manufacture a new grant, widen rights, change Space, or
escape revocation by deriving a new grant. Arbitrary attenuation of an
existing grant and transitive grant trees are deliberately absent.
Delegated creation of new objects is not implemented; a restricted handle's
create call always fails in the kernel.

Each newly issued grant opens a separate native `Ospace`, so committing a
write grant cannot publish the bootstrap or another grant's pending writes.
The existing GE/O engine permits one pending transaction per Space and
returns `object store busy` for competing writers; it has no new transaction
or isolation model. Reads see published versions. A writable grant can hold
that Space busy until commit, revoke or final release: quotas, fair writer
scheduling and per-process memory accounting are not implemented.

O handles and newly created Pipe handles use monotonically allocated
23-bit generations plus a slot byte. Generation exhaustion fails rather
than wrapping and accepting a stale handle. This finite global generation
budget can itself be exhausted; per-process rate limits are not implemented.
Values are indices into the
**caller's** kernel table, not secrets or rights by themselves. A PID is
never accepted as a delegation target. Explicitly inherited Pipe handles
retain their value in the child's separate table, backed by the selected
grant. The O and Pipe tables remain separate interfaces.

## Experimental syscall ABI

The existing amd64 syscall argument slots and error-string/negative-return
convention apply. `int` is 32 bits; `uvlong` is 64 bits. User memory is
validated, kernel pointers are never exported. These additions are not a
frozen general ABI; sibling N13 PRs must reconcile shared-header numbers
before integration.

| Number | Signature | Result |
| --- | --- | --- |
| 96 | `obind(int source, uvlong oid, char *role, int n, int rights)` | 0; replaces/creates role |
| 97 | `olookup(char *role, int n, uvlong info[3])` | new O handle; info = SID, OID, rights |
| 98 | `ounbind(char *role, int n)` | 0; removes role in current view |
| 99 | `orevoke(int handle)` | 0; revokes the referenced restricted grant |
| 100 | `pipeinherit(int handle, int rights)` | 0; selects endpoint rights for next fork hook |

Existing O operation signatures remain unchanged. O handle **values** are
opaque and now include generations. There are at most eight local O handles
and sixteen local Pipe handles. O payloads remain limited to 64 MiB at the
syscall boundary; the Python `hget` binding limits its allocation to 1 MiB.
Labels and binding counts are independently bounded.

Python `space.obind(role, oid, rights[, source_handle])`
uses the initial interpreter's native handle by default. Children must pass
a handle obtained from `space.olookup(role)`, which returns
`(handle, SpaceID, OID, rights)`. `ounbind`, `orevoke`, `hclose`,
`hget(handle, oid)`, `hput(handle, oid, bytes)`, `hcommit(handle)`,
`hcreate(handle, bytes)` and `hexecute(handle, oid)` call the native
operations directly and raise `OSError` on denial. `obind` accepts its
optional fourth argument positionally. The uppercase rights constants
`OREAD`, `OWRITE`, `OCREATE`, `OCOMMIT`, `OEXECUTE`, `ODELEGATE` expose the
bits above. These handle helpers do not commit the Python graph implicitly;
existing `space.oget/oput/oregister` behavior is unchanged.

## Pipe inheritance and transfer

`plan9.pipenew()` returns two distinct full-duplex endpoint handles backed
by the existing native Pipe and its two byte queues. `piperead(h, n)` returns
bytes, `pipewrite(h, bytes)` returns the written count, and `pipeclose(h)`
closes the local reference. This leaves the legacy `plan9.pipe()` ABI alone.
No message framing is added.

`plan9.pipeinherit(h, rights)` selects only this endpoint for the next
`RFPROC` hook. Rights are read=1, write=2, both=3, cancel selection=0, and
must be a subset of the caller's rights. The child receives a separate
local table entry referencing the endpoint; close does not invalidate the
parent's reference. Selection is consumed at the fork hook and is not
selected again in the child. Close unused parent copies to allow EOF.
There is no arbitrary-process/PID insertion or implicit endpoint inheritance.

The existing Pipe queues implement blocking `qread`/`qwrite`, reader wakeup,
backpressure and peer hangup. `conf.pipeqsize` defaults to 256 KiB per
queue, rounded to the existing atomic I/O unit; individual native reads and
writes are limited to 64 KiB. Buffered data drains before EOF on final peer
close. Writes to a closed peer use the existing error/note behavior.
Fork does not copy USB, UART, keyboard, network or output grants.
There is no new hardware authorization policy or claim that all inherited
hardware/services are container-safe.

## Integration seams and limits

The standalone test results below describe the earlier object-context
implementation; former OSPAWN **NOT RUN** statements are superseded by the
integrated result in [N13-INTEGRATION](N13-INTEGRATION.md).

- `Pgrp.ocontext`, `pgrp.c` allocation/copy/release and the `pgrpcpy` copy flag
  connect views to existing group lifetime. `sysproc.c` passes `RFNAMEG` to
  the two existing `pgrpcpy` calls and invokes `pipeprocfork(p)` immediately
  before `procfork(p)`. No scheduler, executable loader, boot, EFI or network
  implementation changes are made.
- `sysgefs.c` performs grant/handle checks, exposes the four O-view syscalls
  and retains the bootstrap exception. `o.c::onativesid` exposes the actual
  validated SID across the existing opaque storage boundary.
- Shared ABI changes are the five numbers in `runtime/abi/sys.h`, one Pgrp
  pointer and the `pgrpcpy`/`pipeprocfork` prototypes. The GCC manifest adds
  `ge/storage/ocontext.c`; MicroPython adds direct bindings and generated
  qstrs.
- Integrated `OSPAWN` selects EMPTY, role-limited COPY or explicitly
  enumerated SHARE views, selected Pipe endpoints and initial environment
  before the child is runnable. The integrated ABI and evidence are recorded
  in [N13-INTEGRATION](N13-INTEGRATION.md). Views remain in memory; no GE/O
  layout changes occur. Full container isolation and physical hardware remain
  unimplemented/unverified.

## Reproducible disposable-guest exercise

The example below is ordinary interactive program input, not a retained test
harness. Copy the Python block to `build/n13/proof.py`. Encode it as hex to
avoid the sanctioned QEMU script's limited Finnish keyboard character map:

```sh
mkdir -p build/n13
python3 - <<'PY'
from pathlib import Path
source = Path('build/n13/proof.py').read_text().replace('    ', '\t').encode()
Path('build/n13/proof-input.txt').write_text(
    "exec( (lambda h: ''.join( map(lambda x,y: chr( int(x+y,16) ),h,h) ) )( iter('"
    + source.hex() + "') ) ) \n")
PY
tools/build-gcc.sh --kernel -j 8 > build/n13/kernel-build.log 2>&1
tools/build-mpy-gcc.sh -j 8 > build/n13/mpy-build.log 2>&1
tools/build-efi-gcc.sh -j 8 > build/n13/efi-build.log 2>&1
tools/qemu-o-boot.sh --tcg --output-dir build/n13/proof \
  --repl-input build/n13/proof-input.txt --expect '^N13 PROOF PASS' \
  --timeout 1200 > build/n13/proof-run.log 2>&1
```

The long keyboard input takes several minutes. A successful marker must be
an executed output line, not the REPL's echoed input. Inspect the serial log
and `report.txt`; QEMU is not physical hardware verification. The program
checks copied versus shared binding maps, OID identity, denied get/put/
create/commit/execute, foreign and stale handles, grant revocation, explicit
Pipe inheritance, rights attenuation, blocked-reader wakeup, transfer larger
than queue capacity, and EOF. The existing queue implementation supplies
backpressure; the delayed reader exercises that path without a debug boot
mode or queue instrumentation.

```python
import space as s
import plan9 as p

def denied(f,*args):
    try:
        f(*args)
    except OSError:
        return
    raise AssertionError('expected denial')

def forbidden(f,*args):
    try:
        f(*args)
    except OSError as e:
        assert str(e)=='permission denied'
        return
    raise AssertionError('expected permission denial')

def proof():
    a=s.oregister(b'A')
    b=s.oregister(b'B')
    denied(s.hget,0,a)
    denied(p.piperead,0,1)
    code=0x4f45584543000003
    s.obind('code',code,17)
    s.obind('data',a,33)
    s.obind('writer',b,10)
    foreign=s.olookup('data')[0]
    ids=[]
    jobs=[]
    for label,oid in [('A',a),('B',b)]:
        s.obind('data',oid,33)
        left,right=p.pipenew()
        p.pipeinherit(right,3)
        pid=p.rfork(p.RFPROC|p.RFNAMEG)
        if pid==0:
            try:
                h,sid,actual,rights=s.olookup('data')
                hc,csid,coid,cr=s.olookup('code')
                assert actual==oid and sid==csid and coid==code
                assert s.hget(h,oid)==label.encode()
                forbidden(s.hget,h,a if oid==b else b)
                forbidden(s.hput,h,oid,b'bad')
                forbidden(s.hcommit,h)
                forbidden(s.hcreate,h,b'bad')
                forbidden(s.hexecute,h,oid)
                denied(s.hget,foreign,oid)
                denied(p.piperead,left,1)
                p.pipewrite(right,b'R')
                assert p.piperead(right,1)==b'G'
                w,wsid,woid,wr=s.olookup('writer')
                s.hput(w,woid,b'B')
                s.hcommit(w)
                s.hclose(w)
                s.ounbind('data')
                forbidden(s.hget,h,oid)
                s.hclose(h)
                s.hclose(hc)
                for i in range(6):
                    assert p.pipewrite(right,b'x'*65536)==65536
                p.pipewrite(right,label.encode())
                p.pipeclose(right)
                p.exits()
            except Exception:
                p.pipewrite(right,b'FAIL')
                p.pipeclose(right)
                p.exits('child-failure')
        p.pipeclose(right)
        jobs.append((pid,left,oid,label))
    for pid,left,oid,label in jobs:
        assert p.piperead(left,1)==b'R'
        p.sleep(50)
        p.pipewrite(left,b'G')
        p.sleep(150)
        count=0
        tail=b''
        while True:
            chunk=p.piperead(left,65536)
            if not chunk:
                break
            count+=len(chunk)
            tail=chunk
        assert count==393217 and tail.endswith(label.encode())
        p.pipeclose(left)
        denied(p.piperead,left,1)
        assert p.wait()==(pid,'')
        parent=s.olookup('data')
        assert parent[2]==b and s.hget(parent[0],b)==b'B'
        s.hclose(parent[0])
        ids.append(oid)
    assert ids[0]!=ids[1]
    s.hclose(foreign)
    denied(s.obind,'bad/role',a,1)
    s.obind('wa',a,10)
    s.obind('wb',b,10)
    wa=s.olookup('wa')[0]
    wb=s.olookup('wb')[0]
    s.hput(wa,a,b'changed')
    s.hcommit(wb)
    assert s.oget(a)==b'A'
    s.orevoke(wa)
    denied(s.hput,wa,a,b'bad')
    s.oput(a,b'A')
    s.hclose(wa)
    s.hclose(wb)
    s.obind('rev',a,33)
    h=s.olookup('rev')[0]
    h2=s.olookup('rev')[0]
    s.orevoke(h)
    denied(s.hget,h2,a)
    denied(s.olookup,'rev')
    s.hclose(h)
    s.hclose(h2)
    s.ounbind('rev')
    s.obind('shared',a,1)
    left,right=p.pipenew()
    p.pipeinherit(right,2)
    pid=p.rfork(p.RFPROC)
    if pid==0:
        try:
            denied(p.piperead,right,1)
            s.ounbind('shared')
            p.pipewrite(right,b'S')
            p.pipeclose(right)
            p.exits()
        except Exception:
            p.exits('sharing-failure')
    p.pipeclose(right)
    assert p.piperead(left,1)==b'S'
    assert p.piperead(left,1)==b''
    p.pipeclose(left)
    assert p.wait()==(pid,'')
    denied(s.olookup,'shared')
    left,right=p.pipenew()
    p.pipeinherit(right,2)
    pid=p.rfork(p.RFPROC|p.RFCNAMEG)
    if pid==0:
        try:
            denied(s.olookup,'data')
            denied(p.piperead,left,1)
            p.pipewrite(right,b'E')
            p.pipeclose(right)
            p.exits()
        except Exception:
            p.exits('empty-view-failure')
    p.pipeclose(right)
    assert p.piperead(left,1)==b'E'
    assert p.piperead(left,1)==b''
    p.pipeclose(left)
    assert p.wait()==(pid,'')
    h=s.olookup('data')[0]
    s.hclose(h)
    h2=s.olookup('data')[0]
    assert h2!=h
    denied(s.hget,h,b)
    s.hclose(h2)
    s.obind('noexec',code,1)
    hn=s.olookup('noexec')[0]
    forbidden(s.hexecute,hn,code)
    forbidden(s.hput,hn,code,b'bad')
    forbidden(s.hcreate,hn,b'bad')
    forbidden(s.hcommit,hn)
    s.orevoke(hn)
    forbidden(s.hget,hn,code)
    s.hclose(hn)
    print('N13 PROOF PASS',ids,code,'copy shared rights revoke IPC EOF backpressure')

proof()

```

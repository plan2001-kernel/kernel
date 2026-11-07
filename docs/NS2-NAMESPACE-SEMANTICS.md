# NS2: process namespace semantics

The ownership chain remains `Proc -> Pgrp -> OContext -> Ohand -> GE object`.
The NS1 fixes in `pgrp.c` and `proc.c` remain in place. No second process
namespace type was introduced, and no persistent GE format was changed.

| Operation | Group and bindings |
|---|---|
| `rfork(RFPROC)` | Increment and share the parent's Pgrp reference |
| `rfork(RFPROC | RFNAMEG)` | Private group, independent binding snapshot |
| `rfork(RFPROC | RFCNAMEG)` | Private group, no bindings |
| `rfork(RFNAMEG)` | Replace caller's group with an independent copy |
| `rfork(RFCNAMEG)` | Replace caller's group with an empty group |

Allocation and copying precede publication. Invalid conflicting flags are
rejected before changes. Copying retains grant references, never object bytes.
The final Pgrp reference destroys its context and releases its binding grants.
Executable replacement retains Pgrp and closes process-local object handles.
Kernel processes retain their existing anchored, synchronized kernel group;
this does not inherit bootstrap GE authority.

OSPAWN deliberately differs: its default is EMPTY. COPY names selected roles;
SHARE names the entire effective view and requires delegation authority for
all retained union layers. SHARE validates that the source has not changed
while selection occurs. Child object and Pipe handles remain process-local.
Bootstrap authority depends on the original process pointer AND PID, never on
an inherited group, executable name, or object number.

Pgrp.ns protects device-mask access. OContext's lock protects its fixed map,
order and epoch; grant gates serialize operations with revocation. No GE I/O
occurs while holding the context spinlock. See NS3 and NS4 for additional rules.

After the existing Dev removal, RFNOMNT sets the denied device mask. Its
all-denied state now also prohibits OBIND, OBINDX, provider export and mounting.
Existing bindings can still be read, unbound and unmounted. Copies and empty
replacement preserve the mask; they cannot clear the restriction. No Dev,
Chan, Qid, filesystem root, or pathname interpretation was restored.

The four-CPU VM demonstration emits the six NS2 markers only after the
corresponding child operations, waits and assertions. NS1 and original N13
regressions additionally exercise orphaned children, exec, failed spawning,
Pipe attenuation and repeated cleanup. Execution results live in
`NS-VERIFICATION.json`; this document is not itself test evidence.

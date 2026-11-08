# T16 — Consolidate and verify the common USB/Ethernet Device ABI

Result: **PASS** for the assigned existing T12 USB control and T13 raw Ethernet
scope. Agentti audited the implementation and call sites, corrected the normative
scope and contract documentation, and ran fresh builds and integration tests.
No demonstrated runtime defect justified a code change. This is evidence for the
tested contract, not a claim that every driver or failure interleaving is tested.

## Provenance and delivery

- MCP task: `476d91c0a8e64ffa86db6bba0644b84a`, claimed before execution.
- Execution date: 2026-11-08 UTC; executor: **agentti**.
- Actual host: `9db093436960`, Debian 13, Linux 6.18.57.
- Worktree: `/workspace/t16`; branch: `agent/t16/device-abi-consolidation`.
- GitHub main was independently fetched/checked at start:
  `654b5ee4aeb592a93fc7c9e764429b17ee196286` (T15 merge).
- Start tree: `7baa3e057e0a77bf388905b9d162f38c531e3f13`.
- Tested implementation start **and end** SHA:
  `654b5ee4aeb592a93fc7c9e764429b17ee196286`. All runtime, ABI, driver, binding,
  build and test sources remain byte-identical to this commit. Documentation
  changed before testing; final reporting adds no executable changes.
- The delivery commit necessarily follows this report's creation. Its exact SHA,
  tree, PR URL and independent readback are recorded in MCP `task_report` and
  `/workspace/artifacts/t16-delivery.json`, rather than a circular self-hash here.
- GitHub OAuth connector identity: `plan2001-kernel`; Plan2001 task tools use
  their OAuth connection. No credential or private key belongs in this report.
  Publication uses connector Git tree/commit/ref operations, not SSH. Git fetch
  provides a second readback. Main and merging remain the owner's responsibility.

The owner decision supersedes the old T16 Wi-Fi roadmap. The normative scope was
updated **first**, before implementation audit and builds. T11 ObjectFIFO is the
delivery dependency; T14/T15 and TCP/UDP are not prerequisites. **Wi-Fi is excluded**:
no driver, firmware, emulator, family, 802.11, management service or test stub.
Any later Wi-Fi assignment requires separate authorization.

## Common contract and actual provider differences

Sources below refer to the tested baseline; function names are stable anchors.
The ABI layouts/numbers are unchanged: `ObjOpenDevices=15`, `ObjOpenDevice=16`,
Device operations 37–40, family USB=1 / Ethernet=2; entry/open/result records are
32/24/16 bytes, Ethernet RX metadata/status are 24/88 bytes.

| Area | Common contract and evidence | USB control endpoint | Raw Ethernet |
| --- | --- | --- | --- |
| ABI and adapters | `runtime/abi/obj.h`; `runtime/libc/port/oobject.c` `obj_devices_open` through device helpers; `micropython/modplan9.c` device bindings. Adapters pack existing OBJ records, not a new syscall or object table. `kernel/core/sysobj.c` validates version, reserved fields, record lengths and generic argument shapes. | Setup packet and USB-specific control rules stay in kernel. | L2 bytes and RX metadata stay unchanged; no protocol/schema translation. |
| Catalog and open | `sysvobj.c` `vobjdevicesopen`, `vobjdevicelist`, `vobjdeviceopen`: common Vtab, discovery records and `VkindDevice`; catalog defaults to metadata/delegate, creation for `iseve`. Open checks catalog `Vcreate`, selectors/version/reserved fields and requested rights. | Endpoint records come from live, attached control endpoints. Maximum rights 206. | Controller/selector record comes from `vetherlist`; selector1 is zero. Maximum rights 238, adding `Vsubscribe`. |
| Handle identity and attenuation | `vlookupin`, `vlookup`, `vinstall`, `vobjgrant`, `vobjopenhandle`, `vobjspawninherit`: generation checked local slots, pinned references, delegation required and no requested-rights amplification. Same object survives aliases and child processes. | Alias shares lock, pending completion, sequence and failed latch. | Alias shares attachment, handoff and ordinary RX FIFO. |
| Limits | `vobjint.h` fixes 96 Handle slots. `vobjdeviceopen` and legacy open paths apply per-kind admission checks. Object, payload, FIFO, subscription and queued-node limits remain generic. | The 4 service / 16 device-or-endpoint checks are **open admission** limits, not absolute alias counts: Grant/OpenHandle/OSPAWN do not repeat them. Documentation now says so. | Same global Handle/lifetime machinery; 64-entry provider handoff is a separate hardware-to-worker bound. |
| SEND | `vobjdevicesend`: requires `Vsend`; generic input cap 16384 and kernel-owned copy before provider use. Invalid flags/shapes/rights fail before effect. | Eight-byte setup; data cap 4096, exact OUT payload / setup-only IN. Root requests and downstream requests outside the standard read allowlist require `Vcontrol`. Synchronous hardware completion creates one pending immutable response and increments sequence. | `vethersend` checks complete frame bounds/link/closed state; `ethercore_tx` success consumes the Block and means accepted/enqueued, **not wire completion**. No synthetic USB completion. |
| RECV | `vobjdevicerecv`: requires `Vrecv`, returns existing `ObjDeviceResult`. Output Handle installation precedes relinquishing retained ownership. | Returns response bytes Handle, sequence, USB family and actual length. No pending response is an error. A full Handle table leaves pending response retrievable. No FIFO required for this synchronous transaction. | Also requires `Vsubscribe`. Returns the existing ordinary RX FIFO, sequence 0, Ethernet family, length 0. Repeated RECV aliases that FIFO, not a frame poll or second FIFO. Callers create independent subscriptions. |
| Result authority | Returned content/FIFO rights are provider result policy, not a grant of an alias to the Device. `vobjgrant`/OpenHandle/OSPAWN separately attenuate Device aliases. Generic FIFO delivery intersects sender rights and its own allowed transferable mask. | Response bytes have `Vread|Vmeta|Vdelegate` (137), even though Device operational rights have a different mask. | FIFO has `Vsubscribe|Vmeta` (40), plus only Device `Vdelegate`/`Vcontrol` if held; never `Vsend`. Frame bytes use 137 for provider publication; receiver rights are attenuated by ordinary delivery. Operational Device capabilities are not all transferable through generic FIFO delivery. |
| CTL | `vobjdevicectl`: operation-specific rights/output shape, not an unconditional generic `Vcontrol` rule. | Clear-halt op 1 requires `Vcontrol`, no output, same session lock; refuses pending/failed sessions. | Status op 2 requires `Vmeta` and exactly 88 output bytes; detach op 3 requires `Vcontrol`, no output. |
| Final close/exit | `vhandput`, `vobjput` Device destructor; `vobjclose`, `vobjprocexit`, `vobjprocexec`, process exit/exec call sites: shared refs, stale generation rejection, cleanup after final owner, not every alias close. | Releases retained reply, `usbcore_dataclose`, `putep`; already returned bytes are independent. | `vetherclose`/detach unregister callback, join worker, drain owned Blocks and close ordinary FIFO. Returned Handles follow their own generic lifetime. |

The audit corrected misleading documentation rather than changing this ABI:
USB sequence/pending/failure rules were under a generic transfer heading, status
CTL was described too broadly as requiring control rights, and the 4/16 open
checks were described as absolute Handle limits. `DEVICE-ABI.md` now distinguishes
all three. Some historical USB-centric comments in `obj.h` remain; the actual
family constants/layouts and helpers already support Ethernet. No layout cleanup
or policy tightening is smuggled into this documentation task.

## Ownership, errors and concurrency

**USB:** `sysvobj.c` `vusbreadonly` / `vobjdevicesend` copy and validate the complete
message, reserve payload budget before hardware, then serialize aliases with the
Device QLock. A second SEND with a pending completion fails. A provider-entered
transfer error latches the session failed across aliases; no automatic retry or
rollback occurs. Reopen is explicit. Invalid requests/denied rights do not enter
the provider. `usbcore.c` `getep`/`putep`, `usbcore_dataopen`/`dataclose` share the
existing exclusive Ep claim with legacy endpoints; read/write reject detached
state. Root hub requests use the root implementation; downstream control traffic
uses actual `usbxhci.c` control read/write setup/data/status TDs and stall handling.
Finite driver timeouts are source-reviewed, not timeout fault-injection coverage.

Fresh T12 covers root and real QEMU USB mouse control endpoints, input/output
bounds, authority denials, short IN completion, bytes delivered through FIFO,
aliases, pending state, actual 96-slot exhaustion with retained completion,
parent/child transfers, final owner exit, actual stall/failed aliases/reopen and
idle detach. It is one vCPU with process interleaving, not USB SMP stress.

**Ethernet:** `sysdevether.c` callback owns each received Block and only uses
IRQ-safe bounded handoff operations (ilock, free, wakeup). The worker creates
immutable whole-frame bytes and publishes with generic `vobjfifopublish` and
24-byte metadata. No allocation, OBJ call or sleeping lock is introduced in the
callback. `ethercore.c` `ethercore_rxunregister` joins active callbacks; detach
gates TX, closes admission, unregisters, wakes/joins the worker, drains pending
Blocks and closes the FIFO. Worker references pin the final wakeup. Physical IRQ
execution is not claimed: the tested virtio RX path is a driver process.

`sysvobj.c` generic FIFO subscribe/publish/recv/revoke remains independent of
device families: bounded independent subscriber queues, drop-oldest per queue,
Handle installation before dequeue, metadata/identity preservation and ordinary
close/revoke semantics. No new FIFO or USB mandatory subscription was added.

Fresh T13 covers real socket peer full-frame TX/RX and metadata; rights denial
and attenuation; cross-process/cross-CPU TEE (fast 8, slow 2 with 6 drops), bounded
handoff overflow, FIFO revoke/drop accounting, explicit detach/stale/reopen,
link down/up, final owner exit with pending traffic and detach during flood.
Two vCPUs are used with observed CPU 0/1 placements. The 8192-frame peer flood
produced this actual status snapshot:

```text
(1, 0, 60, 1514, b'RT\x00\x124V', 1, 0, 880, 204, 676, 0, 3, 64, 64, 0)
```

That is 880 callback arrivals, 204 published, 676 handoff drops, 0 publication
drops, TX accepted 3, high-water/capacity 64/64, queued 0. Host/QEMU losses before
callback arrival are not counted as handoff drops. Ethernet accepts full L2
messages (tested NIC bounds 60–1514); this is not USB packet completion.

## Residual parallel paths and compatibility decision

| Path and concrete evidence | Current role / decision |
| --- | --- |
| Common Device objects: `sysvobj.c` catalog/open/device operations; `sysdevether.c` provider | The audited USB/Ethernet interface. Both use `VkindDevice` and shared Handle/rights/lifetime operations. No duplicate generic table was added. |
| Older object USB service: `ObjOpenUsb=12`, `UsbList=20`, `UsbEpOpen=21`; `sysvobj.c` `usbsvcmaxrights`, `vobjusbopen`, USB list/endpoint open | Still implemented, sharing Vtab and Ep claims. Object USB creation follows `iseve`/`Vcreate`. Broader endpoint interfaces lack a replacement in the narrow control-only common USB path. Not proven dead; retained. |
| Native USB syscalls 63–70: `USBOPEN/LIST/EPOPEN/READ/WRITE/IOCTL/CLOSE/EPNAME`; `sysusb.c`, generated `systab-gcc.h` | Active dispatch and separate 16-slot generation-tagged Usbhtab. Kind/mode/copy/length checks and exit/exec cleanup remain. This is legacy capability policy, not the common object table. Retained, not renamed into a new subsystem. |
| USBIOCTL preparation: `micropython/modplan9.c` `p9_usbioctl`, `sysusb.c` `sysusbioctl`, `usbcore.c` `usbcore_ioctl`; `tools/host/t12/device-checks.py` | **Required live dependency:** T12 uses native open/list/endpoint open, root ioctl 3 to prepare a downstream device, child ioctl 8 for address, ioctl 4 for detach. Actual measured downstream control DATA then uses `obj_device_send/recv`. Adapter bounds writable input/output to 4096 and checks endpoint kind. It creates no extra table. Do not delete it as unused. |
| Native USB READ/WRITE/EPNAME bindings | Active alternative stream APIs; not the measured common control DATA call site. That absence does not prove whole-project dead code. Same `usbcore_dataopen` claim prevents competing endpoint DATA opens. |
| Legacy permission boundary: `sysusb.c` `sysusbopen` / `usbchecksvc` | Legacy service open does **not** apply the new catalog's `iseve`/`Vcreate` gate. It checks valid legacy service Handles and endpoint modes. Therefore common catalog denials are **not** proof of global USB isolation across old APIs. Recorded limitation; no silently incompatible policy change. |
| `etherlegacyrxhook` / `ethernetifinithook` in `ethercore.c` and `etherif.h` | Source search found definitions/declarations and reads but no current kernel assigners. Nil in this native path; not an active Netfile/Chan compatibility service. No evidence requires deleting linked residue. |
| Repeated USB lookup in `vobjdevicesend`, legacy-specific wrappers/destructors | Source/error-path review found no demonstrated unsafe ownership, leak or rights discrepancy. A second lookup is not itself grounds for a refactor. Retained with no runtime change. |
| Disabled native IP startup and UDP/TCP object opens | Remain disabled. Linked historical code is not a T16 compatibility requirement. No networking, crypto, ESP or protocol work was performed. |

## Fresh build and test evidence

Commands ran from `/workspace/t16` using GCC 14.2, Clang/LLD 19 tools, QEMU
10.0.13 TCG and OVMF; `/dev/kvm` is unavailable. The reused toolchain is host
infrastructure, not a reused test result. The kernel and MPY were freshly built.

```sh
export PATH=/workspace/scratch/toolchain/usr/bin:$PATH
export LD_LIBRARY_PATH=/workspace/scratch/toolchain/usr/lib/x86_64-linux-gnu:/workspace/scratch/toolchain/usr/lib/llvm-19/lib
export QEMU_DATA_DIR=/workspace/scratch/toolchain/usr/share/qemu
export OVMF_CODE=/workspace/scratch/toolchain/usr/share/OVMF/OVMF_CODE_4M.fd
export OVMF_VARS=/workspace/scratch/toolchain/usr/share/OVMF/OVMF_VARS_4M.fd
python tools/host/build-kernel.py --kernel --jobs 8 >build/t16/kernel-build.log 2>&1
python tools/host/build-mpy-gcc.py --jobs 8 >build/t16/mpy-build.log 2>&1
bash tools/build-efi-gcc.sh >build/t16/efi-build.log 2>&1
bash tools/build.sh >build/t16/host-build.log 2>&1
T12_OUT=$PWD/build/t16/t12 bash tools/host/t12/run-device-checks.sh >build/t16/t12.log 2>&1
T13_OUT=$PWD/build/t16/t13 bash tools/host/t13/run-ethernet-checks.sh >build/t16/t13.log 2>&1
```

| Check | Fresh result | Evidence |
| --- | --- | --- |
| Canonical kernel | PASS: 133/133 selected units, 135-unit closure; register/header/ABI checks and final link | `build/t16/kernel-build.log`; initial intermediate "link not attempted" precedes the successful final link, not a failed final build |
| Canonical MicroPython | PASS: 254/254 built, 0 cached; ABI/link and no undefined symbols | `build/t16/mpy-build.log` |
| EFI | PASS | `build/t16/efi-build.log` |
| Host formatter and mpy-cross | PASS | `build/t16/host-build.log` |
| T12 USB | **66/66 PASS**, sequential executed assertion verification | `build/t16/t12.log`, `t12/serial-boot.log`, `t12/report.txt` |
| T13 raw Ethernet | **55/55 PASS**, executed assertions and external peer records verified | `build/t16/t13.log`, `t13/serial-boot.log`, `t13/peer.log`, `t13/report.txt` |
| Normal boot/commit/REPL | PASS in both fresh boots: BOOT SUCCESS, GE/O COMMIT SUCCESS, REPL READY, no panic | Both serial logs and runner summaries |
| Runtime source comparison | PASS: no runtime/ABI/build/test source diff from start SHA | Final explicit staged diff inventory; machine-readable report includes source hashes |

Both integration runs use the same freshly built kernel/MPY/EFI artifacts. T12
and T13 execute in separate boots; this is not a combined-device boot claim.
No earlier-stage PASS was credited as fresh. No build or integration test failed
in this execution. The JSON report contains SHA-256 hashes of binaries and logs;
local evidence is retained separately from source (no disk images in the PR).

## Diff inventory and limits

Exactly eight delivery paths: `AGENTS.md`, `README.md`, `docs/ROADMAP-T8-T16.md`,
`docs/STATUS.md`, `docs/DEVICE-ABI.md`, this report, its `.json`, and
`tools/host/mcp-public-files.txt` (indexes the two result files). The changes narrow
current scope, clarify existing semantics, and record evidence. No syscall,
layout, driver, kernel, binding, user program, test or build command is changed.

- **NOT RUN:** T14 IPv6/T15 ESP focused regressions. No common/runtime code
  changed; neither is a prerequisite. Existing sources are preserved unchanged.
- **NOT RUN:** physical devices/KVM, USB SMP stress, active HCI unplug or timeout
  fault injection, allocation failure/TX-queue saturation injection, exhaustive
  scheduling interleavings, combined USB+Ethernet boot. No physical-device result
  or exhaustive robustness claim follows from QEMU coverage.
- **Source reviewed, not separately forced:** creation of aliases beyond the
  4/16 admission thresholds and the full legacy API permission boundary.
- **EXCLUDED by owner:** all Wi-Fi work; not a missing coverage obligation.
- Final publication/readback and PR references are in the delivery manifest and
  MCP completion report. No merge is performed by agentti.

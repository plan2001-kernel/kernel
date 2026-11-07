# T12 finishing result — 2026-11-07

**Integration gate: PASS in the emulated endpoint environment, 66/66 executed
checks. Source publication: BLOCKED. Physical hardware: NOT RUN.** The result
covers one existing native USB control-endpoint provider, including downstream
QEMU USB mouse transfers through xHCI; T13–T16 remain untouched.

## Source and patch provenance

Task `a541c5342a43453a93076a582a0bcf91` continues T12 task
`81f119886e824a87b00a3d6f8b72873e`. Host identity was verified as
`9db093436960` (managed Linux environment, not kuha). The user authorized local
work while kuha is unavailable. Work is isolated at
`/workspace/t12-finish/source`; the earlier workspace and patch are preserved.

The published T11 source was freshly fetched through the Plan2001 source MCP.
Its complete deterministic archive SHA-256 was verified before extraction:
`524417426c965e0a57655ece6a0c3fbcd9cac98f299a055b85828cba6ffb2b0d`.
The original user-supplied T12.patch, SHA-256
`e2813793a47f07c702ac930f1704de0135100702aafdd26e942480a0faa04cd8`,
passed a strict context check and applied to this fresh copy with no fuzz or
rejected hunks. Record: `build/t12-finish/patch-validation.txt`.
The final deliverable is a complete replacement patch relative to T11, not a
patch that assumes the server already contains T12. Final packaging verifies
that applying it to another clean T11 tree reproduces every public file.

## Review findings and narrow corrections

- **USB authorization:** the initial patch let Vsend submit state-changing
  setup requests. SEND now requires Vcontrol except for an explicit downstream
  standard read-only allowlist. Vendor/class IN is not automatically trusted.
  Native root-hub reads require Vcontrol because rhubwrite may finish a prior
  reset. Both control and send rights are checked from the actual Vhand before
  any provider call. [DEVICE-ABI](DEVICE-ABI.md) defines the exact allowlist.
- **Uncertain transfer outcomes:** after an error once usbcore is entered,
  every alias of that session is latched failed; SEND/control require teardown
  and explicit reopen. No automatic retry or rollback claim. A malformed or
  denied request, quota error before provider entry, or pending-slot rejection
  leaves a healthy session usable. The provider's original error is returned.
- **Ownership/buffers:** reviewed exact request lengths, copied input, validated
  output, immutable response allocation and short-response storage/accounting.
  Receive installs its result Handle before dropping the pending response;
  full-table failure preserves it. Vhand references pin the session across
  blocking HCI calls; one QLock serializes aliases and the one pending result.
  Existing endpoint close/stop and reference-release paths are reused.
- **Reproducibility:** tracked `tools/host/t12/device-checks.py` and
  `run-device-checks.sh` use the existing mkovol SOURCE argument and sanctioned
  QEMU O-boot script. No debug kernel, probe selector or second FIFO was added.
- **Peripheral preparation:** a thin writable-buffer adapter exposes the
  existing legacy USBIOCTL to MicroPython. Normal USBnewdev/address/detach
  operations prepare/remove the child endpoint. It introduces no new syscall
  or controller code and preserves legacy policy. All measured peripheral
  data and completion operations use T12 OBJ sessions, not legacy USBREAD/WRITE.

The earlier patch already added the device ABI, shared-core dispatch, libc and
MicroPython adapters, and associated documents. The finishing delta touches
only sysvobj authorization/error state, the legacy buffer adapter, focused
tests and the four requested status/contract documents. ObjectFIFO, GE/O format,
native IPv6/security, existing driver code and syscall numbers are unchanged.

## Reproduction

From the source root, using the normal Linux-hosted toolchains:

```sh
python3 tools/host/build-kernel.py --kernel --jobs 8
python3 tools/host/build-mpy-gcc.py --jobs 8
bash tools/build-efi-gcc.sh
bash tools/build.sh
bash tools/host/t12/run-device-checks.sh
```

Dependencies: GCC/binutils, clang-19/lld-link-19, make, Python 3, QEMU x86,
OVMF and mtools. This executor used Debian snapshot packages extracted under
`/workspace/scratch/toolchain` without changing system packages. Its overrides:

```sh
export PATH=/workspace/scratch/toolchain/usr/bin:$PATH
export LD_LIBRARY_PATH=/workspace/scratch/toolchain/usr/lib/x86_64-linux-gnu:/workspace/scratch/toolchain/usr/lib/llvm-19/lib
export QEMU_DATA_DIR=/workspace/scratch/toolchain/usr/share/qemu
export OVMF_CODE=/workspace/scratch/toolchain/usr/share/OVMF/OVMF_CODE_4M.fd
export OVMF_VARS=/workspace/scratch/toolchain/usr/share/OVMF/OVMF_VARS_4M.fd
T12_OUT=$PWD/build/t12-finish/run3 bash tools/host/t12/run-device-checks.sh
```

The tracked runner creates a wrapper adding exactly an xHCI controller and
USB mouse to the existing disposable q35/TCG O-boot configuration. It seeds
tracked checks as an ordinary init module, then requires successful init.run,
GE/O commit, REPL and a line-anchored executed result. It independently verifies
sequential numbered assertions. It builds no alternative kernel and writes
only regular files under build/. The executed run used one vCPU; the process
checks demonstrate interleaving, not an SMP stress campaign. KVM and physical
devices are not needed.

## Observed builds and execution

All four canonical builds PASS in this round:

- Kernel: 132/132 selected units, 134/134 full closure, ABI/header/register
  checks and final link PASS. `build/t12-finish/kernel-build.log`.
- MicroPython: 247/247 units, ABI checks and final link PASS, no undefined
  symbols. `build/t12-finish/micropython-build.log`.
- EFI: native PE32+ validation/link PASS. `build/t12-finish/efi-build.log`.
- Native GE/O formatter and same-version mpy-cross PASS.
  `build/t12-finish/host-build.log`.

Tested artifact SHA-256 values:

| Artifact | SHA-256 |
| --- | --- |
| Kernel | `2c2eef4de7d2b773f9a9a873ff31f088786e8989d6afe0f65344df4b78ae34fa` |
| MicroPython | `ff984ba69029f973e99a0301a44a8d85dccda768780ae5708227a01dda863d77` |
| EFI | `b54f06fbd4e7d065bb831da39c6acb921af8cade2390a17888773d28eb6c3371` |

The generated syscall table remains 135 named / 62 active / 73 reserved;
device ABI records remain 32/24/16 bytes (compile-time assertions).

Final run: `build/t12-finish/run3/report.txt`, `boot-boot.log` and
`build/t12-finish/run3.log`: **T12-FINISH 66 PASS**, all 66 sequential assertions
observed, normal native boot and GE/O commit PASS, no traceback or kernel panic.

| Checks | Executed result |
| --- | --- |
| Catalog fields/count; attenuated and metadata-only catalog denial; bad selector/family/rights and raw malformed open/list/receive | PASS |
| Empty/mismatched/oversized messages and non-amplifying grants | PASS |
| Vsend-only root, OUT, vendor IN and class IN denial before effects; authorized root control and complete root transactions | PASS |
| Existing ObjectFIFO envelope/content handoff, independent immutable responses, short reply trimming, empty OUT completion | PASS |
| Alias close, last close with pending result, actual 96-Handle ceiling and exact receive failure, successful retry with preserved sequence/bytes | PASS |
| Root provider error, failed-session enforcement on aliases and explicit reopen | PASS |
| Downstream USB mouse present on port 5; legacy enumeration; actual 18-byte DEVICE descriptor through HCI | PASS |
| Attenuated Vsend read-only peripheral descriptor; denied SET_CONFIGURATION and vendor IN; 4096-byte requested short reply equals the 18-byte descriptor | PASS |
| Actual downstream OUT SET_CONFIGURATION and completion | PASS |
| Independent parent/child: 12 successful descriptor transactions each, exact shared sequence increment, exact child PID and empty successful status | PASS |
| Child becomes final device owner, exits with unreceived completion; endpoint can be reopened and used | PASS |
| Actual peripheral STALL (exact Stall Error), no completion or reuse through alias; close/reopen returns the descriptor again | PASS |
| Native idle USB detach (exact device-is-detached error), failed-session enforcement and independently held response surviving close/detach | PASS |

The raw descriptor printed by the final run starts `12 01 00 02` and is 18
bytes. This is a QEMU USB mouse response through xHCI setup/data/status TRBs,
not a root-hub response. Root status replies are explicitly identified
separately. The 4096-byte *request* produced 18 valid response bytes; no claim
is made that the peripheral supplied a 4096-byte payload.

Run history is preserved: run1 passed 44 earlier assertions but failed the
fixture's downstream-present assertion because it searched ports 1–4 only;
the mouse was on port 5. The tracked scanner now searches the configured eight
ports. Source review additionally removed the unsafe root-read exemption.
Run2 passed 62 assertions including downstream, concurrency, exit and detach;
run3 added and passed the four actual STALL/reopen checks. Original 34/34
root-only evidence belongs to the predecessor round and is not included in
the 66 checks or represented as peripheral validation.

## Limits and publication

NOT RUN: physical USB hardware, cable unplug during an active HCI transaction,
forced timeout/DMA/controller recovery faults, allocator failure after a
successful hardware effect, asynchronous cancellation, SMP stress, untrusted-user iseve
gate tests, USB bulk/interrupt/isochronous transfer, broad regressions and
T13–T16. Native idle detach and actual peripheral STALL were executed; they do
not establish all of those cases. Device-wide revoke/cancel and hotplug
notification are not implemented; the explicit failed-session/close contract
is documented, not presented as cancellation or hardware rollback.

Publication is **BLOCKED**: available Plan2001 MCP tools expose only source
reads and task submit/status/claim/report, with no authorized writable source
upload/stage/activate operation. No SSH or unsupported upload path is used.
A task completion report records this round only; it is not publication.
The final complete patch, verified source archive/manifest and execution
logs are delivered for integration. No new server CURRENT or kuha equality
claim is made. No extra notification or automation was created.

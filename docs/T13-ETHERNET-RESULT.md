# T13 — raw Ethernet through common Device ABI

2026-11-07: **emulated integration gate PASS**, 55 executed T13 assertions
and a fresh **66/66 T12 USB regression PASS** on the same kernel/MPY binaries.
Physical NICs and fault injection listed below are NOT RUN. This demonstrates
raw Ethernet transport, not an IP stack or wire-completion API.

## Baseline and execution

Task: `e76dbb6bc4a147c08ec82402e3b863e1`, claimed through Plan2001 MCP.
Authoritative baseline: GitHub `plan2001-kernel/kernel` main commit
`a80b9641ae57e9493f1c4d7b9d11b0a55174917a` (T12 plus merged workflow).
Dedicated branch: `agent/t13/ethernet-raw-frame`; main is not updated by this
work. The historical MCP T11 source snapshot was not used as the baseline.
GitHub publication and MCP task reporting are separate operations.

Actual executor: `9db093436960`, Debian 13, Linux 6.18.57, `/workspace/t13`.
No `/dev/kvm`; supported QEMU 10.0.13 TCG, OVMF, 2 vCPUs / `*ncpu=2`.
Parent and fast consumer run on CPU 0, slow and last-owner children on CPU 1;
`firstuser` log records confirm the requested placements. No SSH, physical
boot, physical partition, GE/O format or T14–T16 change was involved.

Exact environment and commands (all artifacts are real canonical builds):

```sh
export PATH=/workspace/scratch/toolchain/usr/bin:$PATH
export LD_LIBRARY_PATH=/workspace/scratch/toolchain/usr/lib/x86_64-linux-gnu:/workspace/scratch/toolchain/usr/lib/llvm-19/lib
export QEMU_DATA_DIR=/workspace/scratch/toolchain/usr/share/qemu
export OVMF_CODE=/workspace/scratch/toolchain/usr/share/OVMF/OVMF_CODE_4M.fd
export OVMF_VARS=/workspace/scratch/toolchain/usr/share/OVMF/OVMF_VARS_4M.fd
python tools/host/build-kernel.py --kernel --jobs 8
python tools/host/build-mpy-gcc.py --jobs 8
bash tools/build-efi-gcc.sh
bash tools/build.sh
T13_OUT=$PWD/build/t13/final bash tools/host/t13/run-ethernet-checks.sh
T12_OUT=$PWD/build/t13/t12-regression bash tools/host/t12/run-device-checks.sh
```

The focused T13 runner seeds a tracked MPY module through mkovol's existing
SOURCE argument, then calls the sanctioned `tools/qemu-o-boot.sh` with
`--no-build --tcg --cpus 2 --ncpu 2 --obootcpu 0 --timeout 180
--udp6-peer 31301:31302 --obootexec 0x4f45584543000003`.
Despite its historical option name, `--udp6-peer` carries complete raw L2
frames via QEMU's socket netdev, with no guest IPv6 dependency. The peer binds
localhost UDP 31301 and sends raw frames to QEMU port 31302. It changes link
state through the ordinary QEMU monitor. No kernel diagnostic mode is added.

## Executed evidence

| Criterion | Result | Evidence |
| --- | --- | --- |
| Kernel, ABI/register/header checks and link | PASS | 133/133 selected C units, 135-unit closure |
| MicroPython, libc and ELF link | PASS | 247/247 C units, no undefined symbols |
| EFI and native host tools | PASS | Canonical builds, artifact hashes in JSON |
| Native O boot, commit and REPL | PASS | Sanctioned runner validates all milestones |
| Whole-frame TX and RX via real virtio NIC/peer | PASS | Exact 60-byte MAC/EtherType/payload/padding comparisons, binary 00/ff RX |
| Authorized discovery/open; invalid selector/rights | PASS | Exact errors asserted before device access |
| Unauthorized send, receive, subscription, publish and detach | PASS | Attenuated handles and read-only RX FIFO |
| TEE / independent processes / bounded overflow | PASS | Same object identity and envelope in parent and both children; fast 8/8, slow final 2/8 with 6 drops |
| Non-amplification | PASS | Children receive device rights 44, RX FIFO rights 40, bytes rights 9; grant escalation denied |
| IRQ handoff boundedness/overflow | PASS | Real incoming flood; capacity/highwater 64, 705 counted handoff drops |
| Revoke, duplicate revoke and publication after revoke | PASS | FIFO drains/closes, duplicate error retained, worker records publish drop |
| Closed/stale handles and reopen | PASS | Exact errors; next owner reacquires RX sink |
| Link down and recovery | PASS | Virtio config IRQ reports down, TX fails, link-up RX exact bytes |
| Last-owner process exit with queued RX | PASS | FIFO closes/drains, next open succeeds, child on CPU 1 |
| Detach during external RX flood | PASS | Callback/worker join, closed flag set, handoff empty |
| USB T12 behavior after shared changes | PASS | Fresh 66/66 checks; same kernel and MPY hashes |
| Physical NIC/hot-unplug, IRQ-context RX on another driver | NOT RUN | No physical device; virtio RX itself runs in driver rxproc |
| TX queue saturation/closed-queue and allocation-failure injection | NOT RUN | Error ownership paths reviewed; not forced in QEMU |
| Exhaustive SMP race campaign, broad regressions, T14–T16 | NOT RUN | Outside this focused gate |

Final flood snapshot before teardown: arrivals 839, published 134,
handoff_drops 705, publish_drops 0, TX accepted 3, highwater/capacity 64,
queued 0. This accounts only frames reaching the registered callback. The
peer sent 8192 x 1514-byte frames; unobserved frames may be lost earlier in
host UDP/QEMU/driver buffers and are not counted as handoff drops. FIFO
subscription drops are separate counters. No synthetic loopback was used.

First observed whole-frame TX at the external peer (60 bytes, no FCS):

```text
02000000130152540012345688b55431333a4f4e45000000000000000000000000000000000000000000000000000000000000000000000000000000
```

Corresponding RX, compared byte-for-byte by MPY:

```text
52540012345602000000130188b55431333a52583a6f6e6500ff00000000000000000000000000000000000000000000000000000000000000000000
```

The guest observed both complete frames and EtherType `0x88b5`; host peer
receipt is stronger evidence for those TX frames than the SEND return itself.
SEND still promises queue acceptance only, never physical/on-wire completion.

Local logs: `build/t13/{kernel-build,micropython-build,efi-build,host-build}.log`,
`build/t13/final/{boot-boot,serial-boot,peer,qemu-boot}.log` and `report.txt`,
`build/t13/t12-regression/` and its runner log. JSON records exact source and
artifact hashes. Earlier fixture iterations `run`/`run2` failed because the
fixture used invalid subscription rights / unsupported bytes.hex(), and one
peer survived the runner's EXIT trap; these were fixed. `run3` passed 39 and
`run4` passed 51 before the final 55-check cross-CPU/TEE checks. These earlier
iterations are not counted as final acceptance or hidden as successful tests.

## IRQ, ownership and lifecycle review

`etheriq` gives its registered sink an owned private linear Block. Its existing
IRQ-safe copy uses iallocb. `vetherinput` only takes an ilock, validates length,
stores that owned Block in 64 fixed slots or frees it on rejection, updates
counters, unlocks and wakes the worker. It calls no QLock, sleeping allocation,
OBJ/FIFO operation, user memory or protocol parser. All allocation and
immutable-object publication occurs in `device-ether-rx` process context.
The virtio test exercises this handoff through the real NIC driver; it does
not pretend that virtio's rxproc is an actual RX interrupt context.

The worker owns each popped Block until free, copies all L2 bytes into an
immutable VkindBytes object, and publishes through the extracted **same**
ObjectFIFO publication implementation. Each subscription gets a reference to
that same object and its own bounded queue. Publication failure frees both
references and Block and increments publish_drops. No Ethernet branch or
protocol logic exists in the generic publication core.

Detach serializes TX, marks closed under ilock, unregisters and joins active
callbacks, wakes/joins the worker, then closes/drains the ordinary FIFO. Notes
cannot abandon the callback or worker join. A separate worker reference pins
state through its final wakeup, even if the owner observes done first on
another CPU. Pending Blocks are freed during close; these intentional teardown
discards are not additional overflow-counter events. Final device close/exit
performs the same teardown. Outstanding FIFO/bytes handles keep their own
object lifetime, not a physical NIC claim. Revoking the RX FIFO closes queues
but leaves the device session owned until explicit detach or final close;
subsequent RX publication is rejected and counted.

TX allocates/copies the user frame in process context. On ethercore failure
the caller still owns/frees its Block; on success ethercore consumes it and
returns the accepted length. Existing Queue/qpasstry bounds and errors are
reused; there is no new TX queue. Session gate prevents TX/detach overlap.

## Legacy network decision and remaining code

The task explicitly overrides the roadmap's requirement to preserve the old
kernel network service. Source inspection found no O storage/loader, console,
USB or general MPY boot dependency on `ipv6native_boot_start`. Its previous
sole-NIC RX registration conflicts with raw access. T13 removes that startup
and rejects OBJ TCP/UDP opens explicitly. The 55-check MPY/O boot and the
66-check USB boot demonstrate those real dependencies still work. No native
IP service is started and no protocol fallback is used for these tests.

The historical nativev6/nativeudp6/TCP/IP/ESP sources remain in the pc64 link
closure: sysvobj's old provider release/I/O/open/accept references and sysnet6
still pull their implementations into the link. This is an implementation
residue, **not a required compatibility feature or a demonstrated boot need**.
No #l/#I/Netfile/Chan adapter is added for T13; the existing ethercore native
path already works without it. Removing the remaining dead provider symbols
and their entire protocol closure is not claimed by this patch; runtime
disabling is the authorized T13 choice. There is no supported legacy kernel
network functionality to preserve in this profile. Userspace networking is
left to later tasks.

Imported baseline scripts lacked executable bits. This patch restores only
the build/format/QEMU entry points used by this validation; it does not modify
source-publication tools, authorization or host network configuration.

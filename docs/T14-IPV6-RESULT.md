# T14 — MicroPython userspace IPv6 / ICMPv6 / Neighbor Discovery

2026-11-08: **minimal emulated integration gate PASS**. Final run:
**24 guest assertions and 245 independent external-peer assertions PASS**;
**55/55 T13 raw-Ethernet regression PASS**; normal two-boot GE/O persistence
and REPL PASS. This is the bounded static link-local PoC in
[user/ipv6](../user/ipv6/README.md), not a complete router/IPv6 stack.
T15 ESP remains PLANNED and unimplemented. No cryptography or IKE was added.

## Source, executor and changes

Plan2001 MCP task `a96270b9c4a04f57be3872f05fa9eeb9` was claimed, then the
actual GitHub main baseline was verified as
`558a57469ca7fdb18b0af4f47c746257ca2e5aa1` (merged PR #3). Work branch:
`agent/t14/userspace-ipv6`. No old MCP T11 snapshot was used. Main is not
changed/merged by this work; final commit/PR are recorded in the MCP report.

Executor: Debian 13, host `9db093436960`, Linux 6.18.57, `/workspace/t14`.
GCC 14.2.0, Clang/LLD 19, QEMU 10.0.13/OVMF. No /dev/kvm; all boots used TCG.
The T14 parent is on CPU 0; actual OSPAWN node children on CPU 1. Source
`firstuser` records identify the real PIDs/CPUs, not hypothetical placements.
No SSH, real disk/partition writes or physical boot was involved.

`user/ipv6/node.py` contains the real protocol implementation. The focused
fixture under `tools/host/t14/` carries that exact source into the ordinary
GE/O `init` module, then gives a read-only code Handle and device rights 46
to an independently running MPY OID child. Local control/result ObjectFIFOs
use existing generic operations; no network FIFO or provider is introduced.
Packet reception is child `obj_receive` -> immutable `obj_read` -> Node parser;
TX is `obj_device_send`, never host-generated guest replies. The external
peer uses independently written checksums/header code, not the guest module.

**Zero kernel, ABI or MPY C/build changes.** Device and interpreter behavior
are reused from T13. Native kernel IPv6 startup and OBJ TCP/UDP opens stay
disabled. Kernel IP cannot answer in parallel: source remains unchanged and
a real external echo after all node owners close receives no reply. The
parent also completes normal boot/REPL. Optional host serial-log
synchronization is used only to time that final closed-state probe.

## Exact build and execution commands

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
T14_OUT=$PWD/build/t14/final2 bash tools/host/t14/run-ipv6-checks.sh
T13_OUT=$PWD/build/t14/t13-regression bash tools/host/t13/run-ethernet-checks.sh
export T14_REAL_QEMU=$(command -v qemu-system-x86_64)
QEMU=$PWD/build/t14/final2/qemu-data QOB_INI_BOOTEXEC=0x4f45584543000003 \
  bash tools/qemu-o-persist.sh --no-build --tcg --repl --timeout 120 \
  --output-dir $PWD/build/t14/persistence-pass
```

The executed persistence command used the byte-identical copied wrapper
`build/t14/qemu-data`. Its QEMU path variable must be exported. The existing
qob defaults honor `QOB_INI_BOOTEXEC`; this selects the real MPY OID without
adding unsupported persist-script flags or changing the runner. T14 calls
the sanctioned qemu-o-boot with `--no-build --tcg --cpus 2 --ncpu 2
--obootcpu 0 --timeout 180 --udp6-peer 31401:31402
--obootexec 0x4f45584543000003`. The option's historical UDP6 name means raw
Ethernet over a QEMU socket netdev, not guest UDP. External UDP ports are
localhost encapsulation; the wire is EtherType 0x86dd / IPv6 NH=58.

Builds: 133/133 selected kernel C units, 135 closure, ABI/register/header and
final link PASS; MPY 247/247 units, ABI and link/no undefined symbols PASS;
EFI, native formatter and matching mpy-cross PASS. Exact hashes are in
[T14-IPV6-RESULT.json](T14-IPV6-RESULT.json).

## Actual protocol evidence and tests

Local address `fe80::141`, MAC `52:54:00:12:34:56`; peer `fe80::142`,
MAC `02:00:00:00:14:01`. No preloaded neighbor MAC is given to Node. The first
guest ping sends a real multicast NS, learns the peer MAC from a solicited
NA, and exchanges echo both directions. Peer checks all full MAC/IP/ICMP
bytes, declared lengths, NH, hop limits, checksum and echo ID/sequence/data.
Binary echo data 00/ff, odd payload lengths and extra Ethernet padding run.

Actual first guest NS (86 bytes; EtherType 86dd, IPv6 NH58, hop255):

```text
3333ff00014252540012345686dd6000000000203afffe800000000000000000000000000141ff0200000000000000000001ff0001428700f21b00000000fe8000000000000000000000000001420101525400123456
```

Actual peer NA (86 bytes, solicited+override, TLLA):

```text
52540012345602000000140186dd6000000000203afffe800000000000000000000000000142fe8000000000000000000000000001418800005b60000000fe8000000000000000000000000001420201020000001401
```

| Acceptance | Executed result |
| --- | --- |
| Real external peer / virtio / T13 / MPY in both directions | PASS: guest- and host-initiated Echo Request/Reply, independent exact full-frame checks |
| Address/neighbor/multicast | PASS: configured address/MAC, NS/NA resolution, solicited-node mapping, all-nodes DAD response, valid unknown-sized ND option skipping |
| Bounded neighbor state | PASS: five extra valid senders force oldest-expiry eviction, capacity four; finite 3000 ms aging and fresh resolution after expiry |
| Finite unanswered paths | PASS: unresolved address emits exactly 3 total probes (initial +2 retries), then nd-timeout; known peer with withheld reply reaches echo-timeout |
| Malformed/unsupported wire input | PASS: 24 external negative cases with silence window and a following valid echo barrier (details below) |
| Short parser inputs | PASS, parser-only: 13-byte Ethernet and 39-byte IPv6 header passed directly; not live-NIC evidence |
| Close/revoke/shutdown | PASS: controlled child close, exact successful PID/status, surviving RX FIFO closed, real FIFO revoke stops polling child and releases device, reopen succeeds |
| No competing native responder | PASS: source still disabled; after child teardown external echo gets no reply |
| Parent / multiprocess / graph | PASS: independent OSPAWN children, explicit attenuated capabilities, parent alias/Counter check and normal GE/O commit/REPL |
| Persistence | PASS: same disposable O partition, boot1 run=1/commit1; boot2 restore1/run=2/commit2, Counter alias and executable hash preserved |
| T13 regression | PASS: fresh 55/55 assertions including TEE, rights, link, flood/drop/revoke/exit; handoff drops490, highwater64 |
| USB T12 regression | NOT RUN this round: no common device/kernel/ABI changes; earlier evidence not recredited |
| Full original T8 campaign | NOT RUN; focused spawn/PID/graph/REPL plus canonical persistence above are the relevant checks |
| Physical NIC/KVM, Linux ping6 interoperability, exhaustive races/fuzzing/clock adjustment | NOT RUN; no production/full-stack claim |

The peer's 24 negative cases are bad IPv6 version, oversized declared payload,
wrong IPv6 destination, wrong Ethernet destination, unsupported Next Header,
invalid checksum, nonzero echo code, unspecified/loopback/multicast echo source,
Ethernet/IPv6 multicast mismatch, ND hop254, zero/overrun/duplicate/wrong-MAC
ND options, multicast ND target, DAD with SLLA, missing SLLA, nonzero NS reserved
field, unsolicited NA, oversized IPv6 packet, and short Ethernet/IP frames.
Each is actually sent to QEMU; a subsequent valid echo proves the guest remains
responsive. **The two short frames are rejected before the MPY parser by the
T13 min-frame boundary; silence does not prove delivery.** The two direct
parser checks separately cover those impossible-to-deliver lengths. Other
malformed IP/ND cases reach MPY and increment fixed rejection counters.

Final live-phase snapshot before timeouts: RX60 (includes 2 direct parser
inputs), TX37, echo requests27, reply1, NS8/NA1, learned8, evicted2,
wrong_dst3, bad_nd10, unsupported2, checksum1, bad_ip5, bad_eth3. The counters
are fixed-key bounded state. Later assertions prove >=4 expirations, 3 unknown
probes, fresh NA/reply, cached-peer reuse, echo-timeout, controlled close and
revoke. Do not interpret parent control poll counts or parser tests as wire
exchanges.

## Reproducibility, limits and earlier attempts

`user/ipv6/README.md` documents API, ownership, startup and scope. Node supports
one supplied unique static fe80::/64 address; no automatic address assignment,
router/SLAAC/RA/DHCPv6, NUD state machine, extension headers, fragmentation,
PMTU/ICMP errors, IPv4, UDP/TCP or ESP. IPv6 MTU is 1280; max accepted L2 frame
1294, payload1240, echo data1232, one pending ping, four default neighbors,
RX subscription16. Timers are cooperative while poll runs, using existing
plan9.time_ns(); no clock-adjustment/rate/security guarantee is claimed.
ND is unauthenticated, not cryptographic peer identity. These limits are
explicit unsupported/drop behavior, not silently omitted router features.

Logs under `/workspace/t14/build/t14/`: canonical `*-build.log`, final2
`{serial-boot,boot-boot,peer,qemu-boot}.log`, `peer.json`, `report.txt`,
`t13-regression/`, `persistence-pass/` and their runner logs. No panic or disk
error was observed in the passing runs. The first live run passed 24/208,
then stricter address/negative checks passed 24/244; the final closed-state
wire probe brought the checked peer count to245. These are development
iterations, not additional final-gate assertions.

Two initial persistence invocations failed at the harness/configuration
boundary: missing exported QEMU-path variable (exit127), then no explicit
MPY selector (kernel-only boot reached ready, persist runner timed out).
The final explicit QEMU/MPY/REPL invocation passed both boots. No source fix
or weakened gate hid these failures; logs are retained with the evidence.

GitHub branch delivery, independent readback and MCP task completion are
separate from the historical source snapshot. This task creates no subsequent
agent tasks and does not merge main or implement T15.

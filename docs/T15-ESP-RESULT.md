# T15 — minimal userspace ESP interface integration

2026-11-08: **minimal emulated integration gate PASS**. Actual run: **13 guest
assertions / 43 independent external-peer assertions**, with fresh T13
**55/55** and T14 **24 guest / 245 peer** regressions PASS. Normal
kernel/MPY boot, GE/O commit, parent REPL and canonical builds PASS.
This proves existing interfaces can carry a small userspace ESP example;
**it is not a security product or production VPN**.

## Scope, baseline and executor

Agentti claimed MCP task `7a953b204f584f4f88b6951c7659808c` and inspected the
actual environment. GitHub main baseline was
`28b3af2c3879c92cb1c2c4d85e5a16d53416151f` (merged T14); branch
`agent/t15/minimal-esp`, worktree `/workspace/t15-esp`. Agentti ran on host
`9db093436960`, Debian13/Linux6.18.57, GCC14.2, Clang/LLD19,
QEMU10.0.13/OVMF/TCG. No /dev/kvm. Parent CPU0, MPY child CPU1.
Main is unchanged/unmerged by this work. Final commit/PR are recorded by MCP.
No old MCP snapshot, SSH, physical boot or physical partition writes were used.

First, roadmap/ESP scope were reduced under the owner's later assignment.
The earlier expansive design is preserved in
[history](history/ESP-SA-BUNDLE-2026-11-08.md), clearly superseded; it no
longer requires key objects/bundles, durable lifecycle/consumed generations,
anti-rollback, replay windows, rekeying, expiry/clock guarantees, secure
storage/distribution or an enterprise security campaign. IKE remains excluded.

## Small implementation and actual interface path

- `user/ipv6/esp_demo.py`: 87-line `EspNode(Node)` reuses **unchanged T14**
  addressing/ND/checksum/echo and the T13 Device ABI/ObjectFIFO. It adds ESP
  transport framing/deframing for the two configured test directions. Echo is
  protected; necessary NS/NA remains ordinary ICMPv6.
- `micropython/modaesgcm.c`: 60-line bounded, **in-process** binding to the
  existing `espcrypto.c` and five vendored BearSSL AES/GHASH/GCM units. No
  second AES/GCM implementation, new syscall or kernel crypto service.
- MPY Makefile/build manifest registers the module and those units, including
  crypto headers in incremental signatures. The private `ikeport.h` uses
  existing user standard types under PLAN2001_GCC_USER; the kernel type branch
  is unchanged. No vendor implementation or license is changed.
- Focused `tools/host/t15/` fixture: two public TEST-ONLY directional SAs,
  independent OpenSSL-backed external peer, small OID child/bootstrap and
  sanctioned qemu-o-boot adapter. No universal harness/kernel probe.

Actual path: trusted ordinary GE/O init -> existing MPY OID OSPAWN child ->
read-only code Handle9 / device46 -> device RX FIFO40 -> subscription12 ->
immutable received bytes9 -> user ESP decrypt -> T14 ICMP handling -> user
ESP encrypt -> existing DeviceSend/ethercore/virtio -> external peer. Local
control uses generic ObjectFIFO. Encryption runs in the MPY ELF, not a kernel
ESP service. The linker contains aesgcm_module/ike_gcm/br_gcm_run and no
esp6/espctl user symbols. **No kernel networking, runtime/abi, syscall table,
provider/FIFO type, pathname, Chan or fd changes.** ND/IP service startup and
OBJ TCP/UDP opens remain disabled as in T13; no parallel native responder.

`tools/host/t15/demo-config.py` alone contains the fixed public test values:
guest-to-peer SPI0x1501/key bytes0..15/salt11223344, peer-to-guest
SPI0x1502/key bytes16..31/salt55667788. Addresses fe80::151/fe80::152.
These are not secrets supplied by the user and must not be used in production.
They are carried in ordinary disposable test code, with no separate key
object/bundle infrastructure. Node's per-running-test sequence supplies an
8-byte explicit IV; nonce=salt4+IV8, AAD=SPI+sequence8, tag16, standard padding
and inner NH58. Intended protected frames have IPv6 NH50, not port50.

## Executed evidence

| Check | Result |
| --- | --- |
| Canonical kernel and libraries | PASS:133/133 selected units,135 closure, ABI/register/header and final link |
| Canonical MPY | PASS:254/254 units, ABI, no undefined symbols,553352-byte ELF |
| EFI and native formatter/mpy-cross | PASS |
| Guest primitive readiness | PASS: NIST empty/one-block AES-GCM vectors, decrypt equality, invalid-tag rejection before live test |
| Guest-to-peer protected echo | PASS: actual ESP Request decrypted independently by peer, protected Reply accepted in MPY |
| Peer-to-guest protected echo | PASS: three real protected Requests and exact decrypted Replies, including binary00ff payload |
| Existing raw NIC/Device/FIFO path | PASS:13 checked guest assertions and43 peer assertions, complete MAC/IP/ESP frames and checksums |
| Required ordinary ND | PASS: real multicast NS/solicited NA in both directions with LL address resolution |
| Packet tag/nonces | PASS: invalid tag gets no echo; guest IV1..4 and peer IV1..5 are distinct **within each running SA** |
| No plaintext fallback | PASS: every guest echo is NH50 ciphertext; a deliberately injected negative plaintext echo gets no response |
| Normal operation/lifetime | PASS: controlled child close, exact reaped PID/status, surviving RX FIFO closed, parent Counter alias/commit/REPL |
| Focused T13 regression | PASS:55/55; actual handoff drops480/highwater64; same new MPY ELF |
| Focused T14 regression | PASS:24 guest/245 peer; same new MPY ELF and existing unchanged fixture |
| Physical NIC/KVM, T12/full T8 rerun, two-boot persistence rerun | NOT RUN; no shared Device ABI/storage code change; normal commit/REPL checked in all three live runs |
| Lifecycle/replay/VPN/production cryptographic tests, T16 | NOT RUN / out of scope under later assignment |

Wire logs include full encrypted frame hex plus independently verified
recovered ICMP bytes. Four guest ESP frames were observed; peer sent five
(one intentionally corrupt). Guest snapshot: esp_tx4/esp_rx4, bad-tag1,
plain-drop1, NS1/NA1, echo requests3/reply1. Ordinary ND is plaintext by design.
The explicit negative plaintext probe is not an intended protected exchange;
no guest echo response or protected-data fallback was emitted in plaintext.

## Reproduction and retained logs

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
T15_OUT=$PWD/build/t15/run2 bash tools/host/t15/run-esp-checks.sh
T13_OUT=$PWD/build/t15/t13-regression bash tools/host/t13/run-ethernet-checks.sh
T14_OUT=$PWD/build/t15/t14-regression bash tools/host/t14/run-ipv6-checks.sh
```

Host peer uses installed Python cryptography AESGCM. The wrapper calls the
sanctioned runner with `--no-build --tcg --cpus 2 --ncpu 2 --obootcpu 0`,
`--udp6-peer 31501:31502` and the normal MPY executable OID. Despite that option's
historical name, localhost UDP only encapsulates raw Ethernet for QEMU; no
guest UDP protocol is used. The parent remains an ordinary MPY process and
returns to its REPL. [Usage and limits](../user/ipv6/README.md).

Exact source/artifact/log hashes are in [JSON](T15-ESP-RESULT.json). Local logs
are under `/workspace/t15-esp/build/t15/`: canonical *-build.log,
run2/{boot-boot,serial-boot,peer,qemu-boot}.log/peer.json/report.txt,
and the t13-regression/t14-regression folders and runner logs. Initial MPY
compilation failed on existing C name/size_t collisions; the binding names
and private USER port include branch were corrected, then254/254 passed.
The first live invocation's peer timed out before guest boot (5-second startup
window), so the guest reported ND timeout; startup now waits within the same
180-second boot bound, retaining 5-second post-start packet bounds. Those
failure logs are retained, not counted as passing runs or hidden by a weakened
protocol gate. No current integration failure is known in the tested scope.

## Limits

Fixed credentials and resettable counters are for this **disposable example
only**. No security across reboot/reused keys, durable key objects, lifecycle,
replay protection (duplicate valid packets may be accepted), anti-rollback,
rekeying, expiry, secure distribution/storage, VPN negotiation, IKE or full
RFC4106/production security is claimed. There is no key-management subsystem
and no user keys were requested. Echo payload is capped at1196 bytes to keep
ESP within T14's minimum IPv6 MTU. This is deliberate interface-validation
scope, not a security-hardening product or an authorization for T16.

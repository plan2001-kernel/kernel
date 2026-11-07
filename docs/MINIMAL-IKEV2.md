# Minimal IKEv2: userspace IKE, kernel ESP

> Test harnesses and userspace programs named below are retained in the
> separate `Plan2001-O-weekend-tests/plan2001` companion bundle. Relative
> `tools/...` and `user/...` paths refer to that bundle layout; they are not
> build or boot prerequisites of this canonical kernel tree.

The IKEv2 responder is now a separate **native Plan2001-O userspace ELF**,
`build/gcc/iked.elf`. It runs through the existing OSPAWN loader. The kernel
contains the IPv6 ESP datapath and a private Pipe control service. It contains
no IKE parser, IKE state machine, PSK table or P-256 implementation.

`runtime/abi/sys.h`, syscall numbers and syscall argument layouts are unchanged.
There is no new syscall, generic ioctl, pathname service, device filesystem,
raw-IP userspace endpoint, Linux ABI or dynamically loaded crypto module.

## Components

| Component | Location | Responsibility |
| --- | --- | --- |
| `iked` | `user/iked/main.c` | UDP/500 loop, startup configuration, Pipe client |
| IKE engine | `user/iked/ikecore.*` | IKE_SA_INIT, PSK AUTH, messages, cookies, timers |
| ESP datapath | `kernel/net/esp6.*` | AES-GCM, selectors, sequence numbers, replay window |
| ESP control operations | `kernel/net/espcontrol.*` | Validate/install/delete/status, generations, hard expiry |
| Pipe owner and IPv6 hooks | `kernel/net/espctl.*` | Bootstrap capability, framing, EOF cleanup, packet dispatch |
| Control wire contract | `runtime/abi/espctl.h` | Explicit fixed-width, big-endian versioned messages |
| Crypto | `runtime/ikecrypto/` | Shared AES-GCM; P-256/SHA256/HMAC linked into userspace IKE |

The kernel links only the AES-CTR/GHASH/GCM subset of the license-preserving
BearSSL sources and its small ESP wrapper. The user ELF statically links the
IKE crypto subset too. There is one source copy of the vendor library, with
its upstream archive hash and MIT license retained. Build symbol inspection
checks that `ikecore_*`, `ike_shared` and `br_ec_p256_m31` are absent from the
kernel and present in the user executable.

## Bootstrap and launch

Optional LF-terminated `plan9.ini` settings:

```ini
ip6addr=fd00:2001::2
ip6mask=/64
ikeaddr=fd00:2001::2
ikeid=plan2001
espctl=1
```

The first four settings retain their previous meanings. The network interface
keeps its link-local address as well as the optional configured IPv6 address.
`ikeaddr` must be local to that interface; absent it, the service uses its
link-local address. The default ID_FQDN is `plan2001`.

`espctl=1` explicitly enables **one private control capability per boot**.
After the trusted GE/O bootstrap holder's first OEXECUTE, the kernel installs
one native Pipe endpoint in that process's existing Pipe handle table and sets
its private `espctl` environment entry to the integer handle. The kernel owns
the other endpoint. Without this option no controller handle is provisioned.

This is a kernel bootstrap action, not an overload of ENVGET or a public
service-open operation. The small `plan9.envget(name)` binding invokes the
already existing ENVGET syscall and returns at most 128 bytes from the calling
process's environment. An environment string containing another process's
handle does not grant access to that process's handle table.

Build the native artifacts:

```sh
CC=gcc-14 tools/build-gcc.sh --kernel -j 3
CC=gcc-14 tools/build-iked.sh
# Rebuild all objects after adding the MicroPython envget binding/qstr.
CC=gcc-14 tools/build-mpy-gcc.sh --no-incremental -j 3
```

The IKE executable is stored as ordinary GE/O object bytes at
`0x4f45584543000004`, alongside the unchanged MicroPython OID `...0003`:

```sh
IKEEXEC=build/gcc/iked.elf tools/mkovol build/o.img 129 path/to/init-module.py
```

`IKEEXEC` is optional. It reuses the formatter's existing `rawexec/rawexecoid`
seed slots; neither the GE/O format nor the kernel's object loader changes.
The server is not embedded into the kernel. Normal images without `IKEEXEC`
remain valid and do not launch an IKE server automatically.

`user/iked/launch.py` supplies `start_iked(peers, oid=...)`. Include this helper
in the trusted init module or load its source through a trusted GE/O object;
it does not require a filesystem import. Pass a list of `(ID_FQDN, psk)` pairs,
with 1..64 ASCII identity bytes and exactly 32 random PSK bytes per peer. No
PSKs are supplied by default. The caller owns and must erase any original
secret buffers it retains; immutable Python objects cannot be securely erased
by this helper. Durable secret storage and key rotation are not added here.

The helper:

1. Reads the already granted control handle and creates ordinary configuration
   and startup-log Pipe pairs with PIPENEW.
2. Uses existing OSPAWN with an empty object view, explicit UDP6 permission and
   only the selected Pipe endpoints. The executable gets the handle numbers
   in argv; **no PSK is put in argv or the environment**.
3. Closes the bootstrap's copies of the delegated endpoints, so it cannot hide
   daemon death from the kernel by retaining a control-Pipe reference.
4. Sends one bounded configuration frame and closes its endpoint to signal
   completion. The frame buffer is erased afterward.
5. Reads the bounded startup acknowledgement and returns the child PID.

The helper uses the existing 32 MiB mapped-page budget; the kernel's existing
16 MiB user-stack reservation is included in that budget. This is not a new
quota system. The child never receives the bootstrap's whole-Space grant or
ambient device authority. UDP6 IKE datagrams use the OBJ five-call path
(`ObjOpenUdp6`, `SendTo/RecvFrom`, `OBJCLOSE`); the former
NET6OPEN/RECVFROM/SENDTO/CLOSE numbers are reserved. No ESP packets travel
through userspace.

## Private control protocol v1

The only way to address this service is possession of its Pipe endpoint. A
process with only UDP6 permission cannot discover/open it, and packets on
UDP/500 are never interpreted as control messages.

Each request has the 24-byte header defined in `runtime/abi/espctl.h`: magic,
version, opcode, monotonically increasing request ID, bounded body length,
zero request status and zero reserved field. The response echoes the request
ID and opcode with its response bit set. Integers are encoded explicitly;
no pointers, native structs or compiler padding cross the boundary. Both ends
handle partial reads; the client also handles partial writes. Each kernel reply
fits the native Pipe atomic I/O limit; a short/error reply ends the session.
The maximum body is 384 bytes.

| Operation | Current contract |
| --- | --- |
| HELLO | Get the configured IPv6 address and responder identity |
| RANDOM | Get 1..256 bytes from the kernel's existing seeded CSPRNG |
| INSTALL | Install one validated ESP SA pair, return a kernel generation |
| DELETE | Delete a specific slot **and generation**, retain deny policy |
| STATUS | Get monotonic time, active slots, generations and inbound activity |

The entropy operation is needed because the existing userspace ABI has no
filesystem-free CSPRNG syscall. It adds no syscall and is restricted to this
private capability. There is no fallback to an unseeded or weak user PRNG.
PSKs and IKE private keys belong to the user process; the generic configuration
Pipe transports bytes without the ESP service parsing or retaining PSKs.
Negotiated ESP keys are sent to this control endpoint. IKE_CHILD metadata retains no copies of ESP keys after
installation. Owned key-derivation and control staging buffers are erased.
Pipe syscall staging copies are erased before freeing them.

The controller is trusted to authenticate its peers. The kernel independently
checks slot bounds, fixed suite/mode, IPv6 family, configured local address,
non-overlapping pinned host selectors, SPIs, key sizes and the hard-lifetime
ceiling. A rejected INSTALL leaves the current SA unchanged. A new successful
installation receives a monotonic kernel generation; a stale DELETE cannot
remove its replacement. AUTH succeeds only after installation succeeds.

Bad framing, sequence or version ends the control session. EOF/error clears
all that session's ESP keys. Pinned selectors remain, so protected traffic
cannot silently fall back to plaintext. An independent kernel expiry worker
erases expired ESP keys even while the daemon is blocked, stopped or absent.
Packet delivery and Pipe I/O occur outside the ESP state lock.

This slice has one controller authority. **Automatic restart, control-capability
reissuance and live PSK changes are deliberately not implemented.** Once the
one-shot capability has been transferred and its session ends, a fresh boot is
required to grant another one through the current bootstrap path. Ordinary
IKE connections may still close and reconnect repeatedly while `iked` runs.

## Preserved IKE/ESP profile and bounds

- IPv6-only IKEv2 responder, UDP/500 and raw ESP protocol 50; no UDP/4500/NAT-T.
- IKE AES-128-GCM-16, PRF-HMAC-SHA256, ECDH group 19 (P-256), mutual PSK AUTH.
- ESP AES-128-GCM-16, 32-bit sequence numbers, 64-packet anti-replay window,
  no ESN or separate integrity transform.
- At most 16 provisioned peers/ESP policies and 20 IKE records, including
  pending handshakes and deletion tombstones. One active child per identity.
- Host-to-host IPv6 selectors, all protocols/ports. Tunnel mode by default;
  transport mode only when explicitly requested. Endpoints remain pinned.
- IKE datagrams at most 1232 bytes; outer ESP IPv6 packets at most 1500 bytes.
  Inner IPv6 extension headers, IPsec PMTU and fragmentation are unchanged
  unsupported cases; oversized protected traffic is dropped, never sent clear.
- Handshake timeout 30 s, IKE/child hard lifetime 3600 s, inbound inactivity
  timeout 300 s and deleted-response cache 10 s.
- Expensive initial handshakes: 4/s, burst 8; two pending per source address.
  Stateless cookies are required at four pending handshakes, with current or
  previous 30-second time bucket. Cookies do not bypass rate admission.
- Exact IKE retransmissions reuse cached ciphertext; Message IDs, endpoints,
  SPIs, lengths, proposals, tags and selectors are checked before state changes.
- INFORMATIONAL/DELETE works. CREATE_CHILD_SA/rekey gets NO_ADDITIONAL_SAS;
  renew with a new IKE handshake. A superseded IKE SA can acknowledge its DELETE
  for up to 10 seconds without deleting the replacement child.

The strongSwan profile remains `aes128gcm16-prfsha256-ecp256` and
`esp_proposals = aes128gcm16-noesn`, with host IPv6 traffic selectors. Use
fresh authentication before the hard lifetime, and keep activity within the
inactivity timeout. RFC 7815 client compatibility is validated for this agreed
profile; this is not an implementation of every RFC 7296 feature.

## Validation and explicitly deferred work

Disposable evidence is under `build/ike-userspace/`. The QEMU disk contains the
actual native `iked.elf` O object, launched by the trusted MicroPython bootstrap
using OSPAWN. StrongSwan 6.0.1 is the external client; the test host's missing
IPv6 ESP/XFRM module is replaced only on the **client** with strongSwan's raw
ESP `kernel-libipsec` backend. The responder's ESP remains in the guest kernel.
The final native build passed mutual/wrong-PSK authentication, bidirectional
ESP, two concurrent clients, replay/tag/plaintext rejection, 1,000 malformed
IKE datagrams processed by the user process, reauthentication and reconnect.
After forced daemon exit, all kernel ESP key bytes were zero and both deny
policies remained; protected traffic was blocked. The copied bootstrap handle
was also denied after its delegation. No kernel panic was observed.

Test-only random PSKs are delivered through the userspace configuration Pipe,
not injected into a kernel PSK table. See `report.json` for the final results.

The IKE core's independent crypto/protocol checks and 20,000 malformed messages
run with ASan/UBSan against the new backend. Separate control checks exercise
stale-generation protection, invalid commands, EOF cleanup, daemon-independent
expiry and 10,000 mutated control bodies. These are focused validation of this
refactor, not a new general test framework or production-audit claim.

The boundaries intentionally leave room for later work without implementing
it now: privileged capability regrant/supervision, a durable secret provider
and PSK rotation, broader policy grants, PMTU/fragmentation, operational status
and long-duration/failure testing. A protocol version and reserved fields make
future changes explicit; they do not turn on these capabilities in v1. The
existing host-only profile and wider OS isolation limitations still apply.

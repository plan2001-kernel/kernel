# Minimal MicroPython IPv6 node (current AMD64 demonstration)

`node.py` is the actual guest userspace implementation. It uses the existing
device ABI and ObjectFIFO; it implements no kernel provider, socket, filesystem
or cryptography. This entire document describes the **current implementation**
and test fixture, not the [target memory-publication IPC](../../docs/ARCHITECTURE.md).
Phase 6 removes the FIFO dependency after device transport migration.

## Interface and ownership

Execute the trusted source from an authorized bytes/GE/O Handle in a separate
ordinary MicroPython process, then construct `Node(device, address)`. No
runtime pathname/import is required. The caller chooses one already unique
static `fe80::/64` address as 16 bytes and an authorized Ethernet session
Handle. Address assignment, DAD during assignment and SLAAC are not provided.
The node does correctly answer other nodes' DAD solicitations for its address.

The child needs only device rights **46** (Vsend|Vrecv|Vmeta|Vsubscribe), not
Vcreate, Vcontrol or Vdelegate. The trusted parent discovers/opens the NIC
through a catalog with Vcreate, delegates the attenuated session with existing
`space.spawn(..., vhandles=...)`, and closes its own device alias. The child
obtains FIFO rights 40, makes its own capacity-16 subscription with rights 12,
and reads immutable frames with rights 9. No ambient numeric device/OID grants
access. Node owns and closes the supplied child device Handle, FIFO Handle
and subscription, including constructor failure and transport error.

The focused [boot fixture](../../tools/host/t14/boot-checks.py) demonstrates
the complete OID-based startup: the sanctioned formatter seeds the ordinary
`init` module, containing the unmodified node source plus a small child
program as a string. The trusted bootstrap creates a bytes capability for
that source, delegates it read-only, and OSPAWNs the existing MPY executable
OID `0x4f45584543000003`. The child reads/closes that capability and executes
the source in MPY. The source is carried in the normal GE/O module, not a host
path at runtime. Test control uses the same generic ObjectFIFO locally; it is
not a new network-control protocol or production daemon.

Minimal child use after loading the source:

```python
node = Node(device, b'\xfe\x80' + bytes(12) + b'\x01\x41')
node.ping(b'\xfe\x80' + bytes(12) + b'\x01\x42', b'hello')
try:
    while node.poll(20):
        # Poll local control in bounded intervals; call node.close() to stop.
        # The parent REPL remains in a separate process.
        pass
finally:
    node.close()
```

The example loop is intentional service code for the child, not a command to
run forever in the only REPL. Node does not start implicitly on import or boot.
`ping(peer, data, identifier=0x1414, sequence=1)` allows one pending exchange,
maximum 1232 data bytes; a concurrent ping raises ValueError. Results are in
`last_ping` as `(status, peer, id_and_sequence_bytes, data)`: reply,
nd-timeout or echo-timeout. Transport errors close the node; ping raises the
OSError, poll returns False. `snapshot()` returns bounded state/counters.
`close()` is idempotent. `handle()` is the parser, not a substitute NIC path;
direct calls in parser tests must not be credited as live traffic.

Defaults: four neighbors, expiry 3000 ms, three **total** ND probes (initial
plus two retries) 250 ms apart, echo timeout 600 ms. Constructor options are
`neighbor_limit` (1..8), `neighbor_ms` (1000..60000), `retry_ms` (100..2000),
`probe_limit` (1..3), `echo_ms` (100..5000). The caller must drive poll regularly;
timeouts are cooperative and use plan9.time_ns(). These are PoC timings, not
full NUD reachability states. No late replies complete a timed-out ping.

## Deliberate subset

- Ethernet EtherType 0x86dd; one static local link-local address, unicast
  echo peers in the selected link-local /64. ICMPv6 NH=58 only, bounded IPv6
  payload <=1240 bytes / total IPv6 size <=1280. Frame ceiling 1294 bytes;
  NIC must support at least 1294. Transmit honors Ethernet minimum padding;
  receive ignores padding beyond the declared IPv6 payload within that ceiling.
- Decode version, payload length, Next Header, hop limit, source/destination;
  check L2 destination/multicast mapping and ICMPv6 pseudoheader checksum.
  Echo replies preserve identifier, sequence and arbitrary data. Traffic-class
  and flow-label bits are not interpreted; transmitted headers use zero.
- NS/NA hop limit 255, target/source/destination/flags/option validation,
  solicited-node and all-nodes multicast mappings, exact Ethernet LL options.
  Unknown well-sized ND options are skipped; zero/overrun/duplicate/wrong LL
  options are rejected. Unspecified source is allowed only for valid DAD NS
  without SLLA. DAD NA is multicast and not solicited. Ordinary multicast NS
  requires SLLA. Only a solicited NA matching a pending resolution may learn
  its target; valid NS may learn its sender. Neighbor entries have finite
  lifetime and oldest-expiry eviction at capacity. ND remains unauthenticated;
  checking MAC consistency is not peer authentication.
- No IPv4, router/default-route service, RA, DHCPv6, SLAAC, PMTU/ICMP errors,
  extension headers, fragmentation/reassembly, proxy ND, unsolicited NA cache
  updates, full NUD, UDP, TCP, ESP or IKE. Unsupported traffic is dropped and
  counted. No production RFC-complete IPv6 stack/security claim.

`poll()` closes every received object Handle, even on parsing/send error.
RX metadata is kernel-generated; protocol logic uses only the immutable frame
bytes. Allocation remains bounded by the T13 frame cap, node state and FIFO
capacity; stats keys are fixed and only the latest request/result is retained.
The kernel native IP service stays disabled, with no fallback responder.


## Optional T15 disposable ESP example

`esp_demo.py` defines `EspNode(Node)` after the original node source is
loaded. The original T14 Node is unchanged. `EspNode(device, local, peer,
tx_sa, rx_sa)` accepts two distinct `(SPI, key16, salt4)` test SAs from the
fixture and reuses T14 ND, checksum, echo and owned Handles. Only ND is sent
as ordinary ICMPv6; echo TX is always transport-mode ESP NH=50, and plaintext
echo RX is discarded. There is no fallback to the inherited plaintext path.
ESP sequence/explicit IV increases per direction within the running test;
AAD is SPI+sequence, nonce is salt+8-byte IV, tag16, padding is 1..padlen and
the inner Next Header is58. Decryption verifies tags before ICMP handling.
Echo payload is capped at1196 bytes to stay within the IPv6 minimum MTU.

The small in-process `aesgcm` module exposes
`encrypt(key16, nonce12, aad, plaintext)` and
`decrypt(key16, nonce12, aad, ciphertext_and_tag16)`. It reuses the existing
`espcrypto.c` / BearSSL implementation. Plaintext length is at most2048 and
AAD at most64 bytes; decrypt returns only authenticated bytes or raises
ValueError. These are local MPY functions, not new syscalls or a kernel ESP
service. Only the private crypto port header/build registration is adapted
for user compilation; vendor algorithms and kernel networking are unchanged.

Run `bash tools/host/t15/run-esp-checks.sh` after canonical builds, with the
same QEMU/OVMF environment as the T14 result. It combines the unchanged node,
ESP subclass, PUBLIC TEST-ONLY config and child into an ordinary disposable
GE/O init module; a read-only code Handle and device46 are explicitly passed
to a separate MPY OID child. This is test code carrying public literals, not
a persistent key object, versioned bundle or lifecycle service. The parent
returns to normal commit/REPL.

Fixed test keys/salts are confined to `tools/host/t15/demo-config.py` and its
disposable generated test module; the ESP subclass contains no demo keys.
No replay window, anti-rollback, persistent lifecycle, rekeying, expiry,
secure distribution/storage, VPN negotiation or IKE is implemented. Duplicate
valid ESP packets may be accepted. Counters restart with the disposable VM;
no security across restarts or reused demo keys, production security or full
RFC4106 compliance is claimed. This module is only an interface example.

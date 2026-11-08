> **SUPERSEDED HISTORICAL DESIGN, NOT ACTIVE REQUIREMENTS.** The owner
> replaced this expansive design with the minimal integration test in
> [current T15 scope](../ESP-SA-BUNDLE.md). Retained as decision history only.

# ESP-SA-BUNDLE v1 — externally provisioned ESP PoC

**Normative target decision, 2026-11-08. PLANNED, NOT IMPLEMENTED.** This
specification supersedes older networking goals in the roadmap, README and
historical [MINIMAL-IKEV2](../MINIMAL-IKEV2.md) report. IKEv2 is removed from both
the active PoC and future target: no negotiation, handshake, PSK authentication,
UDP/500 service, key-exchange implementation, IKE daemon or deferred IKE
migration is required or authorized. Historical source and evidence remain;
this documentation task does not remove binaries, C code or ABI entries.

Here **ESP** means IPsec Encapsulating Security Payload, IPv6 Next Header 50.
It does not mean the UEFI EFI System Partition; boot/install procedures are
unaffected.

## Placement and minimum proof

T13 raw Ethernet / common Device ABI / existing ObjectFIFO supplies L2 bytes.
T14 must separately demonstrate userspace IPv6, addressing, ICMPv6 and Neighbor
Discovery. T14 now has a separately recorded
[emulated integration result](../T14-IPV6-RESULT.md); this does not satisfy the
ESP gates below. A future T15 implementation adds ESP processing in that userspace
path, with hardware drivers and authorized raw device access in the kernel.
Existing ESP/AES-GCM code may be reused or refactored after an actual dependency
review; linked kernel ESP code and its old private control service are not a
working userspace ESP implementation.

The initial proof is two IPv6 hosts, one pair of unidirectional SAs,
AES-128-GCM-16 and transport mode. Bidirectional protected ICMPv6 echo is
sufficient. UDP/TCP are independent userspace objectives, not prerequisites.
No protocol-specific kernel object/provider/FIFO, new syscall, pathname/Chan/fd
layer or extra protocol daemon is part of this design. T16 USB/Wi-Fi work is
independent. A general VPN/router, tunnel mode, algorithm negotiation and
automatic rekeying are outside this initial proof.

## Provisioning boundary and identity

The user supplies **one versioned, explicit, bounded binary GE/O object** to
both hosts through a pre-existing **authenticated and confidential external
channel**. That channel, peer authentication, key generation/distribution,
secure remote-object transport and key exchange infrastructure are outside this
PoC. Merely storing or sending the object is not key exchange or proof of peer
identity. No secret transport protocol or secure store is invented here.

The two hosts may store the same authorized content at different local
`(Space, OID)` values. An optional object name is not required; neither a name
nor a bare OID grants authority. Activation requires an authorized object
Handle and explicit local policy/device authority. ObjectFIFO may distribute
an authorized object locally with attenuated rights; it does not secure
interhost transfer. TEE/delegation of secret material must be explicitly
restricted. Neither generic GE/O storage nor MPY memory is claimed to be a
hardware-backed or encrypted secret store.

## Required field semantics

This specifies semantics, not a frozen ABI, byte offset table or implemented
encoding. CBOR is one possible bounded binary encoding for a later coding task;
no decoder or new GE/O format is implemented by this document. The future
parser must cap total bytes/field counts/lengths before allocation and reject
truncation, duplicate/ambiguous fields, unsupported versions and invalid values.
The initial profile permits exactly one SA pair, not an unbounded SA database.

| Field group | Required meaning |
| --- | --- |
| Format/profile | Bundle version; fixed AES-128-GCM-16 algorithm/profile; transport mode; unsupported modes/algorithms rejected |
| Session identity | Unique session/key-generation identifier; identifies a one-time activation, not a substitute for fresh keys or a nonce |
| Endpoints | Distinct A and B IPv6 addresses, local role mapping and explicit address/direction/traffic selectors; reject unexpected peers |
| A to B SA | Nonzero 32-bit SPI, 16-byte AES key, 4-byte salt; B receives this SPI, A transmits it |
| B to A SA | Separate nonzero 32-bit SPI, independent 16-byte AES key, independent 4-byte salt; A receives this SPI, B transmits it |
| Validity/limits | Not-before/expiry policy, maximum active duration and finite packet/byte limits; no counter wrap or indefinite validity |
| Protection policy | Explicit protected ICMPv6 selectors, permitted link/ND controls, deny behavior and bounded replay-window policy; no plaintext downgrade |

There are **four secret components**, totaling **40 secret bytes**:
A-to-B key (16), A-to-B salt (4), B-to-A key (16), B-to-A salt (4).
Those bytes alone are **not the complete object**. SPIs, IPv6 selectors,
profile/version, session identity, lifetime and policy are also mandatory.
The directions use separate SPIs and independent fresh key material; role or
SPI mismatch must fail validation rather than be guessed.

| Host | Local TX SA | Local RX SA |
| --- | --- | --- |
| A | A-to-B key/salt/SPI | B-to-A key/salt/SPI |
| B | B-to-A key/salt/SPI | A-to-B key/salt/SPI |

Provisioning is separate from running sequence counters, replay-window bits
and other mutable state. A live replay/sequence snapshot must not be embedded
in a reusable provisioning secret and replayed to resume an old session.

## Activation, nonce uniqueness and termination

1. Start inactive and fail closed for the protected selectors. Validate the
   entire object, role, peers, SPIs, policy, key lengths, freshness and lifetime
   before making either SA available. Do not partially activate one direction.
2. Mark the session/bundle consumed before any cryptographic use. The future
   implementation must reject a previously consumed bundle, including after
   crash/restart. It needs a verified durable consumed-generation mechanism or
   equivalent externally enforced freshness; this is a design requirement,
   not an existing GE/O anti-rollback feature. If freshness cannot be established,
   refuse activation. Rolling back a volume must not permit unsafe reuse.
3. Strictly forbid reuse of an AES-GCM **(key, salt, explicit IV/nonce)** tuple.
   RFC 4106 uses the 4-byte salt with an 8-byte explicit IV. Each transmitting
   SA must generate unique explicit IVs under its key/salt, with bounded
   counters and no reset/repetition. Reusing a session identifier, changing
   only an SPI, or restoring counters does not make reused keys safe.
4. Use finite packet/byte/time limits and stop before sequence/IV exhaustion.
   The initial profile has no ESN/wrap support; a 32-bit ESP sequence must not
   wrap. Expiry uses verified time semantics and bounded active duration;
   uncertain validity must fail closed, not extend a session.
5. Crash, controller/service exit, restart, missing keys, expiration, invalid
   configuration or link failure must stop protected traffic. Do not restart
   with the old bundle or reopen a plaintext path. A new session requires
   **fresh unique keys and salts** and a new bundle/generation, supplied through
   the same external trusted channel. Lost volatile state is not resumable.
6. Release handles, drain stale queues/state and securely discard secret
   buffers when possible. Do not log keys/salts or put them in argv, ordinary
   diagnostics or public fixtures. GC, persistent versions, snapshots and
   backups can retain copies; no guaranteed erasure or secret storage is
   claimed. Provisioning/retention remains a deployment responsibility.

No key material is requested or supplied in this documentation task.

## Packet acceptance and failure policy

Validate IPv6 lengths, supported headers/mode, peer/selector matches, ESP SPI,
sequence, padding, trailer and payload bounds. Authenticate the GCM tag before
releasing plaintext to ICMPv6 or updating committed replay state. Reject wrong
tags, wrong peers/SPIs, malformed ciphertext and replayed packets. The replay
window must be bounded, support permitted out-of-order authenticated packets,
and reject duplicates/too-old packets; an unauthenticated high sequence must
not advance it. Runtime RX replay state is local to that activation.

Protected traffic must never silently fall back to unprotected IPv6 on a
parsing/authentication error, crash, expiry, missing bundle or link recovery.
T14's necessary Neighbor Discovery remains available under a narrow explicit
control policy; that exception must not admit plaintext protected echo or
arbitrary ICMPv6/data. Address configuration and basic T14 exchange must be
independently testable before enabling the ESP policy. No obsolete kernel IP
stack may answer the userspace node's traffic in parallel.

## Security qualification

Manual AES-GCM provisioning is an **experimental PoC**, not production key
management. RFC 4106 section 2 says implementations MUST use an automated key
management system (such as IKE) to avoid unsafe manual keys/nonces. This
IKE-free manual-provisioning experiment therefore must **not** be described as
fully RFC 4106-compliant or production security. IKEv2 remains excluded from
the target; the caveat does not authorize implementing it. A later production
security design would need its own review and authorization.

## Future acceptance gates — all NOT RUN in this docs task

- Demonstrate T14 IPv6 addressing, ICMPv6 and ND separately over real T13 NIC
  traffic; keep its evidence separate from ESP and from historical kernel tests.
- Load one explicitly authorized bundle on both hosts, with different local
  OIDs, and prove the opposite TX/RX mapping with protected echo both ways.
- Observe IPv6 NH=50 on the external link, exact recovered echo bytes, valid
  tags and permitted ND; protected echo must not appear as plaintext frames.
- Reject invalid/oversized/version-mismatched bundles, wrong role/peer/SPI or
  selector, missing key, tampered tag/ciphertext, duplicate/too-old packets and
  invalid sequence/padding; test authenticated reordering and forged high
  sequences without replay-window poisoning.
- Prove finite limits and fail-closed behavior at expiry, counter exhaustion,
  crash, controller exit, missing object, link loss/recovery and restart.
  Attempt reactivation/rollback of a consumed bundle; it must fail. Then
  demonstrate a fresh bundle/session. No nonce reuse or plaintext fallback.
- Verify bounded queues/memory, capability attenuation, lifecycle and best-effort
  cleanup. Record PASS/FAIL/NOT RUN honestly; historical kernel ESP evidence
  cannot satisfy these userspace gates.

Implementation of T14/T15 requires a separate coding task. This specification
alone authorizes no source, ABI, build, test, hardware or storage-format change.

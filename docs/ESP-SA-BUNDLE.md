# T15 — minimal userspace ESP integration test

**Owner decision, 2026-11-08: this smaller assignment supersedes the earlier
ESP-SA-BUNDLE design.** Despite this retained filename, T15 requires no GE/O
key object, versioned bundle, durable consumed generation or key lifecycle.
The [older expansive proposal](history/ESP-SA-BUNDLE-2026-11-08.md) is history,
not an implementation checklist. IKEv2 remains entirely outside the target.

**Observed result:** [T15-ESP-RESULT](T15-ESP-RESULT.md) records the minimal
emulated gate PASS (13 guest / 43 peer assertions and focused regressions),
not a production security claim.

## Purpose and boundary

Validate the existing kernel syscall, authorized Device ABI and single
ObjectFIFO interfaces with a small userspace protocol example. Reuse the T14
MicroPython IPv6 node and T13 raw Ethernet path. Demonstrate ESP transport mode
(IPv6 Next Header **50**, not UDP/TCP port 50), carrying simple encrypted
ICMPv6 echo in both directions. Necessary ND may remain plaintext ICMPv6.
The intended protected echo must never silently use T14's plaintext path.

Use two fixed, distinct directional **TEST-ONLY** SAs in a disposable fixture:
SPI, AES-128-GCM key and four-byte salt per direction. Correct packet nonce
(distinct per SA within this running test), ciphertext, padding/trailer and
tag calculation/verification are necessary packet mechanics. Reuse existing
AES-GCM primitives in the user process where practical; do not introduce a
kernel ESP service, protocol provider/FIFO, syscall, pathname or Chan/fd layer.

## Integration gate

- Normal kernel/MPY boot, GE/O commit and parent REPL remain operational.
- Actual guest userspace plus external peer exchange whole ESP frames over
  real T13 RX/TX through ordinary authorized Handles and ObjectFIFO.
- Observe NH=50, decrypt exact echo bytes in both directions and verify tags;
  independently check the link contains no plaintext protected echo.
- Rerun focused T13/T14 regressions and record commands, hashes, actual tests,
  failures and NOT RUN limits in a concise result. No production test matrix.

## Explicit non-goals

No persistent key/bundle infrastructure, secure distribution/storage, durable
lifecycle/consumed-generation management, anti-rollback, replay window,
automatic rekeying, expiry/clock guarantees, VPN negotiation, enterprise VPN,
IKE, UDP/TCP dependency or exhaustive cryptographic security campaign.
Fixed demo keys and counters belong only to this disposable test. They have
no security guarantee across reboot or reuse. This is not production security
or a full RFC 4106 compliance claim; this narrow test does not implement its
automatic key-management requirement. No keys are requested from the user.
IPsec ESP is unrelated to the UEFI EFI System Partition; boot docs are intact.

# Optional CBOR and O-Envelope v1

> Test harnesses and userspace programs named below are retained in the
> separate `Plan2001-O-weekend-tests/plan2001` companion bundle. Relative
> `tools/...` and `user/...` paths refer to that bundle layout; they are not
> build or boot prerequisites of this canonical kernel tree.

CBOR is an application codec, not a GE/O storage format. An object remains
an OID plus arbitrary bytes, including zero bytes. JSON, raw images and any
other byte representation remain valid through the same GE operations.
There is no automatic format detection or conversion in GE. OID allocation,
Space identity, persistent GE records, R1 metadata and NS2–NS4 authorization
are unchanged. No syscalls, VFS, directory semantics or storage engine were
added. Hashtags remain independent R1 relation data.

## C library and RFC 8949 subset

`runtime/cbor/cbor.h` and `cbor.c` provide an independent, allocation-free C11
codec. It uses standard integer/string headers and a caller-owned `CbDoc`.
`oenv.h`/`oenv.c` implement the optional application profile. Neither library
includes GE, kernel or Python types. O-Envelope takes a two-span SHA-256
callback: MicroPython uses existing libsec through `p9sha256parts`; host tests
use OpenSSL. No second cryptographic implementation is introduced.

Supported CBOR values:

| Type | Supported range / representation |
|---|---|
| Unsigned integer | 0 through 2^64−1 |
| Negative integer | −1 through −2^64; C stores the encoded argument −1−value |
| Byte string | Arbitrary bytes, including NUL and invalid UTF-8 |
| Text string | Strictly validated UTF-8, including embedded NUL |
| Array | Definite length, recursively supported values |
| Map | Definite length, recursively supported values, scalar keys |
| Boolean | false and true |
| Null | null |

Floats, semantic tags, indefinite lengths, undefined, other simple values and
container-valued map keys return errors. This is a documented RFC 8949 subset,
not a claim to decode every CBOR item. Trailing bytes are rejected: loads
accepts exactly one document. Non-preferred integer/length widths are accepted
when valid, then normalized on encoding. Unsupported indefinite structures
are rejected rather than interpreted as empty or partial values.

Limits are 512 total nodes, depth 16 (root depth zero), and 1 MiB for encoded
input/output. Container counts are checked before multiplication/traversal;
string lengths are compared against remaining input before conversion or
pointer advancement. CbDoc occupies 20,488 bytes on the tested amd64 ABI.
Encoding/decoding has no internal heap allocator. Recursive encoding uses a
bounded map-key ordering array per stack frame; workspace figures in the
report do not include that stack or the hash provider's internals.

The decoder borrows string spans from the input. Keep input alive while using
the document; encoding output and O-Envelope scratch must not overlap borrowed
spans. The encoder accepts caller-built documents and bounds graph traversal,
including malformed links/cycles. On error, partial output is not a document.
The decoder clears its document on failure.

Duplicate scalar keys are rejected, including equal integers encoded using
different widths. Equality retains CBOR types: text and bytes differ, and
true differs from integer 1. MicroPython additionally rejects conversions
where Python dictionary equality would collapse distinct CBOR keys, such as
true and 1, rather than silently losing an entry.

## Deterministic encoding

`cb_encode` implements **core deterministic encoding, RFC 8949 §4.2.1**:
shortest integer/length heads, definite lengths and map keys sorted by the
bytewise lexicographic order of their deterministic encodings. It does not
use the alternative length-first ordering in §4.2.3. All nested maps follow
the same rule. UTF-8 is validated, not Unicode-normalized.

For example, keys 1000 and −1 encode as `19 03 e8` and `20`. Core ordering
puts 1000 first, despite its longer encoding. The host suite tests this
explicitly. Raw byte strings are never decoded as text.

## MicroPython

```python
import cbor

encoded = cbor.dumps({"name": "kuva.jpg", "data": b"\x00\xff"})
decoded = cbor.loads(encoded)
assert decoded["data"] == b"\x00\xff"
```

Supported input types are built-in int, bytes, str, list, tuple, dict, bool
and None. Tuples encode as arrays and decode as lists. Integer conversion is
checked across the full CBOR range; larger Python integers fail. Cyclic
Python structures stop at the depth limit. No external object lookup occurs.
Resource limits raise MemoryError; unsupported/malformed values and integrity
failures raise ValueError. Non-buffer loads arguments may raise TypeError.

## O-Envelope v1

An envelope is a CBOR map. `pack` adds v=1 when absent, validates known fields,
and always inserts/replaces sha256. It does not mutate the input Python dict.
Unknown fields remain and participate in the digest. Unknown scalar keys are
allowed within the supported CBOR subset.

| Field | Type | Required |
|---|---|---|
| v | Integer 1, not boolean | Yes on unpack; defaulted by pack |
| name | UTF-8 text | No |
| data | Byte string | No |
| created | UTF-8 timestamp text | No |
| media_type | UTF-8 text | No |
| metadata | Map | No |
| parts | Array | No |
| previous | 32-byte digest reference | No |
| sha256 | 32-byte digest | No on unpack; always produced by pack |

`created` is preserved text; this profile does not enforce a date syntax or
clock accuracy. `parts` may contain several byte strings, maps or other
supported values; no directory semantics or implicit object fetches exist.
`previous` is preserved as an opaque digest reference, not resolved or
verified against another object. No local OID is required or injected into
an envelope. A receiver can register the same bytes under its own OID.

The integrity digest is exactly:

```text
SHA256(
    UTF8("Plan2001-O O-Envelope v1")
    || 0x00
    || core_deterministic_cbor(envelope_without_root_sha256)
)
```

Only the root's sha256 field is excluded. A nested metadata field named
sha256 is ordinary covered content. The digest covers name, data, timestamp,
metadata, parts, previous and every unknown field. `unpack` verifies an
existing digest using a fixed 32-byte comparison; changing these fields
without resealing causes failure. Reordering a map or using a valid wider
integer encoding does not change its deterministic content digest.

`oenv.unpack(blob)` accepts an unsigned envelope if no digest is present,
because sha256 is optional in the profile. Use `oenv.unpack(blob, True)` to
require a digest. Neither mode authenticates an author, origin, creation
time or previous-chain object. An attacker who can replace content and its
digest can reseal it: signatures are outside this profile.

```python
import ge
import cbor
import oenv

blob = oenv.pack({
    "name": "kesakuva.jpg",
    "data": b"\x00\xffJPEG",
    "created": "2014-07-15T12:30:00Z",
    "media_type": "image/jpeg",
    "metadata": {"album": "2014"},
    "parts": [b"preview", {"data": b"second payload"}],
})
oid = ge.create(blob)  # existing authorized writer only
result = oenv.unpack(ge.get(oid), True)
assert result["data"] == b"\x00\xffJPEG"
```

CBOR functions cannot grant access to any OID. In the real NS4 test, a child
reads its selected envelope but is denied a known excluded OID and creation.
After grant revocation it is denied both ge.get and native handle reads;
its relation query becomes empty. Bytes previously read into process memory
remain usable, as they do for raw data.

## Commands and evidence

Host dependencies: GCC 14, OpenSSL development library, Python and the
independent reference package `cbor2==5.6.5`. QEMU/OVMF, mtools and the normal
Plan2001-O build prerequisites are needed for native execution.

```sh
python3 -m pip install cbor2==5.6.5  # preferably in a host venv
CC=gcc-14 bash tools/test-cbor-host.sh
CC=gcc-14 bash tools/build-gcc.sh --kernel -j3
CC=gcc-14 bash tools/build-mpy-gcc.sh -j3
bash tools/qemu-cbor-oenv.sh
```

The complete runner is `CC=gcc-14 bash tools/test-cbor-oenv.sh`. It builds
once, then runs the codec two-boot proof, existing NS-COMPLETE, original N13,
and original R1 REPL/persistence regressions sequentially. It finally runs
`tools/host/report-cbor-oenv.py`. The reference checker verifies the actual
stored bytes with cbor2 and hashlib and hashes the unchanged GE/security
source modules. All fixture map keys are text, for which cbor2's canonical
ordering coincides with core bytewise ordering; mixed-key ordering has its
separate published-encoding host assertion.

During this development session cbor2 was installed under
`/workspace/toolchain/cbor-python`. The executed report command used
`PYTHONPATH=/workspace/toolchain/cbor-python` and the normal toolchain
environment. This location is a session dependency, not part of GE or the
runtime library.

The host suite includes RFC 8949 Appendix A supported vectors, every proper
prefix of those vectors, malformed UTF-8, duplicate keys, unsupported values,
huge lengths, node/depth exhaustion, a cyclic C document, envelope version and
type errors, tampering and 30,000 deterministic fuzz inputs under ASan/UBSan.
A fixed independent SHA-256 vector seals the canonical bytes
`a361760164646174614200ff646e616d656474657374` to
`4fe5c6c67d82a89bdc2cd6a238f71aa4eff0d4a6c73070f433fc54e03be59d6d`.

The native proof uses one new disposable regular-file disk for two independent
four-CPU TCG boots. Boot one persists an envelope, raw bytes, JSON, an empty
object, plain CBOR and an original tagged object. Boot two reads the same OIDs,
compares the original bytes and digest, rechecks fields and reconstructs
fresh authorized grants. The R1 metadata bytes are unchanged across these
codec operations and both boots. No physical partition is accessed.

Actual native result:

```text
CBOR PERSIST OID 64 SHA256 c4d8af5f8948010c17fa05936b2465c4aeb3bb6a3bef171748466b74dc77405e
CBOR GE RAW JSON EMPTY CBOR PASS
CBOR NS4 ALLOWED DENIED REVOKED PASS
CBOR R1 UNCHANGED PASS
CBOR OENV REBOOT PERSISTENCE PASS
CBOR-OENV COMPLETE PASS
```

Complete paths, binary hashes, source hashes and execution statuses are in
`CBOR-O-ENVELOPE-VERIFICATION.json`. Earlier failed harness runs (a missing
bytes.hex convenience method in the test and a mismatched expected marker)
are retained under build/cbor; they are not counted as passing runs.

## Performance and memory

The report records optimized C host timings and process maximum RSS for
16-byte, 1-KiB, 64-KiB and 256-KiB payloads. A fresh host process is used per
size; RSS includes OpenSSL and runtime overhead. Decode borrows string spans,
so its time does not include copying the payload. Workspace sizes account
for CbDoc and the described encoded/scratch buffers, excluding stack and
crypto internals. They are not measured peak heap values.

The performance fixture is `{"name":"kuva.jpg","data":payload}`, sealed with
v and sha256. Envelope sizes are respectively 81, 1091, 65605 and 262213 bytes:
65, 67, 69 and 69 bytes of overhead respectively, as the byte-string
length heads grow.
Native QEMU tests additionally run all four sizes through the real Python
modules and record retained GC heap deltas with GC disabled. These deltas
include result objects and uncollected argument temporaries, and omit freed
transient workspace; they must not be presented as peak memory measurements.

## Source snapshot

The implementation and build inputs are contained in this source directory.
The build uses the checked-in source files and local toolchain; it does not need
remote source synchronization.

Standard: [RFC 8949](https://www.rfc-editor.org/rfc/rfc8949.html), especially
§4.2.1 and Appendix A.

## Final measured host results

Optimized GCC 14, fresh process per size; microseconds per operation.

| Raw bytes | Envelope bytes | Encode µs | Decode µs | Pack µs | Unpack µs | Peak RSS KiB |
|---:|---:|---:|---:|---:|---:|---:|
| 16 | 81 | 0.016 | 0.136 | 3.958 | 1.122 | 3584 |
| 1024 | 1091 | 0.024 | 0.125 | 4.044 | 1.763 | 3584 |
| 65536 | 65605 | 2.225 | 0.272 | 75.041 | 50.452 | 3712 |
| 262144 | 262213 | 8.226 | 0.241 | 215.958 | 193.450 | 4096 |

The final executed combined command was:

```sh
source /workspace/toolchain/env.sh
PYTHONPATH=/workspace/toolchain/cbor-python bash tools/test-cbor-oenv.sh
```

It exited 0. Complete runner output: `build/cbor/full-suite-final.log`.

- cbor: `build/cbor-oenv-vm.9XoZVK`
- ns: `build/ns-complete-vm.Gv8ufW`
- n13: `build/ns1-vm.niZ6R9`
- r1: `build/ge-relations-r1.coEq90`

Tested binary SHA-256:

- `9pc64.elf`: `b5ebea7d7225346aece43ac0f0ca83e512aa1c7802e614097b2dcc24eddabea9`
- `micropython.elf`: `ce282e52220a2b7a8fdc9bc788db6b2ac7587a6e3f93479287c6ad1c02c86b70`

NOT EXECUTED: KVM (`/dev/kvm` absent), physical hardware (disposable
images only), and a remote PR (repository URL/remote not supplied).

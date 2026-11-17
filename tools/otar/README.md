# Offline tar transfer to GE/O

`otar` imports and exports Unix files in an existing GE/O v2 partition or
bare-volume image. It opens the existing store; it never formats it or copies a
whole disk image. Stop Plan2001 (including any VM using the volume) first.
The host volume writer is a trusted offline installer, with authority from
Linux access to the device. OwnerID and Unix metadata are not authorization.

## Build and install

Requires Python 3, GCC, and the sibling canonical kernel source tree. The tar
reader uses Python's standard library; libarchive is not required. The Linux
backend compiles the kernel's current GE storage, allocator, recovery, B-tree,
object and sync code with a single-process host adapter. Worker queue operations
are synchronous; the native sync barriers still flush the host device.

From the userland checkout:

```sh
python3 tools/otar/build_backend.py --output .agent-work/otar/backend
sudo ln -s "$(pwd)/tools/otar/otar" /usr/local/bin/otar
python3 -m unittest discover -s tests/host -p test_otar.py -v
# Also exercise the real engine on a fresh sparse image (no physical device):
OTAR_BACKEND="$(pwd)/.agent-work/otar/backend/otar-ge" \
  python3 -m unittest discover -s tests/host -p 'test_otar*.py' -v
```

The default backend is `.agent-work/otar/backend/otar-ge` in the userland tree.
Use `--kernel PATH` and `--backend FILE` for other locations. Rebuild the backend
when changing kernel revisions; generated translation units are build artifacts.
The command resolves symlinks to find its sources, so install a symlink rather
than copying just the Python entry point.

## Import and export

```sh
sudo otar inspect /dev/disk/by-partuuid/REPLACE_WITH_PARTUUID
sudo otar list /dev/disk/by-partuuid/REPLACE_WITH_PARTUUID --root 0xROOT

# Keep archive names relative; GNU tar's leading ./ is accepted.
tar --format=ustar -cf program.tar -C staging .
sudo otar -xf program.tar /dev/disk/by-partuuid/REPLACE_WITH_PARTUUID \
  --root 0xROOT -C /home --dry-run
sudo otar -xf program.tar /dev/disk/by-partuuid/REPLACE_WITH_PARTUUID \
  --root 0xROOT -C /home
sudo otar -cf exported.tar /dev/disk/by-partuuid/REPLACE_WITH_PARTUUID \
  --root 0xROOT -C /home
```

The explicit root selects a durable `P2PROJ01` project, not a legacy UXFS
manifest. `inspect` lists project objects; not every listed directory is a boot
root. Select the one configured by the Unix supervisor. Partition labels alone
do not identify the F5/F6 root. Whole-disk block devices are rejected. Files must
contain the GE/O volume itself, not a GPT disk image.

Ordinary import validates the entire archive before opening the volume, then
prepares all namespace and metadata changes before allocating objects. Only
regular files and directories are supported. Absolute paths, parent traversal,
duplicate normalized names, links, sparse members and special files fail.
The operation preserves unrelated files and existing file OIDs. Reimporting the
same data and metadata is a no-op. Other aliases of an updated OID see the new
version. Existing opaque metadata is retained; recognized CBOR maps preserve
unknown fields and original descriptive name/creation time. Archive Unix
mode/uid/gid/timestamps describe origin; they do not grant native rights.

Files are stored as `RAW + CBOR metadata + P2OTAIL1 footer`. Normal tar export
writes only RAW as file contents and maps supported Unix metadata to tar fields.
Unknown metadata requires the object backup format below. Normal import cannot
write `/.programs`. Native ELF admission is a separate explicit command:

```sh
sudo otar admit VOLUME /usr/bin/program --root 0xROOT
```

Admission validates the current native AMD64 ELF layout and creates a fresh
read/execute snapshot in `/.programs`. It does not make a mutable file alias the
admitted program. The kernel checks the ELF again when executing it. ARM64
admission is not implemented by this host command.

An existing development image's bootstrap may require TinyCC's seeded identity.
For that specific case, explicitly reuse a byte-identical existing object:

```sh
sudo otar -xf toolchain.tar VOLUME --root 0xROOT \
  --reuse-object /usr/bin/tcc=0xSEEDED_COMPILER_OID
```

The selected archive member must match the existing object's RAW bytes exactly.
Its existing envelope and identity are preserved. This is an explicit installer
operation, not automatic deduplication or execution admission from a filename.

## Full objects and restoration

```sh
sudo otar backup VOLUME files.otar --root 0xROOT -C /home/project
sudo otar restore VOLUME files.otar --root 0xROOT -C /home/restored
```

A version-1 object package is a tar containing `manifest.cbor` and
`objects/OBJECT_ID.p2o`. It preserves full framed values, original identities
and OwnerID as provenance, SHA-256 digests, and graph topology. Shared objects
and cycles are represented once. Restore validates the package and creates
new identities in an empty destination directory, reconstructing aliases and
namespace records. Raw file envelopes and unknown metadata survive exactly.
Namespace records necessarily receive new OIDs, checksums and epochs; the
existing destination's BindingID high-water mark is respected. Saved rights
are attenuated by the destination rights, and target OwnerID provenance follows
the destination root. Restoring never activates `.programs` catalogs; backups
can retain their inert bytes, but their bindings are omitted during restore.
The per-volume reference index is rebuilt for the new bindings.

## Publication and limits

The destination is locked for the entire operation; block devices are opened
exclusively, and all GE I/O is bounded by the volume size. The lock cannot
exclude another computer using the same disk. Never run a host writer against
a running guest's volume.

New OID allocations are committed first and may leave unreachable empty objects
if interrupted. File values, directory changes and the reference index then
publish in one native GE/O transaction. The transaction, including framing and
structural metadata, is limited to 64 MiB. Split larger ordinary imports into
archives. This version does not provide an atomic multi-transaction import.
Flush errors and uncertain publication are reported as failures; inspect the
volume before retrying. No space reclamation or format migration is performed.

#!/usr/bin/env bash
# One bounded, disposable QEMU/OVMF boot of the current canonical amd64
# Plan2001-O kernel and native GE/O volume.
#
# The script builds or reuses only the normal canonical artifacts:
#   build/gcc/9pc64.elf        tools/build-gcc.sh --kernel
#   build/gcc/bootx64.efi      tools/build-efi-gcc.sh
#   build/gcc/micropython.elf  tools/build-mpy-gcc.sh
#   build/format-o/format-o    tools/build.sh
#   build/mpy-cross/mpy-cross  tools/build.sh
#
# It creates a fresh bare O volume with tools/gcc-ovol-ream.sh --image
# (tools/mkovol + the real GE/O reamer + the same-version mpy-cross), puts
# that volume in a real GPT partition next to a FAT EFI System Partition in
# a disposable regular-file disk, and boots the disk with QEMU/OVMF.  The
# guest opens the O partition by PARTUUID through the native SD block path.
#
# The default controller is virtio-blk-pci-non-transitional (the modern
# virtio 1.0 device handled by kernel/core/sdvirtio10.c).  --controller ahci
# is available for comparison; the default remains explicit and stable.
#
# No physical block device is ever opened for writing.  All generated files
# are regular files under build/qemu-o (or --output-dir).  There is no
# --device or install option.
#
# Exit status: 0 PASS, 1 FAIL/blocked, 2 usage.
#
# This file is also sourced by tools/qemu-o-persist.sh with
# QEMU_O_BOOT_LIB=1 so both sanctioned scripts share the same host logic.

set -euo pipefail

QOB_ROOT=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]:-$0}")/.." && pwd)
QOB_QEMU=${QEMU:-qemu-system-x86_64}
QOB_OVMF_CODE=${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}
QOB_OVMF_VARS=${OVMF_VARS:-/usr/share/OVMF/OVMF_VARS_4M.fd}
QOB_KERNEL=$QOB_ROOT/build/gcc/9pc64.elf
QOB_EFI=$QOB_ROOT/build/gcc/bootx64.efi
QOB_MPY=$QOB_ROOT/build/gcc/micropython.elf
QOB_FORMAT=$QOB_ROOT/build/format-o/format-o
QOB_MPYCROSS=$QOB_ROOT/build/mpy-cross/mpy-cross
QOB_JOBS=${QOB_JOBS:-8}

qob_set_defaults()
{
	: "${QOB_OUT:=$QOB_ROOT/build/qemu-o/boot}"
	: "${QOB_MIB:=129}"
	: "${QOB_TIMEOUT:=120}"
	: "${QOB_CONTROLLER:=virtio}"
	: "${QOB_UDP6_PEER:=}"
	: "${QOB_TCG:=0}"
	: "${QOB_ACCEL:=}"
	: "${QOB_INTERACTIVE:=0}"
	: "${QOB_BUILD:=auto}"
	: "${QOB_REPL_INPUT:=}"
	: "${QOB_EXPECT:=}"
	: "${QOB_NO_REPL:=0}"
	: "${QOB_ESP_MIB:=64}"
	: "${QOB_ESP_START_LBA:=2048}"
	: "${QOB_O_START_LBA:=133120}"
	: "${QOB_TAIL_SECTORS:=2048}"
	: "${QOB_MEM:=2048}"
	: "${QOB_CPUS:=1}"
	: "${QOB_INI_NCPU:=}"
	: "${QOB_INI_BOOTEXEC:=}"
	: "${QOB_INI_BOOTCPU:=}"
	: "${QOB_INI_BOOTGRAPH:=}"
	: "${QOB_HOLD:=15}"
	: "${QOB_KERNEL_ONLY:=0}"
}
qob_set_defaults

qob_say() { printf 'qemu-o-boot: %s\n' "$*"; }
qob_die() { printf 'qemu-o-boot: %s\n' "$*" >&2; exit 1; }

qob_usage()
{
	cat <<'EOF'
usage: tools/qemu-o-boot.sh [options]

Build or reuse the canonical kernel/loader/MicroPython O image, create one
fresh disposable GPT disk containing an ESP and an O partition, and boot it
through QEMU/OVMF.  Exit 0 only when the native O path reaches init.run(),
commits GE/O state and the expected REPL/idle milestone.

options:
  --rebuild             run the canonical GCC builds before booting
  --no-build            fail if any canonical build artifact is missing
                        (default: build only missing artifacts)
  --output-dir DIR      logs/images directory (default build/qemu-o/boot)
  --mib N               fresh O volume size in MiB (default 129)
  --controller NAME     virtio (default, verified) or ahci (diagnostic;
                        intermittent sdE0: bad disk under this QEMU/q35)
  --udp6-peer HOSTPORT:QEMUPORT
                        attach a virtio NIC to a local UDP-encapsulated
                        Ethernet peer (host service port, QEMU local port)
  --timeout SEC         boot timeout (default 120)
  --tcg                 use TCG instead of KVM (slow fallback)
  --accel MODE          kvm, tcg or kvm:tcg (try KVM, then TCG)
  --interactive         open a GTK window and hold it QOB_HOLD seconds
                        (default 15) after boot; serial is still logged
  --no-repl             boot with -N (SYSTEM IDLE) instead of -I (REPL)
  --repl-input FILE     type FILE into the native PS/2 REPL via the QEMU
                        monitor after REPL READY (requires --no-repl off)
  --expect TEXT         require TEXT in the serial log before passing
  --cpus N              QEMU vCPU count (default 1)
  --ncpu N              write *ncpu=N (default: the --cpus value)
  --obootexec OID       write obootexec=OID (decimal or 0x hex)
  --obootcpu CPU        write obootcpu=CPU (exact logical kernel CPU)
  --obootgraph OID      write obootgraph=OID: trusted persistent ObjectSpace
                        root graph on the obootvol GE/O volume
  --kernel-only         omit obootexec/obootrepl and require the kernel-only
                        PLAN2001 KERNEL READY idle state (no MicroPython)
  --jobs N              canonical build parallelism (default 8)
  -h, --help            this message

environment:
  QEMU, OVMF_CODE, OVMF_VARS, QOB_JOBS, QOB_HOLD

generated files (all regular files, never block devices):
  o.img, esp.img, disk.img, disk.layout, disk.partuuid, plan9.ini,
  serial-*.log, boot-*.log, qemu-*.log, report.txt, o-*.json
EOF
}

qob_prepare_outdir()
{
	case "$QOB_OUT" in
	/dev|/dev/*|/proc|/proc/*|/sys|/sys/*)
		qob_die "refusing output directory $QOB_OUT: not a regular-file location" ;;
	esac
	if [ -e "$QOB_OUT" ] && [ ! -d "$QOB_OUT" ]; then
		qob_die "refusing output path $QOB_OUT: not a directory"
	fi
	mkdir -p "$QOB_OUT"
	if [ -L "$QOB_OUT" ] || [ ! -d "$QOB_OUT" ] || [ -b "$QOB_OUT" ]; then
		qob_die "refusing output directory $QOB_OUT: not a plain directory"
	fi
}

qob_safe_rm()
{
	local f=$1
	if [ -L "$f" ] || { [ -e "$f" ] && [ ! -f "$f" ]; }; then
		qob_die "refusing non-regular path $f"
	fi
	rm -f -- "$f"
}

qob_need_cmd() { command -v "$1" >/dev/null 2>&1 || qob_die "missing command: $1"; }
qob_need_file() { [ -r "$1" ] || qob_die "missing file: $1"; }

qob_preflight()
{
	local f

	qob_need_cmd python3
	qob_need_cmd truncate
	qob_need_cmd dd
	qob_need_cmd sha256sum
	qob_need_cmd mformat
	qob_need_cmd mcopy
	qob_need_cmd mmd
	qob_need_cmd "$QOB_QEMU"
	qob_need_file "$QOB_OVMF_CODE"
	qob_need_file "$QOB_OVMF_VARS"

	case "$QOB_CONTROLLER" in
	virtio) ;;
	ahci) qob_say "warning: AHCI is a diagnostic path; virtio is the verified controller" ;;
	*) qob_die "--controller must be virtio or ahci" ;;
	esac
	case "$QOB_MIB" in
	''|*[!0-9]*) qob_die "--mib must be an integer" ;;
	esac
	[ "$QOB_MIB" -ge 129 ] || qob_die "--mib must be at least 129"
	case "$QOB_TIMEOUT" in
	''|*[!0-9]*) qob_die "--timeout must be an integer" ;;
	esac
	[ "$QOB_TIMEOUT" -ge 10 ] || qob_die "--timeout must be at least 10"
	case "$QOB_CPUS" in
	''|*[!0-9]*) qob_die "--cpus must be an integer" ;;
	esac
	[ "$QOB_CPUS" -ge 1 ] || qob_die "--cpus must be at least 1"
	if [ -n "$QOB_INI_NCPU" ]; then
		case "$QOB_INI_NCPU" in
		''|*[!0-9]*) qob_die "--ncpu must be an integer" ;;
		esac
		[ "$QOB_INI_NCPU" -ge 1 ] || qob_die "--ncpu must be at least 1"
	fi
	if [ -n "$QOB_INI_BOOTEXEC" ]; then
		case "$QOB_INI_BOOTEXEC" in
		*[!0-9a-fA-FxX]*) qob_die "--obootexec must be a decimal or 0x hex OID" ;;
		esac
	fi
	if [ -n "$QOB_INI_BOOTCPU" ]; then
		case "$QOB_INI_BOOTCPU" in
		''|*[!0-9]*) qob_die "--obootcpu must be a non-negative integer" ;;
		esac
	fi
	if [ -n "$QOB_INI_BOOTGRAPH" ]; then
		case "$QOB_INI_BOOTGRAPH" in
		*[!0-9a-fA-FxX]*) qob_die "--obootgraph must be a decimal or 0x hex OID" ;;
		esac
	fi

	QOB_ACCEL=${QOB_ACCEL:-$([ "$QOB_TCG" = 1 ] && echo tcg || echo kvm)}
	case "$QOB_ACCEL" in
	kvm|tcg|kvm:tcg) ;;
	*) qob_die "--accel must be kvm, tcg or kvm:tcg" ;;
	esac
	if [ "$QOB_ACCEL" = kvm ]; then
		[ -r /dev/kvm ] && [ -w /dev/kvm ] ||
			qob_die "KVM is unavailable; pass --accel kvm:tcg for fallback"
	fi
	if [ "$QOB_INTERACTIVE" = 1 ] && [ "$QOB_ACCEL" = kvm ]; then
		qob_say "interactive mode: GTK display requested (KVM)"
	fi
	if [ "$QOB_BUILD" = never ]; then
		for f in "$QOB_KERNEL" "$QOB_EFI" "$QOB_FORMAT"; do
			[ -r "$f" ] || qob_die "missing canonical artifact $f (--no-build)"
		done
		if [ "$QOB_KERNEL_ONLY" != 1 ]; then
			for f in "$QOB_MPY" "$QOB_MPYCROSS"; do
				[ -r "$f" ] || qob_die "missing canonical artifact $f (--no-build)"
			done
		else
			[ -r "$QOB_MPYCROSS" ] || qob_die "missing canonical artifact $QOB_MPYCROSS (--no-build)"
		fi
	fi
}

qob_build_artifacts()
{
	local out=$QOB_OUT f need=0

	if [ "$QOB_BUILD" = never ]; then
		for f in "$QOB_KERNEL" "$QOB_EFI" "$QOB_FORMAT"; do
			[ -r "$f" ] || qob_die "missing canonical artifact $f (--no-build)"
		done
		if [ "$QOB_KERNEL_ONLY" != 1 ]; then
			for f in "$QOB_MPY" "$QOB_MPYCROSS"; do
				[ -r "$f" ] || qob_die "missing canonical artifact $f (--no-build)"
			done
		fi
		return 0
	fi
	if [ "$QOB_BUILD" = always ]; then
		need=1
	else
		for f in "$QOB_KERNEL" "$QOB_EFI" "$QOB_FORMAT"; do
			[ -r "$f" ] || need=1
		done
		if [ "$QOB_KERNEL_ONLY" != 1 ]; then
			for f in "$QOB_MPY" "$QOB_MPYCROSS"; do
				[ -r "$f" ] || need=1
			done
		else
			[ -r "$QOB_MPYCROSS" ] || need=1
		fi
	fi
	[ "$need" = 1 ] || { qob_say "reusing canonical build artifacts"; return 0; }

	qob_say "building canonical artifacts (logs in $out)"
	"$QOB_ROOT/tools/build-gcc.sh" --kernel -j "$QOB_JOBS" \
		>"$out/build-kernel.log" 2>&1 || {
		tail -40 "$out/build-kernel.log" >&2
		qob_die "canonical kernel build failed (see $out/build-kernel.log)"
	}
	"$QOB_ROOT/tools/build-efi-gcc.sh" -j "$QOB_JOBS" \
		>"$out/build-efi.log" 2>&1 || {
		tail -40 "$out/build-efi.log" >&2
		qob_die "canonical EFI build failed (see $out/build-efi.log)"
	}
	if [ "$QOB_KERNEL_ONLY" != 1 ]; then
		"$QOB_ROOT/tools/build-mpy-gcc.sh" -j "$QOB_JOBS" \
			>"$out/build-mpy.log" 2>&1 || {
			tail -40 "$out/build-mpy.log" >&2
			qob_die "canonical MicroPython build failed (see $out/build-mpy.log)"
		}
	fi
	"$QOB_ROOT/tools/build.sh" \
		>"$out/build-host.log" 2>&1 || {
		tail -40 "$out/build-host.log" >&2
		qob_die "canonical host-tool build failed (see $out/build-host.log)"
	}
	for f in "$QOB_KERNEL" "$QOB_EFI" "$QOB_FORMAT"; do
		[ -r "$f" ] || qob_die "build did not produce $f"
	done
	if [ "$QOB_KERNEL_ONLY" != 1 ]; then
		for f in "$QOB_MPY" "$QOB_MPYCROSS"; do
			[ -r "$f" ] || qob_die "build did not produce $f"
		done
	else
		[ -r "$QOB_MPYCROSS" ] || qob_die "build did not produce $QOB_MPYCROSS"
	fi
}

qob_new_o_image()
{
	local img=$1 logdir=$2
	mkdir -p "$logdir"
	qob_safe_rm "$img"
	qob_say "reaming fresh disposable O image $img (${QOB_MIB} MiB)"
	if [ "$QOB_KERNEL_ONLY" = 1 ]; then
		# Seed the init Space and demo code but deliberately no MicroPython
		# executable: the kernel-only boot must not execute an external app.
		NOMPEXEC=1 MPYCROSS="$QOB_MPYCROSS" FORMATO="$QOB_FORMAT" \
			"$QOB_ROOT/tools/mkovol" "$img" "$QOB_MIB" \
			"$QOB_ROOT/tools/host/odemo.py" \
			>"$logdir/ream-wrap.log" 2>&1 || {
			tail -50 "$logdir/ream-wrap.log" >&2
			qob_die "O image ream/seed failed (see $logdir/ream-wrap.log)"
		}
	else
		GCC_OREAM_OUT="$logdir" "$QOB_ROOT/tools/gcc-ovol-ream.sh" \
			--image "$img" "$QOB_MIB" >"$logdir/ream-wrap.log" 2>&1 || {
			tail -50 "$logdir/ream-wrap.log" >&2
			qob_die "O image ream/seed failed (see $logdir/ream-wrap.log)"
		}
	fi
	[ -f "$img" ] || qob_die "O image was not created: $img"
	[ "$(stat -c %s "$img")" = "$((QOB_MIB * 1048576))" ] ||
		qob_die "O image size is not ${QOB_MIB} MiB"
}

qob_write_ini()
{
	local path=$1 puuid=$2 bootfile=$3 repl=$4

	{
		printf 'bootfile=%s\n' "$bootfile"
		printf 'oboot=server\n'
		printf 'obootvol=PARTUUID=%s\n' "$puuid"
		[ "$QOB_KERNEL_ONLY" = 1 ] || [ -z "$QOB_INI_BOOTEXEC" ] || printf 'obootexec=%s\n' "$QOB_INI_BOOTEXEC"
		[ -n "$QOB_INI_BOOTCPU" ] && printf 'obootcpu=%s\n' "$QOB_INI_BOOTCPU"
		[ -z "$QOB_INI_BOOTGRAPH" ] || printf 'obootgraph=%s\n' "$QOB_INI_BOOTGRAPH"
		[ "$QOB_KERNEL_ONLY" = 1 ] || [ "$repl" != 1 ] || printf 'obootrepl=1\n'
		printf 'sdflushlog=1\n'
		printf 'console=0\n'
		printf '*ncpu=%s\n' "${QOB_INI_NCPU:-$QOB_CPUS}"
	} >"$path"
	# The loader rejects a nonempty final line without LF; printf always
	# terminates each line, and this check keeps the contract explicit.
	[ "$(tail -c 1 "$path" | od -An -tx1 | tr -d ' \n')" = "0a" ] ||
		qob_die "generated plan9.ini does not end in LF"
}

qob_make_esp()
{
	local ini=$1 esp=$2

	qob_safe_rm "$esp"
	truncate -s "$((QOB_ESP_MIB * 1024 * 1024))" "$esp" ||
		qob_die "cannot allocate ESP image"
	mformat -i "$esp" -F -v PLAN2001 :: >/dev/null ||
		qob_die "mformat failed on $esp"
	mmd -i "$esp" ::/EFI ::/EFI/BOOT >/dev/null ||
		qob_die "mmd failed on $esp"
	mcopy -i "$esp" "$QOB_EFI" ::/EFI/BOOT/BOOTX64.EFI ||
		qob_die "cannot copy the UEFI loader into the ESP"
	mcopy -i "$esp" "$QOB_KERNEL" ::/9pc64.elf ||
		qob_die "cannot copy the kernel into the ESP"
	mcopy -i "$esp" "$ini" ::/plan9.ini ||
		qob_die "cannot copy plan9.ini into the ESP"
}

# Create a real GPT disk with a protective MBR, primary and backup headers,
# one standard ESP and one O partition whose unique GUID is $4.  This uses no
# sfdisk/parted/gdisk dependency and writes only the regular file $2.
qob_make_disk()
{
	local oimg=$1 disk=$2 esp=$3 puuid=$4 layout=$5
	local osz osectors total

	osz=$(stat -c %s "$oimg")
	[ $((osz % 512)) = 0 ] || qob_die "O image size is not sector aligned"
	[ $((osz % 1048576)) = 0 ] || qob_die "O image size is not MiB aligned"
	[ $((QOB_O_START_LBA % 2048)) = 0 ] ||
		qob_die "internal O start LBA is not MiB aligned"
	osectors=$((osz / 512))
	total=$((QOB_O_START_LBA + osectors + QOB_TAIL_SECTORS))

	qob_safe_rm "$disk"
	truncate -s "$((total * 512))" "$disk" ||
		qob_die "cannot allocate disposable disk image"

	python3 - "$disk" "$QOB_O_START_LBA" "$osectors" "$puuid" <<'PY' || qob_die "GPT construction/verification failed"
import struct
import sys
import uuid
import zlib

disk, o_start, o_sectors, puuid = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
SECTOR = 512
ESP_START = 2048
ESP_SECTORS = 131072
TAIL = 2048
TOTAL = o_start + o_sectors + TAIL

ESP_TYPE = uuid.UUID("C12A7328-F81F-11D2-BA4B-00A0C93EC93B")
O_TYPE = uuid.UUID("C91818F9-8025-47AF-89D2-F030D7000C2C")
O_UNIQUE = uuid.UUID(puuid)
ESP_UNIQUE = uuid.uuid4()
DISK_GUID = uuid.uuid4()


def entry(ptype, unique, first, last, name):
    e = bytearray(128)
    e[0:16] = ptype.bytes_le
    e[16:32] = unique.bytes_le
    struct.pack_into("<Q", e, 32, first)
    struct.pack_into("<Q", e, 40, last)
    struct.pack_into("<Q", e, 48, 0)
    raw = name.encode("utf-16le")[:72]
    e[56:56 + len(raw)] = raw
    return bytes(e)


def header(current, backup, entries_lba, first_usable, last_usable, disk_guid, entries_crc):
    h = bytearray(512)
    h[0:8] = b"EFI PART"
    struct.pack_into("<I", h, 8, 0x00010000)
    struct.pack_into("<I", h, 12, 92)
    struct.pack_into("<Q", h, 24, current)
    struct.pack_into("<Q", h, 32, backup)
    struct.pack_into("<Q", h, 40, first_usable)
    struct.pack_into("<Q", h, 48, last_usable)
    h[56:72] = disk_guid.bytes_le
    struct.pack_into("<Q", h, 72, entries_lba)
    struct.pack_into("<I", h, 80, 128)
    struct.pack_into("<I", h, 84, 128)
    struct.pack_into("<I", h, 88, entries_crc)
    struct.pack_into("<I", h, 16, zlib.crc32(h[:92]) & 0xffffffff)
    return bytes(h)


entries = bytearray(128 * 128)
entries[0:128] = entry(ESP_TYPE, ESP_UNIQUE, ESP_START,
                       ESP_START + ESP_SECTORS - 1, "ESP")
entries[128:256] = entry(O_TYPE, O_UNIQUE, o_start,
                         o_start + o_sectors - 1, "plan2001")
entries = bytes(entries)
entries_crc = zlib.crc32(entries) & 0xffffffff
first_usable = 34
last_usable = TOTAL - 34
assert o_start >= first_usable
assert o_start + o_sectors - 1 < last_usable
assert ESP_START + ESP_SECTORS - 1 < o_start

primary = header(1, TOTAL - 1, 2, first_usable, last_usable, DISK_GUID, entries_crc)
backup = header(TOTAL - 1, 1, TOTAL - 33, first_usable, last_usable, DISK_GUID, entries_crc)

mbr = bytearray(512)
pe = bytearray(16)
pe[0] = 0
pe[1:4] = b"\x00\x02\x00"
pe[4] = 0xEE
pe[5:8] = b"\xff\xff\xff"
struct.pack_into("<I", pe, 8, 1)
struct.pack_into("<I", pe, 12, min(TOTAL - 1, 0xFFFFFFFF))
mbr[446:462] = pe
mbr[510:512] = b"\x55\xaa"

with open(disk, "r+b") as f:
    f.seek(0)
    f.write(mbr)
    f.seek(SECTOR)
    f.write(primary)
    f.seek(2 * SECTOR)
    f.write(entries)
    f.seek((TOTAL - 33) * SECTOR)
    f.write(entries)
    f.seek((TOTAL - 1) * SECTOR)
    f.write(backup)

# Read the table back and require the exact PARTUUID and bounds the loader
# and kernel will see.
with open(disk, "rb") as f:
    f.seek(SECTOR)
    h = f.read(92)
    assert h[0:8] == b"EFI PART", "missing primary GPT header"
    assert struct.unpack_from("<I", h, 16)[0] == zlib.crc32(h[:16] + b"\0\0\0\0" + h[20:]) & 0xffffffff
    elba = struct.unpack_from("<Q", h, 72)[0]
    f.seek(elba * SECTOR)
    raw = f.read(128 * 128)
    assert zlib.crc32(raw) & 0xffffffff == struct.unpack_from("<I", h, 88)[0]
    o = raw[128:256]
    assert uuid.UUID(bytes_le=o[16:32]) == O_UNIQUE
    assert struct.unpack_from("<Q", o, 32)[0] == o_start
    assert struct.unpack_from("<Q", o, 40)[0] == o_start + o_sectors - 1
    assert uuid.UUID(bytes_le=o[0:16]) == O_TYPE
    f.seek((TOTAL - 1) * SECTOR)
    assert f.read(8) == b"EFI PART", "missing backup GPT header"

print("gpt: disk_sectors=%d esp=%d+%d o=%d+%d partuuid=%s" %
      (TOTAL, ESP_START, ESP_SECTORS, o_start, o_sectors, puuid))
PY

	dd if="$esp" of="$disk" bs=1M seek=1 conv=notrunc status=none ||
		qob_die "cannot copy the ESP into the disposable disk"
	dd if="$oimg" of="$disk" bs=1M seek="$((QOB_O_START_LBA / 2048))" \
		conv=notrunc status=none ||
		qob_die "cannot copy the O volume into the disposable disk"

	{
		printf 'o_start_lba=%s\n' "$QOB_O_START_LBA"
		printf 'o_sectors=%s\n' "$osectors"
		printf 'esp_start_lba=%s\n' "$QOB_ESP_START_LBA"
		printf 'esp_sectors=%s\n' "$((QOB_ESP_MIB * 2048))"
		printf 'total_sectors=%s\n' "$total"
		printf 'partuuid=%s\n' "$puuid"
	} >"$layout"
}

qob_read_layout()
{
	local layout=$1 key=$2
	sed -n "s/^${key}=//p" "$layout"
}

qob_extract_o()
{
	local disk=$1 dest=$2 layout=$3 start count
	start=$(qob_read_layout "$layout" o_start_lba)
	count=$(qob_read_layout "$layout" o_sectors)
	[ -n "$start" ] && [ -n "$count" ] || qob_die "bad disk layout $layout"
	qob_safe_rm "$dest"
	dd if="$disk" of="$dest" bs=512 skip="$start" count="$count" status=none ||
		qob_die "cannot extract the O partition from $disk"
}

# Replace /plan9.ini inside an existing disposable GPT disk without touching
# the O partition.  Used by two-boot tests that change trusted boot selectors
# between boots of the exact same image.
qob_update_disk_ini()
{
	local disk=$1 layout=$2 ini=$3
	local start count esp
	start=$(qob_read_layout "$layout" esp_start_lba)
	count=$(qob_read_layout "$layout" esp_sectors)
	[ -n "$start" ] && [ -n "$count" ] || qob_die "bad disk layout $layout"
	esp="$QOB_OUT/esp-update.img"
	qob_safe_rm "$esp"
	dd if="$disk" of="$esp" bs=512 skip="$start" count="$count" status=none ||
		qob_die "cannot extract the ESP from $disk"
	mcopy -o -i "$esp" "$ini" ::/plan9.ini ||
		qob_die "cannot update plan9.ini in the ESP"
	dd if="$esp" of="$disk" bs=512 seek="$start" conv=notrunc status=none ||
		qob_die "cannot write the updated ESP into $disk"
	qob_safe_rm "$esp"
}

qob_oinspect()
{
	local img=$1 json=$2 err=$3
	python3 "$QOB_ROOT/tools/oinspect.py" "$img" >"$json" 2>"$err" || {
		cat "$err" >&2
		qob_die "oinspect failed on $img"
	}
}

qob_has_fixed() { grep -aqF -- "$1" "$2"; }
qob_has_ere() { grep -aqE -- "$1" "$2"; }
qob_count_fixed() { { grep -aoF -- "$1" "$2" 2>/dev/null || true; } | wc -l; }

qob_fail_marker()
{
	qob_has_ere 'panic:|suicide|PLAN2001-O: BOOT FAILED|bad disk|error syncing:|cache flush:|i/o error|no disk has that partition' "$1"
}

qob_sanitize()
{
	tr -d '\r' <"$1" 2>/dev/null | sed 's/\x1b\[[0-9;=?]*[a-zA-Z]//g' || true
}

qob_stop_qemu()
{
	local pid=${1:-}
	[ -n "$pid" ] || return 0
	if kill -0 "$pid" 2>/dev/null; then
		kill "$pid" 2>/dev/null || true
		for _ in $(seq 1 50); do
			kill -0 "$pid" 2>/dev/null || break
			sleep 0.1
		done
		kill -9 "$pid" 2>/dev/null || true
	fi
	wait "$pid" 2>/dev/null || true
}

QOB_CURRENT_PID=
QOB_MONDIR=
QOB_ACCEL_SELECTED=

qob_cleanup()
{
	if [ -n "${QOB_CURRENT_PID:-}" ]; then
		qob_stop_qemu "$QOB_CURRENT_PID"
		QOB_CURRENT_PID=
	fi
	if [ -n "${QOB_MONDIR:-}" ]; then
		rm -rf "$QOB_MONDIR"
		QOB_MONDIR=
	fi
}

# Run one HMP monitor command and print its reply without ANSI escapes.
# Used only for validation evidence, e.g. the actually selected accelerator.
qob_monitor_query()
{
	local mon=$1 cmd=$2
	python3 - "$mon" "$cmd" <<'PY'
import re
import socket
import sys
import time

mon, cmd = sys.argv[1], sys.argv[2]
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
deadline = time.time() + 5
while True:
    try:
        s.connect(mon)
        break
    except OSError:
        if time.time() >= deadline:
            raise SystemExit("qemu monitor socket did not appear")
        time.sleep(0.1)

def read_to_prompt():
    buf = b""
    end = time.time() + 5
    while time.time() < end:
        s.settimeout(0.2)
        try:
            part = s.recv(4096)
        except socket.timeout:
            continue
        if not part:
            break
        buf += part
        if b"(qemu) " in buf:
            break
    return buf

read_to_prompt()                 # discard the monitor banner / first prompt
s.sendall((cmd + "\n").encode())
buf = read_to_prompt()
text = re.sub(rb"\x1b\[[0-9;]*[A-Za-z]", b"", buf).decode("utf-8", "replace")
print(text.replace("\r", " ").strip())
PY
}

# Type an ASCII file into the guest's native PS/2 REPL through the QEMU HMP
# monitor.  The mapping follows the canonical p9console.c Finnish PC layout
# for the printable characters it supports.  Unsupported characters are a
# hard error rather than silently sending the wrong key.
qob_inject_input()
{
	local mon=$1 input=$2 serial=${3:-}
	python3 - "$mon" "$input" "$serial" "${QOB_REPL_SYNC_LINES:-0}" <<'PY'
import socket
import sys
import time

mon, path, serial, sync = sys.argv[1:5]
sync = sync == "1"
if sync and not serial:
    raise SystemExit("REPL line synchronization requires a serial log")

# QEMU key names for the physical scan codes used by p9console.c.  The
# guest layout is Finnish, so e.g. '+' is the unshifted key QEMU calls
# "minus" and '(' is shift-8.
keys = {}
for ch in "abcdefghijklmnopqrstuvwxyz":
    keys[ch] = ch
for ch in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
    keys[ch] = "shift-" + ch.lower()
for ch in "0123456789":
    keys[ch] = ch
keys.update({
    " ": "spc",
    "\n": "ret",
    "\r": "ret",
    "+": "minus",
    "-": "slash",
    "_": "shift-slash",
    "=": "shift-0",
    "(": "shift-8",
    ")": "shift-9",
    ":": "shift-dot",
    ".": "dot",
    ",": "comma",
    "?": "shift-minus",
    "!": "shift-1",
    '"': "shift-2",
    "#": "shift-3",
    "%": "shift-5",
    "&": "shift-6",
    "/": "shift-7",
    "*": "shift-backslash",
    "'": "backslash",
})

data = open(path, "rb").read().decode("ascii")

s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
deadline = time.time() + 10
while True:
    try:
        s.connect(mon)
        break
    except OSError:
        if time.time() >= deadline:
            raise SystemExit("qemu monitor socket did not appear")
        time.sleep(0.1)


def command(cmd):
    s.sendall((cmd + "\n").encode())
    buf = b""
    end = time.time() + 5
    while time.time() < end:
        s.settimeout(0.2)
        try:
            part = s.recv(4096)
        except socket.timeout:
            continue
        if not part:
            break
        buf += part
        if b"(qemu) " in buf:
            return
    raise SystemExit("QEMU monitor did not acknowledge: " + cmd)


# In graph acceptance the guest commits GE/O state after every REPL
# statement.  Do not type the following statement while that commit runs:
# the PS/2 keyboard queue can drop scancodes even though the QEMU monitor
# acknowledges sendkey.  Check the actual guest echo before continuing.
def serial_bytes():
    with open(serial, "rb") as log:
        return log.read().replace(b"\r", b"")

def last_prompt():
    position = serial_bytes().rfind(b">>> ")
    if position < 0:
        raise SystemExit("native REPL prompt missing before input")
    return position

def await_line(expected, previous_prompt):
    expected_echo = b">>> " + expected.encode("ascii")
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        log = serial_bytes()
        # The next prompt proves that the previous statement and commit
        # finished.  Compare the typed line before advancing, so dropped
        # keystrokes fail at their source instead of corrupting later tests.
        next_prompt = log.find(b">>> ", previous_prompt + 4)
        if next_prompt >= 0:
            echoed_line = log[previous_prompt:].split(b"\n", 1)[0]
            if echoed_line != expected_echo:
                raise SystemExit("guest REPL input mismatch: expected %r, got %r" %
                                 (expected_echo, echoed_line))
            return next_prompt
        time.sleep(0.05)
    raise SystemExit("native REPL did not finish statement: %r" % expected)

prompt = last_prompt() if sync else -1
line = []
for ch in data:
    key = keys.get(ch)
    if key is None:
        raise SystemExit("unsupported REPL input character: %r" % ch)
    command("sendkey %s 20" % key)
    time.sleep(0.08)
    if sync:
        if ch in "\r\n":
            prompt = await_line("".join(line), prompt)
            line.clear()
        else:
            line.append(ch)
if data and not data.endswith("\n"):
    command("sendkey ret 20")
    if sync:
        await_line("".join(line), prompt)
s.close()
PY
}

# qob_boot_once DISK TAG SUCCESS_ERE MODE INPUT EXPECT
#   MODE is repl or norepl.  Sets QOB_BOOT_RESULT and QOB_BOOT_REASON.
qob_boot_once()
{
	local disk=$1 tag=$2 success=$3 mode=$4 input=$5 expect=$6
	local serial="$QOB_OUT/serial-$tag.log"
	local qlog="$QOB_OUT/qemu-$tag.log"
	local bootlog="$QOB_OUT/boot-$tag.log"
	local vars="$QOB_OUT/vars-$tag.fd"
	local mondir mon pid status start_ts deadline success_seen=0 injected=0 crashed=0
	local reason= result=fail

	qob_safe_rm "$serial"
	qob_safe_rm "$qlog"
	qob_safe_rm "$bootlog"
	qob_safe_rm "$vars"
	cp "$QOB_OVMF_VARS" "$vars" || qob_die "cannot copy OVMF vars"
	: >"$serial"
	: >"$qlog"
	mondir=$(mktemp -d "${TMPDIR:-/tmp}/qemu-o-mon.XXXXXX")
	QOB_MONDIR=$mondir
	mon=$mondir/mon

	local accel=(-machine "q35,accel=$QOB_ACCEL") cpu=(-cpu max)
	if [ "$QOB_ACCEL" = kvm ]; then
		cpu=(-cpu host)
	fi
	local display=(-display none)
	local extra=() qemu_net=(-nic none)
	if [ "$QOB_INTERACTIVE" = 1 ]; then
		display=(-display gtk)
		extra=(-device VGA)
	fi
	local ctrl=()
	case "$QOB_CONTROLLER" in
	virtio)
		ctrl=(-drive "file=$disk,format=raw,if=none,id=d0" \
			-device "virtio-blk-pci-non-transitional,drive=d0,bootindex=0") ;;
	ahci)
		ctrl=(-drive "file=$disk,format=raw,if=none,id=d0" \
			-device "ide-hd,drive=d0,bus=ide.0,bootindex=0") ;;
	esac
	if [ -n "$QOB_UDP6_PEER" ]; then
		local peerport=${QOB_UDP6_PEER%%:*}
		local qemuport=${QOB_UDP6_PEER#*:}
		qemu_net=(-netdev "socket,id=net0,udp=127.0.0.1:${peerport},localaddr=127.0.0.1:${qemuport}" \
			-device "virtio-net-pci-non-transitional,netdev=net0,mac=52:54:00:12:34:56")
	fi

	qob_say "booting $tag with $QOB_CONTROLLER (accel=$QOB_ACCEL)"
	"$QOB_QEMU" -name "p2001-qemu-o-$tag" -nodefaults \
		-m "$QOB_MEM" -smp "$QOB_CPUS" "${accel[@]}" "${cpu[@]}" \
		-drive "if=pflash,format=raw,readonly=on,file=$QOB_OVMF_CODE" \
		-drive "if=pflash,format=raw,file=$vars" \
		"${ctrl[@]}" "${extra[@]}" "${qemu_net[@]}" "${display[@]}" \
		-serial "file:$serial" \
		-monitor "unix:$mon,server=on,wait=off" -no-reboot \
		>"$qlog" 2>&1 &
	pid=$!
	QOB_CURRENT_PID=$pid
	start_ts=$(date +%s)
	deadline=$((start_ts + QOB_TIMEOUT))

	while :; do
		if qob_fail_marker "$serial"; then
			reason="fatal guest marker (panic/bad disk/flush/sync error)"
			break
		fi
		if [ "$success_seen" = 0 ] && qob_has_ere "$success" "$serial"; then
			success_seen=1
			if [ "$mode" = crash ]; then
				qob_say "crash checkpoint reached; SIGKILL without guest shutdown"
				kill -9 "$pid" 2>/dev/null || true
				wait "$pid" 2>/dev/null || true
				QOB_CURRENT_PID=
				crashed=1
				result=pass
				break
			fi
			if [ -z "${QOB_ACCEL_SELECTED:-}" ]; then
				QOB_ACCEL_SELECTED=$(qob_monitor_query "$mon" "info kvm" 2>/dev/null |
					tr '\n' ' ')
				QOB_ACCEL_SELECTED=$(printf '%s' "$QOB_ACCEL_SELECTED" |
					grep -o 'kvm support: [a-z]*' | head -1 || true)
				[ -n "$QOB_ACCEL_SELECTED" ] || QOB_ACCEL_SELECTED=unknown
			fi
			if [ "$mode" = repl ]; then
				# The native REPL prints its prompt without a newline.
				local pdeadline=$(( $(date +%s) + 10 ))
				while ! qob_has_fixed '>>>' "$serial"; do
					if [ "$(date +%s)" -ge "$pdeadline" ]; then
						reason="REPL prompt not observed after REPL READY"
						break 2
					fi
					sleep 0.2
				done
			fi
			if [ -n "$input" ]; then
				local prompts pdeadline
				if [ "$mode" != repl ]; then
					reason="--repl-input requires the REPL boot (-N off)"
					break
				fi
				qob_say "typing REPL input from $input"
				prompts=$(qob_count_fixed '>>>' "$serial")
				if ! qob_inject_input "$mon" "$input" "$serial"; then
					reason="REPL input injection failed"
					break
				fi
				# In crash injection the guest may deliberately block on an
				# unpublished writer. Kill at the observed guest marker without
				# requiring the post-statement persistence prompt.
				if [ "${QOB_CRASH_AFTER_EXPECT:-0}" != 1 ]; then
					# Normal tests require a completed REPL statement/commit.
					pdeadline=$(( $(date +%s) + ${QOB_REPL_COMPLETION_TIMEOUT:-15} ))
					while [ "$(qob_count_fixed '>>>' "$serial")" -le "$prompts" ] ||
                    ! tail -c 16 "$serial" | tr -d '\r' | tail -n 1 | grep -qxF '>>> '; do
					if qob_fail_marker "$serial"; then
						reason="fatal marker while processing REPL input"
						break 2
					fi
					if ! kill -0 "$pid" 2>/dev/null; then
						wait "$pid" 2>/dev/null || status=$?
						QOB_CURRENT_PID=
						reason="QEMU exited while processing REPL input"
						break 2
					fi
					if [ "$(date +%s)" -ge "$pdeadline" ]; then
						reason="REPL did not return to a prompt after input"
						break 2
					fi
						sleep 0.2
					done
				fi
				injected=1
			fi
		fi

		if [ "$success_seen" = 1 ]; then
			if [ -n "$expect" ]; then
				if qob_has_ere "$expect" "$serial"; then
					if [ "${QOB_CRASH_AFTER_EXPECT:-0}" = 1 ]; then
						qob_say "injected power-loss: SIGKILL after marker $expect"
						kill -9 "$pid" 2>/dev/null || true
						wait "$pid" 2>/dev/null || true
						QOB_CURRENT_PID=
						crashed=1
					fi
					result=pass
					break
				fi
			elif [ "$injected" = 1 ]; then
				sleep 2
				result=pass
				break
			else
				result=pass
				break
			fi
		fi

		if ! kill -0 "$pid" 2>/dev/null; then
			wait "$pid" 2>/dev/null || status=$?
			QOB_CURRENT_PID=
			reason="QEMU exited (status ${status:-0}) before the expected boot milestone"
			break
		fi
		if [ "$(date +%s)" -ge "$deadline" ]; then
			if [ "$success_seen" = 1 ] && [ -n "$expect" ]; then
				reason="expected text not observed before timeout: $expect"
			else
				reason="boot timeout after ${QOB_TIMEOUT}s"
			fi
			break
		fi
		sleep 0.2
	done

	if [ "$result" = pass ] && [ "$crashed" = 0 ] && ! kill -0 "$pid" 2>/dev/null; then
		wait "$pid" 2>/dev/null || status=$?
		QOB_CURRENT_PID=
		result=fail
		reason="QEMU exited (status ${status:-0}) after the success marker"
	fi
	if [ "$QOB_INTERACTIVE" = 1 ] && [ "$result" = pass ]; then
		qob_say "interactive QEMU is running as pid $pid for ${QOB_HOLD}s"
		sleep "$QOB_HOLD" || true
	fi
	if [ "$result" = pass ] && [ "$crashed" = 0 ] && ! kill -0 "$pid" 2>/dev/null; then
		wait "$pid" 2>/dev/null || status=$?
		QOB_CURRENT_PID=
		result=fail
		reason="QEMU exited (status ${status:-0}) during the interactive hold"
	fi
	qob_stop_qemu "$pid"
	QOB_CURRENT_PID=
	qob_sanitize "$serial" >"$bootlog" || true
	rm -rf "$mondir"
	QOB_MONDIR=

	if [ "$result" = pass ] && qob_fail_marker "$bootlog"; then
		result=fail
		reason="fatal guest marker in the completed serial log"
	fi
	QOB_BOOT_RESULT=$result
	QOB_BOOT_REASON=${reason:-OK}
	[ "$result" = pass ]
}

qob_stage()
{
	local log=$1 marker=$2
	if qob_has_fixed "$marker" "$log"; then echo PASS; else echo MISSING; fi
}

qob_show_tail()
{
	local log=$1
	printf '%s\n' "--- serial tail ($log) ---"
	tail -60 "$log" 2>/dev/null || true
}

qob_main()
{
	local puuid repl=1 success

	while [ $# -gt 0 ]; do
		case "$1" in
		--rebuild) QOB_BUILD=always ;;
		--no-build) QOB_BUILD=never ;;
		--output-dir) [ $# -ge 2 ] || qob_die "--output-dir needs a value"; QOB_OUT=$2; shift ;;
		--mib) [ $# -ge 2 ] || qob_die "--mib needs a value"; QOB_MIB=$2; shift ;;
		--controller) [ $# -ge 2 ] || qob_die "--controller needs a value"; QOB_CONTROLLER=$2; shift ;;
		--udp6-peer)
			[ $# -ge 2 ] || qob_die "--udp6-peer needs HOSTPORT:QEMUPORT"
			QOB_UDP6_PEER=$2
			shift ;;
		--timeout) [ $# -ge 2 ] || qob_die "--timeout needs a value"; QOB_TIMEOUT=$2; shift ;;
		--tcg) QOB_TCG=1; QOB_ACCEL=tcg ;;
		--accel) [ $# -ge 2 ] || qob_die "--accel needs a value"; QOB_ACCEL=$2; shift ;;
		--interactive) QOB_INTERACTIVE=1 ;;
		--no-repl) QOB_NO_REPL=1; repl=0 ;;
		--repl-input) [ $# -ge 2 ] || qob_die "--repl-input needs a file"; QOB_REPL_INPUT=$2; shift ;;
		--expect) [ $# -ge 2 ] || qob_die "--expect needs text"; QOB_EXPECT=$2; shift ;;
		--cpus) [ $# -ge 2 ] || qob_die "--cpus needs a value"; QOB_CPUS=$2; shift ;;
		--ncpu) [ $# -ge 2 ] || qob_die "--ncpu needs a value"; QOB_INI_NCPU=$2; shift ;;
		--obootexec) [ $# -ge 2 ] || qob_die "--obootexec needs an OID"; QOB_INI_BOOTEXEC=$2; shift ;;
		--obootcpu) [ $# -ge 2 ] || qob_die "--obootcpu needs a CPU"; QOB_INI_BOOTCPU=$2; shift ;;
		--obootgraph) [ $# -ge 2 ] || qob_die "--obootgraph needs an OID"; QOB_INI_BOOTGRAPH=$2; shift ;;
		--kernel-only) QOB_KERNEL_ONLY=1 ;;
		--jobs) [ $# -ge 2 ] || qob_die "--jobs needs a value"; QOB_JOBS=$2; shift ;;
		-h|--help) qob_usage; exit 0 ;;
		*) qob_usage >&2; exit 2 ;;
		esac
		shift
	done
	if [ -n "$QOB_UDP6_PEER" ]; then
		local peerport qemuport
		case "$QOB_UDP6_PEER" in
		[0-9]*:[0-9]*) peerport=${QOB_UDP6_PEER%%:*}; qemuport=${QOB_UDP6_PEER#*:} ;;
		*) qob_die "--udp6-peer must be HOSTPORT:QEMUPORT" ;;
		esac
		[[ "$peerport" =~ ^[0-9]{1,5}$ && "$qemuport" =~ ^[0-9]{1,5}$ ]] ||
			qob_die "--udp6-peer ports must be decimal integers"
		((10#$peerport >= 1 && 10#$peerport <= 65535 &&
		  10#$qemuport >= 1 && 10#$qemuport <= 65535)) ||
			qob_die "--udp6-peer ports must be in 1..65535"
	fi
	if [ "$QOB_KERNEL_ONLY" = 1 ]; then
		[ -z "$QOB_INI_BOOTEXEC" ] || qob_die "--kernel-only cannot be combined with --obootexec"
		[ -z "$QOB_INI_BOOTGRAPH" ] || qob_die "--kernel-only cannot be combined with --obootgraph"
		repl=0
	fi
	if [ -n "$QOB_REPL_INPUT" ] && [ "$repl" = 0 ]; then
		qob_die "--repl-input cannot be used with --no-repl"
	fi

	trap qob_cleanup EXIT
	trap 'exit 130' INT
	trap 'exit 143' TERM
	qob_prepare_outdir
	qob_preflight
	qob_build_artifacts

	puuid=$(python3 -c 'import uuid; print(uuid.uuid4())')
	printf '%s\n' "$puuid" >"$QOB_OUT/disk.partuuid"
	qob_new_o_image "$QOB_OUT/o.img" "$QOB_OUT/ream"
	qob_write_ini "$QOB_OUT/plan9.ini" "$puuid" /9pc64.elf "$repl"
	qob_make_esp "$QOB_OUT/plan9.ini" "$QOB_OUT/esp.img"
	qob_make_disk "$QOB_OUT/o.img" "$QOB_OUT/disk.img" \
		"$QOB_OUT/esp.img" "$puuid" "$QOB_OUT/disk.layout"

	if [ "$QOB_KERNEL_ONLY" = 1 ]; then
		success='PLAN2001 KERNEL READY'
	elif [ "$repl" = 1 ]; then
		success='PLAN2001-O: REPL READY'
	else
		success='PLAN2001-O: SYSTEM IDLE'
	fi
	if ! qob_boot_once "$QOB_OUT/disk.img" boot "$success" \
		"$([ "$repl" = 1 ] && echo repl || echo norepl)" \
		"$QOB_REPL_INPUT" "$QOB_EXPECT"; then
		qob_show_tail "$QOB_OUT/boot-boot.log"
		qob_die "boot FAIL: $QOB_BOOT_REASON (logs in $QOB_OUT)"
	fi

	qob_extract_o "$QOB_OUT/disk.img" "$QOB_OUT/o-after.img" "$QOB_OUT/disk.layout"
	qob_oinspect "$QOB_OUT/o-after.img" "$QOB_OUT/o-after.json" "$QOB_OUT/o-after.err"

	local bootlog="$QOB_OUT/boot-boot.log"
	local panic=NO bad=NO
	qob_has_ere 'panic:|suicide|PLAN2001-O: BOOT FAILED' "$bootlog" && panic=YES
	qob_has_ere 'bad disk|error syncing:|cache flush:|i/o error' "$bootlog" && bad=YES
	local nspace mp_sha expected_mp
	nspace=$(python3 - "$QOB_OUT/o-after.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
print(d['volume']['N'])
PY
)
	mp_sha=MISSING
	expected_mp=none
	if [ "$QOB_KERNEL_ONLY" != 1 ]; then
		mp_sha=$(python3 - "$QOB_OUT/o-after.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
print(d.get('objects', {}).get('4f45584543000003', {}).get('sha256', 'MISSING'))
PY
)
		expected_mp=$(sha256sum "$QOB_MPY" | awk '{print $1}')
	fi

	local fails=0
	fail() { printf 'FAIL: %s\n' "$*" >&2; fails=$((fails + 1)); }
	qob_has_fixed '[P2 L01] Plan2001 loader' "$bootlog" || fail "loader did not run"
	qob_has_fixed '[P2 L22] ELF kernel file closed' "$bootlog" || fail "loader did not finish ELF load"
	qob_has_fixed 'Plan 9' "$bootlog" || fail "no kernel banner"
	qob_has_fixed 'o: O volume ' "$bootlog" || fail "O volume not opened"
	qob_has_fixed "PARTUUID=$puuid" "$bootlog" || fail "boot did not use the generated PARTUUID"
	qob_has_ere 'sd: controllers initialized \([1-9][0-9]*\) native-only' "$bootlog" ||
		fail "no native SD controller"
	if [ "$QOB_CONTROLLER" = virtio ] && [ "$QOB_KERNEL_ONLY" != 1 ]; then
		qob_has_fixed 'sdvirtio: flush PASS' "$bootlog" || fail "virtio flush PASS missing"
	fi
	if [ "$QOB_KERNEL_ONLY" = 1 ]; then
		qob_has_fixed 'o-server: BOOTEXEC requested=none mode=kernel-only' "$bootlog" ||
			fail "kernel-only boot mode missing"
		qob_has_fixed 'PLAN2001 KERNEL READY' "$bootlog" || fail "PLAN2001 KERNEL READY missing"
		! qob_has_fixed 'OEXECUTE oid=0x4f45584543000003' "$bootlog" ||
			fail "kernel-only boot executed MicroPython"
	else
		qob_has_fixed 'KERNEL-READY' "$bootlog" || fail "no o-server kernel ready"
		qob_has_fixed 'o: OEXECUTE oid=0x4f45584543000003' "$bootlog" || fail "MicroPython OEXECUTE missing"
		qob_has_fixed 'space: O: init has no graph' "$bootlog" || fail "fresh image was not fresh"
		qob_has_fixed 'space: O: init initialized from O code (odemo)' "$bootlog" || fail "init was not initialized"
		qob_has_fixed 'init: run() -> 1' "$bootlog" || fail "init.run() did not return 1 on the fresh image"
		qob_has_fixed 'space: O: committed init version 1' "$bootlog" || fail "GE/O commit version 1 missing"
		qob_has_fixed 'init: done' "$bootlog" || fail "init: done missing"
		qob_has_fixed 'PLAN2001-O: BOOT SUCCESS' "$bootlog" || fail "boot success marker missing"
		qob_has_fixed 'PLAN2001-O: GE/O COMMIT SUCCESS' "$bootlog" || fail "GE/O commit success marker missing"
		if [ "$repl" = 1 ]; then
			qob_has_fixed 'PLAN2001-O: REPL READY' "$bootlog" || fail "REPL READY missing"
			qob_has_fixed '>>>' "$bootlog" || fail "REPL prompt missing"
		else
			qob_has_fixed 'PLAN2001-O: SYSTEM IDLE' "$bootlog" || fail "SYSTEM IDLE missing"
		fi
		[ "$nspace" -ge 1 ] 2>/dev/null || fail "O Space version did not advance"
		[ "$mp_sha" = "$expected_mp" ] || fail "OID_MP_EXEC bytes in GE/O differ from the build"
	fi
	[ "$panic" = NO ] || fail "panic/BOOT FAILED marker present"
	[ "$bad" = NO ] || fail "bad disk / flush / sync I/O error present"

	{
		printf 'Plan2001-O QEMU native O boot\n'
		printf 'result=%s\n' "$([ "$fails" = 0 ] && echo PASS || echo FAIL)"
		printf 'reason=%s\n' "$QOB_BOOT_REASON"
		printf 'controller=%s\n' "$QOB_CONTROLLER"
		printf 'network=%s\n' "$([ -n "$QOB_UDP6_PEER" ] && echo "udp-encapsulated-peer:$QOB_UDP6_PEER" || echo none)"
		printf 'accel=%s\n' "$QOB_ACCEL"
		printf 'accel_selected=%s\n' "${QOB_ACCEL_SELECTED:-unknown}"
		printf 'qemu=%s\n' "$("$QOB_QEMU" --version | head -1)"
		printf 'ovmf_code=%s\novmf_vars=%s\n' "$QOB_OVMF_CODE" "$QOB_OVMF_VARS"
		printf 'kernel=%s\nkernel_sha256=%s\n' "$QOB_KERNEL" "$(sha256sum "$QOB_KERNEL" | awk '{print $1}')"
		printf 'loader=%s\nloader_sha256=%s\n' "$QOB_EFI" "$(sha256sum "$QOB_EFI" | awk '{print $1}')"
		printf 'kernel_only=%s\n' "$QOB_KERNEL_ONLY"
		printf 'micropython=%s\nmicropython_sha256=%s\n' "$([ "$QOB_KERNEL_ONLY" = 1 ] && echo none || echo "$QOB_MPY")" "$expected_mp"
		printf 'o_image=%s\no_image_sha256=%s\n' "$QOB_OUT/o.img" "$(sha256sum "$QOB_OUT/o.img" | awk '{print $1}')"
		printf 'disk=%s\ndisk_sha256=%s\n' "$QOB_OUT/disk.img" "$(sha256sum "$QOB_OUT/disk.img" | awk '{print $1}')"
		printf 'partuuid=%s\no_space_N=%s\n' "$puuid" "$nspace"
		printf 'o_mp_sha256=%s\n' "$mp_sha"
		printf 'panic=%s\nbad_disk_or_sync_error=%s\n' "$panic" "$bad"
		if [ "$QOB_KERNEL_ONLY" = 1 ]; then
			for m in '[P2 L01] Plan2001 loader' '[P2 L22] ELF kernel file closed' \
				'o: O volume ' \
				'o-server: BOOTEXEC requested=none mode=kernel-only' \
				'PLAN2001 KERNEL READY'; do
				printf '%s: %s\n' "$m" "$(qob_stage "$bootlog" "$m")"
			done
		else
			for m in '[P2 L01] Plan2001 loader' '[P2 L22] ELF kernel file closed' \
				'KERNEL-READY' 'o: O volume ' \
				'o: OEXECUTE oid=0x4f45584543000003' \
				'space: O: init has no graph' \
				'space: O: init initialized from O code (odemo)' \
				'init: run() -> 1' 'space: O: committed init version 1' \
				'init: done' 'PLAN2001-O: BOOT SUCCESS' \
				'PLAN2001-O: GE/O COMMIT SUCCESS' \
				'PLAN2001-O: REPL READY' 'PLAN2001-O: SYSTEM IDLE'; do
				printf '%s: %s\n' "$m" "$(qob_stage "$bootlog" "$m")"
			done
		fi
		printf 'logs=%s\n' "$QOB_OUT"
		qob_show_tail "$bootlog"
	} | tee "$QOB_OUT/report.txt"

	if [ "$fails" -ne 0 ]; then
		qob_die "$fails boot checkpoint(s) failed (report: $QOB_OUT/report.txt)"
	fi
	if [ "$QOB_KERNEL_ONLY" = 1 ]; then
		qob_say "PASS: native O boot and kernel-only ready state ($QOB_OUT/report.txt)"
	else
		qob_say "PASS: native O boot, GE/O commit and expected milestone ($QOB_OUT/report.txt)"
	fi
}

if [ "${QEMU_O_BOOT_LIB:-0}" != 1 ]; then
	qob_main "$@"
fi

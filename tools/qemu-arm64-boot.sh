#!/bin/sh
# Plan2001-O experimental ARM64 boot gate.
#
# Builds the freestanding ARM64 kernel and BOOTAA64.EFI with the local
# Clang/LLD toolchain, exposes them through QEMU's vvfat disk, boots QEMU
# virt with the EDK2 AArch64 firmware, and requires the ARM64 marker that is
# printed only after the kernel has entered EL1, enabled its own MMU and
# validated the shared Plan2001 BootInfo v1 blob.
#
# This is a disposable host-side check; it writes only under build/arm64.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out=$root/build/arm64
esp=$out/esp
log=$out/qemu/serial.log
err=$out/qemu/qemu.err

if [ "${QEMU_ARM64_NOBUILD:-0}" != "1" ]; then
	python3 "$root/tools/host/build-arm64.py" -j "${JOBS:-4}"
	sh "$root/tools/host/build-efi-arm64.sh" -j "${JOBS:-4}"
fi
[ -f "$out/9karm64.elf" ] || {
	echo "qemu-arm64-boot: missing $out/9karm64.elf" >&2
	exit 1
}
[ -f "$out/bootaa64.efi" ] || {
	echo "qemu-arm64-boot: missing $out/bootaa64.efi" >&2
	exit 1
}

fw=${QEMU_ARM64_FIRMWARE:-}
if [ -z "$fw" ]; then
	for f in \
		"$(dirname "$(command -v qemu-system-aarch64)")/../share/qemu/edk2-aarch64-code.fd" \
		/data/data/com.termux/files/usr/share/qemu/edk2-aarch64-code.fd \
		/usr/share/qemu/edk2-aarch64-code.fd \
		/usr/share/AAVMF/AAVMF_CODE.fd \
		/usr/share/qemu-efi-aarch64/QEMU_EFI.fd
	do
		if [ -f "$f" ]; then
			fw=$f
			break
		fi
	done
fi
if [ -z "$fw" ] || [ ! -f "$fw" ]; then
	echo "qemu-arm64-boot: no AArch64 EDK2 firmware; set QEMU_ARM64_FIRMWARE" >&2
	exit 1
fi

rm -rf "$esp"
mkdir -p "$esp/EFI/BOOT" "$out/qemu"
cp "$out/bootaa64.efi" "$esp/EFI/BOOT/BOOTAA64.EFI"
cp "$out/9karm64.elf" "$esp/9karm64.elf"
printf 'bootfile=/9karm64.elf\noboot=server\n' > "$esp/plan9.ini"

rm -f "$log" "$err"
set +e
timeout "${QEMU_ARM64_TIMEOUT:-40}" qemu-system-aarch64 \
	-M virt -cpu cortex-a57 -m 512 \
	-bios "$fw" \
	-drive if=none,id=esp,file=fat:rw:"$esp",format=raw \
	-device virtio-blk-device,drive=esp \
	-display none -monitor none -no-reboot \
	-serial "file:$log" 2>"$err"
rc=$?
set -e
[ "$rc" -eq 0 ] || [ "$rc" -eq 124 ] || {
	cat "$err" >&2
	echo "qemu-arm64-boot: QEMU exited $rc" >&2
	exit 1
}

# PL011 output is line-terminated with CRLF; normalise before matching so the
# end-of-line anchors work on every terminal/log transport.
plain=$out/qemu/serial-plain.log
tr -d '\r' < "$log" > "$plain"

if ! grep -q '^PLAN2001-O: ARM64 BOOT: shared kernel initialization reached$' "$plain"; then
	echo "qemu-arm64-boot: FAIL: kernel marker missing (log $log)" >&2
	tail -n 30 "$plain" >&2 || true
	exit 1
fi
if ! grep -q '^PLAN2001-O: shared boot: BootInfo v1 accepted$' "$plain"; then
	echo "qemu-arm64-boot: FAIL: shared BootInfo marker missing (log $log)" >&2
	tail -n 30 "$plain" >&2 || true
	exit 1
fi
if ! grep -q '^PLAN2001-O: ARM64 MEM: allocator self-test passed$' "$plain"; then
	echo "qemu-arm64-boot: FAIL: page allocator marker missing (log $log)" >&2
	tail -n 30 "$plain" >&2 || true
	exit 1
fi
if ! grep -q '^PLAN2001-O: ARM64 timer: 3 ticks, interrupts working$' "$plain"; then
	echo "qemu-arm64-boot: FAIL: timer interrupt marker missing (log $log)" >&2
	tail -n 30 "$plain" >&2 || true
	exit 1
fi

echo "qemu-arm64-boot: PASS (log $log)"
grep '^PLAN2001-O:' "$plain"

#!/usr/bin/env bash
# CI-blocking negative architecture audit for Phase 6.
#
# It rejects reintroduced ObjectFIFO/Pipe symbols, ABI entries, adapters,
# generated syscall names, linked symbols and any kernel IPv4/IPv6/ARP/ESP
# protocol implementation.  The selftest proves the checks fail on temporary
# forbidden fixtures before the real tree is inspected.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
[ -f build/gcc/9pc64.elf ] || tools/build-gcc.sh --kernel >/dev/null
[ -f build/gcc/micropython.elf ] || tools/build-mpy-gcc.sh >/dev/null
python3 tools/host/arch-audit.py --selftest
python3 tools/host/arch-audit.py

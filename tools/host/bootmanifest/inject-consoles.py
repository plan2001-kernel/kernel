#!/usr/bin/env python3
"""QEMU HMP input + framebuffer focus verification for boot-owned consoles."""
from __future__ import annotations
import hashlib
from pathlib import Path
import socket
import sys
import time

mon, serial, output = sys.argv[1:4]
serial = Path(serial)
output = Path(output)
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
deadline = time.monotonic() + 10
while True:
    try:
        s.connect(mon)
        break
    except OSError:
        if time.monotonic() >= deadline:
            raise SystemExit("QEMU monitor unavailable")
        time.sleep(0.1)

def command(cmd):
    s.sendall((cmd + "\n").encode("ascii"))
    data = b""
    end = time.monotonic() + 10
    while time.monotonic() < end:
        s.settimeout(0.4)
        try:
            p = s.recv(4096)
        except socket.timeout:
            continue
        if not p:
            break
        data += p
        if b"(qemu) " in data:
            if b"Error:" in data or b"invalid" in data.lower():
                raise SystemExit(f"QEMU monitor rejected {cmd!r}: {data!r}")
            return
    raise SystemExit("QEMU monitor timed out: " + cmd)

def observed(marker, timeout=35):
    needle = marker.encode()
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if needle in serial.read_bytes().replace(b"\r", b""):
            return
        time.sleep(0.1)
    raise SystemExit("missing guest console marker " + marker)

def snapshot(name):
    path = output / ("focus-" + name + ".ppm")
    command("screendump " + str(path))
    if not path.exists() or path.stat().st_size < 1000:
        raise SystemExit("framebuffer screenshot unavailable: " + str(path))
    return hashlib.sha256(path.read_bytes()).hexdigest()

def type_line(line):
    for ch in line:
        key = "shift-equal" if ch == "+" else "ret" if ch == "\n" else ch
        if key not in ("shift-equal", "ret") and ch not in "0123456789":
            raise SystemExit("unsupported input: " + ch)
        command("sendkey " + key + " 40")
        time.sleep(0.08)

# Drain initial HMP banner.
s.settimeout(2)
data = b""
while b"(qemu) " not in data:
    data += s.recv(4096)

command("sendkey ctrl-alt-f2 80")
observed("boot-console: FOCUS alpha")
time.sleep(0.3)
type_line("123456+654321\n")
observed("777777")
time.sleep(0.4)
a = snapshot("alpha")

command("sendkey ctrl-alt-f3 80")
observed("boot-console: FOCUS beta")
time.sleep(0.3)
type_line("222222+333333\n")
observed("555555")
time.sleep(0.4)
b = snapshot("beta")

command("sendkey ctrl-alt-f1 80")
observed("boot-console: FOCUS kernel")
time.sleep(0.4)
k = snapshot("kernel")

if len({a,b,k}) != 3:
    raise SystemExit("focus did not render three distinct framebuffer views")
s.close()
print("PASS: two independent REPL inputs, focus changes and three framebuffer views")

#!/usr/bin/env python3
"""Real VGA/QEMU acceptance: decode Plan2001's own 12x24 framebuffer font.

No serial-console substitution, OCR heuristics, synthetic region publisher,
or userspace console owner. Keys enter through QEMU's PS/2 keyboard.
"""
import re
import socket
import sys
import time
from pathlib import Path
from PIL import Image

mon, serial, out, fontfile = map(Path, sys.argv[1:])
out.mkdir(parents=True, exist_ok=True)
source = fontfile.read_text()
table = source.split("static const ushort fbconsolefont[101][24] = {", 1)[1]
rows = re.findall(r"\{([^{}]+)\}", table)
font = []
for row in rows:
    nums = re.findall(r"0x[0-9a-fA-F]+", row)
    if len(nums) == 24:
        font.append(tuple(int(x, 16) for x in nums))
assert len(font) == 101, f"invalid console font: {len(font)} glyphs"
dictionary = {bits: chr(32 + i) for i, bits in enumerate(font[:95])}
dictionary.update({bits: chr(128 + i) for i, bits in enumerate(font[95:])})

deadline = time.monotonic() + 45
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
while True:
    try:
        sock.connect(str(mon))
        break
    except OSError:
        if time.monotonic() >= deadline:
            raise RuntimeError("QEMU monitor unavailable")
        time.sleep(0.2)


def prompt():
    data = bytearray()
    until = time.monotonic() + 10
    while time.monotonic() < until:
        sock.settimeout(0.4)
        try:
            piece = sock.recv(32768)
        except socket.timeout:
            continue
        if not piece:
            raise RuntimeError("QEMU monitor disconnected")
        data.extend(piece)
        if b"(qemu) " in data:
            return bytes(data)
    raise RuntimeError("QEMU monitor command timed out: " + repr(data[-300:]))


prompt()


def command(s):
    sock.sendall((s + "\n").encode())
    data = prompt()
    if b"Error" in data or b"error:" in data:
        raise RuntimeError(f"HMP {s}: {data.decode(errors='replace')}")


def send(key):
    command("sendkey " + key)
    time.sleep(0.08)


def focus(n):
    send(f"ctrl-alt-f{n}")
    time.sleep(0.3)


def typecode(code):
    for ch in code:
        if ch == "\n":
            key = "ret"
        elif ch in "()":
            key = "shift-9" if ch == "(" else "shift-0"
        elif ch in "abcdefghijklmnopqrstuvwxyz0123456789":
            key = ch
        else:
            raise ValueError("unsupported test key " + repr(ch))
        send(key)
    time.sleep(1)


def log():
    return serial.read_text(errors="replace") if serial.exists() else ""


def waitlog(markers, seconds=65):
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        s = log()
        if "panic:" in s or "BOOT FAILED" in s:
            raise AssertionError("Guest failed: " + s[-2000:])
        if all(x in s for x in markers):
            return
        time.sleep(0.5)
    raise AssertionError("Guest missing logs " + repr(markers) + "\n" + log()[-2500:])


def dump(label):
    file = out / (label + ".ppm")
    command("screendump " + str(file))
    img = Image.open(file).convert("RGB")
    width, height = img.size
    # bootfb reserves 12 pixels, rounded up to one 24px text row.
    top = 24
    ncols = min(width // 12, 512)
    nrows = min((height - top) // 24, 256)
    px = img.load()
    lines = []
    for row in range(nrows):
        line = []
        for col in range(ncols):
            pattern = []
            for y in range(top + 24*row, top + 24*(row+1)):
                value = 0
                for x in range(12*col, 12*(col+1)):
                    r, g, b = px[x, y]
                    value = (value << 1) | (1 if r+g+b >= 500 else 0)
                pattern.append(value)
            line.append(dictionary.get(tuple(pattern), "?"))
        lines.append("".join(line).rstrip())
    text = "\n".join(lines)
    (out / (label + ".txt")).write_text(text)
    return text


def waitscreen(label, needle, seconds=85):
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        s = dump(label)
        if needle in s:
            return s
        if "panic:" in log():
            raise AssertionError("Guest panicked: " + log()[-2000:])
        time.sleep(1)
    raise AssertionError(f"{label}: missing {needle!r}\n{s[-2500:]}\n{log()[-1000:]}")


try:
    waitlog(["console: SystemConsole ready, 2 independent sessions",
             "console: session A pid=", "console: session B pid="])
    system = dump("system-initial")
    assert any(x.strip() for x in system.splitlines()), "empty SystemConsole"
    focus(2)
    a = waitscreen("A-prompt", ">>>")
    assert "222" not in a
    typecode("print(111)\n")
    a = waitscreen("A-111", "111")
    assert a.count("111") >= 2, "A did not echo AND evaluate 111"

    focus(3)
    b = waitscreen("B-prompt", ">>>")
    assert "111" not in b, "A output leaked into B"
    typecode("print(222)\n")
    b = waitscreen("B-222", "222")
    assert b.count("222") >= 2, "B did not echo AND evaluate 222"
    assert "111" not in b

    focus(2)
    a = waitscreen("A-restored", "111")
    assert "222" not in a, "B output leaked into A"
    focus(3)
    send("ctrl-d")
    waitlog(["console: session B pid=", "exited"], 30)

    focus(2)
    typecode("print(333)\n")
    a = waitscreen("A-survives-B", "333")
    assert a.count("333") >= 2, "A REPL not executable after B exit"
    focus(1)
    sysview = waitscreen("system-after-B", "SystemConsole")
    assert "console: session B" in sysview, "SystemConsole lost kernel diagnostics"
    assert "222" not in sysview, "user output contaminated SystemConsole"
    print("PASS: kernel SystemConsole; REPL A/B focus; isolated input/output; B exit; A survives")
finally:
    sock.close()

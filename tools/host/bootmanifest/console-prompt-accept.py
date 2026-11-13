#!/usr/bin/env python3
"""Real VGA/QEMU acceptance for three REPLs and the kernel command prompt.

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
    # QEMU releases sendkey after 100 ms; never overlap modifier keys.
    time.sleep(0.16)


def focus(n):
    send(f"ctrl-alt-f{n}")
    time.sleep(0.3)


def typecode(code):
    for ch in code:
        if ch == "\n":
            key = "ret"
        elif ch == " ":
            key = "spc"
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


def waitfor(label, predicate, seconds=85):
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        s = dump(label)
        if predicate(s):
            return s
        if "panic:" in log():
            raise AssertionError("Guest panicked: " + log()[-2000:])
        time.sleep(1)
    raise AssertionError(f"{label}: condition not met\n{s[-2500:]}\n{log()[-1000:]}")


def waitscreen(label, needle, seconds=85):
    return waitfor(label, lambda s: needle in s, seconds)


def sessions(text):
    found = {}
    for name in ("replA", "replB", "replC"):
        matches = re.findall(rf"name={name} pid=(\d+) (live|exited|starting)",
                             text)
        if matches:
            found[name] = (int(matches[-1][0]), matches[-1][1])
    return found


def session_predicate(states):
    def predicate(text):
        found = sessions(text)
        if set(found) != set(states):
            return False
        return all(found[name][0] > 0 and found[name][1] == state
                   for name, state in states.items())
    return predicate


try:
    # Boot markers are printed concurrently by independent kernel processes
    # and can interleave on the diagnostic serial channel.  The framebuffer
    # prompt is emitted only after the permanent console manager is ready.
    focus(1)
    waitscreen("system-initial", "kernel>", 120)
    typecode("sessions\n")
    initial = waitfor("system-sessions-boot",
                      session_predicate({"replA": "live", "replB": "live",
                                         "replC": "live"}), 45)
    boot_pids = {name: value[0] for name, value in sessions(initial).items()}
    assert len(set(boot_pids.values())) == 3, "REPL PIDs are not distinct"

    typecode("helx")
    send("backspace")
    typecode("p\n")
    help_screen = waitscreen("system-help", "dmesg")
    for word in ("help", "status", "sessions", "dmesg"):
        assert word in help_screen, f"help omitted {word}"

    typecode("nonsense\n")
    bad = waitscreen("system-unknown", "unknown command: nonsense")
    assert "try 'help'" in bad, "unknown command did not point at help"

    typecode("status\n")
    status = waitscreen("system-status", "boot: manifest processes=3")
    assert "PLAN2001-O-KERNEL-BUILD" in status, "status omitted kernel identity"

    typecode("sessions\n")
    live = waitfor("system-sessions-live",
                   session_predicate({"replA": "live", "replB": "live",
                                      "replC": "live"}), 30)
    first = sessions(live)
    pids = {name: value[0] for name, value in first.items()}
    assert len(set(pids.values())) == 3, "REPL PIDs are not distinct"

    focus(2)
    a = waitscreen("A-prompt", ">>>")
    assert "222" not in a and "333" not in a
    typecode("print(111)\n")
    a = waitscreen("A-111", "111")
    assert a.count("111") >= 2, "A did not echo AND evaluate 111"

    focus(3)
    b = waitscreen("B-prompt", ">>>")
    assert "111" not in b and "333" not in b
    typecode("print(222)\n")
    b = waitscreen("B-222", "222")
    assert b.count("222") >= 2, "B did not echo AND evaluate 222"
    assert "111" not in b

    focus(4)
    c = waitscreen("C-prompt", ">>>")
    assert "111" not in c and "222" not in c
    typecode("print(333)\n")
    c = waitscreen("C-333", "333")
    assert c.count("333") >= 2, "C did not echo AND evaluate 333"
    assert "111" not in c and "222" not in c

    focus(2)
    a = waitscreen("A-restored", "111")
    assert "222" not in a and "333" not in a
    focus(3)
    send("ctrl-d")
    time.sleep(1)

    focus(1)
    typecode("sessions\n")
    exited = waitfor("system-sessions-B-exited",
                     session_predicate({"replA": "live", "replB": "exited",
                                        "replC": "live"}), 30)
    after = sessions(exited)
    assert after["replA"][0] == pids["replA"], "A PID changed after B exit"
    assert after["replC"][0] == pids["replC"], "C PID changed after B exit"

    typecode("dmesg\n")
    diag = waitscreen("system-dmesg", "console: session replB")
    assert "exited" in diag, "dmesg lacks the B exit diagnostic"
    assert "222" not in diag, "REPL output contaminated kernel diagnostics"

    focus(2)
    typecode("print(444)\n")
    a = waitscreen("A-survives-B", "444")
    assert a.count("444") >= 2, "A REPL not executable after B exit"

    focus(4)
    typecode("print(555)\n")
    c = waitscreen("C-survives-B", "555")
    assert c.count("555") >= 2, "C REPL not executable after B exit"

    focus(1)
    waitscreen("system-after-B", "kernel>")
    typecode("sessions\n")
    waitfor("system-sessions-final",
            session_predicate({"replA": "live", "replB": "exited",
                               "replC": "live"}), 30)
    print("PASS: 3 REPLs, kernel command prompt, isolated sessions, "
          "B exit, A/C survive, dmesg keeps diagnostics")
finally:
    sock.close()

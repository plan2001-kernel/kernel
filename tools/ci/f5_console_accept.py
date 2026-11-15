#!/usr/bin/env python3
"""F5 console test: physical QEMU PS/2 key injection and VGA pixel evidence.

Reads kernel's canonical framebuffer bitmap font; no serial echo assertion,
simulated userland terminal, or fake kernel output. Fails closed on timeouts.
"""
import re
import socket
import sys
import time
from pathlib import Path

from PIL import Image

if len(sys.argv) not in (5, 6):
    raise SystemExit("usage: f5_console_accept.py MONITOR SERIAL SCREENS FONT [initial|reboot]")
mode = sys.argv[5] if len(sys.argv) == 6 else "initial"
if mode not in ("initial", "reboot", "persist-initial", "persist-reboot",
                "newlib-supervised", "ash"):
    raise SystemExit("invalid boot phase")
monitor, serial, out, fontfile = map(Path, sys.argv[1:5])
out.mkdir(parents=True, exist_ok=True)

source = fontfile.read_text(encoding="utf-8")
table = source.split("static const ushort fbconsolefont[101][24] = {", 1)[1]
font = []
for row in re.findall(r"\{([^{}]+)\}", table):
    values = re.findall(r"0x[0-9a-fA-F]+", row)
    if len(values) == 24:
        font.append(tuple(int(value, 16) for value in values))
assert len(font) == 101, "unexpected kernel VGA font"
glyphs = {bits: chr(32+i) for i, bits in enumerate(font[:95])}
glyphs.update({bits: chr(128+i) for i, bits in enumerate(font[95:])})

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
deadline = time.monotonic() + 45
while True:
    try:
        sock.connect(str(monitor))
        break
    except OSError:
        if time.monotonic() > deadline:
            raise RuntimeError("QEMU monitor socket unavailable")
        time.sleep(0.2)


def reply():
    data = bytearray()
    until = time.monotonic() + 10
    while time.monotonic() < until:
        sock.settimeout(0.5)
        try:
            part = sock.recv(16384)
        except socket.timeout:
            continue
        if not part:
            raise RuntimeError("QEMU monitor disconnected")
        data.extend(part)
        if b"(qemu) " in data:
            return bytes(data)
    raise RuntimeError("QEMU monitor command timed out")


def command(line):
    sock.sendall((line + "\n").encode("ascii"))
    r = reply()
    if b"Error" in r or b"error:" in r or b"unknown key" in r.lower():
        raise RuntimeError(f"QEMU monitor {line!r}: {r!r}")


def key(name):
    command("sendkey " + name)
    time.sleep(0.16)


def focus(number):
    key(f"ctrl-alt-f{number}")
    time.sleep(0.4)


def enter(line):
    specials = {" ": "spc", "/": "slash", ".": "dot",
                ">": "shift-dot", "<":"shift-comma", "|": "shift-backslash", "$":"shift-4",
                "(":"shift-9", ")":"shift-0", ":":"shift-semicolon",
                ";":"semicolon", "=":"equal", "+":"shift-equal"}
    for letter in line:
        key("shift-"+letter.lower() if letter.isupper() else specials.get(letter, letter))
    key("ret")


def screen(label):
    file = out / f"{label}.ppm"
    command(f"screendump {file}")
    img = Image.open(file).convert("RGB")
    width, height = img.size
    pixels = img.load()
    lines = []
    # Current kernel VGA font is 12x24, text begins at y=24.
    for row in range(min((height-24)//24, 256)):
        chars = []
        for col in range(min(width//12, 512)):
            bits = []
            for y in range(24+row*24, 24+(row+1)*24):
                value = 0
                for x in range(12*col, 12*(col+1)):
                    r, g, b = pixels[x, y]
                    value = (value << 1) | (r+g+b >= 500)
                bits.append(value)
            chars.append(glyphs.get(tuple(bits), "?"))
        lines.append("".join(chars).rstrip())
    text = "\n".join(lines)
    (out / f"{label}.txt").write_text(text, encoding="utf-8")
    return text


def waitscreen(label, expected, timeout=100):
    end = time.monotonic() + timeout
    last = ""
    while time.monotonic() < end:
        last = screen(label)
        if expected in last:
            return last
        if "panic:" in serial.read_text(errors="replace"):
            raise AssertionError("kernel panic: " + serial.read_text(errors="replace")[-1800:])
        time.sleep(1)
    raise AssertionError(f"{label}: missing {expected!r}\n{last[-2200:]}")


try:
    reply()  # QEMU monitor greeting.
    waitscreen("system-boot", "kernel>", 120)
    focus(5)
    if mode == "ash":
        waitscreen("ash-ready", "PLAN2001-ASH-READY")
        enter("echo sum$((2+3))")
        waitscreen("ash-arithmetic", "sum5")
        enter("echo data > /tmp/hw; read x < /tmp/hw; echo FILE:$x")
        waitscreen("ash-file", "FILE:data")
        enter("echo piped | while read x; do echo PIPE:$x; done")
        waitscreen("ash-pipeline", "PIPE:piped")
        focus(1)
        text=waitscreen("ash-isolation", "kernel>")
        assert "PIPE:piped" not in text
        print("PASS: upstream ash interactive F5, arithmetic, files, forked pipeline, isolation")
    elif mode == "newlib-supervised":
        ready = waitscreen("newlib-supervised-boot",
                           "PLAN2001-NEWLIB-SUPERVISED-READY")
        assert "PLAN2001-UNIX-SUPERVISOR-READY" in ready, (
            "trusted F5 supervisor did not launch Newlib child")
        waitscreen("newlib-delegated-stdout",
                   "PLAN2001-NEWLIB-DELEGATED-STDOUT-PASS")
        waitscreen("newlib-delegated-stderr",
                   "PLAN2001-NEWLIB-DELEGATED-STDERR-PASS")
        # Capabilities must not leak to the separate SystemConsole.
        focus(1)
        system = waitscreen("newlib-system-isolation", "kernel>")
        assert "PLAN2001-NEWLIB-DELEGATED" not in system
        focus(5)
        enter("pipeok")
        result = waitscreen("newlib-delegated-read",
                            "PLAN2001-NEWLIB-DELEGATED-STDIN-PASS:pipeok")
        assert "PLAN2001-NEWLIB-DELEGATED-FAIL" not in result
        # Wait for the child to close all three delegated descriptors and
        # exit; the trusted supervisor must then release its F5 session.
        ended = False
        until = time.monotonic() + 20
        while time.monotonic() < until:
            if "console: session unixInitF5 pid=" in serial.read_text(errors="replace"):
                ended = True
                break
            time.sleep(0.3)
        assert ended, "Newlib child did not finish and release F5 session"
        print("PASS: Newlib public read(0)/write(1,2), delegated F5 "
              "capabilities, EOF and console isolation in real QEMU")
    else:
        ready = waitscreen("f5-init-handoff", "PLAN2001-UNIX-SHELL-READY")
        assert "PLAN2001-UNIX-SUPERVISOR-READY" in ready
        assert "PLAN2001-UNIX-INIT-ROOT:/" in ready
        assert "PLAN2001-PVD-READY" in ready
        assert "PLAN2001-BUSYBOX-F5-READY" in ready
        assert "unix:/$" in ready, "F5 did not open at the Unix root prompt"

        if mode == "persist-reboot":
            enter("cat /tmp/persisted")
            waitscreen("geo-restored-cat", "\npersist2026\nunix:/$")
            enter("ls /tmp")
            waitscreen("geo-restored-list", "persisted")
            # A SECOND native process reads the exact same committed
            # service view via two delegated UXPIPE channels, no volume.
            enter("cat /tmp/fromclient")
            waitscreen("geo-rpc-reboot-local", "\nremote\nunix:/$")
            enter("uxfile get /tmp/fromclient")
            waitscreen("geo-rpc-reboot-remote", "\nremote\nunix:/$")
            print("PASS: GE/O restored file written by a separate UXPIPE RPC client")
            print("PASS: real GE/O manifest and file restored across fresh QEMU boot")
        elif mode == "persist-initial":
            enter("echo persist2026 > /tmp/persisted")
            enter("cat /tmp/persisted")
            waitscreen("geo-firstboot-cat", "\npersist2026\nunix:/$")
            # The F5 Unix-init remains the only UXFS/GE/O owner. A
            # separate SPAWNed process sees shell changes via bounded RPC.
            enter("uxfile get /tmp/persisted")
            waitscreen("geo-rpc-shell-to-child", "\npersist2026\nunix:/$")
            enter("uxfile put /tmp/fromclient remote")
            enter("cat /tmp/fromclient")
            waitscreen("geo-rpc-child-to-shell", "\nremote\nunix:/$")
            enter("uxfile put /etc/protected secret")
            waitscreen("geo-rpc-denied", "uxfile: output failed")
            enter("cat /etc/protected")
            waitscreen("geo-rpc-denied-no-file", "cat: no such directory")
            spawns = re.findall(
                r"o: SPAWN pid=\d+ handle=\d+ oid=0x4f45584543000006\b",
                serial.read_text(errors="replace"))
            assert len(spawns) >= 4, (
                "Unix file RPC did not launch separate capability-limited clients")
            print("PASS: shared persistent GE/O service across independent "
                  "Unix file clients and shell; no path privilege escalation")
            print("PASS: real GE/O file committed via delegated storage grant")
        elif mode == "reboot":
            # This is a second cold QEMU process with the SAME GE/O disk image.
            # The previous boot ended in /usr/bin; RAM-only cwd must reset.
            assert "unix:/usr/bin$" not in ready, "previous boot's cwd leaked"
            enter("pwd")
            waitscreen("reboot-pwd-root", "\n/\nunix:/$")
            enter("ls /usr")
            waitscreen("reboot-ls-usr", "\nbin\nunix:/$")
            print("PASS: second QEMU boot of same GE/O disk enters unix:/$, "
                  "fresh PVD and shell handoff")
        else:
            for letter in "hello":
                key(letter)
            key("ret")
            result = waitscreen("f5-input-output", "PLAN2001-BUSYBOX-F5-READ:hello")
            assert "PLAN2001-BUSYBOX-F5-READY" in result
            focus(1)
            system = waitscreen("system-isolated", "kernel>")
            assert "PLAN2001-BUSYBOX-F5-READ:hello" not in system, (
                "stdout leaked into SystemConsole"
            )
            focus(5)
            restored = waitscreen("f5-restored", "PLAN2001-BUSYBOX-F5-READ:hello")
            assert "unix:/$" in restored
            enter("pwd")
            waitscreen("pvd-pwd-root", "\n/\nunix:/$")
            enter("ls")
            waitscreen("pvd-ls-root", "\nbin\ndev\netc\nhome\ntmp\nusr\nvar\n")
            # The manifest boots the trusted supervisor (first SPAWN); the
            # shell's ls request must start a second native child rather than
            # quietly using the in-process fallback. Both use the same seeded
            # child ELF OID, but separate kernel Process Handles.
            spawns = re.findall(
                r"o: SPAWN pid=\d+ handle=\d+ oid=0x4f45584543000006\b",
                serial.read_text(errors="replace"))
            assert len(spawns) >= 2, (
                "F5 ls did not execute as a second native SPAWN process: "
                + str(spawns)
            )
            # Must launch two extra real kernel processes; the older
            # "cat file | cat" path keeps the sequential legacy fallback.
            enter("echo piped | cat")
            waitscreen("native-spawn-concurrent-pipe", "\npiped\nunix:/$")
            pipe_spawns = re.findall(
                r"o: SPAWN pid=\d+ handle=\d+ oid=0x4f45584543000006\b",
                serial.read_text(errors="replace"))
            assert len(pipe_spawns) >= 4, (
                "echo|cat did not SPAWN both producer and consumer: "
                + str(pipe_spawns))
            enter("cd /usr/bin")
            enter("pwd")
            waitscreen("pvd-pwd-bin", "\n/usr/bin\nunix:/usr/bin$")
            enter("cd ..")
            enter("pwd")
            waitscreen("pvd-pwd-usr", "\n/usr\nunix:/usr$")
            enter("cd /missing")
            waitscreen("pvd-missing", "cd: no such directory")
            enter("pwd")
            waitscreen("pvd-cwd-unchanged", "\n/usr\nunix:/usr$")
            enter("cd ../../../../")
            enter("pwd")
            waitscreen("pvd-root-confined", "\n/\nunix:/$")
            # Stage 4/7: these new paths and redirections execute inside the
            # actual F5 native ELF, with QEMU PS/2 keys and framebuffer output.
            enter("echo test > /tmp/file")
            enter("cat /tmp/file")
            waitscreen("uxfs-redirection-cat", "\ntest\nunix:/$")
            enter("echo more >> /tmp/file")
            enter("cat /tmp/file")
            waitscreen("uxfs-append-cat", "\ntest\nmore\nunix:/$")
            enter("cat /tmp/file | cat")
            waitscreen("uxfs-pipeline-cat", "\ntest\nmore\nunix:/$")
            enter("mkdir /tmp/newdir")
            enter("cp /tmp/file /tmp/newdir/copy")
            enter("mv /tmp/newdir/copy /tmp/newdir/moved")
            enter("cat /tmp/newdir/moved")
            waitscreen("uxfs-copy-move-cat", "\ntest\nmore\nunix:/$")
            enter("rm /tmp/newdir/moved")
            enter("cat /tmp/newdir/moved")
            waitscreen("uxfs-rm-denies", "cat: no such directory")

            # End boot 1 in a non-root directory, verified before stopping QEMU.
            enter("cd /usr/bin")
            enter("pwd")
            waitscreen("pvd-first-boot-last-cwd", "\n/usr/bin\nunix:/usr/bin$")
            print("PASS: first boot unix-init -> F5 shell, pwd/cd/ls, "
                  "PS/2+VGA and kernel console isolation")
finally:
    sock.close()

#!/usr/bin/env python3
"""F5 console test: physical QEMU PS/2 key injection and VGA pixel evidence.

Reads kernel's canonical framebuffer bitmap font; no serial echo assertion,
simulated userland terminal, or fake kernel output. Fails closed on timeouts.
"""
import os
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
match = re.search(
    r"static const ushort fbconsolefont\[(\d+)\]\[24\] = \{(.*?)\n\};",
    source, re.S)
assert match, "kernel VGA font table not found"
font_count = int(match.group(1))
table = match.group(2)
font = []
for row in re.findall(r"\{([^{}]+)\}", table):
    values = re.findall(r"0x[0-9a-fA-F]+", row)
    if len(values) == 24:
        font.append(tuple(int(value, 16) for value in values))
assert len(font) == font_count, "unexpected kernel VGA font geometry"
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
    return r


def key(name):
    command("sendkey " + name)
    time.sleep(0.16)


def focus(number):
    key(f"ctrl-alt-f{number}")
    time.sleep(0.4)


def type_text(line):
    # The fixture pins keymap=us (and older manifest consoles decode US
    # scancodes). HMP sendkey uses PHYSICAL keys, not typed characters.
    specials = {" ": "spc", "/": "slash", ".": "dot", "-": "minus",
                ">": "shift-dot", "<": "shift-comma", "|": "shift-backslash",
                "$": "shift-4", "(": "shift-9", ")": "shift-0",
                ":": "shift-semicolon", ";": "semicolon", "+": "shift-equal"}
    for letter in line:
        key("shift-"+letter.lower() if letter.isupper() else specials.get(letter, letter))


def enter(line):
    type_text(line)
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


def waitscreen(label, expected, timeout=100, occurrences=1):
    end = time.monotonic() + timeout
    last = ""
    while time.monotonic() < end:
        last = screen(label)
        if last.count(expected) >= occurrences:
            return last
        if "panic:" in serial.read_text(errors="replace"):
            raise AssertionError("kernel panic: " + serial.read_text(errors="replace")[-1800:])
        time.sleep(1)
    raise AssertionError(f"{label}: missing {expected!r}\n{last[-2200:]}")


try:
    reply()  # QEMU monitor greeting.
    # Check the live QEMU device tree, not merely the intended command array.
    controller = os.environ.get("UX_HW_CONTROLLER", "virtio")
    expected_devices = {"virtio": ("virtio-blk-pci",),
                        "ahci": ("ich9-ahci", "ide-hd")}
    if controller not in expected_devices:
        raise AssertionError(f"unsupported QEMU controller: {controller}")
    device_tree = command("info qtree").decode("utf-8", errors="replace").lower()
    for device in expected_devices[controller]:
        if device not in device_tree:
            raise AssertionError(f"{controller}: missing actual QEMU device {device}")
    waitscreen("system-boot", "kernel>", 120)
    focus(5)
    if mode == "ash":
        ready = waitscreen("ash-ready", "PLAN2001-ASH-READY")
        assert "ash$ " in ready
        enter("echo visible")
        typed = waitscreen("ash-input-echo", "visible")
        assert "echo visible" in typed, "typed command was not echoed"
        enter("test 5 -eq 5; echo test-pass")
        typed = waitscreen("ash-test", "\ntest-pass\n")
        assert "test 5 -eq 5; echo test-pass" in typed
        enter("printf print-pass")
        printf_result = waitscreen("ash-printf", "print-pass",occurrences=2)
        assert "printf print-pass" in printf_result
        enter("true; echo true-pass")
        waitscreen("ash-true", "true-pass",occurrences=2)
        enter("false; echo false-pass")
        waitscreen("ash-false", "false-pass",occurrences=2)
        # Backspace edits the visible, buffered input before Enter submits it.
        type_text("echo editedx")
        key("backspace")
        key("ret")
        edited = waitscreen("ash-line-edit", "edited")
        assert "echo edited" in edited and "editedx" not in edited
        enter("echo data > /tmp/hw; read x < /tmp/hw; set")
        file_result = waitscreen("ash-file", "\nx='data'\n")
        assert "echo data > /tmp/hw; read x < /tmp/hw; set" in file_result
        enter("echo piped | while read x; do echo PIPE; done")
        pipeline = waitscreen("ash-pipeline", "PIPE", occurrences=2)
        assert "echo piped | while read x; do echo PIPE; done" in pipeline
        enter("ls; echo TREE-cwd")
        cwd_listing=waitscreen("ash-ls-cwd","TREE-cwd",occurrences=2)
        for name in ("bin", "dev", "etc", "home", "tmp", "usr", "var"):
            assert f"\n{name}\n" in cwd_listing, f"ls missing {name}"
        enter("ls /; echo TREE-root")
        root = waitscreen("ash-ls-root", "TREE-root", occurrences=2)
        for name in ("bin", "dev", "etc", "home", "tmp", "usr", "var"):
            assert f"\n{name}\n" in root, f"ls / missing {name}"
        assert "\n.\n" not in root and "\n..\n" not in root
        enter("ls -a /; echo TREE-all")
        all_entries=waitscreen("ash-ls-all","TREE-all",occurrences=2)
        assert ".\n" in all_entries and "..\n" in all_entries
        enter("ls /usr; echo TREE-usr")
        usr = waitscreen("ash-ls-usr", "TREE-usr", occurrences=2)
        assert "\nbin\n" in usr
        for path in ("/bin", "/dev", "/etc", "/home", "/tmp", "/var", "/usr/bin"):
            label=path.strip("/").replace("/","-") or "root"
            enter("ls " + path + "; echo TREE-" + label)
            listing=waitscreen("ash-ls-"+label,"TREE-"+label,occurrences=2)
            assert "ls:" not in listing, "ls failed for " + path
            if path=="/tmp":
                assert "\nhw\n" in listing, "ls /tmp missed the created file"
        enter("ls /missing; echo MISSING-pass")
        missing = waitscreen("ash-ls-missing", "MISSING-pass",occurrences=2)
        assert "ls: /missing: no such file or directory" in missing
        enter("pwd")
        waitscreen("ash-pwd-root", "\n/\nash$ ")
        enter("cd /usr/bin; pwd; cd ..; pwd; cd /; echo TREE-cd")
        cwd = waitscreen("ash-cd-paths", "TREE-cd",occurrences=2)
        assert cwd.count("/usr/bin") >= 2 and cwd.count("/usr") >= 2
        focus(1)
        text=waitscreen("ash-isolation", "kernel>")
        assert "PIPE:piped" not in text
        print("PASS: ash input echo/editing, Enter, echo/printf/test/true/false, ls/PVD paths, files, pipeline, isolation")
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

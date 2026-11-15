# Linux GE/O tools

These commands inspect a Plan2001 GE/O volume using the sibling `kernel`
checkout's native `tools/oinspect.py`. `rebuild` additionally needs its built
`build/format-o/format-o` formatter. Python 3 and Linux coreutils are required.
The kernel checkout is an input; no kernel source changes are made.

From the userland checkout:

```sh
sudo tools/geo/geols /dev/nvme0n1p5
sudo tools/geo/geols /dev/nvme0n1p5 --json
sudo tools/geo/geocp get /dev/nvme0n1p5 0x4f45584543000003 micropython.elf
sudo tools/geo/geocp rebuild /dev/nvme0n1p5 new-volume.img \
  --put 0x4f45584543000005=unix-supervisor.elf \
  --put 0x4f45584543000006=ash.elf
```

`get` exports one live object's payload and refuses to overwrite an existing
output. `rebuild` creates a new sparse regular image the same size as the
input, preserves all live payloads except explicit replacements, and verifies
every resulting payload digest. It preserves the MicroPython Environment
and checks its module digests. The input volume is never written.

This is an **offline reformat and reseed**, not an in-place object insertion.
Volume layout, identity, object versions and history are not preserved.
Owner-aware volumes are rejected because their provenance requires native
transaction support. Use only experimental volumes that can be reformatted.
Quiesce guest access before reading a volume or making a snapshot.

For a different kernel location, invoke the Python entry point explicitly:

```sh
python3 tools/geo/geo.py --kernel /path/to/kernel ls volume.img
```

After identifying and backing up the intended unmounted physical partition,
validate and deploy the new image with the kernel's `gcc-ovol-ream.sh`
`--image-check`, `--plan`, `--install` and `--verify` commands. See the kernel's
hardware-boot guide for its separate ESP/GRUB installer. These commands do
not select a target disk or write GRUB themselves.

Optional command installation (absolute source paths):

```sh
sudo ln -s "$PWD/tools/geo/geols" /usr/local/bin/geols
sudo ln -s "$PWD/tools/geo/geocp" /usr/local/bin/geocp
```

The links use this checkout, so keep it available. Raw-device access normally
requires `sudo`; regular images can be inspected without it. These tools are
for trusted experimental images, not hostile-input parsing.

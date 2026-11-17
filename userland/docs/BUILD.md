# Build and test workspace

This document describes the **source-fetching and host-control workflow**, not the entire native port. The separate [native multicall workflow](BUSYBOX-MULTICALL.md) builds 30 upstream BusyBox command names on Plan2001; full POSIX and full BusyBox support are still incomplete. The host-control workflow produces a **statically linked Linux-host reference BusyBox**. A separate [libc ABI audit](libc-audit.md) builds the native libc archive and links the upstream BusyBox `true.c` applet as a diagnostic, without claiming a multicall port.

## Source layout and revisions

The kernel is at the repository root and userland is in `userland/`.
Run userland build helpers from that directory. The source helper creates a
`deps/kernel` symlink to the current checkout for build scripts that take a
kernel path, and fetches the immutable BusyBox 1.36.1 commit declared in
[`config/sources.json`](../config/sources.json). It records both source
commits in `reports/sources.json`. Kernel builds via the symlink place build
output in the current checkout. Use a clean checkout at the reported commit
when reproducing CI results.

## Local Debian execution

Use Python 3, Git, GCC, GNU Make, binutils (`readelf`) and `file`. On a Debian host:

```sh
sudo apt-get update
sudo apt-get install -y git python3 build-essential binutils file

mkdir -p ~/Src/Plan2001
cd ~/Src/Plan2001
git clone https://github.com/plan2001-kernel/kernel.git
cd kernel/userland

export PLAN2001_DEPS="$HOME/Src/Plan2001/deps"
export PLAN2001_REPORTS="$HOME/Src/Plan2001/reports"

# Fresh destination required; the scripts never overwrite a checkout.
# The kernel comes from this checkout. BusyBox stays at its pinned SHA.
python3 tools/ci/fetch_sources.py \
  --lock config/sources.json \
  --dest "$PLAN2001_DEPS" \
  --reports "$PLAN2001_REPORTS"

python3 tools/ci/audit_kernel.py \
  --kernel "$PLAN2001_DEPS/kernel" \
  --reports "$PLAN2001_REPORTS"

python3 tools/ci/host_control.py \
  --busybox "$PLAN2001_DEPS/busybox" \
  --reports "$PLAN2001_REPORTS"
```

For a fresh second run, remove/recreate **only the throwaway `deps` and `reports` paths you deliberately created**, never your kernel development checkout, O volume or userland working tree. The fetcher intentionally refuses to overwrite an existing checkout.

## GitHub Actions

Workflow: [`.github/workflows/build.yml`](../../.github/workflows/build.yml).

Triggers: `pull_request`, pushes to `main`, and manual `workflow_dispatch`. Runner: `ubuntu-24.04`. Token permission: `contents: read` only.

Steps:

1. Check out the combined repository revision under test.
2. Install host build tools.
3. Prepare the same-checkout kernel and the immutable BusyBox SHA, then record their revisions in `sources.json`.
4. Audit `runtime/abi` and `runtime/libc`. Compile one native ObjectSpace ABI header as a **host syntax test**, and inventory the runtime source files.
5. Configure BusyBox via `make allnoconfig`, enable exactly `CONFIG_STATIC`, `CONFIG_TRUE`, `CONFIG_FALSE` and `CONFIG_ECHO`, build with **Linux host libc**, check ELF dynamic dependencies, run `true`, `false`, `echo`.
6. Upload logs, config, ELF inspection and source inventory as a 14-day diagnostic artifact. Summarize what was and was not run.

The static Linux executable is **not** uploaded as a Plan2001 deliverable. It is an upstream/toolchain sanity control, not evidence of working Plan2001 libc linking, native ABI execution, or QEMU.

## Expected status matrix

| Check | Current workflow |
| --- | --- |
| Exact source revision verification | Executed |
| Kernel `runtime/libc` source inventory | Executed |
| Kernel ObjectSpace C-header host syntax | Executed |
| BusyBox static link against Linux host libc | Executed as control |
| BusyBox Linux host applet smoke | Executed as control |
| BusyBox static link against Plan2001 libc | Native `true.c` diagnostic link; separate three-applet freestanding target workflow |
| Plan2001 QEMU/OVMF BusyBox execution | Separate native `true.c` and three-applet guest smoke workflows (check each run) |
| Physical hardware | **NOT RUN** |

Host-control build logs and the test summary are available under the workflow run's **Artifacts** and **Step summary**. Failure of the host control is a real CI failure; success must not be advertised as target compatibility.

## Dedicated libc audit workflow

The separate [libc audit](libc-audit.md) runs in `.github/workflows/libc-audit.yml` using the same checkout and immutable BusyBox pin. It builds the actual native libc archive and startup objects, the Linux host-control BusyBox, and a native BusyBox `true.c` diagnostic link. It then computes a reproducible `nm` symbol-gap report and a manifest-versus-source check. The host and target outputs have deliberately separate statuses; a host success is not a Plan2001 multicall success. See the audit document for artifacts, local commands and limitations.

## Native BusyBox subset and QEMU

The separate [BusyBox native target workflow](../../.github/workflows/busybox-target.yml) runs a three-applet diagnostic. It builds unmodified upstream `true.c`, `false.c`, and `echo.c` against the pinned Plan2001 libc with a small userland adapter, writes a native AMD64 static ELF, and executes those applets inside the real Plan2001 QEMU guest using a disposable GE/O image. A successful job publishes the executable in a `busybox-plan2001-amd64` artifact and guest serial evidence in `busybox-native-target-qemu`. This is not yet a complete POSIX or full upstream BusyBox port.

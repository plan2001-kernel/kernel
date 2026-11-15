# Live-kernel build and test workspace

This is the **working source/build mechanism** for the Plan2001 userland project. It is not yet a complete Plan2001 BusyBox port. The host-control workflow produces a **statically linked Linux-host reference BusyBox**. A separate [libc ABI audit](libc-audit.md) builds the native libc archive and links the upstream BusyBox `true.c` applet as a diagnostic, without claiming a multicall port.

## Separation of repositories

The CI and local development directory layout is:

```text
workspace/
  userland/   # writable: code, docs, CI; GitHub repo plan2001-kernel/userland
  kernel/     # latest main resolved to a detached SHA for this run, INPUT ONLY
  busybox/    # pinned upstream BusyBox checkout; host-control build writes here
reports/      # generated diagnostics, outside all source Git trees
```

The GitHub Actions job implements the same layout using runner temp directories and a userland checkout:

```text
$GITHUB_WORKSPACE/             userland checkout (the PR under test)
$RUNNER_TEMP/plan2001-sources/
  kernel/                     freshly fetched main, detached for this run
  busybox/                    pinned exact upstream BusyBox SHA
$RUNNER_TEMP/plan2001-reports/  reports/artifacts
```

**No Git submodule or copy of the kernel sources is committed into userland.** The kernel remains its own canonical repo. The kernel checkout is logically read-only; Git does not enforce OS file permissions, but the audit and build scripts do not write to it. The BusyBox checkout may contain build output; discard/recreate it between runs.

## Source policy: live kernel main, immutable BusyBox

[`config/sources.json`](../config/sources.json) is the source policy:
`kernel.repository=plan2001-kernel/kernel` with `kernel.ref=main`, and
`busybox.repository=mirror/busybox` with one immutable 40-digit commit
(BusyBox **1.36.1**, upstream tag `1_36_1`).

Every job fetches the **current kernel `main` at source-fetch time**,
resolves it to its precise 40-character commit, then checks out the commit
detached. The SHA is written to the uploaded `reports/sources.json`
alongside the fixed BusyBox SHA, so the run is auditable and repeatable
*by manually checking out that recorded kernel SHA*. The next job fetches
`main` afresh. No stale kernel revision lock is maintained.

BusyBox stays pinned; changing its commit requires review and a license and
compatibility check. The mirror is only a fetch location; original BusyBox
is GPLv2. A floating kernel intentionally sacrifices identical CI inputs
across different run dates in exchange for always testing current kernel
development. It does not silently modify the user's working kernel tree.

## Local Debian execution

Use Python 3, Git, GCC, GNU Make, binutils (`readelf`) and `file`. On a Debian host:

```sh
sudo apt-get update
sudo apt-get install -y git python3 build-essential binutils file

mkdir -p ~/Src/Plan2001
cd ~/Src/Plan2001
git clone https://github.com/plan2001-kernel/userland.git
cd userland

export PLAN2001_DEPS="$HOME/Src/Plan2001/deps"
export PLAN2001_REPORTS="$HOME/Src/Plan2001/reports"

# Fresh destination required; the scripts never overwrite a checkout.
# Kernel is fetched from live main at this moment, BusyBox stays at its SHA.
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

The source trees will look like sibling `kernel` and `busybox` dependencies relative to the `userland` checkout, but there is no assumption that they live inside the userland repo. On a development host, inspect the **exact kernel SHA recorded in the run's `sources.json`** when reproducing a specific CI result. A fresh automated run uses the latest `main`, never the host's existing kernel checkout.

## GitHub Actions

Workflow: [`.github/workflows/build.yml`](../.github/workflows/build.yml).

Triggers: `pull_request`, pushes to `main`, and manual `workflow_dispatch`. Runner: `ubuntu-24.04`. Token permission: `contents: read` only.

Steps:

1. Check out the `userland` commit under test.
2. Install host build tools.
3. Fetch the latest kernel `main` and immutable BusyBox SHA into separate sibling directories; detach both commits and record them in `sources.json`.
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

## Transition to a real Plan2001 target build

Once the missing compatibility interfaces have been audited and implemented:

1. Add a separate `tools/ci/build_target.py` or equivalent that explicitly invokes the verified Plan2001 user toolchain, target startup code, and the adapted native C runtime.
2. Ensure target objects use no Linux headers, Linux startup objects, Linux syscalls, or host libc.
3. Produce a statically linked ELF and audit its ABI, entry point, PT_INTERP/dynamic section and undefined symbols.
4. Add a distinct, clearly named GitHub Actions job that uses the pinned kernel's boot/install/QEMU fixture to prove execution on the **real Plan2001 AMD64 kernel**.
5. Only after the guest test passes add persistent ObjectSpace paths, shell pipes and wider POSIX functionality. Treat ARM64 as a separate future port.

Changes needed in native kernel ABI belong to [kernel](https://github.com/plan2001-kernel/kernel), with a separately reviewed PR. Do not introduce POSIX path syscalls into the kernel to satisfy this userspace goal.

## Dedicated libc audit workflow

The separate [libc audit](libc-audit.md) runs in `.github/workflows/libc-audit.yml` using the same immutable source pins. It builds the actual native libc archive and startup objects, the Linux host-control BusyBox, and a native BusyBox `true.c` diagnostic link. It then computes a reproducible `nm` symbol-gap report and a manifest-versus-source check. The host and target outputs have deliberately separate statuses; a host success is not a Plan2001 multicall success. See the audit document for artifacts, local commands and limitations.

## Native BusyBox subset and QEMU

The separate [BusyBox native target guide](BUSYBOX-TARGET.md) documents the dedicated `.github/workflows/busybox-target.yml` job. It builds unmodified upstream `true.c`, `false.c`, and `echo.c` against the pinned Plan2001 libc with a small userland adapter, writes a native AMD64 static ELF, and executes those applets inside the real Plan2001 QEMU guest using a disposable GE/O image. A successful job publishes the executable in a `busybox-plan2001-amd64` artifact and guest serial evidence in `busybox-native-target-qemu`. This is not yet a complete POSIX or full upstream BusyBox port.

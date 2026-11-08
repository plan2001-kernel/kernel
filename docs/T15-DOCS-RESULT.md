# T15 target correction — documentation result

> **Historical documentation-round result.** Its expansive target was later
> superseded by the owner's minimal T15 implementation assignment. See
> [current scope](ESP-SA-BUNDLE.md); the original result below records that
> earlier decision and is not an active implementation checklist.

**Documentation task PASS (2026-11-08). T14/T15 implementations remain PLANNED.**
Task `d86c7cf9d7ca4cbcbab58472bcc51a96` was claimed through Plan2001 MCP;
the owner then explicitly assigned this documentation work. Authoritative
baseline: GitHub main `4e7f7a1bf4de126c3e99480b6c911af721d6a620`, containing
the merged T13 PR. Executor: Debian 13 host `9db093436960`, working directory
`/workspace/t15-docs`, branch `agent/t15/esp-object-docs`. The older MCP T11
source snapshot is not the baseline. Main must remain unchanged.

## Result and changed documents

The target excludes IKEv2 entirely and specifies externally provisioned,
minimal userspace ESP. The single normative [ESP-SA-BUNDLE](ESP-SA-BUNDLE.md)
specifies two directional SAs, four secrets/40 bytes plus metadata, opposite
host mappings, external authenticated/confidential delivery, local OIDs versus
authority, bounded parsing, tag/replay checks, one-time activation, nonce
uniqueness, restart/rollback rejection, finite limits and fail-closed behavior.
It explicitly records the RFC 4106 automatic-key-management requirement and
the experimental, non-production/non-full-compliance nature of manual keys.
No keys, implemented secure store or secret-distribution system are supplied.

| Document | Change |
| --- | --- |
| `AGENTS.md` | Binding newer decision, no reinstated negotiation target; GitHub-first workflow preserved |
| `README.md` | Replace old active IKE/ESP section, correct T13 status, link new target |
| `docs/GENERAL-SOFTWARE-SPECIFICATION.md` | Userspace IPv6/ESP target, provisioning and failure policy, no legacy compatibility obligation |
| `docs/ROADMAP-T8-T16.md` | Correct normative 2.4/2.5, T14/T15/T16 scopes/dependencies, T13 status and crosslinks |
| `docs/ESP-SA-BUNDLE.md` | New normative field/lifecycle/security design and future gates; no frozen binary ABI |
| `docs/STATUS.md` | Current target/status correction, no claimed protocol implementation or new runtime result |
| `docs/KERNEL-API-ABI.md` | Mark old network-provider descriptions as migration history after T13 |
| `docs/OBJECT-ABI-MIGRATION.md` | Same explicit current-versus-historical network boundary |
| `docs/MINIMAL-IKEV2.md` | Prominent SUPERSEDED / HISTORICAL / NON-TARGET banner; original evidence retained |
| `runtime/ikecrypto/README.md` | Preserve crypto provenance, label IKE names retained source rather than a target |
| `docs/T15-DOCS-RESULT.md` | This result, documentation checks and remaining-reference inventory |

T14 first demonstrates IPv6/ICMPv6/ND over T13 independently of ESP; UDP/TCP
are independent later userspace objectives, not ESP prerequisites. T16 common
USB/Wi-Fi work is independent. T13's recorded emulated PASS and physical NIC
NOT RUN are preserved; its native IP startup and OBJ TCP/UDP opens remain
disabled. Existing linked ESP/protocol code has not been removed and is not
proof of a working userspace ESP service.

## Remaining IKE-reference inventory

Case-insensitive whole-repository searches covered Markdown/text/instructions
and then all retained source/tool files. All remaining documentation matches
are classified below; none is an active IKE implementation goal.

| Matching file(s) | Why the references remain |
| --- | --- |
| `AGENTS.md`, `README.md`, `docs/GENERAL-SOFTWARE-SPECIFICATION.md` | Explicit exclusion of IKE/PSK/UDP-500 targets and historical crosslink |
| `docs/ROADMAP-T8-T16.md` | Explicit override/exclusion and marked historical baseline/report; T15 has no negotiation target |
| `docs/ESP-SA-BUNDLE.md` | Target exclusion, historical crosslink and accurate RFC 4106 caveat; RFC mention is not authorization |
| `docs/STATUS.md` | New exclusion plus dated weekend-integration evidence about the companion bundle |
| `docs/MINIMAL-IKEV2.md` | Original implementation/build/launch/test/future-work evidence retained verbatim below superseding banner; all NON-TARGET |
| `runtime/ikecrypto/README.md` | Actual source filenames, crypto subset and provenance, with explicit NON-TARGET context |
| `docs/NS1-PROCESS-NAMESPACES.md` | Historical inspected tree includes local IKEv2 work; not a new protocol objective |
| `docs/TINYCC-SPACE2.md` | Historical optional iked seed OID collision warning; still accurate evidence |
| `docs/UJ1-TEST-OUTPUT.txt` | Two captured kernel log messages saying IKE runs in userspace; immutable historical output, not current runtime verification |
| `tools/host/mcp-public-files.txt` | Indexed pathname for the retained historical report; source-publication index unchanged |
| `docs/T15-DOCS-RESULT.md` | This inventory and result description |

Outside documentation, matching actual-source files are `include/libsec.h`
(a generic PSK field), `runtime/ikecrypto/ikeport.h` (integer-width assertion),
`kernel/net/esp6.c` (old SA-control comments), `kernel/net/espctl.c` (historical
control-service comments/log text), `tools/host/mkovol` and
`tools/host/oseed.py` (optional IKE ELF seeding). None was edited. There is no
claim that IKE binaries, optional seeding, ESP control code or linked crypto
were removed. They impose no target compatibility obligation.

The broader ESP search also encounters boot/storage/hardware reports and
instructions, including QEMU/GCC boot docs, PRODUCTION-BOOT-AND-AUTHORITY,
O-FORMAT, development workflow, TODO, integration reports and dependency/layout
inventories. Their ESP refers to the **UEFI EFI System Partition**, or to
accurate dated evidence. It is distinct from IPsec ESP (IPv6 NH=50). UEFI boot
documents and procedures were not changed. The T13 result remains unchanged
and records its then-current task scope as history.

## Checks and limits

Documentation-only verification commands:

```sh
git diff --check
git diff --name-status
rg -n -i '\bike(v2)?\b|\biked\b|UDP[/ :]*500|\bpsks?\b' . --glob '*.md' --glob '*.txt' --glob '*AGENTS*'
rg -n -i '\bike(v2)?\b|\biked\b|UDP[/ :]*500|\bpsks?\b' . --hidden --glob '!.git'
rg -n -i '\bESP\b|preserv.*(network|stack)|retain.*(network|stack)' . --glob '*.md' --glob '*.txt' --glob '*AGENTS*'
```

- **PASS:** manual review classifies all remaining IKE references as exclusion,
  historical evidence, actual source/provenance or this inventory. No active
  IKE goal, key-exchange service or legacy-stack compatibility obligation.
- **PASS:** documentation-only diff/scope and whitespace checks; local relative
  Markdown file links in changed documents resolve. No non-Markdown changes,
  executable-mode changes, new build/test infrastructure or ABI/source edits.
- **PASS:** original MINIMAL-IKEV2 evidence body and crypto provenance retained;
  historical NS1/TinyCC/UJ1 logs and UEFI documentation remain unchanged.
- **PASS:** semantic review covers external confidential/authenticated delivery,
  four directional secrets versus whole object, local identity/authority,
  lifetimes, replay/tag/nonce/restart safety, fail closed, RFC caveat and honest
  PLANNED versus implemented status.
- **NOT RUN:** builds, boots, QEMU, hardware and protocol tests. This task is
  documentation only; earlier T13/T12 results were not rerun or newly credited.

Delivery uses a dedicated GitHub commit/PR, independent remote readback and
MCP task_report/status. Exact delivered commit and PR are recorded in that
report; this document avoids a self-referential commit hash. No main merge,
MCP source-snapshot publication or follow-on task submission is authorized.

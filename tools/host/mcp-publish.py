#!/usr/bin/env python3
"""Publish the complete canonical Plan2001-O source tree.

The owner workflow is: edit on kuha, publish the whole current source tree to
plan2001.com at the end of every task round, including failed or incomplete
rounds. This program therefore never gates publication on a build or test
result. It records the result only as session metadata.

Publication is content-addressed, staged, verified byte-for-byte and made
visible with atomic renames. The previous complete snapshot is always kept.

Finalization is crash-safe: the remote helper journals the intended activation
before changing CURRENT, appends the session record only after CURRENT is
durable, and reconciles an interrupted journal idempotently. If a snapshot was
activated but post-publication verification failed, the next run re-verifies
that exact activation instead of publishing it again as a new session. An old
pending draft is kept in history, but never moves CURRENT backwards from a
newer snapshot.
"""
import argparse
import base64
import datetime as dt
import fcntl
import gzip
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import urllib.error
import urllib.request
import uuid

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build" / "mcp-publish"
PUBLIC_LIST = ROOT / "tools" / "host" / "mcp-public-files.txt"
PRIVATE_LIST = ROOT / "tools" / "host" / "mcp-private-files.txt"
TEXT_CHECK = ROOT / "tools" / "host" / "check-public-text.py"
REMOTE_HELPER = ROOT / "tools" / "host" / "mcp-publish-remote.py"
SSH_HOST = os.environ.get("PLAN2001_PUBLISH_SSH_HOST", "plan2001.com")
REMOTE_ROOT = os.environ.get("PLAN2001_PUBLISH_ROOT", "/var/lib/plan2001-mcp/kernel-source")
MCP_URL = os.environ.get("PLAN2001_MCP_URL", "https://plan2001.com/mcp")
MCP_VERSION = "2025-06-18"
SKIP_DIRS = {"build", "__pycache__", ".cache", "node_modules", "." + "".join(map(chr, (103, 105, 116)))}
MAX_FILES = 4000
MAX_SOURCE = 512 * 1024 * 1024
PENDING = BUILD / "pending"
RESULTS = ("completed", "failed", "incomplete", "unknown")
PUBLISHER_VERSION = 3


class RemotePublishError(RuntimeError):
    def __init__(self, message: str, payload: dict | None = None):
        super().__init__(message)
        self.payload = payload or {}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def safe_rel(value: str) -> str:
    p = PurePosixPath(value)
    if (not value or p.is_absolute() or "\\" in value or
            any(part in ("", ".", "..") for part in value.split("/")) or
            any(not (c.isascii() and (c.isalnum() or c in "/_.-+@")) for c in value)):
        raise ValueError(f"unsafe source path: {value!r}")
    return p.as_posix()


def read_list(path: Path) -> list[str]:
    raw = path.read_text(encoding="utf-8")
    values = [safe_rel(line) for line in raw.splitlines() if line]
    if len(values) != len(set(values)) or values != sorted(values):
        raise ValueError(f"{path.name} must be unique and sorted")
    return values


def ignored_path(rel: PurePosixPath) -> bool:
    return (any(part in SKIP_DIRS for part in rel.parts)
            or rel.suffix == ".pyc")


def validate_workspace(public: list[str], private: list[str]) -> None:
    expected = set(public) | set(private)
    if len(expected) != len(public) + len(private):
        raise ValueError("public and local-only lists overlap")
    found = set()
    for path in ROOT.rglob("*"):
        rel = path.relative_to(ROOT)
        if ignored_path(PurePosixPath(rel.as_posix())):
            continue
        if path.is_symlink():
            raise ValueError(f"symlinks are not allowed in the source tree: {rel}")
        if path.is_file():
            found.add(rel.as_posix())
    missing = sorted(expected - found)
    extra = sorted(found - expected)
    if missing or extra:
        details = []
        if missing:
            details.append("missing listed files: " + ", ".join(missing[:12]))
        if extra:
            details.append("unclassified files: " + ", ".join(extra[:12]))
        raise ValueError("; ".join(details))
    for rel in public:
        p = ROOT.joinpath(*PurePosixPath(rel).parts)
        if not p.is_file() or p.is_symlink():
            raise ValueError(f"public source is not a regular file: {rel}")


def copy_source(destination: Path, public: list[str]) -> dict[str, str]:
    hashes = {}
    total = 0
    for rel in public:
        source = ROOT.joinpath(*PurePosixPath(rel).parts)
        target = destination.joinpath(*PurePosixPath(rel).parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        data = source.read_bytes()
        target.write_bytes(data)
        os.chmod(target, stat.S_IMODE(source.stat().st_mode))
        hashes[rel] = digest(data)
        total += len(data)
        if total > MAX_SOURCE:
            raise ValueError("public source exceeds the configured total size limit")
    for rel, expected in hashes.items():
        source = ROOT.joinpath(*PurePosixPath(rel).parts)
        if digest(source.read_bytes()) != expected:
            raise ValueError(f"source changed while staging: {rel}")
    return hashes


def verify_tree(source: Path, public: list[str], expected: dict[str, str]) -> None:
    for rel in public:
        p = source.joinpath(*PurePosixPath(rel).parts)
        if not p.is_file() or p.is_symlink() or digest(p.read_bytes()) != expected[rel]:
            raise ValueError(f"staged source changed during validation: {rel}")


def make_snapshot(source: Path, public: list[str], hashes: dict[str, str]) -> Path:
    entries = []
    total = 0
    for rel in public:
        p = source.joinpath(*PurePosixPath(rel).parts)
        data = p.read_bytes()
        if digest(data) != hashes[rel]:
            raise ValueError(f"staged source digest mismatch: {rel}")
        total += len(data)
        entries.append({"path": rel, "size": len(data), "sha256": hashes[rel]})
    if len(entries) > MAX_FILES:
        raise ValueError(f"too many public files: {len(entries)}")
    entries.sort(key=lambda item: item["path"])
    BUILD.mkdir(parents=True, exist_ok=True)
    snapshots = BUILD / "snapshots"
    snapshots.mkdir(parents=True, exist_ok=True)
    temp = Path(tempfile.mkdtemp(prefix=".snapshot-", dir=snapshots))
    files_dir = temp / "files"
    files_dir.mkdir()
    try:
        for entry in entries:
            rel = entry["path"]
            src = source.joinpath(*PurePosixPath(rel).parts)
            dst = files_dir.joinpath(*PurePosixPath(rel).parts)
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, dst)
            os.chmod(dst, 0o444)
        archive = temp / "source.tar.gz"
        with archive.open("wb") as raw:
            with gzip.GzipFile(filename="", mode="wb", fileobj=raw, compresslevel=9, mtime=0) as gz:
                with tarfile.open(fileobj=gz, mode="w", format=tarfile.USTAR_FORMAT) as tf:
                    for entry in entries:
                        rel = entry["path"]
                        src = files_dir.joinpath(*PurePosixPath(rel).parts)
                        ti = tarfile.TarInfo("source/" + rel)
                        ti.size = entry["size"]
                        ti.mode = 0o444
                        ti.uid = ti.gid = 0
                        ti.uname = ti.gname = ""
                        ti.mtime = 0
                        with src.open("rb") as stream:
                            tf.addfile(ti, stream)
        os.chmod(archive, 0o444)
        manifest_json = json.dumps(entries, ensure_ascii=False, separators=(",", ":")) + "\n"
        manifest_tsv = "".join(f"{x['size']}\t{x['sha256']}\t{x['path']}\n" for x in entries)
        (temp / "manifest.json").write_text(manifest_json, encoding="utf-8")
        (temp / "manifest.tsv").write_text(manifest_tsv, encoding="utf-8")
        os.chmod(temp / "manifest.json", 0o444)
        os.chmod(temp / "manifest.tsv", 0o444)
        archive_bytes = archive.read_bytes()
        manifest_bytes = (temp / "manifest.tsv").read_bytes()
        archive_sha = digest(archive_bytes)
        index_sha = digest(manifest_bytes)
        metadata = {
            "snapshot_id": archive_sha,
            "revision": archive_sha,
            "source_date": utc_now(),
            "source_project": "Plan2001-O",
            "file_count": len(entries),
            "total_source_size": total,
            "archive_name": "source.tar.gz",
            "archive_format": "deterministic-gzip-ustar-v1",
            "archive_size": len(archive_bytes),
            "archive_sha256": archive_sha,
            "manifest_sha256": index_sha,
            "manifest_size": len(manifest_bytes),
        }
        (temp / "snapshot.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
        os.chmod(temp / "snapshot.json", 0o444)
        verify_snapshot(temp)
        ident = metadata["snapshot_id"]
        final = snapshots / ident
        if final.exists():
            verify_snapshot(final)
            shutil.rmtree(temp)
            return final
        os.replace(temp, final)
        return final
    except BaseException:
        shutil.rmtree(temp, ignore_errors=True)
        raise


def verify_snapshot(path: Path) -> dict:
    meta = json.loads((path / "snapshot.json").read_text(encoding="utf-8"))
    entries = json.loads((path / "manifest.json").read_text(encoding="utf-8"))
    index = (path / "manifest.tsv").read_bytes()
    archive_path = path / "source.tar.gz"
    expected_index = "".join(f"{x['size']}\t{x['sha256']}\t{x['path']}\n" for x in entries).encode("utf-8")
    if (digest(index) != meta["manifest_sha256"] or index != expected_index or
            len(entries) != meta["file_count"]):
        raise ValueError("snapshot manifest validation failed")
    expected = {x["path"]: x for x in entries}
    if len(expected) != len(entries) or sum(x["size"] for x in entries) != meta["total_source_size"]:
        raise ValueError("snapshot manifest count or size mismatch")
    for rel, item in expected.items():
        safe_rel(rel)
        p = path / "files" / Path(*PurePosixPath(rel).parts)
        if not p.is_file() or p.is_symlink():
            raise ValueError(f"snapshot file missing: {rel}")
        data = p.read_bytes()
        if len(data) != item["size"] or digest(data) != item["sha256"]:
            raise ValueError(f"snapshot file digest mismatch: {rel}")
    archive = archive_path.read_bytes()
    archive_sha = digest(archive)
    if (len(archive) != meta["archive_size"] or archive_sha != meta["archive_sha256"] or
            meta["snapshot_id"] != archive_sha):
        raise ValueError("snapshot archive digest mismatch")
    found = set()
    with tarfile.open(archive_path, "r:gz") as tf:
        for member in tf:
            if not member.isfile() or member.issym() or member.islnk():
                raise ValueError("snapshot archive contains a non-regular entry")
            relpath = PurePosixPath(member.name)
            if relpath.is_absolute() or not relpath.parts or relpath.parts[0] != "source":
                raise ValueError("snapshot archive root mismatch")
            rel = PurePosixPath(*relpath.parts[1:]).as_posix()
            safe_rel(rel)
            if rel not in expected or rel in found:
                raise ValueError("snapshot archive contains an unknown or duplicate path")
            stream = tf.extractfile(member)
            if stream is None:
                raise ValueError("snapshot archive entry has no content")
            data = stream.read()
            if len(data) != expected[rel]["size"] or digest(data) != expected[rel]["sha256"]:
                raise ValueError(f"snapshot archive content mismatch: {rel}")
            found.add(rel)
    if found != set(expected):
        raise ValueError("snapshot archive is incomplete")
    return meta


class MCP:
    def __init__(self, url: str):
        self.url = url
        self.version = MCP_VERSION
        self.next_id = 1

    def post(self, message: dict, initialized: bool = True) -> tuple[int, dict | None]:
        headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
        if initialized:
            headers["MCP-Protocol-Version"] = self.version
        request = urllib.request.Request(self.url, json.dumps(message).encode(), headers=headers, method="POST")
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                data = response.read()
                return response.status, json.loads(data) if data else None
        except urllib.error.HTTPError as error:
            raise RuntimeError(f"MCP HTTP {error.code}: {error.read()[:300]!r}") from error

    def request(self, method: str, params: dict | None = None, initialized: bool = True) -> dict:
        ident = self.next_id
        self.next_id += 1
        status, response = self.post({"jsonrpc": "2.0", "id": ident, "method": method, "params": params or {}}, initialized)
        if status != 200 or not response or response.get("id") != ident:
            raise RuntimeError(f"unexpected MCP response: HTTP {status}, {response!r}")
        if "error" in response:
            raise RuntimeError(str(response["error"]))
        return response["result"]

    def initialize(self) -> None:
        result = self.request("initialize", {"protocolVersion": self.version, "capabilities": {},
                               "clientInfo": {"name": "plan2001-source-publisher", "version": "3.0"}}, initialized=False)
        if result.get("protocolVersion") != self.version:
            self.version = result["protocolVersion"]
        status, body = self.post({"jsonrpc": "2.0", "method": "notifications/initialized"}, initialized=True)
        if status != 202 or body is not None:
            raise RuntimeError(f"MCP initialization notification failed: HTTP {status}")

    def call(self, name: str, arguments: dict) -> dict:
        result = self.request("tools/call", {"name": name, "arguments": arguments})
        if result.get("isError"):
            content = result.get("content", [{}])[0].get("text", "MCP tool error")
            raise RuntimeError(content)
        if "structuredContent" in result:
            return result["structuredContent"]
        content = result.get("content", [])
        return json.loads(content[0]["text"]) if content else {}


def ssh(command: str, input_bytes: bytes | None = None, timeout: int = 900) -> subprocess.CompletedProcess:
    args = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=20", SSH_HOST, command]
    result = subprocess.run(args, input=input_bytes, capture_output=True, timeout=timeout)
    return result


def ssh_checked(command: str, input_bytes: bytes | None = None, timeout: int = 900) -> bytes:
    result = ssh(command, input_bytes, timeout)
    if result.returncode != 0:
        raise RuntimeError(f"remote command failed ({result.returncode}): "
                           f"{(result.stdout + result.stderr).decode('utf-8', 'replace')[-800:]}")
    return result.stdout


def parse_payload(result: subprocess.CompletedProcess) -> dict | None:
    output = (result.stdout + result.stderr).decode("utf-8", "replace").strip()
    for line in reversed([x for x in output.splitlines() if x.strip()]):
        try:
            value = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(value, dict):
            return value
    return None


def upload_helper() -> tuple[str, str]:
    nonce = uuid.uuid4().hex
    remote_helper = f"/tmp/plan2001-publish-{nonce}.py"
    ssh_checked(f"cat > {shlex.quote(remote_helper)}", REMOTE_HELPER.read_bytes(), timeout=120)
    return nonce, remote_helper


def get_status(client: MCP) -> dict:
    return client.call("kernel_status", {})


def remote_reconcile() -> dict:
    _, remote_helper = upload_helper()
    try:
        command = (f"sudo -n python3 {shlex.quote(remote_helper)} "
                   f"--root {shlex.quote(REMOTE_ROOT)} --reconcile-only")
        result = ssh(command, timeout=600)
        payload = parse_payload(result)
        if result.returncode != 0:
            reason = payload.get("reason") if isinstance(payload, dict) else \
                (result.stdout + result.stderr).decode("utf-8", "replace")[-800:]
            raise RemotePublishError(f"remote reconcile failed: {reason}", payload)
        if not isinstance(payload, dict):
            raise RemotePublishError("remote reconcile returned no JSON status", None)
        return payload
    finally:
        ssh(f"sudo -n rm -f {shlex.quote(remote_helper)}", timeout=120)


def verify_remote_ssh(snapshot: Path, meta: dict, require_current: bool) -> str:
    out = ssh_checked(
        f"sudo -n sh -c {shlex.quote('cat ' + REMOTE_ROOT + '/CURRENT; '
                                     'sha256sum ' + REMOTE_ROOT + '/snapshots/' +
                                     meta['snapshot_id'] + '/source.tar.gz; '
                                     'sha256sum ' + REMOTE_ROOT + '/snapshots/' +
                                     meta['snapshot_id'] + '/manifest.tsv')}"
    ).decode("utf-8")
    lines = [line.strip() for line in out.splitlines() if line.strip()]
    if not lines:
        raise RuntimeError("remote CURRENT is unreadable")
    current = lines[0]
    if require_current and current != meta["snapshot_id"]:
        raise RuntimeError(f"remote CURRENT is {current}, not the published snapshot")
    if len(lines) < 3 or lines[1].split()[0] != meta["archive_sha256"] or lines[2].split()[0] != meta["manifest_sha256"]:
        raise RuntimeError("remote snapshot hashes do not match the published snapshot")
    return current


def remote_current() -> str:
    out = ssh_checked(f"sudo -n cat {shlex.quote(REMOTE_ROOT + '/CURRENT')}")
    value = out.decode("utf-8").strip()
    if not re.fullmatch(r"[0-9a-f]{64}", value):
        raise RuntimeError("remote CURRENT is invalid")
    return value


def upload_tree(snapshot: Path, stage: str) -> None:
    tar_path = BUILD / f".upload-{uuid.uuid4().hex}.tar"
    try:
        with tarfile.open(tar_path, "w") as tf:
            for path in sorted(snapshot.rglob("*")):
                tf.add(path, arcname=path.relative_to(snapshot).as_posix(), recursive=False)
        with tar_path.open("rb") as stream:
            result = ssh(f"sudo -n tar -C {shlex.quote(stage)} --no-same-owner -xf -", stream.read(), timeout=900)
        if result.returncode != 0:
            raise RuntimeError(f"snapshot upload failed: {(result.stdout + result.stderr).decode('utf-8', 'replace')[-800:]}")
    finally:
        tar_path.unlink(missing_ok=True)


def remote_publish(snapshot: Path, meta: dict, session: dict, inject_failure: str,
                   base_current: str = "", mode: str = "auto") -> dict:
    if not REMOTE_HELPER.is_file():
        raise RuntimeError("remote helper is missing")
    nonce, remote_helper = upload_helper()
    stage = f"{REMOTE_ROOT}/.stage.{nonce}"
    remote_session = f"/tmp/plan2001-session-{nonce}.json"
    ssh_checked("sudo -n true")
    ssh_checked(f"sudo -n install -d -o root -g plan2001-mcp -m 0755 {shlex.quote(stage)}")
    try:
        upload_tree(snapshot, stage)
        ssh_checked(f"cat > {shlex.quote(remote_session)}",
                    (json.dumps(session, sort_keys=True, ensure_ascii=False) + "\n").encode("utf-8"))
        command = (
            f"sudo -n python3 {shlex.quote(remote_helper)} "
            f"--root {shlex.quote(REMOTE_ROOT)} --stage {shlex.quote(stage)} "
            f"--snapshot-id {meta['snapshot_id']} --archive-sha {meta['archive_sha256']} "
            f"--archive-size {meta['archive_size']} --manifest-sha {meta['manifest_sha256']} "
            f"--manifest-size {meta['manifest_size']} --session {shlex.quote(remote_session)} "
            f"--base-current {shlex.quote(base_current or '')} --mode {mode} "
            f"--inject-failure {inject_failure}"
        )
        result = ssh(command, timeout=1800)
        output = (result.stdout + result.stderr).decode("utf-8", "replace").strip()
        payload = parse_payload(result)
        if result.returncode != 0:
            reason = payload.get("reason") if isinstance(payload, dict) else output[-800:]
            raise RemotePublishError(f"remote publish failed ({result.returncode}): {reason}", payload)
        if not isinstance(payload, dict) or payload.get("status") not in ("published", "stored"):
            raise RemotePublishError(f"unexpected remote publish result: {output[-800:]}", payload)
        return payload
    finally:
        ssh(f"sudo -n rm -rf {shlex.quote(stage)}; sudo -n rm -f "
            f"{shlex.quote(remote_helper)} {shlex.quote(remote_session)}", timeout=120)


def verify_mcp(client: MCP, snapshot: Path, full: bool, require_current: bool) -> dict:
    meta = verify_snapshot(snapshot)
    status = get_status(client)
    if require_current:
        if status.get("snapshot_id") != meta["snapshot_id"]:
            raise RuntimeError("MCP current snapshot ID does not match the published snapshot")
        remote_meta = status.get("metadata", {})
    else:
        if meta["snapshot_id"] not in status.get("available_snapshots", []):
            raise RuntimeError("MCP does not list the stored historical snapshot")
        remote_meta = client.call("kernel_manifest", {"snapshot_id": meta["snapshot_id"],
                                                       "limit": 1}).get("metadata", {})
    if (remote_meta.get("archive_sha256") != meta["archive_sha256"] or
            remote_meta.get("file_count") != meta["file_count"] or
            remote_meta.get("manifest_sha256") != meta["manifest_sha256"]):
        raise RuntimeError("MCP source metadata does not match the published snapshot")
    source_path = "kernel/amd64/main.c"
    entry = next((x for x in json.loads((snapshot / "manifest.json").read_text()) if x["path"] == source_path), None)
    if entry is None:
        raise RuntimeError(f"MCP smoke-test path is absent: {source_path}")
    result = client.call("kernel_read", {"snapshot_id": meta["snapshot_id"], "path": source_path,
                                         "offset": 0, "length": min(1024, entry["size"])})
    content = result["content"]
    data = content.encode("utf-8") if result.get("encoding") == "utf-8" else base64.b64decode(content, validate=True)
    expected = (snapshot / "files" / source_path).read_bytes()
    if data != expected[:len(data)] or result.get("sha256") != entry["sha256"]:
        raise RuntimeError("MCP kernel source read does not match the published snapshot")
    if not full:
        return meta
    assembled = bytearray()
    offset = 0
    while offset < meta["archive_size"]:
        part = client.call("kernel_archive_chunk", {"snapshot_id": meta["snapshot_id"],
                                                    "offset": offset, "length": 8192})
        block = base64.b64decode(part["data_base64"], validate=True)
        if part.get("offset") != offset or part.get("sha256") != meta["archive_sha256"] or not block:
            raise RuntimeError("MCP archive chunk metadata is inconsistent")
        assembled.extend(block)
        offset += len(block)
    if len(assembled) != meta["archive_size"] or digest(assembled) != meta["archive_sha256"]:
        raise RuntimeError("MCP full archive reconstruction failed")
    if bytes(assembled) != (snapshot / "source.tar.gz").read_bytes():
        raise RuntimeError("MCP archive bytes differ from the published snapshot")
    return meta


def write_pending(snapshot: Path, session: dict, error: str, status: str,
                  actual_current: str | None, base_current: str) -> Path:
    PENDING.mkdir(parents=True, exist_ok=True)
    path = PENDING / f"{snapshot.name}.json"
    record = {
        "snapshot": str(snapshot),
        "snapshot_id": snapshot.name,
        "session": session,
        "created_at": utc_now(),
        "attempts": 0,
        "last_error": error,
        "status": status,
        "remote_current": actual_current or "",
        "base_snapshot_id": base_current,
    }
    temp = path.with_suffix(".new")
    temp.write_text(json.dumps(record, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    os.replace(temp, path)
    return path


def update_pending(record_path: Path, error: str, status: str,
                   actual_current: str | None) -> None:
    try:
        record = json.loads(record_path.read_text(encoding="utf-8"))
        record["attempts"] = int(record.get("attempts", 0)) + 1
        record["last_error"] = error
        record["status"] = status
        record["remote_current"] = actual_current or ""
        record["last_attempt"] = utc_now()
        temp = record_path.with_suffix(".new")
        temp.write_text(json.dumps(record, sort_keys=True, indent=2) + "\n", encoding="utf-8")
        os.replace(temp, record_path)
    except Exception:
        pass


def resolve_failure(client: MCP | None, snapshot: Path, meta: dict, session: dict,
                    error: Exception, base_current: str,
                    reconcile: dict | None) -> tuple[Path, str]:
    payload = getattr(error, "payload", {}) or {}
    actual = payload.get("current")
    if not actual and reconcile:
        actual = reconcile.get("current")
    if not actual:
        try:
            actual = remote_current()
        except Exception:
            actual = None
    if actual == meta["snapshot_id"]:
        status = "activated-unverified"
        message = (f"snapshot was ACTIVATED (CURRENT={actual}) but verification failed; "
                   f"pending re-verification")
    elif payload.get("status") == "stored" or payload.get("history_only"):
        status = "stored-unverified"
        message = (f"snapshot was stored in history but verification failed; "
                   f"CURRENT remains {actual or 'unknown'}")
    else:
        status = "not-activated"
        message = (f"publication did not activate the snapshot; CURRENT is "
                   f"{actual if actual else 'unknown'}")
    path = write_pending(snapshot, session, str(error), status, actual, base_current)
    return path, message


def recover_pending(client: MCP | None, full: bool) -> None:
    if not PENDING.is_dir():
        return
    try:
        reconcile = remote_reconcile()
    except Exception as error:
        print(f"mcp-publish: pending reconciliation failed: {error}", file=sys.stderr)
        reconcile = None
    for record_path in sorted(PENDING.glob("*.json")):
        record = {}
        try:
            record = json.loads(record_path.read_text(encoding="utf-8"))
            snapshot = Path(record["snapshot"])
            meta = verify_snapshot(snapshot)
            session = record["session"]
            if not session.get("session_id"):
                session["session_id"] = uuid.uuid4().hex
            base = session.get("base_snapshot_id") or record.get("base_snapshot_id", "")
            current = (reconcile or {}).get("current") or remote_current()
            result = remote_publish(snapshot, meta, session, "none",
                                    base_current=base, mode="auto")
            if result.get("status") == "published":
                verify_remote_ssh(snapshot, meta, require_current=True)
                if client is not None:
                    verify_mcp(client, snapshot, full=full, require_current=True)
                print(f"mcp-publish: recovered pending snapshot {result['snapshot_id']} "
                      f"(active, session {result['session_id']})")
            else:
                verify_remote_ssh(snapshot, meta, require_current=False)
                if client is not None:
                    verify_mcp(client, snapshot, full=full, require_current=False)
                print(f"mcp-publish: kept pending snapshot {result['snapshot_id']} in history; "
                      f"CURRENT remains {result['current']} (newer snapshot preserved)")
            record_path.unlink(missing_ok=True)
        except Exception as error:
            payload = getattr(error, "payload", {}) or {}
            actual = payload.get("current")
            status = "activated-unverified" if actual == record.get("snapshot_id") else "not-activated"
            update_pending(record_path, str(error), status, actual)
            print(f"mcp-publish: pending retry failed: {error}", file=sys.stderr)


def build_snapshot() -> tuple[Path, dict]:
    public = read_list(PUBLIC_LIST)
    private = read_list(PRIVATE_LIST)
    validate_workspace(public, private)
    stage_parent = Path(tempfile.mkdtemp(prefix="stage-", dir=BUILD))
    try:
        stage = stage_parent / "tree"
        stage.mkdir()
        copied = copy_source(stage, public)
        subprocess.run([sys.executable, str(TEXT_CHECK), "--root", str(stage)], check=True)
        snapshot = make_snapshot(stage, public, copied)
        verify_tree(stage, public, copied)
        meta = verify_snapshot(snapshot)
        return snapshot, meta
    finally:
        shutil.rmtree(stage_parent, ignore_errors=True)


def session_record(args: argparse.Namespace, meta: dict, previous: str | None,
                   session_id: str) -> dict:
    record = {
        "session_id": session_id,
        "task_id": args.task_id,
        "result": args.result,
        "status": args.result,
        "note": args.note,
        "build_status": args.build_status,
        "test_status": args.test_status,
        "publisher": "kuha:mcp-publish.py",
        "publisher_version": PUBLISHER_VERSION,
        "planned_previous_snapshot_id": previous or "",
        "base_snapshot_id": previous or "",
        "snapshot_id": meta["snapshot_id"],
        "archive_sha256": meta["archive_sha256"],
        "manifest_sha256": meta["manifest_sha256"],
        "file_count": meta["file_count"],
        "total_source_size": meta["total_source_size"],
        "source_date": meta["source_date"],
        "generated_at": utc_now(),
    }
    if args.test_results:
        try:
            record["test_results"] = json.loads(args.test_results)
        except json.JSONDecodeError:
            record["test_results"] = args.test_results
    return record


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--task-id", default="")
    parser.add_argument("--result", choices=RESULTS, default="unknown")
    parser.add_argument("--note", default="")
    parser.add_argument("--build-status", default="not-run")
    parser.add_argument("--test-status", default="not-run")
    parser.add_argument("--test-results", default="")
    parser.add_argument("--verify-full", action="store_true", help="reconstruct and hash the full archive over MCP")
    parser.add_argument("--inject-failure", choices=("none", "before-activate",
                                                    "after-activate", "after-session"), default="none",
                        help="test-only failure injection")
    parser.add_argument("--dry-run", action="store_true", help="create and verify the snapshot locally only")
    parser.add_argument("--recover-only", action="store_true", help="retry pending publications only")
    args = parser.parse_args()

    BUILD.mkdir(parents=True, exist_ok=True)
    PENDING.mkdir(parents=True, exist_ok=True)
    lock_path = BUILD / "publish.lock"
    with lock_path.open("a+b") as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        if args.dry_run:
            snapshot, meta = build_snapshot()
            print(json.dumps(meta, indent=2))
            return 0
        client = None
        try:
            client = MCP(MCP_URL)
            client.initialize()
        except Exception as error:
            print(f"mcp-publish: MCP unavailable ({error}); publication will be SSH-verified",
                  file=sys.stderr)
        try:
            reconcile = remote_reconcile()
        except Exception as error:
            print(f"mcp-publish: remote reconcile failed: {error}", file=sys.stderr)
            reconcile = None
        if client is not None:
            recover_pending(client, args.verify_full)
        if args.recover_only:
            remaining = len(list(PENDING.glob("*.json")))
            print(f"mcp-publish: recover-only finished, {remaining} pending")
            return 0 if remaining == 0 else 1
        snapshot, meta = build_snapshot()
        try:
            current = (reconcile or {}).get("current") or remote_current()
        except Exception:
            current = None
        session_id = uuid.uuid4().hex
        session = session_record(args, meta, current, session_id)
        try:
            result = remote_publish(snapshot, meta, session, args.inject_failure,
                                    base_current=current or "", mode="auto")
            if result.get("status") == "published":
                verify_remote_ssh(snapshot, meta, require_current=True)
                if client is not None:
                    verify_mcp(client, snapshot, full=args.verify_full, require_current=True)
                print(f"mcp-publish: published {result['snapshot_id']} "
                      f"({meta['file_count']} files, previous {result['previous_snapshot_id'] or 'none'}, "
                      f"session {result['session_id']}, activated={result.get('current_changed', True)}, "
                      f"verified {'ssh+mcp' if client is not None else 'ssh'})")
                return 0
            verify_remote_ssh(snapshot, meta, require_current=False)
            if client is not None:
                verify_mcp(client, snapshot, full=args.verify_full, require_current=False)
            print(f"mcp-publish: stored snapshot {result['snapshot_id']} in history; "
                  f"CURRENT remains {result['current']} (newer snapshot preserved), "
                  f"session {result['session_id']}")
            return 0
        except Exception as error:
            path, message = resolve_failure(client, snapshot, meta, session, error,
                                            current or "", reconcile)
            print(f"mcp-publish: {message}; pending {path.name}: {error}", file=sys.stderr)
            return 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"mcp-publish: {error}", file=sys.stderr)
        raise SystemExit(1)

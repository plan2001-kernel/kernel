#!/usr/bin/env python3
"""Root-side activation helper for a Plan2001-O source snapshot.

Run on plan2001.com through sudo. The public MCP service only reads the
kernel-source tree; this helper is the only writer of CURRENT and snapshots.

Finalization is journaled so a crash cannot leave a session record claiming an
activation that did not happen. CURRENT is the authoritative pointer for the
active snapshot. A prepared journal is completed idempotently by the next
reconcile: if CURRENT already names the snapshot, the session record is added;
if CURRENT is still the previous snapshot, activation is completed; if CURRENT
has advanced to another snapshot, the old draft is kept in history without
moving CURRENT backwards.
"""
from __future__ import annotations

import argparse
import fcntl
import grp
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import sys
import tarfile
import time
import uuid

SERVICE_GROUP = "plan2001-mcp"
SNAPSHOT_NAME = "snapshot.json"
JOURNAL_NAME = ".activation.json"
SEQUENCE_NAME = ".current.seq"
SESSIONS_NAME = "sessions.jsonl"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def digest_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def safe_rel(value: str) -> str:
    p = PurePosixPath(value)
    if (not value or p.is_absolute() or "\\" in value or
            any(part in ("", ".", "..") for part in value.split("/"))):
        raise ValueError(f"unsafe snapshot path: {value!r}")
    return p.as_posix()


def utc_now() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def service_gid() -> int:
    return grp.getgrnam(SERVICE_GROUP).gr_gid


def verify_snapshot(root: Path) -> dict:
    meta = json.loads((root / SNAPSHOT_NAME).read_text(encoding="utf-8"))
    entries = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
    index_bytes = (root / "manifest.tsv").read_bytes()
    expected_index = "".join(
        f"{x['size']}\t{x['sha256']}\t{x['path']}\n" for x in entries
    ).encode("utf-8")
    if index_bytes != expected_index or digest(index_bytes) != meta["manifest_sha256"]:
        raise ValueError("manifest.tsv does not match manifest.json")
    if len(entries) != meta["file_count"]:
        raise ValueError("manifest file count mismatch")
    if sum(x["size"] for x in entries) != meta["total_source_size"]:
        raise ValueError("manifest total size mismatch")
    expected = {}
    for item in entries:
        rel = safe_rel(item["path"])
        if rel in expected:
            raise ValueError(f"duplicate manifest path: {rel}")
        expected[rel] = item
        path = root / "files" / Path(*PurePosixPath(rel).parts)
        if not path.is_file() or path.is_symlink():
            raise ValueError(f"snapshot file missing: {rel}")
        data = path.read_bytes()
        if len(data) != item["size"] or digest(data) != item["sha256"]:
            raise ValueError(f"snapshot file digest mismatch: {rel}")
    archive_path = root / "source.tar.gz"
    archive = archive_path.read_bytes()
    if (len(archive) != meta["archive_size"] or digest(archive) != meta["archive_sha256"] or
            meta["snapshot_id"] != meta["archive_sha256"]):
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


def seal_tree(root: Path, gid: int) -> None:
    for path in sorted(root.rglob("*"), key=lambda p: len(p.parts), reverse=True):
        if path.is_symlink():
            raise ValueError(f"symlink in snapshot: {path}")
        if path.is_dir():
            os.chown(path, 0, gid)
            os.chmod(path, 0o555)
        else:
            os.chown(path, 0, gid)
            os.chmod(path, 0o444)
    os.chown(root, 0, gid)
    os.chmod(root, 0o555)


def fsync_dir(path: Path) -> None:
    fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def atomic_write(path: Path, data: bytes, mode: int, gid: int) -> None:
    tmp = path.with_name(path.name + ".new")
    with tmp.open("wb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    os.chown(tmp, 0, gid)
    os.chmod(tmp, mode)
    os.replace(tmp, path)
    fsync_dir(path.parent)


def read_current(root: Path) -> str:
    path = root / "CURRENT"
    if not path.is_file():
        return ""
    value = path.read_text(encoding="utf-8").strip()
    if value and (len(value) != 64 or any(c not in "0123456789abcdef" for c in value)):
        raise ValueError("CURRENT is not a snapshot ID")
    return value


def write_current(root: Path, sid: str, gid: int) -> None:
    atomic_write(root / "CURRENT", (sid + "\n").encode("utf-8"), 0o640, gid)


def read_sequence(root: Path) -> int:
    path = root / SEQUENCE_NAME
    if not path.is_file():
        return 0
    value = path.read_text(encoding="utf-8").strip()
    if not value.isdigit():
        raise ValueError("activation sequence is invalid")
    return int(value)


def write_sequence(root: Path, value: int, gid: int) -> None:
    atomic_write(root / SEQUENCE_NAME, f"{value}\n".encode("utf-8"), 0o640, gid)


def read_sessions(root: Path) -> list[dict]:
    path = root / SESSIONS_NAME
    if not path.is_file():
        return []
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        try:
            row = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(row, dict):
            rows.append(row)
    return rows


def find_session(root: Path, session_id: str) -> dict | None:
    for row in read_sessions(root):
        if row.get("session_id") == session_id:
            return row
    return None


def append_session(root: Path, session: dict, gid: int) -> tuple[dict, bool]:
    record = dict(session)
    record.setdefault("session_id", uuid.uuid4().hex)
    record.setdefault("recorded_at", utc_now())
    session_id = record["session_id"]
    existing = find_session(root, session_id)
    if existing is not None:
        if existing.get("snapshot_id") != record.get("snapshot_id"):
            raise ValueError("session ID is already recorded for another snapshot")
        return existing, False
    line = json.dumps(record, sort_keys=True, ensure_ascii=False, separators=(",", ":")) + "\n"
    lock_path = root / ".sessions.lock"
    with lock_path.open("a+") as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
        existing = find_session(root, session_id)
        if existing is not None:
            fcntl.flock(lock.fileno(), fcntl.LOCK_UN)
            if existing.get("snapshot_id") != record.get("snapshot_id"):
                raise ValueError("session ID is already recorded for another snapshot")
            return existing, False
        with (root / SESSIONS_NAME).open("a", encoding="utf-8") as stream:
            stream.write(line)
            stream.flush()
            os.fsync(stream.fileno())
        os.chown(root / SESSIONS_NAME, 0, gid)
        os.chmod(root / SESSIONS_NAME, 0o644)
        fcntl.flock(lock.fileno(), fcntl.LOCK_UN)
    return record, True


def journal_path(root: Path) -> Path:
    return root / JOURNAL_NAME


def read_journal(root: Path) -> dict | None:
    path = journal_path(root)
    if not path.is_file():
        return None
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise ValueError(f"activation journal is corrupt: {error}") from error
    if not isinstance(value, dict) or not value.get("snapshot_id"):
        raise ValueError("activation journal is invalid")
    return value


def write_journal(root: Path, journal: dict, gid: int) -> None:
    data = (json.dumps(journal, sort_keys=True, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    atomic_write(journal_path(root), data, 0o640, gid)


def clear_journal(root: Path) -> None:
    journal_path(root).unlink(missing_ok=True)
    fsync_dir(root)


def session_template(session: dict, snapshot_id: str, meta: dict, previous: str,
                     gid: int) -> dict:
    record = dict(session)
    record.setdefault("session_id", uuid.uuid4().hex)
    record["snapshot_id"] = snapshot_id
    record["archive_sha256"] = meta["archive_sha256"]
    record["archive_size"] = meta["archive_size"]
    record["manifest_sha256"] = meta["manifest_sha256"]
    record["manifest_size"] = meta.get("manifest_size", 0)
    record["file_count"] = meta["file_count"]
    record["total_source_size"] = meta["total_source_size"]
    record["source_date"] = meta.get("source_date", "")
    record["previous_snapshot_id"] = previous
    return record


def reconcile_locked(root: Path, gid: int) -> dict:
    journal = read_journal(root)
    if journal is None:
        return {"status": "ok", "reconciled": False,
                "current": read_current(root), "sequence": read_sequence(root)}
    snapshot_id = journal["snapshot_id"]
    previous = journal.get("previous_snapshot_id", "")
    destination = root / "snapshots" / snapshot_id
    if not destination.is_dir():
        raise ValueError("activation journal points at a missing snapshot")
    meta = verify_snapshot(destination)
    current = read_current(root)
    session = journal.get("session") or {}
    record = session_template(session, snapshot_id, meta, previous, gid)
    if current == snapshot_id:
        record["activated"] = True
        record["activation_status"] = "recovered-active"
        record["activation_sequence"] = int(journal.get("new_sequence", read_sequence(root)))
        stored, appended = append_session(root, record, gid)
        if read_sequence(root) < record["activation_sequence"]:
            write_sequence(root, record["activation_sequence"], gid)
        clear_journal(root)
        return {"status": "ok", "reconciled": True, "action": "completed",
                "current": current, "snapshot_id": snapshot_id,
                "session_id": stored["session_id"], "session_appended": appended,
                "sequence": read_sequence(root)}
    if not current or current == previous:
        new_sequence = int(journal.get("new_sequence", read_sequence(root) + 1))
        write_current(root, snapshot_id, gid)
        write_sequence(root, new_sequence, gid)
        record["activated"] = True
        record["activation_status"] = "recovered-activation"
        record["activation_sequence"] = new_sequence
        stored, appended = append_session(root, record, gid)
        clear_journal(root)
        return {"status": "ok", "reconciled": True, "action": "activated",
                "current": snapshot_id, "snapshot_id": snapshot_id,
                "session_id": stored["session_id"], "session_appended": appended,
                "sequence": new_sequence}
    record["activated"] = False
    record["activation_status"] = "history-only"
    record["superseded_by"] = current
    record["previous_snapshot_id"] = current
    stored, appended = append_session(root, record, gid)
    clear_journal(root)
    return {"status": "ok", "reconciled": True, "action": "superseded",
            "current": current, "snapshot_id": snapshot_id,
            "session_id": stored["session_id"], "session_appended": appended,
            "sequence": read_sequence(root)}


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--root", type=Path, required=True)
    p.add_argument("--stage", type=Path)
    p.add_argument("--snapshot-id")
    p.add_argument("--archive-sha")
    p.add_argument("--archive-size", type=int)
    p.add_argument("--manifest-sha")
    p.add_argument("--manifest-size", type=int)
    p.add_argument("--session", type=Path)
    p.add_argument("--base-current", default="")
    p.add_argument("--mode", choices=("auto", "activate", "history-only"), default="auto")
    p.add_argument("--inject-failure", choices=("none", "before-activate",
                                                "after-activate", "after-session"), default="none")
    p.add_argument("--reconcile-only", action="store_true")
    return p.parse_args()


def validate_args(args: argparse.Namespace) -> None:
    if args.reconcile_only:
        return
    required = ("stage", "snapshot_id", "archive_sha", "archive_size",
                "manifest_sha", "manifest_size", "session")
    missing = [name for name in required if getattr(args, name) is None]
    if missing:
        raise SystemExit("missing required arguments: " + ", ".join(missing))


def main() -> int:
    args = parse_args()
    validate_args(args)
    root = args.root.resolve()
    if root != Path("/var/lib/plan2001-mcp/kernel-source"):
        raise SystemExit("unexpected kernel-source root")
    if not (root / "snapshots").is_dir():
        raise SystemExit("snapshot directory is missing")
    gid = service_gid()
    with (root / ".publish.lock").open("a+") as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
        reconciled = reconcile_locked(root, gid)
        if args.reconcile_only:
            print(json.dumps(reconciled, separators=(",", ":")))
            return 0
        stage = args.stage.resolve()
        if stage.parent != root or not stage.name.startswith(".stage."):
            raise SystemExit("stage must be a private directory under kernel-source")
        previous = read_current(root)
        if previous:
            previous_dir = root / "snapshots" / previous
            if not previous_dir.is_dir():
                raise SystemExit("CURRENT points at a missing snapshot")
            verify_snapshot(previous_dir)
        meta = verify_snapshot(stage)
        if (meta["snapshot_id"] != args.snapshot_id or
                meta["archive_sha256"] != args.archive_sha or
                meta["archive_size"] != args.archive_size or
                meta["manifest_sha256"] != args.manifest_sha or
                meta.get("manifest_size", 0) != args.manifest_size):
            raise SystemExit("uploaded snapshot metadata does not match the request")
        session = json.loads(args.session.read_text(encoding="utf-8"))
        if args.inject_failure == "before-activate":
            shutil.rmtree(stage, ignore_errors=True)
            print(json.dumps({"status": "failed", "reason": "injected-before-activate",
                              "current": previous, "snapshot_id": args.snapshot_id},
                             separators=(",", ":")))
            return 3
        destination = root / "snapshots" / args.snapshot_id
        if destination.exists():
            verify_snapshot(destination)
            shutil.rmtree(stage)
        else:
            seal_tree(stage, gid)
            os.replace(stage, destination)
            fsync_dir(root / "snapshots")
        current = read_current(root)
        base = args.base_current.strip()
        if current == args.snapshot_id:
            action = "already-active"
        elif args.mode == "history-only":
            action = "history"
        elif args.mode == "activate":
            action = "activate"
        elif not current:
            action = "activate"
        elif base and current == base:
            action = "activate"
        else:
            action = "history"

        if action == "history":
            record = session_template(session, args.snapshot_id, meta, current, gid)
            record["activated"] = False
            record["activation_status"] = "history-only"
            record["superseded_by"] = current
            stored, appended = append_session(root, record, gid)
            result = {
                "status": "stored",
                "snapshot_id": args.snapshot_id,
                "previous_snapshot_id": current,
                "current": current,
                "current_changed": False,
                "history_only": True,
                "session_id": stored["session_id"],
                "session_appended": appended,
                "file_count": meta["file_count"],
                "archive_sha256": args.archive_sha,
                "manifest_sha256": args.manifest_sha,
            }
            print(json.dumps(result, separators=(",", ":")))
            return 0

        if action == "already-active":
            prior = previous if previous != args.snapshot_id else \
                session.get("planned_previous_snapshot_id", "")
            record = session_template(session, args.snapshot_id, meta, prior, gid)
            record["activated"] = True
            record["activation_status"] = "already-active"
            record["activation_sequence"] = read_sequence(root)
            stored, appended = append_session(root, record, gid)
            result = {
                "status": "published",
                "already_active": True,
                "snapshot_id": args.snapshot_id,
                "previous_snapshot_id": previous,
                "current": args.snapshot_id,
                "current_changed": False,
                "session_id": stored["session_id"],
                "session_appended": appended,
                "file_count": meta["file_count"],
                "archive_sha256": args.archive_sha,
                "manifest_sha256": args.manifest_sha,
            }
            print(json.dumps(result, separators=(",", ":")))
            return 0

        new_sequence = read_sequence(root) + 1
        record = session_template(session, args.snapshot_id, meta, previous, gid)
        record["activated"] = True
        record["activation_status"] = "activated"
        record["activation_sequence"] = new_sequence
        journal = {
            "version": 1,
            "phase": "prepared",
            "snapshot_id": args.snapshot_id,
            "previous_snapshot_id": previous,
            "base_snapshot_id": base,
            "new_sequence": new_sequence,
            "created_at": utc_now(),
            "session": session,
        }
        write_journal(root, journal, gid)
        write_current(root, args.snapshot_id, gid)
        write_sequence(root, new_sequence, gid)
        if args.inject_failure == "after-activate":
            print(json.dumps({"status": "failed", "reason": "injected-after-activate",
                              "current": args.snapshot_id, "snapshot_id": args.snapshot_id,
                              "journal": True, "session_id": record["session_id"]},
                             separators=(",", ":")))
            return 4
        stored, appended = append_session(root, record, gid)
        if args.inject_failure == "after-session":
            print(json.dumps({"status": "failed", "reason": "injected-after-session",
                              "current": args.snapshot_id, "snapshot_id": args.snapshot_id,
                              "journal": True, "session_id": stored["session_id"]},
                             separators=(",", ":")))
            return 5
        clear_journal(root)
        result = {
            "status": "published",
            "snapshot_id": args.snapshot_id,
            "previous_snapshot_id": previous,
            "current": args.snapshot_id,
            "current_changed": True,
            "session_id": stored["session_id"],
            "session_appended": appended,
            "activation_sequence": new_sequence,
            "file_count": meta["file_count"],
            "archive_sha256": args.archive_sha,
            "manifest_sha256": args.manifest_sha,
        }
        print(json.dumps(result, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except SystemExit:
        raise
    except Exception as error:
        print(json.dumps({"status": "failed", "reason": str(error)}, separators=(",", ":")),
              file=sys.stdout)
        raise SystemExit(1)

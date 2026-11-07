#!/usr/bin/env python3
"""Server-side delivery of Plan2001 task-completion notices to the Room.

The authoritative transition is ``task_report`` in the queue service.  The
service appends the completed/failed task to ``events.jsonl``; this module
scans that durable log and posts one message per task to the Room.  Delivery is
idempotent: the task id is embedded in the message, a local ledger records
successful sends, and recent Room messages are checked before posting.

The Room endpoint (jedi.ydns.eu) is IPv6-only.  Running this on plan2001.com
keeps completion notices independent of kuha's intermittent IPv6 route.  The
invite code, session cookie and other credentials are read from restricted
files and are never printed or logged.
"""

from __future__ import annotations

import argparse
import fcntl
import json
import os
import re
import sys
import tempfile
import time
from pathlib import Path
from typing import Dict, List, Optional, Tuple
from urllib import error, parse, request
from http.cookiejar import MozillaCookieJar

MAX_SUMMARY = 1200
SCAN_LAST = 500
TASK_ID_RE = re.compile(r"^[A-Za-z0-9_-]{6,128}$")
TASK_LINE_RE = re.compile(r"^Task: ([A-Za-z0-9_-]{6,128})$", re.MULTILINE)


class NotifyError(Exception):
    pass


def log(msg: str) -> None:
    print(msg, flush=True)


def utc_now() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def load_json(path: Path) -> dict:
    with path.open(encoding="utf-8") as f:
        return json.load(f)


def write_json_atomic(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    fd, tmp = tempfile.mkstemp(dir=str(path.parent), prefix=".tmp-", text=True)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            json.dump(value, f, ensure_ascii=False, indent=2)
            f.write("\n")
            f.flush()
            os.fsync(f.fileno())
        os.chmod(tmp, 0o600)
        os.replace(tmp, str(path))
    finally:
        try:
            os.unlink(tmp)
        except FileNotFoundError:
            pass


def origin_of(base: str) -> str:
    u = parse.urlsplit(base)
    return "%s://%s" % (u.scheme, u.netloc)


def agent_label(task: dict) -> str:
    worker = task.get("worker_agent")
    if isinstance(worker, str) and worker:
        return worker
    agent = str(task.get("agent") or "agent")
    return {"opencode": "OpenCode", "codex": "Codex"}.get(agent.lower(), agent)


def message_text(rec: dict) -> str:
    tid = str(rec["task_id"])
    title = str(rec.get("title") or "(untitled)")
    agent = str(rec.get("agent") or "agent")
    status = str(rec["status"])
    summary = str(rec.get("summary") or "").strip()
    if len(summary) > MAX_SUMMARY:
        summary = summary[: MAX_SUMMARY - 1].rstrip() + "\u2026"
    if not summary:
        summary = "(no summary)"
    return (
        "[Plan2001 agent] task %s\n"
        "Task: %s\n"
        "Title: %s\n"
        "Agent: %s\n"
        "Status: %s\n"
        "Result: %s"
    ) % (status, tid, title, agent, status, summary)


def record_for_task(task: dict) -> Optional[dict]:
    tid = task.get("task_id")
    if not isinstance(tid, str) or not TASK_ID_RE.match(tid):
        return None
    if task.get("status") not in ("completed", "failed", "cancelled"):
        return None
    return {
        "task_id": tid,
        "title": str(task.get("title") or "(untitled)"),
        "agent": agent_label(task),
        "status": str(task["status"]),
        "summary": str(task.get("summary") or ""),
    }


def completed_records(events_path: Path) -> Dict[str, dict]:
    """Return the latest completed/failed record per task id, in log order.

    Each record carries the event sequence number in ``_seq`` so a missing
    ledger cannot replay events that existed when the notifier was installed.
    """
    records: Dict[str, dict] = {}
    try:
        lines = events_path.read_text(encoding="utf-8", errors="replace").splitlines()
    except FileNotFoundError:
        return records
    except OSError as e:
        raise NotifyError("event log unreadable: %s" % e)
    for line in lines:
        line = line.strip()
        if not line:
            continue
        try:
            event = json.loads(line)
        except ValueError:
            continue
        task = event.get("task") if isinstance(event, dict) else None
        if not isinstance(task, dict):
            continue
        rec = record_for_task(task)
        if rec is not None:
            seq = event.get("seq")
            rec["_seq"] = seq if isinstance(seq, int) else -1
            records[rec["task_id"]] = rec
    return records


def baseline_seq(state_dir: Path) -> int:
    try:
        marker = load_json(state_dir / "baseline.json")
    except FileNotFoundError:
        return -1
    except (OSError, ValueError):
        return -1
    value = marker.get("baseline_seq") if isinstance(marker, dict) else None
    return value if isinstance(value, int) else -1


class Room:
    def __init__(self, config: dict, state_dir: Path) -> None:
        self.base = str(config["url"]).rstrip("/")
        self.invite = str(config["invite"])
        self.name = str(config.get("name") or "Plan2001 Agent")
        self.room_name = str(config.get("room_name") or "Plan2001")
        self.origin = origin_of(self.base)
        self.state_dir = state_dir
        self.cookie_file = state_dir / "cookies.txt"
        state_dir.mkdir(parents=True, exist_ok=True, mode=0o700)
        os.chmod(str(state_dir), 0o700)
        self.jar = MozillaCookieJar(str(self.cookie_file))
        if self.cookie_file.exists():
            try:
                self.jar.load(ignore_discard=True, ignore_expires=True)
            except (OSError, ValueError):
                self.jar.clear()
        self.opener = request.build_opener(request.HTTPCookieProcessor(self.jar))

    def _req(self, method: str, path: str, body: Optional[dict] = None):
        data = None
        headers = {"Accept": "application/json"}
        if body is not None:
            data = json.dumps(body).encode("utf-8")
            headers["Content-Type"] = "application/json"
            headers["Origin"] = self.origin
        req = request.Request(self.base + path, data=data, headers=headers, method=method)
        try:
            with self.opener.open(req, timeout=30) as resp:
                raw = resp.read()
                return resp.status, (json.loads(raw) if raw else None)
        except error.HTTPError as e:
            return e.code, None
        except error.URLError as e:
            raise NotifyError("room connection failed: %s" % e.reason)
        except (OSError, ValueError) as e:
            raise NotifyError("room request failed: %s" % e)

    def _save_cookies(self) -> None:
        self.jar.save(ignore_discard=True, ignore_expires=True)
        os.chmod(str(self.cookie_file), 0o600)

    def _find_room(self, me: Optional[dict]) -> Optional[dict]:
        if not isinstance(me, dict):
            return None
        for room in me.get("rooms") or []:
            if isinstance(room, dict) and room.get("name") == self.room_name:
                return room
        return None

    def ensure_session(self) -> dict:
        code, me = self._req("GET", "/api/me")
        if code == 200:
            room = self._find_room(me)
            if room:
                self._save_cookies()
                return room
        code, resp = self._req(
            "POST", "/api/join", {"invite": self.invite, "name": self.name}
        )
        if code != 200 or not isinstance(resp, dict):
            raise NotifyError("room join failed: HTTP %s" % code)
        self._save_cookies()
        room = resp.get("room")
        if not isinstance(room, dict):
            room = self._find_room(resp.get("me"))
        if not isinstance(room, dict) or not room.get("id"):
            raise NotifyError("room join response did not include the room")
        return room

    @staticmethod
    def room_id(room: dict) -> str:
        rid = room.get("id")
        if not isinstance(rid, str) or not rid:
            raise NotifyError("room id missing")
        return rid

    def messages(self, room_id: str) -> List[dict]:
        q = parse.urlencode({"last": str(SCAN_LAST)})
        code, msgs = self._req("GET", "/api/rooms/%s/messages?%s" % (room_id, q))
        if code != 200 or not isinstance(msgs, list):
            raise NotifyError("room message read failed: HTTP %s" % code)
        return [m for m in msgs if isinstance(m, dict)]

    def post(self, room_id: str, text: str) -> dict:
        code, msg = self._req(
            "POST",
            "/api/rooms/%s/messages" % room_id,
            {"text": text, "urgent": False, "notify": True},
        )
        if code != 200 or not isinstance(msg, dict):
            raise NotifyError("room message post failed: HTTP %s" % code)
        return msg


def load_sent(state_dir: Path) -> dict:
    path = state_dir / "sent.json"
    if not path.exists():
        return {}
    try:
        value = load_json(path)
        return value if isinstance(value, dict) else {}
    except (OSError, ValueError):
        raise NotifyError("sent ledger unreadable: %s" % path)


def mark_sent(state_dir: Path, task_id: str, status: str, seq: Optional[int],
              baseline: bool = False) -> None:
    path = state_dir / "sent.json"
    sent = load_sent(state_dir)
    entry = {
        "status": status,
        "sent_at": utc_now(),
        "seq": seq,
    }
    if baseline:
        entry["baseline"] = True
    sent[task_id] = entry
    write_json_atomic(path, sent)


def max_event_seq(events_path: Path) -> int:
    highest = -1
    try:
        lines = events_path.read_text(encoding="utf-8", errors="replace").splitlines()
    except FileNotFoundError:
        return highest
    except OSError as e:
        raise NotifyError("event log unreadable: %s" % e)
    for line in lines:
        try:
            event = json.loads(line)
        except ValueError:
            continue
        seq = event.get("seq") if isinstance(event, dict) else None
        if isinstance(seq, int) and seq > highest:
            highest = seq
    return highest


def baseline(events_path: Path, state_dir: Path) -> None:
    """Mark all currently completed tasks as already handled.

    This runs once, before the HTTP server starts accepting reports, so
    deploying the notifier does not replay the whole historical queue into the
    Room.  New reports appended after this point are delivered normally.
    """
    marker = state_dir / "baseline.json"
    if marker.exists():
        return
    state_dir.mkdir(parents=True, exist_ok=True, mode=0o700)
    records = completed_records(events_path)
    cutoff = max_event_seq(events_path)
    path = state_dir / "sent.json"
    sent = load_sent(state_dir)
    for tid, rec in records.items():
        if tid in sent:
            continue
        sent[tid] = {
            "status": rec["status"],
            "sent_at": utc_now(),
            "seq": None,
            "baseline": True,
        }
    write_json_atomic(path, sent)
    write_json_atomic(marker, {"created_at": utc_now(), "baseline_tasks": len(records),
                               "baseline_seq": cutoff})
    log("room notify baseline: %d historical task(s) marked handled" % len(records))


def _recent_task_ids(room: Room, room_id: str) -> set:
    ids = set()
    for msg in room.messages(room_id):
        text = str(msg.get("text") or "")
        for match in TASK_LINE_RE.finditer(text):
            ids.add(match.group(1))
    return ids


def deliver(events_path: Path, state_dir: Path, config: dict,
            dry_run: bool = False, blocking: bool = True) -> int:
    """Post notices for completed tasks not yet in the ledger.

    Returns 0 when every pending notice was delivered (or none were pending),
    1 when at least one delivery failed and should be retried.  Raises
    NotifyError when the Room cannot be reached or configured; callers retry.
    """
    state_dir.mkdir(parents=True, exist_ok=True, mode=0o700)
    lock_path = state_dir / "notify.lock"
    lock_fh = lock_path.open("w")
    try:
        os.chmod(str(lock_path), 0o600)
        flags = fcntl.LOCK_EX if blocking else fcntl.LOCK_EX | fcntl.LOCK_NB
        try:
            fcntl.flock(lock_fh, flags)
        except BlockingIOError:
            return 0
        try:
            records = completed_records(events_path)
            sent = load_sent(state_dir)
            cutoff = baseline_seq(state_dir)
            pending = []
            for tid, rec in records.items():
                if tid in sent:
                    continue
                if rec.get("_seq", -1) <= cutoff:
                    # Ledger loss must not replay the pre-deployment history.
                    mark_sent(state_dir, tid, rec["status"], None, baseline=True)
                    continue
                pending.append((tid, rec))
            if not pending:
                return 0
            room = Room(config, state_dir)
            room_id = Room.room_id(room.ensure_session())
            present = set() if dry_run else _recent_task_ids(room, room_id)
            failures = 0
            for tid, rec in pending:
                if tid in present:
                    mark_sent(state_dir, tid, rec["status"], None)
                    log("room notification for task %s already present; recorded" % tid)
                    continue
                if dry_run:
                    log("would notify task %s (%s)" % (tid, rec["status"]))
                    continue
                try:
                    msg = room.post(room_id, message_text(rec))
                except NotifyError as e:
                    failures += 1
                    log("room notification for task %s failed: %s; retained for retry" % (tid, e))
                    break
                seq = msg.get("seq")
                mark_sent(state_dir, tid, rec["status"], seq if isinstance(seq, int) else None)
                log("sent room notification for task %s (%s) seq=%s" % (tid, rec["status"], seq))
            return 1 if failures else 0
        finally:
            fcntl.flock(lock_fh, fcntl.LOCK_UN)
    finally:
        lock_fh.close()


def load_config(path: Path) -> dict:
    try:
        raw = path.read_text(encoding="utf-8")
    except OSError as e:
        raise NotifyError("room config unreadable: %s" % e)
    mode = path.stat().st_mode & 0o777
    if mode & 0o007:
        raise NotifyError("room config must not be world accessible: %s" % path)
    values = {}
    for line in raw.splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key] = value
    config = {
        "url": values.get("room_url", ""),
        "invite": values.get("room_invite", ""),
        "name": values.get("room_agent_name", "Plan2001 Agent"),
        "room_name": values.get("room_name", "Plan2001"),
    }
    for key in ("url", "invite"):
        if not config[key]:
            raise NotifyError("room config missing %s" % key)
    return config


def send_test(state_dir: Path, config: dict, task_id: str, title: str) -> int:
    if not TASK_ID_RE.match(task_id):
        raise NotifyError("test task id must match %s" % TASK_ID_RE.pattern)
    room = Room(config, state_dir)
    room_id = Room.room_id(room.ensure_session())
    rec = {
        "task_id": task_id,
        "title": title,
        "agent": "OpenCode",
        "status": "completed",
        "summary": "Verification message sent by the plan2001.com MCP service; this is not a queued task.",
    }
    msg = room.post(room_id, message_text(rec))
    seq = msg.get("seq")
    mark_sent(state_dir, task_id, "completed", seq if isinstance(seq, int) else None)
    log("sent test room notification for task %s seq=%s" % (task_id, seq))
    return 0


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--state-dir", type=Path, required=True)
    parser.add_argument("--events", type=Path, required=True)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--drain", action="store_true")
    group.add_argument("--dry-run", action="store_true")
    group.add_argument("--test", action="store_true")
    args = parser.parse_args(argv)
    try:
        config = load_config(args.config)
        if args.test:
            stamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
            return send_test(args.state_dir, config, "srvtest-" + stamp,
                             "plan2001.com Room notification self-test")
        if args.dry_run:
            return deliver(args.events, args.state_dir, config, dry_run=True)
        return deliver(args.events, args.state_dir, config)
    except NotifyError as e:
        log("room notify error: %s" % e)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

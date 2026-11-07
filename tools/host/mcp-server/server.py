#!/usr/bin/env python3
"""Ubuntu Streamable HTTP MCP endpoints for Plan2001 source and agent queue."""
import base64, fcntl, hashlib, hmac, json, os, re, threading, time, uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path, PurePosixPath
import oauth_auth as oauth
import room_notify

ROOT = Path(os.environ.get("PLAN2001_MCP_ROOT", "/var/lib/plan2001-mcp"))
SNAPS = ROOT / "kernel-source/snapshots"
QUEUE = ROOT / "agent-queue"
CONF = ROOT / "private.conf"
EVENTS = QUEUE / "events.jsonl"
STATE = QUEUE / "state.json"
ROOM_DIR = QUEUE / "room"
MAX_BODY = 512 * 1024
PROTO = "2025-06-18"
NOTIFY_EVENT = threading.Event()

def notify_loop():
    backoff = 10
    last_error = ""
    while True:
        NOTIFY_EVENT.wait(timeout=backoff)
        NOTIFY_EVENT.clear()
        try:
            room_notify.deliver(EVENTS, ROOM_DIR, room_notify.load_config(CONF))
            if last_error:
                print("room notify recovered", flush=True)
            last_error = ""
            backoff = 10
        except Exception as e:
            message = str(e)
            if message != last_error:
                print("room notify deferred: %s" % message, flush=True)
                last_error = message
            backoff = min(300, max(10, backoff * 2))

def load_config():
    out = {}
    for line in CONF.read_text().splitlines():
        if "=" in line:
            k, v = line.split("=", 1); out[k] = v
    return out

def state():
    try: return json.loads(STATE.read_text())
    except FileNotFoundError: return {"tasks": [], "attempts": [], "next_seq": 1}

def save_state(s):
    tmp = STATE.with_suffix(".new")
    with tmp.open("w") as f:
        json.dump(s, f, separators=(",", ":")); f.write("\n"); f.flush(); os.fsync(f.fileno())
    os.replace(tmp, STATE)

def append_event(s, task):
    seq = s["next_seq"]; s["next_seq"] += 1
    with EVENTS.open("a") as f:
        f.write(json.dumps({"seq": seq, "task": task}, separators=(",", ":")) + "\n"); f.flush(); os.fsync(f.fileno())
    return seq

def sha(b): return hashlib.sha256(b).hexdigest()
def safe_path(v):
    p = PurePosixPath(v)
    if not v or p.is_absolute() or "\\" in v or any(x in ("", ".", "..") for x in v.split("/")): raise ValueError("invalid source path")
    return p.as_posix()

def read_sessions():
    path = ROOT / "kernel-source/sessions.jsonl"
    if not path.is_file(): return []
    rows = []
    try:
        for line in path.read_text(encoding="utf-8").splitlines():
            if not line.strip(): continue
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(row, dict): rows.append(row)
    except OSError:
        return []
    return rows

def list_snapshots():
    try:
        names = [p.name for p in SNAPS.iterdir() if p.is_dir()]
    except OSError:
        return []
    return sorted(x for x in names if re.fullmatch(r"[0-9a-f]{64}", x))

def pending_activation():
    path = ROOT / "kernel-source/.activation.json"
    try: value = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError: return None
    except (OSError, json.JSONDecodeError): return {"status": "invalid"}
    if not isinstance(value, dict): return {"status": "invalid"}
    return {"status": "prepared", "phase": value.get("phase"),
            "snapshot_id": value.get("snapshot_id"),
            "previous_snapshot_id": value.get("previous_snapshot_id", ""),
            "created_at": value.get("created_at", "")}

def source_call(name, a):
    cur = (ROOT / "kernel-source/CURRENT").read_text().strip()
    if not re.fullmatch(r"[0-9a-f]{64}", cur): raise ValueError("invalid CURRENT")
    if name in ("kernel_status", "kernel_pop"):
        sessions = read_sessions()
        latest = sessions[-1] if sessions else None
        current_session = next((r for r in reversed(sessions)
                                if r.get("snapshot_id") == cur), None)
        return {"snapshot_id": cur, "metadata": json.loads((SNAPS / cur / "snapshot.json").read_text()),
                "latest_session": latest,
                "current_session": current_session,
                "activation_consistent": latest is None or latest.get("snapshot_id") == cur,
                "pending_activation": pending_activation(),
                "available_snapshots": list_snapshots()}
    if name == "kernel_history":
        rows = list(reversed(read_sessions()))
        want = a.get("snapshot_id")
        if want: rows = [r for r in rows if r.get("snapshot_id") == want]
        off = max(0, int(a.get("cursor", 0))); lim = max(1, min(int(a.get("limit", 20)), 100))
        return {"current": cur, "sessions": rows[off:off+lim],
                "nextCursor": str(off+lim) if off+lim < len(rows) else None,
                "available_snapshots": list_snapshots()}
    sid = a.get("snapshot_id", cur)
    if not re.fullmatch(r"[0-9a-f]{64}", sid): raise ValueError("invalid snapshot_id")
    d = SNAPS / sid
    if not d.is_dir(): raise ValueError("unknown snapshot_id")
    meta = json.loads((d / "snapshot.json").read_text()); entries = json.loads((d / "manifest.json").read_text())
    if name in ("kernel_list", "kernel_manifest"):
        prefix=a.get("prefix", ""); limit=max(1,min(int(a.get("limit",100)),100)); off=int(a.get("cursor",0))
        if name=="kernel_manifest": rows=[e for e in entries if e["path"].startswith(prefix)]
        else:
            dirs=set()
            files=[]
            for e in entries:
                p=e["path"]
                if p.startswith(prefix):
                    tail=p[len(prefix):].lstrip("/")
                    if "/" in tail: dirs.add(tail.split("/",1)[0])
                    else: files.append({"name":p,"type":"file","size":e["size"],"sha256":e["sha256"]})
            rows=[{"name":x,"type":"directory"} for x in sorted(dirs)]+files
        page=rows[off:off+limit]; ret={"snapshot_id":sid,"entries":page,"nextCursor":str(off+limit) if off+limit<len(rows) else None}
        if name=="kernel_manifest": ret["metadata"]=meta
        return ret
    if name=="kernel_read":
        rel=safe_path(a["path"]); b=(d/"files"/rel).read_bytes(); off=int(a.get("offset",0))
        if "start_line" in a:
            lines=b.splitlines(keepends=True); start=int(a["start_line"])-1; off=sum(map(len,lines[:start])); n=sum(map(len,lines[start:start+min(400,int(a.get("line_count",100)))]))
        else: n=min(4096,int(a.get("length",4096)))
        chunk=b[off:off+n]; enc="utf-8"
        try: content=chunk.decode("utf-8")
        except UnicodeDecodeError: enc="base64"; content=base64.b64encode(chunk).decode()
        return {"snapshot_id":sid,"path":rel,"encoding":enc,"offset":off,"length":len(chunk),"total_size":len(b),"sha256":sha(b),"content":content,"eof":off+len(chunk)>=len(b)}
    if name=="kernel_search":
        q=a["query"]; mode=a.get("mode","text"); rows=[]
        for e in entries:
            p=e["path"]
            if mode=="filename":
                if q.lower() in p.lower(): rows.append({"path":p,"line":0,"text":p})
            else:
                try: lines=(d/"files"/p).read_text().splitlines()
                except (UnicodeError,OSError): continue
                rows.extend({"path":p,"line":i,"text":line[:500]} for i,line in enumerate(lines,1) if q in line)
            if len(rows)>=1000: break
        off=int(a.get("cursor",0)); lim=min(40,int(a.get("limit",40)))
        return {"snapshot_id":sid,"mode":mode,"query":q,"matches":rows[off:off+lim],"nextCursor":str(off+lim) if off+lim<len(rows) else None}
    if name=="kernel_archive_chunk":
        b=(d/"source.tar.gz").read_bytes(); off=int(a["offset"]); n=min(8192,int(a["length"]))
        return {"snapshot_id":sid,"offset":off,"length":len(b[off:off+n]),"total_size":len(b),"sha256":meta["archive_sha256"],"data_base64":base64.b64encode(b[off:off+n]).decode(),"eof":off+n>=len(b)}
    raise ValueError("unknown tool")

class Handler(BaseHTTPRequestHandler):
    server_version="Plan2001MCP/1.0"
    def log_message(self,*args): pass
    def reply(self,status,obj=None,headers=None):
        b=b"" if obj is None else json.dumps(obj,separators=(",", ":")).encode(); self.send_response(status)
        if obj is not None: self.send_header("Content-Type","application/json")
        for name,value in (headers or {}).items(): self.send_header(name,value)
        self.send_header("Content-Length",str(len(b))); self.end_headers()
        if b: self.wfile.write(b)
    def do_GET(self):
        if oauth.handle_get(self): return
        return self.reply(405,{"error":"POST required"})
    def do_POST(self):
        if oauth.handle_post(self): return
        if self.path not in ("/mcp","/agent-mcp"): return self.reply(404,{"error":"not found"})
        length=int(self.headers.get("Content-Length","0"))
        if length>MAX_BODY: return self.reply(413,{"error":"request too large"})
        try: req=json.loads(self.rfile.read(length))
        except Exception: return self.reply(400,{"error":"invalid JSON"})
        method=req.get("method"); rid=req.get("id"); params=req.get("params") or {}
        if self.path == "/agent-mcp":
            identity=oauth.bearer(self)
            if identity is None:
                return oauth.fail_auth(self,rid," ".join(oauth.SCOPES))
        if method=="notifications/initialized": return self.reply(202)
        if method=="initialize":
            result={"protocolVersion":PROTO,"capabilities":{"tools":{}},"serverInfo":{"name":"plan2001-source" if self.path=="/mcp" else "plan2001-agent-queue","version":"1.0.0"}}
            if self.path=="/agent-mcp":
                result["instructions"]=("Plan2001 agent-mcp is an OpenCode task queue, not a version-control "
                    "or code-hosting connector. A client display name is only a label and grants no "
                    "repository access. Use task_submit to enqueue and task_status/task_queue_status to check. "
                    "Use task_cancel to cancel one task; task_clear with confirm=CLEAR_ALL_TASKS cancels all "
                    "pending work and requests cancellation of the active task. Submit replacement work "
                    "separately. Only the worker uses task_claim/task_report. Active leases do not expire. "
                    "Do not request or pass queue secrets; OAuth supplies them. The host stores task text but does not execute it.")
            return self.reply(200,{"jsonrpc":"2.0","id":rid,"result":result})
        if method=="ping": return self.reply(200,{"jsonrpc":"2.0","id":rid,"result":{}})
        if method=="tools/list":
            tools=SOURCE_TOOLS if self.path=="/mcp" else oauth.with_security_schemes(AGENT_TOOLS)
            return self.reply(200,{"jsonrpc":"2.0","id":rid,"result":{"tools":tools}})
        if method!="tools/call": return self.reply(200,{"jsonrpc":"2.0","id":rid,"error":{"code":-32601,"message":"method not found"}})
        try:
            name=params["name"]; args=params.get("arguments") or {}
            if self.path=="/mcp": result=source_call(name,args)
            else:
                required_scope=oauth.scope_for_tool(name)
                if required_scope:
                    identity=oauth.bearer(self)
                    auth_header=self.headers.get("Authorization","")
                    if identity is not None:
                        if required_scope not in identity["scopes"]:
                            return oauth.fail_auth(self,rid,required_scope,insufficient=True)
                        args=oauth.inject_queue_credentials(name,args,load_config())
                    elif auth_header or not args.get({"task_submit":"write_key","task_status":"read_key","task_claim":"worker_key","task_report":"worker_key","task_cancel":"worker_key","task_clear":"write_key","task_queue_status":"read_key"}[name]):
                        return oauth.fail_auth(self,rid,required_scope)
                with (QUEUE/"lock").open("r+") as lk:
                    fcntl.flock(lk,fcntl.LOCK_EX); result=self.queue_call(name,args)
            return self.reply(200,{"jsonrpc":"2.0","id":rid,"result":{"content":[{"type":"text","text":json.dumps(result,ensure_ascii=False)}],"structuredContent":result,"isError":False}})
        except Exception as e: return self.reply(200,{"jsonrpc":"2.0","id":rid,"result":{"content":[{"type":"text","text":str(e)}],"isError":True}})
    def queue_call(self,n,a):
        cfg=load_config(); s=state(); ts=s["tasks"]; now=int(time.time())
        retention=int(cfg.get("retention_seconds",2592000))
        # Retention applies only to terminal records. A leased task has no
        # expiry, and its creation time must never make it disappear.
        ts[:]=[t for t in ts if t.get("status") not in ("completed", "failed", "cancelled") or now-int(t.get("completed_at",t["created_at"]))<retention]
        # Bound retained events and unfinished work before accepting writes.
        if len(ts)>40:
            done=[t for t in ts if t["status"] in ("completed","failed","cancelled")]
            for t in done[:max(0,len(ts)-40)]: ts.remove(t)
        save_state(s)
        def auth(field,key):
            if not hmac.compare_digest(str(a.get(field,"")),cfg.get(key,"")): raise ValueError("authentication failed")
        if n=="task_submit":
            auth("write_key","write_key")
            if a.get("agent")!="opencode" or not a.get("title") or len(a.get("title",""))>200 or not a.get("instructions") or len(a.get("instructions",""))>65536: raise ValueError("invalid arguments")
            idem=a["idempotency_key"]; reqhash=sha(json.dumps([a["agent"],a["title"],a["instructions"],a.get("source_snapshot","")],separators=(",", ":")).encode())
            for t in ts:
                if t["idempotency_key"]==idem:
                    if t["request_hash"]!=reqhash: raise ValueError("idempotency conflict")
                    return {"task_id":t["task_id"],"status":t["status"],"created_at":t["created_at"],"duplicate":True}
            cutoff=now-3600
            if sum(t["created_at"]>=cutoff for t in ts)>=int(cfg.get("max_submits_per_hour",10)): raise ValueError("submission rate limit")
            if len(ts)>=int(cfg.get("max_tasks",60)): raise ValueError("task retention limit")
            if len(ts)>=int(cfg.get("max_events",2048)) or EVENTS.stat().st_size>=int(cfg.get("max_storage_bytes",33554432)): raise ValueError("queue storage limit")
            if sum(t["status"] in ("pending","leased") for t in ts)>=int(cfg.get("max_unfinished",20)): raise ValueError("queue full")
            t={"task_id":uuid.uuid4().hex,"status":"pending","agent":"opencode","title":a["title"],"instructions":a["instructions"],"source_snapshot":a.get("source_snapshot",""),"idempotency_key":idem,"request_hash":reqhash,"created_at":now,"updated_at":now,"claims":0}
            t["seq"]=append_event(s,t); ts.append(t); save_state(s); return {"task_id":t["task_id"],"status":"pending","created_at":now,"duplicate":False}
        if n=="task_queue_status":
            auth("read_key","write_key")
            active=[{"task_id":t["task_id"],"status":t["status"],"title":t["title"],"created_at":t["created_at"],"updated_at":t["updated_at"],"cancel_requested":bool(t.get("cancel_requested")),"lease_expires_at":None if t["status"]=="leased" else None} for t in ts if t["status"] in ("pending","leased")]
            return {"active":active,"pending":sum(t["status"]=="pending" for t in active),"leased":sum(t["status"]=="leased" for t in active)}
        if n=="task_status":
            auth("read_key","write_key"); t=next((x for x in ts if x["task_id"]==a.get("task_id")),None)
            if t is None: raise ValueError("not found")
            result={k:t[k] for k in ("task_id","status","title","agent","created_at","updated_at","completed_at","summary","test_results") if k in t}
            result["cancel_requested"]=bool(t.get("cancel_requested"))
            result["lease_expires_at"]=None if t.get("status")=="leased" else t.get("lease_until")
            return result
        if n=="task_claim":
            auth("worker_key","worker_key"); agent=a.get("agent")
            # Worker leases have no time limit. A crash is recovered by resuming
            # inflight.json; an operator can cancel a stuck task through MCP.
            if any(x["status"]=="leased" for x in ts): return {"task":None}
            t=next((x for x in ts if x["status"]=="pending" and x["agent"]==agent),None)
            if t is None: return {"task":None}
            tok=os.urandom(32).hex(); t.update(status="leased",lease_token=tok,updated_at=now,claims=t["claims"]+1)
            t.pop("lease_until",None); t.pop("cancel_requested",None); t.pop("cancel_requested_at",None); t.pop("cancel_reason",None)
            t["seq"]=append_event(s,t); save_state(s)
            return {"task":{k:t[k] for k in ("task_id","title","instructions","source_snapshot","created_at","updated_at")},"lease_token":tok}
        if n=="task_cancel":
            auth("worker_key","worker_key")
            t=next((x for x in ts if x["task_id"]==a.get("task_id")),None)
            if t is None: raise ValueError("not found")
            if t["status"] not in ("pending","leased"):
                return {"task_id":t["task_id"],"status":t["status"],"already_terminal":True}
            reason=str(a.get("reason") or "Cancelled by MCP task_cancel")[:1024]
            if t["status"]=="leased":
                t.update(cancel_requested=True,cancel_requested_at=now,cancel_reason=reason,updated_at=now)
                t["seq"]=append_event(s,t); save_state(s)
                return {"task_id":t["task_id"],"status":"cancel_requested","worker_must_stop":True}
            t.update(status="cancelled",summary=reason,test_results=[],updated_at=now,completed_at=now)
            t.pop("lease_token",None); t["seq"]=append_event(s,t); save_state(s); NOTIFY_EVENT.set()
            return {"task_id":t["task_id"],"status":"cancelled"}
        if n=="task_clear":
            auth("write_key","write_key")
            if a.get("confirm")!="CLEAR_ALL_TASKS": raise ValueError("confirmation must equal CLEAR_ALL_TASKS")
            reason=str(a.get("reason") or "Cancelled by MCP task_clear")[:1024]
            pending_cancelled=[]; leased_cancel_requested=[]
            for t in ts:
                if t["status"]=="pending":
                    t.update(status="cancelled",summary=reason,test_results=[],updated_at=now,completed_at=now)
                    t["seq"]=append_event(s,t); pending_cancelled.append(t["task_id"])
                elif t["status"]=="leased":
                    t.update(cancel_requested=True,cancel_requested_at=now,cancel_reason=reason,updated_at=now)
                    t["seq"]=append_event(s,t); leased_cancel_requested.append(t["task_id"])
            save_state(s)
            if pending_cancelled: NOTIFY_EVENT.set()
            return {"status":"cleared","pending_cancelled":pending_cancelled,"leased_cancel_requested":leased_cancel_requested}
        if n=="task_report":
            auth("worker_key","worker_key"); t=next((x for x in ts if x["task_id"]==a.get("task_id")),None)
            if not t or t["status"]!="leased" or not hmac.compare_digest(t.get("lease_token",""),str(a.get("lease_token",""))): raise ValueError("invalid lease")
            status=a.get("status")
            if status not in ("completed","failed","cancelled"): raise ValueError("invalid status")
            if t.get("cancel_requested") and status!="cancelled": raise ValueError("task cancellation requested")
            summary=a.get("summary","")
            if status=="cancelled": summary=t.get("cancel_reason") or summary or "Cancelled by MCP request"
            t.update(status=status,summary=summary,test_results=a.get("test_results",[]),artifact_refs=a.get("artifact_refs",[]),updated_at=now,completed_at=now)
            worker_agent=a.get("worker_agent")
            if worker_agent in ("OpenCode","Codex"): t["worker_agent"]=worker_agent
            else: t.pop("worker_agent",None)
            t.pop("lease_token",None); t.pop("lease_until",None); t.pop("cancel_requested",None); t.pop("cancel_requested_at",None); t.pop("cancel_reason",None)
            t["seq"]=append_event(s,t); save_state(s); NOTIFY_EVENT.set()
            return {"task_id":t["task_id"],"status":status,"updated_at":now}
        raise ValueError("unknown tool")

if __name__=="__main__":
    SOURCE_TOOLS,AGENT_TOOLS=json.loads((Path(__file__).parent/"tools.json").read_text())
    try:
        room_notify.baseline(EVENTS, ROOM_DIR)
        threading.Thread(target=notify_loop, name="plan2001-room-notify", daemon=True).start()
    except Exception as e:
        print("room notify disabled: %s" % e, flush=True)
    ThreadingHTTPServer(("127.0.0.1",8765),Handler).serve_forever()

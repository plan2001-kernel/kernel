# plan2001.com MCP service source

This directory is the canonical copy of the small Ubuntu MCP service that
serves both endpoints:

- `https://plan2001.com/mcp` — read-only Plan2001-O source snapshots;
- `https://plan2001.com/agent-mcp` — authenticated OpenCode task queue.

Deployed files on `plan2001.com`:

| File | Deployed path |
|---|---|
| `server.py` | `/opt/plan2001-mcp/server.py` |
| `room_notify.py` | `/opt/plan2001-mcp/room_notify.py` |
| `tools.json` | `/opt/plan2001-mcp/tools.json` |
| `oauth_auth.py` | `/opt/plan2001-mcp/oauth_auth.py` |
| `plan2001-mcp.service` | `/etc/systemd/system/plan2001-mcp.service` |
| `nginx-plan2001-mcp-http` | `/etc/nginx/sites-available/plan2001-mcp-http` |

Runtime state is **not** part of this directory or of the published source:
`private.conf`, `oauth-private.json`, the agent queue database, OAuth tokens
and the source snapshots themselves live only on the server under
`/var/lib/plan2001-mcp` with restricted ownership. The service binds
`127.0.0.1:8765`; nginx terminates TLS and proxies `/mcp` and `/agent-mcp`.

## Deploying a change

```sh
ssh plan2001.com 'sudo -n cp -a /opt/plan2001-mcp/server.py /opt/plan2001-mcp/server.py.bak-$(date -u +%Y%m%dT%H%M%SZ)'
scp tools/host/mcp-server/server.py plan2001.com:/tmp/server.py.new
ssh plan2001.com 'sudo -n install -o root -g root -m 0644 /tmp/server.py.new /opt/plan2001-mcp/server.py && sudo -n python3 -m py_compile /opt/plan2001-mcp/server.py && sudo -n systemctl restart plan2001-mcp'
```

Deploy `tools.json`, `oauth_auth.py`, the unit file or the nginx site the same
way, then verify `kernel_status`, `kernel_history` and the four queue tools
before reporting success. The public source publisher writes only
`/var/lib/plan2001-mcp/kernel-source`; it does not modify this service code.

The `kernel_history` tool is additive: existing source tools, queue tools,
schemas and historical `snapshot_id` values remain compatible.

## Queue cancellation and unbounded task duration

Leased tasks do not expire and terminal-record retention never removes pending
or leased work. `task_queue_status` lists active work; `task_cancel` cancels a
pending task immediately or marks a leased task for cancellation. The worker
stops its OpenCode process group and acknowledges the cancellation before the
queue can lease another task. `task_clear` requires `confirm="CLEAR_ALL_TASKS"`,
cancels all pending tasks and requests cancellation of the active leased task;
terminal history is retained. Submit replacement work separately with
`task_submit` after cancellation is acknowledged.

## Room completion notifications

The Room at `jedi.ydns.eu` is IPv6-only. The MCP service is therefore the only
sender of task-completion notices: on a successful `task_report` it persists
the completed/failed event in `agent-queue/events.jsonl`, and a background
thread in the same service posts the notice using the restricted `room_*`
values in `private.conf`. This keeps notices independent of kuha's intermittent
IPv6 route; kuha only needs its normal IPv4 path to plan2001.com.

The first start after deployment writes a baseline ledger in
`agent-queue/room/` so historical events are not replayed into the Room.
Successful sends are recorded there; a task already present in the Room is
detected from its `Task: <id>` marker, and transient Room or network errors are
retried without blocking `task_report`. The optional `worker_agent` argument
on `task_report` preserves the OpenCode/Codex label in the notice. Do not run a
second Room sender on kuha.

import { execFile } from "node:child_process";
import { access, mkdir, readFile, rename, writeFile } from "node:fs/promises";
import { dirname, join } from "node:path";

const PUBLISHER = "tools/host/mcp-publish.py";
const STATE_FILE = "build/mcp-publish/opencode-worker-state.json";
const TOOL = /task_(claim|report)$/;
const ACTIVE = (globalThis.__plan2001WorkerPublishActive ??= new Set());

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));

function now() {
  return new Date().toISOString();
}

async function exists(path) {
  try {
    await access(path);
    return true;
  } catch {
    return false;
  }
}

function parseJson(text) {
  if (typeof text !== "string") {
    if (text && typeof text === "object") return text;
    return null;
  }
  const trimmed = text.trim();
  if (!trimmed) return null;
  try {
    return JSON.parse(trimmed);
  } catch {}
  const start = trimmed.indexOf("{");
  const end = trimmed.lastIndexOf("}");
  if (start >= 0 && end > start) {
    try {
      return JSON.parse(trimmed.slice(start, end + 1));
    } catch {}
  }
  return null;
}

function toolResult(output) {
  if (!output) return null;
  if (output.metadata && typeof output.metadata === "object") {
    const meta = output.metadata;
    if (meta.structuredContent && typeof meta.structuredContent === "object") return meta;
    if (meta.task && typeof meta.task === "object") return meta;
    if (meta.task_id) return meta;
  }
  const parsed = parseJson(output.output);
  return parsed && typeof parsed === "object" ? parsed : null;
}

function toolSucceeded(output) {
  if (!output) return false;
  if (output.metadata && output.metadata.isError) return false;
  const text = String(output.output ?? "").trim();
  if (!text) return true;
  if (/^(error|authentication failed|invalid lease|not found)\b/i.test(text) &&
      !text.startsWith("{")) return false;
  return true;
}

async function loadState(root) {
  const file = join(root, STATE_FILE);
  try {
    const parsed = JSON.parse(await readFile(file, "utf8"));
    if (parsed && Array.isArray(parsed.tasks)) return parsed;
  } catch {}
  return { version: 1, tasks: [] };
}

async function saveState(root, state) {
  const file = join(root, STATE_FILE);
  await mkdir(dirname(file), { recursive: true, mode: 0o755 });
  const temp = `${file}.new-${process.pid}`;
  await writeFile(temp, `${JSON.stringify(state, null, 2)}\n`, { encoding: "utf8", mode: 0o600 });
  await rename(temp, file);
}

function entryKey(sessionID, taskId) {
  return `${sessionID}:${taskId}`;
}

function findEntry(state, sessionID, taskId) {
  return state.tasks.find(item => item.sessionID === sessionID && item.taskId === taskId) || null;
}

async function notify(client, message, variant = "info") {
  try {
    if (client?.tui?.showToast) {
      await client.tui.showToast({ body: { title: "Plan2001 publication", message, variant } });
    }
  } catch {}
}

async function log(client, level, message, extra) {
  try {
    if (client?.app?.log) {
      await client.app.log({ body: { service: "plan2001-worker-publish", level, message, extra } });
    }
  } catch {}
}

function publisherArgs(entry, status, fallbackStatus) {
  const report = entry.report || {};
  const result = report.status || fallbackStatus || status;
  const summary = String(report.summary || entry.summary || "").slice(0, 4000);
  const args = [
    PUBLISHER,
    "--task-id", entry.taskId,
    "--result", result,
    "--note", summary || `automatic publication at ${now()}`,
    "--build-status", "reported",
    "--test-status", report.test_results ? "reported" : "not-run",
  ];
  if (report.test_results !== undefined) {
    try {
      args.push("--test-results", JSON.stringify(report.test_results));
    } catch {}
  }
  return { args, result };
}

export const Plan2001WorkerPublish = async ({ client, directory }) => {
  if (ACTIVE.has(directory)) return {};
  const publisher = join(directory, PUBLISHER);
  if (!(await exists(publisher))) return {};
  ACTIVE.add(directory);

  const state = await loadState(directory);
  let chain = Promise.resolve();

  function serialize(task) {
    const next = chain.then(task, task);
    chain = next.catch(() => {});
    return next;
  }

  async function mark(entry, published, error, snapshotId) {
    entry.published = Boolean(published);
    entry.publishing = false;
    entry.attempts = (entry.attempts || 0) + 1;
    entry.updatedAt = now();
    if (published) {
      entry.publishedAt = entry.updatedAt;
      entry.lastError = "";
      entry.snapshotId = snapshotId || entry.snapshotId || "";
    } else if (error) {
      entry.lastError = String(error).slice(0, 2000);
    }
    await saveState(directory, { ...state, tasks: state.tasks.filter(item => !item.published || item.keep) });
  }

  async function publishEntry(entry, fallbackStatus) {
    if (entry.published || entry.publishing) return;
    entry.publishing = true;
    const { args, result } = publisherArgs(entry, fallbackStatus, fallbackStatus);
    await log(client, "info", `automatic source publication for task ${entry.taskId}`, { result });
    let lastError = null;
    for (let attempt = 1; attempt <= 3; attempt++) {
      try {
        const output = await new Promise((resolve, reject) => {
          execFile("python3", args, {
            cwd: directory,
            timeout: 35 * 60 * 1000,
            maxBuffer: 16 * 1024 * 1024,
            env: { ...process.env },
          }, (error, stdout, stderr) => {
            if (error) {
              reject(new Error(String(stderr || stdout || error.message).slice(-2000)));
            } else {
              resolve(String(stdout || ""));
            }
          });
        });
        const line = output.trim().split("\n").filter(Boolean).pop() || "";
        const snapshot = /\b([0-9a-f]{64})\b/.exec(line)?.[1] || "";
        await mark(entry, true, null, snapshot);
        await notify(client, `Published task ${entry.taskId} (${result})${snapshot ? `: ${snapshot.slice(0, 12)}` : ""}`,
                     result === "completed" ? "success" : "warning");
        return;
      } catch (error) {
        lastError = error;
        if (attempt < 3) await sleep(5000 * attempt);
      }
    }
    await mark(entry, false, lastError?.message || "publisher failed");
    await notify(client, `Automatic publication failed for ${entry.taskId}: ${lastError?.message || "unknown error"}`,
                 "error");
  }

  async function publishSession(sessionID, fallbackStatus) {
    for (const entry of state.tasks) {
      if (entry.sessionID !== sessionID) continue;
      if (entry.published) continue;
      await publishEntry(entry, entry.report?.status || fallbackStatus);
    }
  }

  async function flushOtherSessions(exceptSessionID) {
    for (const entry of [...state.tasks]) {
      if (entry.published || entry.sessionID === exceptSessionID) continue;
      await publishEntry(entry, entry.report?.status || "incomplete");
    }
  }

  async function registerClaim(sessionID, task, agent) {
    if (!task?.task_id) return;
    let entry = findEntry(state, sessionID, task.task_id);
    if (!entry) {
      entry = {
        sessionID,
        taskId: task.task_id,
        agent: agent || task.agent || "opencode",
        title: String(task.title || "").slice(0, 200),
        claimedAt: now(),
        updatedAt: now(),
        report: null,
        published: false,
        publishing: false,
        attempts: 0,
        lastError: "",
        snapshotId: "",
      };
      state.tasks.push(entry);
      await saveState(directory, state);
      await log(client, "info", `tracking claimed task ${entry.taskId}`, { sessionID });
    }
    return entry;
  }

  async function registerReport(sessionID, args, output) {
    const taskId = args?.task_id;
    if (!taskId || !["completed", "failed"].includes(args.status)) {
      return { ok: false, error: new Error("invalid task_report arguments") };
    }
    if (!toolSucceeded(output)) {
      return { ok: false, error: new Error("task_report did not succeed") };
    }
    const parsed = toolResult(output);
    if (parsed && parsed.task_id && parsed.task_id !== taskId) {
      return { ok: false, error: new Error("task_report response does not match the task") };
    }
    let entry = findEntry(state, sessionID, taskId);
    if (!entry) {
      entry = await registerClaim(sessionID, { task_id: taskId, title: args.title }, args.agent);
    }
    if (!entry) return { ok: false, error: new Error("task_report could not be tracked") };
    entry.report = {
      status: args.status,
      summary: String(args.summary || "").slice(0, 8000),
      test_results: args.test_results,
      artifact_refs: args.artifact_refs,
    };
    entry.summary = entry.report.summary;
    entry.updatedAt = now();
    await saveState(directory, state);
    return { ok: true, entry };
  }

  return {
    "tool.execute.after": async (input, output) => {
      const match = TOOL.exec(String(input.tool || ""));
      if (!match) return;
      if (match[1] === "claim") {
        if (!toolSucceeded(output)) return;
        const parsed = toolResult(output);
        const task = parsed?.task || parsed;
        await serialize(async () => {
          await registerClaim(input.sessionID, task, input.args?.agent);
        });
        return;
      }
      await serialize(async () => {
        const result = await registerReport(input.sessionID, input.args || {}, output);
        if (result.ok) await publishEntry(result.entry, result.entry.report?.status);
      });
    },
    event: async ({ event }) => {
      if (event.type === "session.created" && !event.properties?.info?.parentID) {
        await serialize(() => flushOtherSessions(event.properties.info.id));
        return;
      }
      if (event.type === "command.executed") {
        const name = String(event.properties?.name || "");
        if (["new", "session.new", "session_new", "session-new"].includes(name)) {
          await serialize(() => flushOtherSessions(""));
        }
        return;
      }
      if (event.type === "session.idle" || event.type === "session.error" ||
          event.type === "session.deleted") {
        const id = event.properties?.sessionID || event.properties?.info?.id;
        if (!id) return;
        const fallback = event.type === "session.idle" ? "incomplete" : "failed";
        await serialize(() => publishSession(id, fallback));
      }
    },
  };
};

#!/usr/bin/env python3
"""Small single-owner OAuth authorization server for Plan2001's MCP queue.

The OAuth flow is authorization-code + PKCE (S256), with dynamically
registered public clients and opaque, server-side stored bearer/refresh tokens.
The browser password is never sent to ChatGPT or included in an MCP tool call.
"""
import base64
import hashlib
import html
import json
import os
import re
import secrets
import sqlite3
import threading
import time
from http.cookies import SimpleCookie
from urllib.parse import parse_qs, urlencode, urlsplit, urlunsplit

ISSUER = "https://plan2001.com"
RESOURCE = "https://plan2001.com/agent-mcp"
PRM = ISSUER + "/.well-known/oauth-protected-resource"
SCOPES = ("queue:submit", "queue:read", "queue:worker")
SCOPE_LABELS = {
    "queue:submit": "Lähetä tehtäviä OpenCode-jonoon",
    "queue:read": "Tarkista tehtävän tila",
    "queue:worker": "Varaa tehtävä ja raportoi sen tulos",
}
TOOL_SCOPES = {
    "task_submit": "queue:submit",
    "task_status": "queue:read",
    "task_claim": "queue:worker",
    "task_report": "queue:worker",
    "task_cancel": "queue:worker",
    "task_clear": "queue:worker",
    "task_queue_status": "queue:read",
}
CONFIG = "/var/lib/plan2001-mcp/oauth-private.json"
DB = "/var/lib/plan2001-mcp/agent-queue/oauth.sqlite3"
_failures = {}
_failures_lock = threading.Lock()


def _auth_event(event):
    # Fixed event labels only; never log passwords, cookies, OAuth codes, or tokens.
    print("plan2001-oauth: " + event, flush=True)


def _db():
    os.makedirs(os.path.dirname(DB), mode=0o750, exist_ok=True)
    c = sqlite3.connect(DB, timeout=10)
    c.row_factory = sqlite3.Row
    c.execute("PRAGMA journal_mode=WAL")
    c.execute("PRAGMA busy_timeout=10000")
    c.executescript("""
      CREATE TABLE IF NOT EXISTS clients (
        client_id TEXT PRIMARY KEY, client_name TEXT NOT NULL,
        redirect_uris TEXT NOT NULL, created_at INTEGER NOT NULL
      );
      CREATE TABLE IF NOT EXISTS pending (
        flow_id TEXT PRIMARY KEY, csrf TEXT NOT NULL, state TEXT NOT NULL,
        client_id TEXT NOT NULL, redirect_uri TEXT NOT NULL,
        resource TEXT NOT NULL, scope TEXT NOT NULL,
        code_challenge TEXT NOT NULL, created_at INTEGER NOT NULL
      );
      CREATE TABLE IF NOT EXISTS completed_flows (
        flow_id TEXT PRIMARY KEY, csrf_hash TEXT NOT NULL,
        redirect_uri TEXT NOT NULL, state TEXT NOT NULL,
        code TEXT NOT NULL, code_hash TEXT NOT NULL,
        expires_at INTEGER NOT NULL
      );
      CREATE TABLE IF NOT EXISTS codes (
        code_hash TEXT PRIMARY KEY, client_id TEXT NOT NULL,
        redirect_uri TEXT NOT NULL, resource TEXT NOT NULL,
        scope TEXT NOT NULL, code_challenge TEXT NOT NULL,
        expires_at INTEGER NOT NULL
      );
      CREATE TABLE IF NOT EXISTS access_tokens (
        token_hash TEXT PRIMARY KEY, client_id TEXT NOT NULL,
        resource TEXT NOT NULL, scope TEXT NOT NULL,
        issued_at INTEGER NOT NULL, expires_at INTEGER NOT NULL
      );
      CREATE TABLE IF NOT EXISTS refresh_tokens (
        token_hash TEXT PRIMARY KEY, client_id TEXT NOT NULL,
        resource TEXT NOT NULL, scope TEXT NOT NULL,
        expires_at INTEGER NOT NULL
      );
    """)
    os.chmod(DB, 0o640)
    return c


def _digest(value):
    return hashlib.sha256(value.encode("ascii")).hexdigest()


def _json(handler, status, body, headers=None):
    raw = json.dumps(body, separators=(",", ":")).encode()
    handler.send_response(status)
    handler.send_header("Content-Type", "application/json; charset=utf-8")
    handler.send_header("Content-Length", str(len(raw)))
    handler.send_header("Cache-Control", "no-store")
    handler.send_header("Pragma", "no-cache")
    for name, value in (headers or {}).items():
        handler.send_header(name, value)
    handler.end_headers()
    handler.wfile.write(raw)


def _javascript(handler, body):
    raw = body.encode()
    handler.send_response(200)
    handler.send_header("Content-Type", "application/javascript; charset=utf-8")
    handler.send_header("Content-Length", str(len(raw)))
    handler.send_header("Cache-Control", "no-store")
    handler.send_header("X-Content-Type-Options", "nosniff")
    handler.end_headers()
    handler.wfile.write(raw)


def _html(handler, status, body, cookie=None):
    raw = body.encode()
    handler.send_response(status)
    handler.send_header("Content-Type", "text/html; charset=utf-8")
    handler.send_header("Content-Length", str(len(raw)))
    handler.send_header("Cache-Control", "no-store")
    handler.send_header("X-Content-Type-Options", "nosniff")
    handler.send_header("X-Frame-Options", "DENY")
    # Do not set form-action here: ChatGPT may render the remote login page in
    # an embedded origin, where browsers incorrectly treat the fixed endpoint
    # as cross-origin despite its explicit host source. The form target is
    # server-fixed and guarded by a per-flow HttpOnly CSRF cookie.
    handler.send_header("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; script-src 'self' https://plan2001.com; base-uri 'none'; frame-ancestors 'none'")
    if cookie:
        handler.send_header("Set-Cookie", cookie)
    handler.end_headers()
    handler.wfile.write(raw)


def _page(title, content):
    return """<!doctype html><html lang="fi"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>""" + html.escape(title) + """ — Plan2001</title>
<style>body{font:16px/1.6 system-ui,sans-serif;background:#0b1117;color:#e9f0f4;margin:0;padding:7vh 18px}
main{max-width:560px;margin:auto;background:#111b24;border:1px solid #263744;border-radius:16px;padding:26px}
h1{font-size:1.6rem}p,li{color:#b5c4ce}label{display:block;margin:18px 0 6px}input{box-sizing:border-box;width:100%;padding:12px;border-radius:8px;border:1px solid #52636e;background:#0b1117;color:#fff;font:inherit}
button{margin-top:20px;padding:12px 18px;border:0;border-radius:8px;background:#70e0bd;color:#07130f;font:inherit;font-weight:700;cursor:pointer}button:disabled{opacity:.75;cursor:wait}
.error{color:#ffb3aa}.scope{border-top:1px solid #263744;padding:10px 0}</style>
<main><p>Plan2001.com · agent-mcp</p><h1>""" + html.escape(title) + """</h1>""" + content + "</main></html>"


def _valid_redirect(uri):
    try:
        p = urlsplit(uri)
        if p.username or p.password or p.query or p.fragment or not p.path.startswith("/"):
            return False
        if p.scheme == "https" and p.hostname == "chatgpt.com" and p.port is None:
            return (p.path == "/connector_platform_oauth_redirect" or
                    re.fullmatch(r"/connector/oauth/[A-Za-z0-9_-]+", p.path) is not None)
        # Native OpenCode clients receive the response on a temporary
        # loopback listener. Permit only literal local hosts and unprivileged
        # ports; never accept arbitrary cleartext redirect hosts.
        if p.scheme == "http" and p.hostname in ("localhost", "127.0.0.1", "::1"):
            return p.port is not None and 1024 <= p.port <= 65535
        return False
    except ValueError:
        return False


def _form_body(handler):
    length = int(handler.headers.get("Content-Length", "0"))
    if length > 65536:
        raise ValueError("request too large")
    if "application/x-www-form-urlencoded" not in handler.headers.get("Content-Type", ""):
        raise ValueError("form encoded body required")
    raw = handler.rfile.read(length).decode("utf-8", "strict")
    return {k: v[-1] for k, v in parse_qs(raw, keep_blank_values=True).items()}


def _oauth_error(handler, error, description, status=400):
    _json(handler, status, {"error": error, "error_description": description})


def _oauth_metadata():
    return {
        "issuer": ISSUER,
        "authorization_endpoint": ISSUER + "/oauth/authorize",
        "token_endpoint": ISSUER + "/oauth/token",
        "registration_endpoint": ISSUER + "/oauth/register",
        "revocation_endpoint": ISSUER + "/oauth/revoke",
        "response_types_supported": ["code"],
        "grant_types_supported": ["authorization_code", "refresh_token"],
        "token_endpoint_auth_methods_supported": ["none"],
        "revocation_endpoint_auth_methods_supported": ["none"],
        "code_challenge_methods_supported": ["S256"],
        "authorization_response_iss_parameter_supported": True,
        "client_id_metadata_document_supported": False,
        "scopes_supported": list(SCOPES),
    }


def _resource_metadata():
    return {"resource": RESOURCE, "authorization_servers": [ISSUER],
            "scopes_supported": list(SCOPES),
            "resource_documentation": "https://plan2001.com/mcp.html#agenttijono"}


def _auth_challenge(scope, error=None):
    parts = [f'resource_metadata="{PRM}"', f'scope="{scope}"']
    if error:
        parts.append(f'error="{error}"')
    return "Bearer " + ", ".join(parts)


def handle_get(handler):
    path = handler.path.split("?", 1)[0]
    if path == "/oauth/submit.js":
        _javascript(handler, "document.addEventListener('DOMContentLoaded',()=>{const f=document.querySelector('form');const b=document.getElementById('oauth-submit');const s=document.getElementById('oauth-progress');if(f&&b)f.addEventListener('submit',()=>{b.disabled=true;b.textContent='Kirjaudutaan…';if(s)s.textContent='Lähetys vastaanotettu. Odota, että selain palaa ChatGPT:hen.';});});")
        return True
    if path == "/.well-known/oauth-protected-resource":
        _json(handler, 200, _resource_metadata())
        return True
    if path == "/.well-known/oauth-authorization-server":
        _json(handler, 200, _oauth_metadata())
        return True
    if path != "/oauth/authorize":
        return False
    query = parse_qs(urlsplit(handler.path).query)
    one = lambda key: query.get(key, [""])[-1]
    client_id, redirect_uri = one("client_id"), one("redirect_uri")
    state, resource, scope = one("state"), one("resource"), one("scope")
    challenge, method = one("code_challenge"), one("code_challenge_method")
    conn = _db()
    client = conn.execute("SELECT * FROM clients WHERE client_id=?", (client_id,)).fetchone()
    conn.close()
    scopes = scope.split()
    valid = (one("response_type") == "code" and bool(state) and len(state) <= 1024 and
             resource == RESOURCE and method == "S256" and
             re.fullmatch(r"[A-Za-z0-9_-]{43,128}", challenge) is not None and
             client is not None and _valid_redirect(redirect_uri) and
             redirect_uri in json.loads(client["redirect_uris"]) and
             bool(scopes) and len(set(scopes)) == len(scopes) and set(scopes) <= set(SCOPES))
    if not valid:
        _html(handler, 400, _page("OAuth-pyyntö ei kelpaa", "<p>Tarkista plugin-yhteys ja yritä uudelleen.</p>"))
        return True
    flow_id, csrf = secrets.token_urlsafe(32), secrets.token_urlsafe(32)
    conn = _db()
    conn.execute("DELETE FROM pending WHERE created_at < ?", (int(time.time()) - 600,))
    conn.execute("DELETE FROM completed_flows WHERE expires_at < ?", (int(time.time()),))
    conn.execute("INSERT INTO pending VALUES(?,?,?,?,?,?,?,?,?)",
                 (flow_id, csrf, state, client_id, redirect_uri, resource,
                  " ".join(scopes), challenge, int(time.time())))
    conn.commit(); conn.close()
    names = "".join("<li class=\"scope\">" + html.escape(SCOPE_LABELS[s]) + "</li>" for s in scopes)
    content = ("<p>ChatGPT-pyyntö pyytää oikeuksia tähän agenttijonoon:</p><ul>" + names +
               "</ul><p>Kirjaudu plan2001.com-tilin salasanalla. Salasanaa ei välitetä ChatGPT:lle.</p>"
               "<form method=post action=https://plan2001.com/oauth/authorize>"
               f"<input type=hidden name=flow_id value=\"{html.escape(flow_id)}\">"
               f"<input type=hidden name=csrf value=\"{html.escape(csrf)}\">"
               "<label for=password>Plan2001 OAuth-salasana</label>"
               "<input id=password name=password type=password autocomplete=current-password required autofocus>"
               "<label><input type=checkbox name=approve value=yes required style='width:auto'> Hyväksyn yllä luetellut oikeudet</label>"
               "<p id=oauth-progress aria-live=polite>Hyväksy kerran ja odota paluuta ChatGPT:hen.</p>"
               "<button id=oauth-submit type=submit>Kirjaudu ja salli</button></form>"
               "<script src=https://plan2001.com/oauth/submit.js defer></script>")
    cookie_name = "plan2001_flow_" + flow_id
    cookie = f"{cookie_name}={csrf}; Path=/oauth/; Max-Age=600; Secure; HttpOnly; SameSite=Lax"
    _html(handler, 200, _page("Yhdistä ChatGPT agenttijonoon", content), cookie)
    return True


def _client_register(handler, body):
    try:
        data = json.loads(body)
        redirects = data["redirect_uris"]
        if not isinstance(redirects, list) or not 1 <= len(redirects) <= 8:
            raise ValueError
        if any(not isinstance(uri, str) or not _valid_redirect(uri) for uri in redirects):
            raise ValueError
        if data.get("response_types", ["code"]) != ["code"]:
            raise ValueError
        if data.get("grant_types", ["authorization_code"]) not in (["authorization_code"], ["authorization_code", "refresh_token"]):
            raise ValueError
        if data.get("token_endpoint_auth_method", "none") != "none":
            raise ValueError
        name = str(data.get("client_name", "ChatGPT MCP client"))[:120]
    except Exception:
        _oauth_error(handler, "invalid_client_metadata", "Only the ChatGPT OAuth authorization-code client is accepted.")
        return
    client_id = "p2001_" + secrets.token_urlsafe(24)
    conn = _db()
    count = conn.execute("SELECT COUNT(*) FROM clients").fetchone()[0]
    if count >= 500:
        conn.close(); _oauth_error(handler, "invalid_client_metadata", "Client registration limit reached.", 429); return
    conn.execute("INSERT INTO clients VALUES(?,?,?,?)",
                 (client_id, name, json.dumps(redirects), int(time.time())))
    conn.commit(); conn.close()
    _json(handler, 201, {"client_id": client_id, "client_id_issued_at": int(time.time()),
                         "client_name": name, "redirect_uris": redirects,
                         "response_types": ["code"],
                         "grant_types": ["authorization_code", "refresh_token"],
                         "token_endpoint_auth_method": "none"})


def _oauth_post(handler):
    path = handler.path.split("?", 1)[0]
    length = int(handler.headers.get("Content-Length", "0"))
    if length > 65536:
        _oauth_error(handler, "invalid_request", "request too large", 413); return True
    body = handler.rfile.read(length)
    if path == "/oauth/register":
        _client_register(handler, body); return True
    if path == "/oauth/token":
        try:
            if "application/x-www-form-urlencoded" not in handler.headers.get("Content-Type", ""):
                raise ValueError
            d = {k: v[-1] for k, v in parse_qs(body.decode(), keep_blank_values=True).items()}
        except Exception:
            _oauth_error(handler, "invalid_request", "form encoded body required"); return True
        now = int(time.time())
        if d.get("grant_type") == "authorization_code":
            code, client_id, redirect_uri = d.get("code", ""), d.get("client_id", ""), d.get("redirect_uri", "")
            verifier = d.get("code_verifier", "")
            if not code or not client_id or not _valid_redirect(redirect_uri) or not re.fullmatch(r"[A-Za-z0-9._~-]{43,128}", verifier):
                _oauth_error(handler, "invalid_grant", "authorization code or PKCE verifier is invalid"); return True
            conn = _db()
            conn.execute("BEGIN IMMEDIATE")
            row = conn.execute("SELECT * FROM codes WHERE code_hash=?", (_digest(code),)).fetchone()
            conn.execute("DELETE FROM codes WHERE code_hash=?", (_digest(code),))
            valid = row is not None and row["expires_at"] >= now and row["client_id"] == client_id and row["redirect_uri"] == redirect_uri
            if valid:
                actual = base64.urlsafe_b64encode(hashlib.sha256(verifier.encode("ascii")).digest()).rstrip(b"=").decode()
                valid = secrets.compare_digest(actual, row["code_challenge"]) and row["resource"] == RESOURCE and d.get("resource") == row["resource"]
            if not valid:
                conn.execute("DELETE FROM completed_flows WHERE code_hash=?", (_digest(code),))
                conn.commit(); conn.close(); _oauth_error(handler, "invalid_grant", "authorization code is expired, already used, or does not match this client/resource"); return True
            client = conn.execute("SELECT 1 FROM clients WHERE client_id=?", (client_id,)).fetchone()
            if client is None:
                conn.commit(); conn.close(); _oauth_error(handler, "invalid_grant", "OAuth client is no longer registered"); return True
            token, refresh = secrets.token_urlsafe(48), secrets.token_urlsafe(48)
            conn.execute("INSERT INTO access_tokens VALUES(?,?,?,?,?,?)",
                         (_digest(token), client_id, row["resource"], row["scope"], now, now + 3600))
            conn.execute("INSERT INTO refresh_tokens VALUES(?,?,?,?,?)",
                         (_digest(refresh), client_id, row["resource"], row["scope"], now + 30 * 86400))
            conn.execute("DELETE FROM completed_flows WHERE code_hash=?", (_digest(code),))
            conn.commit(); conn.close()
            _json(handler, 200, {"access_token": token, "token_type": "Bearer", "expires_in": 3600,
                                 "refresh_token": refresh, "scope": row["scope"]})
            return True
        if d.get("grant_type") == "refresh_token":
            refresh = d.get("refresh_token", "")
            client_id = d.get("client_id", "")
            conn = _db()
            conn.execute("BEGIN IMMEDIATE")
            row = conn.execute("SELECT * FROM refresh_tokens WHERE token_hash=?", (_digest(refresh),)).fetchone()
            client = conn.execute("SELECT 1 FROM clients WHERE client_id=?", (client_id,)).fetchone()
            if row is None or client is None or row["client_id"] != client_id or row["expires_at"] < now or row["resource"] != d.get("resource"):
                conn.rollback(); conn.close(); _oauth_error(handler, "invalid_grant", "refresh token is expired, revoked, or invalid"); return True
            conn.execute("DELETE FROM refresh_tokens WHERE token_hash=?", (_digest(refresh),))
            token, new_refresh = secrets.token_urlsafe(48), secrets.token_urlsafe(48)
            conn.execute("INSERT INTO access_tokens VALUES(?,?,?,?,?,?)",
                         (_digest(token), client_id, row["resource"], row["scope"], now, now + 3600))
            conn.execute("INSERT INTO refresh_tokens VALUES(?,?,?,?,?)",
                         (_digest(new_refresh), client_id, row["resource"], row["scope"], row["expires_at"]))
            conn.commit(); conn.close()
            _json(handler, 200, {"access_token": token, "token_type": "Bearer", "expires_in": 3600,
                                 "refresh_token": new_refresh, "scope": row["scope"]})
            return True
        _oauth_error(handler, "unsupported_grant_type", "Only authorization_code and refresh_token are supported.")
        return True
    if path == "/oauth/revoke":
        try:
            if "application/x-www-form-urlencoded" not in handler.headers.get("Content-Type", ""):
                raise ValueError
            d = {k: v[-1] for k, v in parse_qs(body.decode(), keep_blank_values=True).items()}
            token = d.get("token", "")
            conn = _db()
            conn.execute("DELETE FROM access_tokens WHERE token_hash=?", (_digest(token),))
            conn.execute("DELETE FROM refresh_tokens WHERE token_hash=?", (_digest(token),))
            conn.commit(); conn.close()
            _json(handler, 200, {})
        except Exception:
            _oauth_error(handler, "invalid_request", "invalid revocation request")
        return True
    return False


def _redirect_with_code(handler, code, state, redirect_uri):
    params = {"code": code, "state": state, "iss": ISSUER}
    dest = urlsplit(redirect_uri)
    query = dest.query + ("&" if dest.query else "") + urlencode(params)
    # A 303 explicitly turns the browser's form POST into a GET to the
    # registered ChatGPT callback; this avoids embedded-browser POST retries.
    handler.send_response(303)
    handler.send_header("Location", urlunsplit((dest.scheme, dest.netloc, dest.path, query, "")))
    handler.send_header("Cache-Control", "no-store")
    handler.send_header("Content-Length", "0")
    handler.end_headers()


def handle_authorize_post(handler):
    try:
        data = _form_body(handler)
        cookie = SimpleCookie(handler.headers.get("Cookie", ""))
        flow_id = data.get("flow_id", "")
        if not re.fullmatch(r"[A-Za-z0-9_-]{40,64}", flow_id):
            _auth_event("invalid-flow-id")
            raise ValueError("authorization session expired; start again from ChatGPT")
        cookie_name = "plan2001_flow_" + flow_id
        flow_cookie = cookie.get(cookie_name)
        if not flow_cookie:
            _auth_event("flow-cookie-missing")
            raise ValueError("authorization session expired; start again from ChatGPT")
        if not secrets.compare_digest(flow_cookie.value, data.get("csrf", "")):
            _auth_event("flow-cookie-mismatch")
            raise ValueError("authorization session expired; start again from ChatGPT")
        conn = _db()
        completed = conn.execute("SELECT * FROM completed_flows WHERE flow_id=?", (flow_id,)).fetchone()
        if completed is not None:
            if completed["expires_at"] >= int(time.time()) and secrets.compare_digest(completed["csrf_hash"], _digest(data.get("csrf", ""))):
                conn.close()
                _redirect_with_code(handler, completed["code"], completed["state"], completed["redirect_uri"])
                _auth_event("authorization-code-replayed")
                return True
            conn.execute("DELETE FROM completed_flows WHERE flow_id=?", (flow_id,))
            conn.commit()
        row = conn.execute("SELECT * FROM pending WHERE flow_id=?", (flow_id,)).fetchone()
        if row is None:
            conn.close(); _auth_event("pending-flow-missing")
            raise ValueError("authorization session expired; start again from ChatGPT")
        if row["created_at"] < int(time.time()) - 600:
            conn.close(); _auth_event("pending-flow-expired")
            raise ValueError("authorization session expired; start again from ChatGPT")
        if not secrets.compare_digest(row["csrf"], data.get("csrf", "")):
            conn.close(); _auth_event("csrf-mismatch")
            raise ValueError("authorization session expired; start again from ChatGPT")
        if data.get("approve") != "yes":
            conn.close(); raise ValueError("authorization was not approved")
        client_ip = handler.headers.get("X-Real-IP", handler.client_address[0])
        if not _password_matches(data.get("password", ""), client_ip):
            conn.close()
            content = "<p class=error>Salasana ei kelpaa tai kirjautuminen on hetkeksi lukittu. Yritä myöhemmin uudelleen.</p>"
            _auth_event("password-rejected")
            _html(handler, 401, _page("Kirjautuminen epäonnistui", content + "<p>Palaa ChatGPT:hen ja aloita OAuth-yhdistäminen uudelleen.</p>"),
                  f"{cookie_name}=; Path=/oauth/; Max-Age=0; Secure; HttpOnly; SameSite=Lax")
            return True
        conn.execute("BEGIN IMMEDIATE")
        row = conn.execute("SELECT * FROM pending WHERE flow_id=?", (flow_id,)).fetchone()
        if row is None or row["created_at"] < int(time.time()) - 600:
            completed = conn.execute("SELECT * FROM completed_flows WHERE flow_id=?", (flow_id,)).fetchone()
            if completed is not None and completed["expires_at"] >= int(time.time()) and secrets.compare_digest(completed["csrf_hash"], _digest(data.get("csrf", ""))):
                conn.commit(); conn.close()
                _redirect_with_code(handler, completed["code"], completed["state"], completed["redirect_uri"])
                _auth_event("authorization-code-replayed")
                return True
            conn.rollback(); conn.close(); raise ValueError("authorization session expired; start again from ChatGPT")
        now = int(time.time())
        code = secrets.token_urlsafe(40)
        conn.execute("DELETE FROM pending WHERE flow_id=?", (flow_id,))
        conn.execute("INSERT INTO codes VALUES(?,?,?,?,?,?,?)",
                     (_digest(code), row["client_id"], row["redirect_uri"], row["resource"], row["scope"], row["code_challenge"], now + 120))
        conn.execute("INSERT INTO completed_flows VALUES(?,?,?,?,?,?,?)",
                     (flow_id, _digest(row["csrf"]), row["redirect_uri"], row["state"], code, _digest(code), now + 120))
        conn.commit(); conn.close()
        _redirect_with_code(handler, code, row["state"], row["redirect_uri"])
        _auth_event("authorization-code-issued")
    except ValueError as exc:
        if str(exc) == "authorization was not approved":
            _auth_event("consent-denied")
        _html(handler, 400, _page("OAuth-kirjautumista ei voitu jatkaa", "<p>" + html.escape(str(exc)) + "</p><p>Aloita yhteyden muodostaminen uudelleen ChatGPT:ssä.</p>"))
    return True


def _password_matches(password, ip):
    now = int(time.time())
    with _failures_lock:
        recent = [t for t in _failures.get(ip, []) if t > now - 900]
        _failures[ip] = recent
        if len(recent) >= 5:
            return False
    try:
        config = json.loads(open(CONFIG, "r", encoding="utf-8").read())
        salt = bytes.fromhex(config["salt"])
        expected = bytes.fromhex(config["scrypt_hash"])
        actual = hashlib.scrypt(password.encode("utf-8"), salt=salt, n=2**14, r=8, p=1, dklen=64)
        matched = secrets.compare_digest(actual, expected)
    except Exception:
        matched = False
    with _failures_lock:
        if matched:
            _failures.pop(ip, None)
        else:
            _failures.setdefault(ip, []).append(now)
    return matched


def handle_post(handler):
    if handler.path.split("?", 1)[0] == "/oauth/authorize":
        return handle_authorize_post(handler)
    if handler.path.split("?", 1)[0] not in ("/oauth/register", "/oauth/token", "/oauth/revoke"):
        return False
    try:
        length = int(handler.headers.get("Content-Length", "0"))
        if length > 65536:
            _oauth_error(handler, "invalid_request", "request too large", 413); return True
    except ValueError:
        _oauth_error(handler, "invalid_request", "invalid content length"); return True
    return _oauth_post(handler)


def verify_access_token(token):
    if not token or len(token) > 256:
        return None
    conn = _db()
    row = conn.execute("SELECT * FROM access_tokens WHERE token_hash=?", (_digest(token),)).fetchone()
    if row is None or row["expires_at"] < int(time.time()) or row["resource"] != RESOURCE:
        conn.close(); return None
    conn.close()
    return {"client_id": row["client_id"], "resource": row["resource"], "scopes": set(row["scope"].split())}


def bearer(handler):
    value = handler.headers.get("Authorization", "")
    if not value.startswith("Bearer "):
        return None
    return verify_access_token(value[7:].strip())


def fail_auth(handler, rid, scope, insufficient=False):
    error = "insufficient_scope" if insufficient else "invalid_token"
    challenge = _auth_challenge(scope, error if insufficient else None)
    body = {"jsonrpc": "2.0", "id": rid,
            "result": {"content": [{"type": "text", "text": "OAuth authorization required. Link Plan2001 in ChatGPT and approve the requested access."}],
                       "isError": True, "_meta": {"mcp/www_authenticate": [challenge]}}}
    _json(handler, 403 if insufficient else 401, body, {"WWW-Authenticate": challenge})


def scope_for_tool(name):
    return TOOL_SCOPES.get(name)


def inject_queue_credentials(name, arguments, config):
    args = dict(arguments)
    # Never trust model-supplied credential fields when an OAuth token is used.
    for key in ("write_key", "read_key", "worker_key"):
        args.pop(key, None)
    if name in ("task_submit", "task_clear"):
        args["write_key"] = config["write_key"]
    elif name in ("task_status", "task_queue_status"):
        args["read_key"] = config["write_key"]
    elif name in ("task_claim", "task_report", "task_cancel"):
        args["worker_key"] = config["worker_key"]
    return args


def with_security_schemes(tools):
    scoped = []
    for tool in tools:
        item = dict(tool)
        item["_meta"] = {"securitySchemes": [{"type": "oauth2", "scopes": [TOOL_SCOPES[item["name"]]]}]}
        scoped.append(item)
    return scoped

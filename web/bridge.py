"""HTTP-to-TCP bridge for the Timetable Inquiry System.

Exposes a small HTTP API on port 50002 so browser-based UIs (which cannot
speak raw TCP) can talk to the C++ server on port 50000. The bridge is a
transparent forwarder and does not parse application-layer semantics. Each
browser session is mapped to one persistent TCP connection, because the
server keeps authentication state per TCP connection.
"""
import collections
import hashlib
import queue
import socket
import threading
import time
import uuid

from flask import Flask, request, jsonify
from flask_cors import CORS

# ---- Configuration ----
SERVER_HOST = "127.0.0.1"
SERVER_PORT = 50000
BRIDGE_PORT = 50002

RECV_MAX_TIMEOUT = 15.0         # absolute max wall-clock wait per command
RECV_IDLE_TIMEOUT = 0.6         # if response started but no bytes for this long, assume done
RECV_INITIAL_TIMEOUT = 8.0      # how long to wait for the FIRST byte of a response
RECV_POLL_TIMEOUT = 0.05        # 50ms poll: lets us check the absolute deadline ~20×/s

# Terminal-line markers, mirrored from client/src/client.cpp:isTerminalLine.
TERMINAL_PREFIXES = ("OK|", "ERROR|", "SUCCESS|", "FAILURE|",
                     "RESULT_NONE|", "WELCOME|",
                     "STATUS_INFO|",   # v2.1: STATUS response
                     "ENC|")           # v2.1: encrypted response (single line)
TERMINAL_EXACT = ("RESULT_END", "BYE")

# ---- Response cache (v2.2) ----
_list_cache: dict = {}       # cmd_upper -> (timestamp, lines)
_LIST_CACHE_TTL = 5.0        # seconds; tune up for production use

# ---- Crypto helpers (v2.1) ----
XOR_KEY = b"DCN2026TimetableKey"


def xor_encrypt(data: bytes, key: bytes = XOR_KEY) -> bytes:
    return bytes(b ^ key[i % len(key)] for i, b in enumerate(data))


def hex_encode(data: bytes) -> str:
    return data.hex()   # Python built-in is lowercase


def hex_decode(s: str) -> bytes:
    try:
        return bytes.fromhex(s)
    except ValueError:
        return b""


def sha256_hex(s: str) -> str:
    return hashlib.sha256(s.encode("utf-8")).hexdigest()


def is_sha256_hex(s: str) -> bool:
    if len(s) != 64:
        return False
    return all(c in "0123456789abcdef" for c in s)


# ---- Session bookkeeping ----
class Session:
    """Per-browser-session state: one TCP socket + dedicated reader thread.

    v2.3 change: the reader thread is the sole owner of `recv` on the session
    socket. It classifies each line and routes it to one of two queues:

      - response_queue : list[str] batches put when a terminal line ends the
                         current command response. Only filled when `_awaiting`
                         is True (i.e. a request is in flight).
      - notify_queue   : deque[str] of `NOTIFY|...` server-pushed lines.
                         Filled regardless of state — they can arrive any time.

    This lets the bridge surface NOTIFY to the browser even when the session is
    idle (no command in flight). The browser polls /api/notifications.

    Idle state machine: if `_awaiting` is False and a non-NOTIFY line arrives,
    it's logged and discarded — this protects against stray bytes corrupting
    the next command's response.

    last_auth_cmd: the most recent successful LOGIN command (with the password
    already SHA-256 hashed). Held so that a transparent socket reconnect can
    replay it and restore the server-side auth state — the C++ server keys
    auth to the TCP connection, so a new socket is a fresh guest unless we
    re-LOGIN. Cleared on LOGOUT or on failed LOGIN.
    """

    def __init__(self, sock):
        self.socket = sock
        self.lock = threading.Lock()                # serializes request handlers
        self.encryption_enabled = False             # v2.1: opt-in ENC| layer
        self.last_auth_cmd = None                   # str | None
        # v2.3 reader plumbing. Session is born "awaiting" — the very first
        # batch the reader delivers is the server's WELCOME banner.
        self.response_queue = queue.Queue()         # list[str] batches
        self.notify_queue = collections.deque()
        self._awaiting = True
        self._awaiting_lock = threading.Lock()
        self._current_batch = []
        self._reader_alive = threading.Event()
        self._reader_alive.set()
        self._reader = threading.Thread(target=self._reader_loop, daemon=True)
        self._reader.start()

    # ---- reader thread ----

    def _reader_loop(self):
        """Sole owner of recv. Routes lines to response_queue / notify_queue."""
        buffer = b""
        sock = self.socket
        sock.settimeout(RECV_POLL_TIMEOUT)
        while self._reader_alive.is_set():
            try:
                chunk = sock.recv(4096)
            except socket.timeout:
                continue
            except OSError as e:
                self._fail_pending(e)
                return
            if not chunk:
                self._fail_pending(ConnectionError("server closed connection"))
                return
            buffer += chunk
            while b"\n" in buffer:
                idx = buffer.index(b"\n")
                line = buffer[:idx].decode("utf-8", errors="replace").rstrip("\r")
                buffer = buffer[idx + 1:]
                self._handle_line(line)

    def _handle_line(self, line: str):
        # 1) NOTIFY: always to notify_queue, regardless of awaiting state.
        if line.startswith("NOTIFY|"):
            self.notify_queue.append(line)
            # Invalidate the global LIST_ALL cache so the next poll sees fresh data.
            _list_cache.clear()
            return
        # 2) Awaiting a command response: accumulate, put batch on terminal.
        if self._awaiting:
            self._current_batch.append(line)
            if is_terminal_line(line) or (line.startswith("INFO|") and "QUIT" in line):
                with self._awaiting_lock:
                    self._awaiting = False
                    batch = self._current_batch
                    self._current_batch = []
                self.response_queue.put(batch)
            return
        # 3) Idle + non-NOTIFY: log and discard so we don't poison the next reply.
        print(f"[bridge] WARN idle reader got unexpected line: {line!r}")

    def _fail_pending(self, exc: BaseException):
        """Reader thread is exiting. Wake up any pending request with an error."""
        self._reader_alive.clear()
        with self._awaiting_lock:
            if self._awaiting:
                self._awaiting = False
                self._current_batch = []
                self.response_queue.put(exc)

    def stop_reader(self):
        self._reader_alive.clear()


sessions = {}                       # session_id -> Session
sessions_lock = threading.Lock()    # guards the sessions dict


def is_terminal_line(line: str) -> bool:
    if line in TERMINAL_EXACT:
        return True
    for prefix in TERMINAL_PREFIXES:
        if line.startswith(prefix):
            return True
    return False


# Note (v2.3): the legacy recv_until_terminal() free function was retired when
# Session gained its dedicated reader thread. All recv work now happens inside
# Session._reader_loop, which classifies each line into response_queue or
# notify_queue. RECV_INITIAL_TIMEOUT / RECV_MAX_TIMEOUT are still honoured at
# the _send_command / _await_initial_batch level.


def _apply_login_hash(cmd: str) -> str:
    """Intercept LOGIN commands and replace plaintext password with SHA-256 hash.

    If the password field is already a 64-char lowercase hex string it is left
    untouched (idempotent: prevents double-hashing).
    """
    if not cmd.upper().startswith("LOGIN|"):
        return cmd
    parts = cmd.split("|")
    if len(parts) < 3:
        return cmd
    password = parts[2]
    if not is_sha256_hex(password):
        password = sha256_hex(password)
    parts[2] = password
    return "|".join(parts[:3])   # protocol uses exactly 3 fields


def _send_command(session: Session, cmd: str):
    """Send one command through the session and return (lines, raw_request, raw_response).

    Handles LOGIN hashing, ENC| wrapping (when encryption_enabled), and
    ENC| response unwrapping transparently.  Must be called while holding
    session.lock.

    v2.3: the actual recv is done by the session's reader thread; this function
    flips the `_awaiting` flag, sends, and waits on session.response_queue. The
    queue may yield either a list[str] (batch of lines) or an Exception (reader
    thread died); the latter is raised here so the caller can trigger a reconnect.

    raw_request / raw_response are the actual bytes sent to / received from the
    TCP server (as utf-8 strings, terminating \n included). Returned so the
    web UI can render a Wire Preview without ever holding the XOR key.
    """
    # 1. Hash LOGIN password before anything else.
    cmd = _apply_login_hash(cmd)

    # 2. Optionally wrap the whole command in ENC|.
    if session.encryption_enabled:
        encrypted = xor_encrypt(cmd.encode("utf-8"))
        payload = "ENC|" + hex_encode(encrypted)
    else:
        payload = cmd

    # 3. Arm the reader thread to capture the next batch.
    with session._awaiting_lock:
        session._awaiting = True
        session._current_batch = []

    # 4. Send.
    raw_request = payload + "\n"
    session.socket.sendall(raw_request.encode("utf-8"))

    # 5. Wait for the reader to deliver a complete batch (or an error).
    try:
        result = session.response_queue.get(timeout=RECV_MAX_TIMEOUT)
    except queue.Empty:
        # Reader is still alive but nothing arrived — disarm.
        with session._awaiting_lock:
            session._awaiting = False
            session._current_batch = []
        raise TimeoutError(f"recv exceeded {RECV_MAX_TIMEOUT}s")

    if isinstance(result, BaseException):
        raise result
    lines = result
    raw_response = "\n".join(lines) + ("\n" if lines else "")

    # 6. If encryption is on, the single response line starts with "ENC|".
    #    Decrypt it and split back into individual lines.
    if session.encryption_enabled and lines and lines[-1].startswith("ENC|"):
        hex_part = lines[-1][4:].strip()
        decrypted = xor_encrypt(hex_decode(hex_part))   # XOR is symmetric
        plaintext = decrypted.decode("utf-8", errors="replace")
        # Re-split the decrypted multi-line payload into individual lines.
        lines = [l.rstrip("\r") for l in plaintext.split("\n") if l.rstrip("\r")]

    # 7. Track auth state so transparent reconnects can restore it.
    upper = cmd.upper()
    first = lines[0] if lines else ""
    if upper.startswith("LOGIN|"):
        # Save on success, clear on failure (so a stale auth doesn't get
        # replayed if the user mistyped a password).
        session.last_auth_cmd = cmd if first.startswith("SUCCESS|") else None
    elif upper == "LOGOUT" and first.startswith("SUCCESS|"):
        session.last_auth_cmd = None

    return lines, raw_request, raw_response


# ---- Flask app ----
app = Flask(__name__)
CORS(app)


def make_response(ok, session_id=None, lines=None, error=None,
                  raw_request=None, raw_response=None, status=200,
                  cache_hit=False):
    payload = {
        "ok": ok,
        "session_id": session_id,
        "lines": lines or [],
        "error": error,
        "raw_request": raw_request,
        "raw_response": raw_response,
        "cache_hit": cache_hit,
    }
    return jsonify(payload), status


def _open_tcp(timeout=3.0):
    """Open a TCP connection to the server with TCP_NODELAY set.

    TCP_NODELAY disables Nagle's algorithm so small interactive command/response
    pairs aren't held back up to 200ms waiting for more bytes to coalesce —
    critical for snappy local UIs.
    """
    sock = socket.create_connection((SERVER_HOST, SERVER_PORT), timeout=timeout)
    try:
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    except OSError:
        pass  # not fatal if the OS rejects it
    return sock


def _await_initial_batch(session: Session, label: str = "WELCOME"):
    """Wait for the first batch the reader delivers (e.g. WELCOME).

    Session is born with `_awaiting=True` precisely so the reader thread routes
    the very first server message into response_queue, even though no client
    code has called `_send_command` yet. This avoids a race where WELCOME could
    arrive on the wire before we've had a chance to "arm" the reader.
    """
    result = session.response_queue.get(timeout=RECV_INITIAL_TIMEOUT)
    if isinstance(result, BaseException):
        raise result
    return result


@app.post("/api/connect")
def api_connect():
    try:
        sock = _open_tcp(timeout=5.0)
    except OSError as e:
        return make_response(False, error=f"Cannot reach TCP server: {e}", status=502)
    session = Session(sock)
    try:
        lines = _await_initial_batch(session, "WELCOME")
    except Exception as e:
        session.stop_reader()
        try:
            sock.close()
        except OSError:
            pass
        return make_response(False, error=f"Failed to read WELCOME: {e}", status=502)
    sid = uuid.uuid4().hex
    with sessions_lock:
        sessions[sid] = session
    print(f"[bridge] session {sid} connected")
    return make_response(True, session_id=sid, lines=lines)


def _reconnect_in_place(session: Session) -> bool:
    """Replace the session's underlying TCP socket without changing its sid.

    The browser's session_id stays valid, encryption_enabled is preserved,
    and — if the user was logged in — we replay the cached LOGIN command on
    the new socket so server-side auth state is restored. After this returns
    True, the session is functionally indistinguishable from before the drop:
    admin-only commands keep working without the user needing to re-login.

    The cached LOGIN already has the SHA-256 hashed password (we never store
    plaintext), so the replay is the same bytes the original LOGIN sent.

    Returns True on success, False if the server is unreachable.
    """
    try:
        new_sock = _open_tcp(timeout=3.0)
    except OSError:
        return False

    # Stop the old reader thread. Closing the old socket unblocks recv().
    session.stop_reader()
    try:
        session.socket.close()
    except OSError:
        pass
    if session._reader.is_alive():
        session._reader.join(timeout=2.0)

    # Reset session state and swap in the new socket.
    session.socket = new_sock
    session.response_queue = queue.Queue()
    session.notify_queue.clear()
    # Born-awaiting again: first batch off the new socket will be WELCOME.
    with session._awaiting_lock:
        session._awaiting = True
        session._current_batch = []
    # Start a fresh reader thread bound to the new socket.
    session._reader_alive = threading.Event()
    session._reader_alive.set()
    session._reader = threading.Thread(target=session._reader_loop, daemon=True)
    session._reader.start()

    # Drain WELCOME so it doesn't pollute the next response.
    try:
        _await_initial_batch(session, "WELCOME (reconnect)")
    except Exception as e:
        print(f"[bridge] reconnect: WELCOME drain failed: {e}")
        return False

    # Replay the cached LOGIN to restore auth state.
    if session.last_auth_cmd:
        try:
            lines, _, _ = _send_command(session, session.last_auth_cmd)
            if not (lines and lines[0].startswith("SUCCESS|")):
                # Auth replay failed (e.g. user was deleted). Clear the cache
                # so we don't keep retrying. Subsequent admin commands will
                # see E202 and the frontend can prompt for a fresh login.
                print(f"[bridge] reconnect: auth replay rejected: {lines[:1]}")
                session.last_auth_cmd = None
        except Exception as e:
            print(f"[bridge] reconnect: auth replay errored: {e}")
            session.last_auth_cmd = None

    return True


@app.post("/api/command")
def api_command():
    body = request.get_json(silent=True) or {}
    sid = body.get("session_id")
    cmd = body.get("command", "")
    if not isinstance(cmd, str) or not cmd.strip():
        return make_response(False, session_id=sid, error="Empty command", status=400)
    with sessions_lock:
        session = sessions.get(sid) if sid else None
    if session is None:
        return make_response(False, session_id=sid, error="Unknown session", status=400)

    cmd_clean = cmd.strip()
    cmd_upper = cmd_clean.upper()
    is_list_all = (cmd_upper == "LIST_ALL" or cmd_upper.startswith("LIST_ALL|"))

    # Cache read: serve LIST_ALL responses from memory if still fresh.
    if is_list_all:
        now = time.time()
        cached = _list_cache.get(cmd_upper)
        if cached and (now - cached[0]) < _LIST_CACHE_TTL:
            print(f"[cache] HIT  {cmd_upper}")
            return make_response(True, session_id=sid, lines=cached[1], cache_hit=True)

    with session.lock:
        # Try once on the existing socket.
        try:
            lines, raw_req, raw_resp = _send_command(session, cmd_clean)
        except Exception as e:
            # Transparent reconnect + retry. The session_id stays the same so
            # the frontend never sees a dropped session for a transient blip.
            print(f"[bridge] {sid[:6]} command failed ({e}); reconnecting in place")
            if not _reconnect_in_place(session):
                _drop_session(sid, reason=f"command error, reconnect failed: {e}")
                return make_response(False, session_id=sid,
                                     error=f"Connection error: {e}", status=502)
            try:
                lines, raw_req, raw_resp = _send_command(session, cmd_clean)
                print(f"[bridge] {sid[:6]} reconnected and retried successfully")
            except Exception as e2:
                _drop_session(sid, reason=f"retry failed: {e2}")
                return make_response(False, session_id=sid,
                                     error=f"Connection error: {e2}", status=502)

    # Cache write: store a successful LIST_ALL result for future requests.
    if is_list_all and "RESULT_END" in lines:
        _list_cache[cmd_upper] = (time.time(), lines)
        print(f"[cache] STORE {cmd_upper} ({len(lines)} lines)")

    # Cache invalidate: any successful write operation (ADD/UPDATE/DELETE)
    # clears all cached list data so the next LIST_ALL always reflects it.
    if lines and lines[0].startswith("OK|") and _list_cache:
        _list_cache.clear()
        print(f"[cache] CLEAR (write op: {cmd_upper})")

    return make_response(True, session_id=sid, lines=lines,
                         raw_request=raw_req, raw_response=raw_resp)


@app.post("/api/disconnect")
def api_disconnect():
    # Accept either application/json or application/x-www-form-urlencoded,
    # because navigator.sendBeacon cannot set a JSON Content-Type.
    sid = None
    body = request.get_json(silent=True)
    if isinstance(body, dict):
        sid = body.get("session_id")
    if not sid:
        sid = request.form.get("session_id")
    if not sid:
        return make_response(False, error="session_id required", status=400)
    _drop_session(sid, reason="client disconnect", graceful=True)
    return make_response(True, session_id=sid, lines=[])


@app.get("/api/notifications")
def api_notifications():
    """Drain and return any NOTIFY|... lines that arrived on this session.

    v2.3: the session's reader thread pushes server-initiated NOTIFY broadcasts
    into a deque. The browser polls this endpoint (~every 2s) and gets back
    a list of strings — empty if nothing new. Pure pull-based; no websockets,
    no long-poll, no extra server load when idle.
    """
    sid = request.args.get("session_id")
    with sessions_lock:
        session = sessions.get(sid) if sid else None
    if session is None:
        return jsonify({"ok": False, "error": "invalid session"}), 400
    drained = []
    while session.notify_queue:
        drained.append(session.notify_queue.popleft())
    return jsonify({"ok": True, "notifications": drained})


@app.post("/api/encryption")
def api_set_encryption():
    """Toggle ENC| encryption for a session. Body: {session_id, enabled: bool}"""
    data = request.get_json(silent=True) or {}
    sid = data.get("session_id")
    enabled = bool(data.get("enabled", False))
    with sessions_lock:
        session = sessions.get(sid) if sid else None
    if session is None:
        return jsonify({"ok": False, "error": "invalid session"}), 400
    session.encryption_enabled = enabled
    print(f"[bridge] session {sid} encryption={'on' if enabled else 'off'}")
    return jsonify({"ok": True, "enabled": enabled})


@app.get("/api/status")
def api_status():
    """Return server STATUS via a dedicated short-lived TCP connection.

    We deliberately do NOT reuse the session's persistent socket, for two reasons:
    1. Avoid contention: STATUS never blocks a user query waiting for session.lock.
    2. Avoid buffer pollution: STATUS response bytes can never bleed into the
       session's recv_buffer and corrupt a subsequent query's response.
    """
    sid = request.args.get("session_id")
    with sessions_lock:
        session = sessions.get(sid) if sid else None
    if session is None:
        return jsonify({"ok": False, "error": "invalid session"}), 400

    try:
        sock = _open_tcp(timeout=2.0)
    except OSError as e:
        return jsonify({"ok": False, "error": f"Cannot reach server: {e}"}), 502

    tmp = Session(sock)   # spawns its own reader thread
    try:
        _await_initial_batch(tmp, "WELCOME (status)")    # discard WELCOME
        lines, _req, _resp = _send_command(tmp, "STATUS")
    except Exception as e:
        print(f"[bridge] status poll error: {e}")
        return jsonify({"ok": False, "error": f"Connection error: {e}"}), 502
    finally:
        tmp.stop_reader()
        try:
            sock.close()
        except OSError:
            pass

    raw = next((l for l in lines if l.startswith("STATUS_INFO|")), None)
    if raw is None:
        return jsonify({"ok": False, "error": "unexpected response", "raw": lines})

    result = {"ok": True}
    for part in raw[len("STATUS_INFO|"):].split("|"):
        if "=" in part:
            k, v = part.split("=", 1)
            result[k] = v
    return jsonify(result)


def _drop_session(sid, reason="", graceful=False):
    with sessions_lock:
        session = sessions.pop(sid, None)
    if session is None:
        return
    try:
        if graceful:
            try:
                # QUIT must also go through the encryption pipeline if enabled.
                with session.lock:
                    _send_command(session, "QUIT")
            except (OSError, Exception):
                pass
        session.stop_reader()
        session.socket.close()
    except OSError:
        pass
    print(f"[bridge] session {sid} disconnected ({reason})")


if __name__ == "__main__":
    print(f"[bridge] starting on http://127.0.0.1:{BRIDGE_PORT}")
    print(f"[bridge] forwarding to TCP {SERVER_HOST}:{SERVER_PORT}")
    print(f"[bridge] LIST_ALL caching enabled (TTL={_LIST_CACHE_TTL}s)")
    app.run(host="127.0.0.1", port=BRIDGE_PORT, threaded=True)

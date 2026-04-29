"""HTTP-to-TCP bridge for the Timetable Inquiry System.

Exposes a small HTTP API on port 50002 so browser-based UIs (which cannot
speak raw TCP) can talk to the C++ server on port 50000. The bridge is a
transparent forwarder and does not parse application-layer semantics. Each
browser session is mapped to one persistent TCP connection, because the
server keeps authentication state per TCP connection.
"""
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

RECV_TIMEOUT = 5.0              # max seconds to wait for a full response
RECV_POLL_TIMEOUT = 0.2         # socket poll timeout inside the recv loop
HELP_KEYWORD_TIMEOUT_MS = 1000  # HELP: keyword-match window before idle fallback
HELP_IDLE_MS = 500              # HELP fallback: stop after this much silence

# Terminal-line markers, mirrored from client/src/client.cpp:isTerminalLine.
TERMINAL_PREFIXES = ("OK|", "ERROR|", "SUCCESS|", "FAILURE|",
                     "RESULT_NONE|", "WELCOME|")
TERMINAL_EXACT = ("RESULT_END", "BYE")


# ---- Session bookkeeping ----
class Session:
    """Per-browser-session state: one TCP socket plus its serialization lock."""

    def __init__(self, sock):
        self.socket = sock
        self.lock = threading.Lock()
        self.recv_buffer = b""


sessions = {}                       # session_id -> Session
sessions_lock = threading.Lock()    # guards the sessions dict


def is_terminal_line(line: str) -> bool:
    if line in TERMINAL_EXACT:
        return True
    for prefix in TERMINAL_PREFIXES:
        if line.startswith(prefix):
            return True
    return False


def recv_until_terminal(session: Session):
    """Read lines from the socket until a terminal line is seen.

    HELP responses have no structural terminator; we use a two-layer fallback:
    primary match on an 'INFO|...QUIT' line, and an idle-timeout fallback that
    triggers after HELP_KEYWORD_TIMEOUT_MS has passed and the wire has been
    quiet for HELP_IDLE_MS. This keeps the bridge from hanging if the HELP
    text ever changes.
    """
    sock = session.socket
    buffer = session.recv_buffer
    lines = []
    first_info_at = None
    last_byte_at = time.time()
    deadline = time.time() + RECV_TIMEOUT

    sock.settimeout(RECV_POLL_TIMEOUT)

    while True:
        # Drain any complete lines already in the buffer.
        while b"\n" in buffer:
            idx = buffer.index(b"\n")
            text = buffer[:idx].decode("utf-8", errors="replace").rstrip("\r")
            buffer = buffer[idx + 1:]
            lines.append(text)
            if first_info_at is None and text.startswith("INFO|"):
                first_info_at = time.time()
            if is_terminal_line(text):
                session.recv_buffer = buffer
                return lines
            if text.startswith("INFO|") and "QUIT" in text:
                session.recv_buffer = buffer
                return lines

        now = time.time()
        # HELP idle-fallback: keyword window elapsed AND wire has gone quiet.
        if first_info_at is not None:
            since_first = (now - first_info_at) * 1000.0
            since_byte = (now - last_byte_at) * 1000.0
            if since_first > HELP_KEYWORD_TIMEOUT_MS and since_byte > HELP_IDLE_MS:
                session.recv_buffer = buffer
                return lines

        if now > deadline:
            session.recv_buffer = buffer
            raise TimeoutError("recv exceeded RECV_TIMEOUT")

        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            continue
        if not chunk:
            # Remote closed the connection.
            session.recv_buffer = buffer
            return lines
        buffer += chunk
        last_byte_at = time.time()


# ---- Flask app ----
app = Flask(__name__)
CORS(app)


def make_response(ok, session_id=None, lines=None, error=None, status=200):
    payload = {
        "ok": ok,
        "session_id": session_id,
        "lines": lines or [],
        "error": error,
    }
    return jsonify(payload), status


@app.post("/api/connect")
def api_connect():
    try:
        sock = socket.create_connection((SERVER_HOST, SERVER_PORT), timeout=5.0)
    except OSError as e:
        return make_response(False, error=f"Cannot reach TCP server: {e}", status=502)
    session = Session(sock)
    try:
        lines = recv_until_terminal(session)
    except Exception as e:
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
    with session.lock:
        try:
            session.socket.sendall((cmd.strip() + "\n").encode("utf-8"))
            lines = recv_until_terminal(session)
        except Exception as e:
            _drop_session(sid, reason=f"command error: {e}")
            return make_response(False, session_id=sid,
                                 error=f"Connection error: {e}", status=502)
    return make_response(True, session_id=sid, lines=lines)


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


def _drop_session(sid, reason="", graceful=False):
    with sessions_lock:
        session = sessions.pop(sid, None)
    if session is None:
        return
    try:
        if graceful:
            try:
                session.socket.sendall(b"QUIT\n")
            except OSError:
                pass
        session.socket.close()
    except OSError:
        pass
    print(f"[bridge] session {sid} disconnected ({reason})")


if __name__ == "__main__":
    print(f"[bridge] starting on http://127.0.0.1:{BRIDGE_PORT}")
    print(f"[bridge] forwarding to TCP {SERVER_HOST}:{SERVER_PORT}")
    app.run(host="127.0.0.1", port=BRIDGE_PORT, threaded=True)

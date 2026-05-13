# Timetable Inquiry System — Web GUI

Browser-based front-end for the DCN Assignment 2 timetable server.
A Python (Flask) bridge translates HTTP (fetch) into the server's
raw TCP protocol (v2.4, pipe-separated), so the browser never speaks
TCP directly. The bridge is **not** a transparent forwarder — it adds
SHA-256 login hashing, optional `ENC|` XOR wrapping, a 5-second
`LIST_ALL` cache, a NOTIFY-routing reader thread, and silent
socket reconnect with login replay.

---

## Architecture

```
Browser  (HTML/CSS/JS, file:// or any static server)
   │
   │  HTTP POST  (CORS, port 50002)
   ▼
web/bridge.py  (Flask)
   │
   │  raw TCP  (port 50000)
   ▼
server.exe  (C++ Winsock, untouched)
```

Each browser tab maps to **one persistent TCP connection** in the bridge,
because the server stores authentication state per connection.

---

## Startup Order

**Step 1 — Start the C++ server**

```bash
# from the project root
./server.exe
# Windows: double-click server.exe, or run the helper script
run-server.bat
```

Confirm it is listening on port 50000.

**Step 2 — Install Python dependencies** (one-time)

```bash
pip install -r web/requirements.txt
```

**Step 3 — Start the bridge**

```bash
python web/bridge.py
```

Expected output:

```
[bridge] starting on http://127.0.0.1:50002
[bridge] forwarding to TCP 127.0.0.1:50000
 * Running on http://127.0.0.1:50002
```

**Step 4 — Open the UI**

Open `web/index.html` in any modern browser.
A `file://` URL works because the bridge has CORS enabled globally.

```
# Example (Chrome)
start chrome web/index.html

# Or just double-click index.html in the file explorer
```

---

## Ports

| Port  | Service         | Notes                        |
|-------|-----------------|------------------------------|
| 50000 | C++ TCP server  | Already running, do not change |
| 50002 | Python bridge   | HTTP API consumed by the browser |

---

## Default Accounts

| Username | Password  | Role    |
|----------|-----------|---------|
| admin    | admin123  | admin   |
| student  | stu123    | student |
| alice    | alice456  | student |
| bob      | bob789    | student |

---

## API Endpoints (bridge)

All endpoints return `{ ok, session_id, lines, error }` (plus `raw_request`, `raw_response`, `cache_hit` for observability).

| Method | Path                  | Body (JSON)                    | Description                                              |
|--------|-----------------------|--------------------------------|----------------------------------------------------------|
| POST   | `/api/connect`        | —                              | Open TCP connection, return `session_id`                 |
| POST   | `/api/command`        | `{session_id, command}`        | Send one protocol command, return server lines           |
| POST   | `/api/disconnect`     | `{session_id}` or form-encoded | Close TCP connection (also accepts `sendBeacon`)         |
| POST   | `/api/encryption`     | `{session_id, enabled}`        | Toggle `ENC\|<hex>` wrapping for this session            |
| GET    | `/api/notifications`  | `?session_id=…`                | Drain pending `NOTIFY` lines pushed by the server (v2.3) |
| GET    | `/api/status`         | `?session_id=…`                | Run `STATUS` on a short-lived side connection            |

`/api/disconnect` accepts both `application/json` and
`application/x-www-form-urlencoded` so that `navigator.sendBeacon`
(page unload) works correctly.

---

## Notes

- The bridge inspects `LOGIN` to SHA-256-hash the password (idempotent
  against already-hashed values) and watches `OK|…` write responses to
  invalidate its `LIST_ALL` cache. Otherwise it forwards bytes as-is;
  `lines` in the response is the array of protocol lines the server
  returned, post-decryption if `ENC|` was used.
- Closing the browser tab sends a `QUIT` to the server via
  `navigator.sendBeacon`, gracefully releasing the TCP connection.
- Concurrent requests on the **same session** are serialized by a
  per-session lock in the bridge; different sessions run concurrently.
- Session timeout: if the server does not respond within 5 seconds,
  the bridge closes the session and returns HTTP 502.

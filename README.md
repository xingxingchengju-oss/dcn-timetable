# Course Timetable Inquiry System
## Data Communications and Networking — Assignment 2

A multi-threaded client–server timetable system implemented with Windows Sockets in C++17. Supports three clients (C++ CLI, Python desktop GUI, browser web GUI) talking to a single TCP server through a documented pipe-delimited application protocol (v2.4, see [docs/protocol.md](docs/protocol.md)).

---

## Project Structure

```
timetable_system/
├── server/
│   ├── server.cpp        # Main server: Winsock, multithreading, broadcast, dispatch
│   ├── database.h        # CSV storage + CRUD + advanced search
│   ├── crypto.h          # XOR / hex / SHA-256 (Windows BCrypt)
│   ├── logger.h          # Thread-safe append-only logger
│   └── protocol.h        # PROTO_VERSION, SERVER_PORT, XOR_KEY constants
├── client/
│   ├── src/client.cpp    # C++ CLI client (colorized, interactive menu)
│   └── timetable_gui.py  # Python desktop GUI (customtkinter, dark theme)
├── web/
│   ├── index.html        # Browser SPA: Quick/Advanced search, Table/Week views
│   ├── app.js            # Frontend logic, SubtleCrypto SHA-256, NOTIFY polling
│   ├── style.css         # Browser styling
│   ├── bridge.py         # Flask HTTP↔TCP bridge with reader thread + cache
│   ├── requirements.txt  # flask, flask-cors
│   └── README.md         # Bridge-specific notes
├── data/
│   ├── timetable.csv     # Course records (9 columns)
│   ├── users.csv         # Username, password, role
│   ├── server.log        # Older log location (relative to old cwd)
│   └── schema.sql        # (placeholder, CSV is the source of truth)
├── docs/
│   ├── Assignment2_2026.pdf
│   ├── Assignment_2_Compilation.pdf
│   ├── protocol.md       # Full application-layer protocol spec (v2.4)
│   └── audit_report.md   # Internal audit / answering rehearsal notes
├── logs/server.log       # Active server log (current cwd convention)
├── assets/               # UI artwork (BNBU logo, campus background)
├── CMakeLists.txt
├── build.bat             # MSVC build (calls vcvars64)
├── run-server.bat / run-client.bat
└── README.md             # This file
```

---

## How to Build

### Requirements
- Windows 10 / 11
- Visual Studio Build Tools 2022 with the **Desktop development with C++** workload
- (For Python GUI / web) Python 3.10+ and `pip`

### Recommended: `build.bat`

```bat
build.bat
```

It auto-calls `vcvars64.bat` to enter the x64 MSVC environment. If your install path differs, edit `VCVARS64` at the top of [build.bat](build.bat).

### Alternative: CMake

```bat
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

Produces `server.exe` and `client.exe` in the project root.

---

## How to Run

Pick **one** of the three clients. All three talk to the same C++ server on TCP **port 50000**.

### A. C++ CLI (simplest demo)

Two terminals at the project root:

```bat
:: terminal 1
server.exe
```
```bat
:: terminal 2
client.exe
:: or: client.exe <host> <port>
```

### B. Python desktop GUI (`customtkinter`)

```bat
:: terminal 1
server.exe

:: terminal 2
python client/timetable_gui.py
```

The GUI opens a Connect dialog pre-filled with `127.0.0.1:50000`. Click **Connect**, then **Login** (defaults below) to unlock admin pages.

> One-time: `pip install customtkinter`

### C. Browser web GUI (HTML + Flask bridge)

Three processes, **in this order**:

```bat
:: 1. start the C++ server
server.exe

:: 2. install bridge deps once
pip install -r web/requirements.txt

:: 3. start the HTTP bridge (port 50002)
python web/bridge.py
```

Then open [web/index.html](web/index.html) in any modern browser (a `file://` URL works because the bridge enables CORS).

The bridge translates browser HTTP/JSON into the raw TCP protocol, transparently SHA-256-hashes login passwords before they hit the wire, caches `LIST_ALL` for 5 seconds, wraps requests in `ENC|<hex>` when the encryption toggle is on, and polls `/api/notifications` to surface server-pushed `NOTIFY` broadcasts as toasts.

---

## Application-Layer Protocol (v2.4)

All messages are pipe-delimited, newline-terminated, UTF-8. See [docs/protocol.md](docs/protocol.md) for the authoritative spec (request/response shapes, error codes E001–E500, encryption layer, version history).

### Client → Server commands

```
LOGIN|<user>|<password>           (password may be plaintext or 64-char SHA-256 hex)
LOGOUT
QUERY|<code>
SEARCH_INSTRUCTOR|<name>          substring, case-insensitive
SEARCH_TIME|<day>|<HH:MM>
SEARCH_ADVANCED|keyword=…|day=…|semester=…|time_range=morning|afternoon|evening
LIST_ALL[|<semester>]
STATUS
ADD|code|title|section|instructor|day|time|duration|room|semester    [admin]
UPDATE|<code>|<section>|<field>|<value>                              [admin]
DELETE|<code>|<section>                                              [admin]
HELP
QUIT

ENC|<hex>                         optional XOR-encrypted wrapper around any of the above
```

### Server → Client responses

| Keyword         | Meaning                                                    |
|-----------------|------------------------------------------------------------|
| `WELCOME\|…`    | Initial banner                                             |
| `SUCCESS\|…`    | Auth or session command succeeded                          |
| `FAILURE\|…`    | Auth failed                                                |
| `RESULT_BEGIN`  | Start of a multi-record result block                       |
| `RESULT\|<9 fields>` | One course record                                     |
| `RESULT_END`    | End of result block                                        |
| `RESULT_NONE\|…`| Query succeeded but matched zero records                   |
| `STATUS_INFO\|active=…\|total=…\|uptime=…` | Server statistics             |
| `OK\|…`         | Admin write succeeded                                      |
| `ERROR\|<code>\|…` | Command rejected (see protocol.md §8 for the code table) |
| `INFO\|…`       | Informational text (used by `HELP`)                        |
| `BYE`           | Server ack of `QUIT`; connection closing                   |

### Server-initiated messages

The server pushes these without a request:

- `WELCOME|Timetable Inquiry System v2.4|…` — sent once upon connect.
- `NOTIFY|<op>|<code>|<section>` (v2.3) — broadcast to **every other** connected client when an admin `ADD`/`UPDATE`/`DELETE` succeeds. `op` is `ADDED`, `UPDATED`, or `DELETED`. This satisfies assignment IV(4) ("changes must be reflected immediately for all connected clients"). NOTIFY is always plaintext, even on an encrypted session.

---

## Default Credentials

| Username | Password   | Role           |
|----------|------------|----------------|
| admin    | admin123   | Administrator  |
| student  | stu123     | Student        |
| alice    | alice456   | Student        |
| bob      | bob789     | Student        |

Passwords on the wire from the web GUI are SHA-256 hashes (the bridge hashes any plaintext it sees, idempotent against an already-hashed value).

---

## Functional Modules

### (1) Database Module — [server/database.h](server/database.h)
- Header-only `Database` class. Loads `data/timetable.csv` (9 columns) and `data/users.csv` into in-memory `vector<Course>` / `vector<User>`. Auto-creates sample data on first run.
- Read APIs: `queryByCode`, `queryByInstructor`, `queryByTime`, `queryAdvanced` (multi-field AND), `listAll`.
- Write APIs: `addCourse`, `updateCourse`, `deleteCourse` — each rewrites the CSV after mutation.
- Auth API: `authenticate(user, pass)` accepts plaintext **or** a 64-hex SHA-256 hash transparently.

### (2) Query Module — clients
- CLI [client/src/client.cpp](client/src/client.cpp): colorized numbered menu, line-oriented recv with terminal-prefix detection.
- Desktop GUI [client/timetable_gui.py](client/timetable_gui.py): sidebar nav, ResultsBox with semantic colouring, threaded sends.
- Browser GUI [web/index.html](web/index.html) + [web/app.js](web/app.js): Quick/Advanced modes, Table/Week views, modals for CRUD, Wire Preview drawer.

### (3) User Management Module
- Two roles, `student` and `admin`. Per-connection session held in `ClientSession`. Admin commands gated by `session.role == "admin"`, otherwise `ERROR|E202|Insufficient privileges`.
- Web GUI hashes the password client-side via `SubtleCrypto.digest('SHA-256', …)` so plaintext never leaves the browser.

### (4) Information Update Module — admin CRUD + **NOTIFY broadcast**
- `ADD`, `UPDATE` (single field), `DELETE` (by code+section); CSV rewritten on every change.
- v2.3: every successful write triggers `broadcastNotify()` ([server.cpp](server/server.cpp)). The server maintains `std::vector<SOCKET> g_clients` (guarded by its own mutex, separate from `db_mutex` to avoid lock-ordering issues) and pushes `NOTIFY|<op>|<code>|<section>` to every connection except the originator. Sockets that fail to send are pruned from the broadcast list proactively.

### (5) Networking & Concurrency Module — [server/server.cpp](server/server.cpp)
- Winsock 2.2, listen socket on port 50000.
- One detached `std::thread` per accepted connection. Up to 64 concurrent sessions; in practice limited only by OS thread quota.
- `TCP_NODELAY` set on every connection (Nagle's algorithm + delayed-ACK can otherwise stall small interactive packets up to ~200 ms).
- Single `db_mutex` for the in-memory data; separate `g_clients_mutex` for the broadcast registry.

### (6) Web Bridge Module — [web/bridge.py](web/bridge.py)
- Flask HTTP API on `http://127.0.0.1:50002` with five endpoints: `/api/connect`, `/api/command`, `/api/disconnect`, `/api/encryption`, `/api/notifications`, plus `/api/status` on a short-lived side connection.
- Each browser session maps to one persistent TCP socket plus a **dedicated reader thread** (v2.3). The reader is the sole owner of `recv` and routes lines into either `response_queue` (for in-flight commands) or `notify_queue` (for server-pushed NOTIFY). An explicit awaiting/idle state machine prevents stray lines from polluting the next response.
- Transparent SHA-256 hashing of `LOGIN` passwords, transparent ENC|hex wrapping when the session's encryption toggle is on, 5-second TTL cache on `LIST_ALL` (invalidated on any write), and silent socket-reconnect with cached LOGIN replay if a TCP socket dies mid-session.

---

## Bonus Features Implemented

- **Three GUI clients** (CLI colorized, Python desktop, browser SPA)
- **Advanced search** (`SEARCH_ADVANCED` with `keyword` / `day` / `semester` / `time_range`)
- **Data caching (two-layer)** — server-side query-result cache (10 s TTL, covers `QUERY`/`SEARCH_*`/`LIST_ALL`, benefits **all** clients including CLI and Python GUI; v2.4, see [server/server.cpp](server/server.cpp) and `STATUS` `cache_hits`/`cache_misses` fields) + a closer 5-second `LIST_ALL` cache in the bridge for browser sessions ([web/bridge.py](web/bridge.py)). Both invalidate on writes.
- **Encryption** — XOR + hex `ENC|` layer (server-side in [server/crypto.h](server/crypto.h), bridge-side in [bridge.py](web/bridge.py)) plus SHA-256 password hashing on both client and server

---

## Known Issues / Limitations

- C++ CLI and Python desktop GUI display **incoming `NOTIFY|…` lines but don't auto-refresh** their result views — they print a yellow `[Server] NOTIFY|...` line (CLI) or silently drop it (GUI). Live UI updates are implemented only in the web SPA. The next user-issued query picks up the new state on all three clients.
- `users.csv` stores plaintext passwords. The wire never carries plaintext (web client hashes; bridge re-hashes), but the file itself is unencrypted.
- `server.exe` reads `data/timetable.csv` via a path relative to its current working directory. Run from the project root (or via [run-server.bat](run-server.bat)).
- `XOR_KEY` is shared and hardcoded in both [server/protocol.h](server/protocol.h) and [web/bridge.py](web/bridge.py). XOR with a known key is obfuscation, not real encryption.

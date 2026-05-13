# Timetable Inquiry System — Application-Layer Protocol Specification

**Version:** 2.4  
**Date:** 2026-05-13  
**Authors:** DCN Project Group

---

## 1. Overview

This document defines the application-layer protocol used between clients and the Timetable Inquiry System server. The protocol is a line-oriented, text-based protocol running over TCP. Browser clients reach the same TCP server via a Flask HTTP bridge (`web/bridge.py`); the wire format on the upstream socket is identical.

All messages are **UTF-8 encoded** and terminated with a single newline character (`\n`).

---

## 2. Transport

| Transport | Port  | Description                                                                |
|-----------|-------|----------------------------------------------------------------------------|
| TCP       | 50000 | All clients (C++ CLI, Python desktop GUI, and the web bridge upstream)     |
| HTTP      | 50002 | Browser-facing Flask bridge (`web/bridge.py`) that relays JSON↔TCP for the web SPA |

The server supports up to **64 simultaneous client connections**, each handled in a dedicated thread. The HTTP bridge is an out-of-band relay — every browser session still consumes exactly one upstream TCP connection from the bridge to the server.

---

## 3. Message Format

### 3.1 General Syntax

Fields within a single message line are separated by the pipe character `|`. Every message is terminated by `\n`.

```
KEYWORD|field1|field2|...\n
```

- **KEYWORD** is always uppercase ASCII.
- Field values must not contain `|` or `\n`.
- Leading and trailing whitespace in each field is ignored.
- Empty lines from the client are silently discarded.

### 3.2 Request (Client → Server)

```
COMMAND|param1|param2|...\n
```

### 3.3 Response (Server → Client)

Single-line response:
```
STATUS|data\n
```

Multi-line response (zero or more `RESULT` lines between markers):
```
RESULT_BEGIN\n
RESULT|field1|field2|...\n
RESULT|field1|field2|...\n
RESULT_END\n
```

### 3.4 Encryption Layer (v2.1)

An optional XOR-based encryption layer is available for demonstration purposes. This is **not production-grade** — it exists to demonstrate the concept of an application-layer cipher.

When a client enables encryption, it prefixes its message with `ENC|`:

```
ENC|<hex-encoded ciphertext>\n
```

The server detects the `ENC|` prefix and decrypts the payload before processing. The server's response is **also encrypted** if and only if the corresponding request was encrypted.

**Algorithm:** XOR each byte of the UTF-8 message with the corresponding byte of the repeating key.

**Key:** `DCN2026TimetableKey`

**Encoding:** The ciphertext is encoded as a lowercase hexadecimal string (2 hex chars per byte).

**Example (plaintext `LIST_ALL`):**

```
C: ENC|<hex-encoded ciphertext of "LIST_ALL">
S: ENC|<hex-encoded ciphertext of "RESULT_BEGIN\nRESULT|...">
```

Encryption is **opt-in per message**. A client may send some messages plain and others encrypted.

---

## 4. Session Lifecycle

```
Client                          Server
  |                               |
  |------- TCP connect ---------> |
  |<------ WELCOME|... ---------- |
  |                               |
  |------- LOGIN|user|pass -----> |
  |<------ SUCCESS|role --------- |
  |                               |
  |------- (commands) ----------> |
  |<------ (responses) ---------- |
  |                               |
  |------- QUIT ----------------> |
  |<------ BYE ------------------ |
  |------- TCP close -----------> |
```

A session begins **unauthenticated** (role: `guest`). Authentication is not required for read-only queries but is required for all write operations (ADD, UPDATE, DELETE).

---

## 5. Commands

### 5.1 Authentication

#### `LOGIN`

Authenticate with the server.

**Request:**
```
LOGIN|<username>|<password>
```

The `<password>` field accepts either:
- **Plaintext** — any string whose length is not 64 characters
- **SHA-256 hex digest** — a 64-character lowercase hex string of the password's SHA-256 hash

The server accepts either form transparently: when a 64-character hex string arrives, it is treated as an already-hashed digest and compared by hashing the stored credential and matching the two digests; when any other length arrives, it is compared as plaintext. This lets the web client hash with `SubtleCrypto` before the bytes ever leave the browser (so plaintext never traverses the wire), while CLI demos remain readable. The credential store (`data/users.csv`) is currently plaintext for ease of grading; replacing it with pre-hashed values is a drop-in change because of this dual-format comparison.

**Success response:**
```
SUCCESS|<role>
```
where `<role>` is `student` or `admin`.

**Failure response:**
```
FAILURE|E001|Invalid username or password
```

**Example (plaintext):**
```
C: LOGIN|admin|secret123
S: SUCCESS|admin
```

**Example (pre-hashed):**
```
C: LOGIN|admin|2bb80d537b1da3e38bd30361aa855686bde0eacd7162fef6a25fe97bf527a25b
S: SUCCESS|admin
```

---

#### `LOGOUT`

End the current authenticated session. The connection remains open.

**Request:**
```
LOGOUT
```

**Response:**
```
SUCCESS|Logged out
```

---

### 5.2 Query Commands

All query commands are available to unauthenticated clients.

#### `QUERY`

Search for courses by course code (exact or prefix match).

**Request:**
```
QUERY|<course_code>
```

`<course_code>` is case-insensitive (e.g., `COMP3003`, `comp3003`).

**Success response (one or more results):**
```
RESULT_BEGIN
RESULT|<code>|<title>|<section>|<instructor>|<day>|<time>|<duration>|<classroom>|<semester>
...
RESULT_END
```

**Empty response:**
```
RESULT_NONE|No courses found for <course_code>
```

**Error:**
```
ERROR|E101|Missing course code
```

**Example:**
```
C: QUERY|COMP3003
S: RESULT_BEGIN
S: RESULT|COMP3003|Computer Networks|S1|Dr. Chan|Mon|10:00|2h|A101|2026S1
S: RESULT|COMP3003|Computer Networks|S2|Dr. Chan|Wed|14:00|2h|B202|2026S1
S: RESULT_END
```

---

#### `SEARCH_INSTRUCTOR`

Search for courses taught by a given instructor (case-insensitive partial match).

**Request:**
```
SEARCH_INSTRUCTOR|<name>
```

**Success response:** Same multi-line `RESULT_BEGIN` / `RESULT_END` format as `QUERY`.

**Empty response:**
```
RESULT_NONE|No courses found for instructor: <name>
```

**Error:**
```
ERROR|E101|Missing instructor name
```

**Example:**
```
C: SEARCH_INSTRUCTOR|Chan
S: RESULT_BEGIN
S: RESULT|COMP3003|Computer Networks|S1|Dr. Chan|Mon|10:00|2h|A101|2026S1
S: RESULT_END
```

---

#### `SEARCH_TIME`

Search for courses at a specific day and start time.

**Request:**
```
SEARCH_TIME|<day>|<time>
```

- `<day>`: `Mon` / `Tue` / `Wed` / `Thu` / `Fri` (case-insensitive)
- `<time>`: `HH:MM` in 24-hour format

**Success response:** Same multi-line format.

**Empty response:**
```
RESULT_NONE|No courses found at <day> <time>
```

**Error:**
```
ERROR|E101|Usage: SEARCH_TIME|<day>|<time>
```

**Example:**
```
C: SEARCH_TIME|Mon|10:00
S: RESULT_BEGIN
S: RESULT|COMP3003|Computer Networks|S1|Dr. Chan|Mon|10:00|2h|A101|2026S1
S: RESULT_END
```

---

#### `SEARCH_ADVANCED` (v2.1)

Search for courses matching multiple optional key=value criteria simultaneously. All parameters are optional; if none are provided, all courses are returned.

**Request:**
```
SEARCH_ADVANCED[|<key>=<value>...]
```

Parameters may appear in any order, separated by `|`.

| Key          | Match type                                                                                | Example                |
|--------------|-------------------------------------------------------------------------------------------|------------------------|
| `keyword`    | Case-insensitive substring across code, title, instructor, classroom, day, time, semester | `keyword=COMP`         |
| `day`        | Case-insensitive exact match                                                              | `day=Mon`              |
| `semester`   | Exact match                                                                               | `semester=2026S1`      |
| `time_range` | Named band: `morning` (08:00–11:59), `afternoon` (12:00–17:59), `evening` (18:00+)       | `time_range=afternoon` |

A result is returned only if it matches **all** provided parameters (logical AND).

**Success response:** Same multi-line `RESULT_BEGIN` / `RESULT_END` format as `QUERY`.

**Empty response:**
```
RESULT_NONE|No courses match the criteria
```

**Example:**
```
C: SEARCH_ADVANCED|keyword=COMP|day=Mon|semester=2026S1
S: RESULT_BEGIN
S: RESULT|COMP3003|Data Communications|S1|Dr. Chan|Mon|10:00|2h|A101|2026S1
S: RESULT_END
```

---

#### `LIST_ALL`

List all courses, optionally filtered by semester.

**Request:**
```
LIST_ALL
LIST_ALL|<semester>
```

`<semester>` format: `2026S1`, `2026S2`, etc.

**Success response:** Same multi-line format.

**Empty response:**
```
RESULT_NONE|No courses found
```

**Example:**
```
C: LIST_ALL|2026S1
S: RESULT_BEGIN
S: RESULT|COMP3003|Computer Networks|S1|Dr. Chan|Mon|10:00|2h|A101|2026S1
S: RESULT|MATH1001|Calculus I|S1|Dr. Lee|Tue|09:00|2h|C301|2026S1
S: RESULT_END
```

---

### 5.3 Admin Commands

The following commands require the client to be authenticated with role `admin`. Sending them unauthenticated or as a `student` returns:
```
ERROR|E202|Insufficient privileges (admin required)
```

#### `ADD`

Add a new course record.

**Request:**
```
ADD|<code>|<title>|<section>|<instructor>|<day>|<time>|<duration>|<classroom>|<semester>
```

| Field        | Example       | Description                          |
|--------------|---------------|--------------------------------------|
| `code`       | `COMP3003`    | Course code (uppercase)              |
| `title`      | `Computer Networks` | Full course title              |
| `section`    | `S1`          | Section identifier                   |
| `instructor` | `Dr. Chan`    | Instructor name                      |
| `day`        | `Mon`         | Day of week                          |
| `time`       | `10:00`       | Start time (HH:MM, 24-hour)          |
| `duration`   | `2h`          | Duration (e.g., `1h`, `1.5h`, `2h`) |
| `classroom`  | `A101`        | Room identifier                      |
| `semester`   | `2026S1`      | Semester code                        |

**Success response:**
```
OK|Course added: <code>|<section>
```

**Error responses:**
```
ERROR|E101|ADD requires 9 fields: code|title|section|instructor|day|time|duration|classroom|semester
ERROR|E301|Duplicate course: <code> section <section> already exists
```

**Example:**
```
C: ADD|COMP4001|Advanced AI|S1|Dr. Wang|Fri|14:00|2h|D401|2026S1
S: OK|Course added: COMP4001|S1
```

---

#### `UPDATE`

Update a single field of an existing course record.

**Request:**
```
UPDATE|<code>|<section>|<field>|<value>
```

Valid `<field>` values (case-insensitive):

| Field        | Example new value |
|--------------|-------------------|
| `TITLE`      | `Intro to AI`     |
| `INSTRUCTOR` | `Dr. Zhang`       |
| `DAY`        | `Tue`             |
| `TIME`       | `14:00`           |
| `DURATION`   | `1.5h`            |
| `CLASSROOM`  | `B202`            |
| `SEMESTER`   | `2026S2`          |

**Success response:**
```
OK|Updated <code>|<section>: <field> -> <value>
```

**Error responses:**
```
ERROR|E101|Usage: UPDATE|<code>|<section>|<field>|<value>
ERROR|E302|Course not found: <code> section <section>
ERROR|E303|Invalid field: <field>
```

**Example:**
```
C: UPDATE|COMP3003|S1|CLASSROOM|C205
S: OK|Updated COMP3003|S1: CLASSROOM -> C205
```

---

#### `DELETE`

Remove a course record by code and section.

**Request:**
```
DELETE|<code>|<section>
```

**Success response:**
```
OK|Deleted <code>|<section>
```

**Error responses:**
```
ERROR|E101|Usage: DELETE|<code>|<section>
ERROR|E302|Course not found: <code> section <section>
```

**Example:**
```
C: DELETE|COMP4001|S1
S: OK|Deleted COMP4001|S1
```

---

### 5.4 Utility Commands

#### `STATUS` (v2.1)

Request server runtime statistics. Available to all clients (no authentication required).

**Request:**
```
STATUS
```

**Response:**
```
STATUS_INFO|active=<n>|total=<n>|uptime=<HH:MM:SS>|cache_hits=<n>|cache_misses=<n>
```

| Field          | Type    | Description                                                                       |
|----------------|---------|-----------------------------------------------------------------------------------|
| `active`       | integer | Currently connected clients **excluding the connection issuing this STATUS** (so a single browser tab polling its own STATUS sees `active=1`, not 2) |
| `total`        | integer | Total commands dispatched since startup (request counter, not connection counter) |
| `uptime`       | string  | Server uptime formatted as `HH:MM:SS`                                             |
| `cache_hits`   | integer | Server query-cache hits since startup (v2.4 — see §6.2)                           |
| `cache_misses` | integer | Server query-cache misses since startup (v2.4)                                    |

Fields are emitted as `key=value` pairs separated by `|`, in the order shown above. Clients should parse by key (not by position) so future fields can be appended without breaking older clients.

**Example:**
```
C: STATUS
S: STATUS_INFO|active=3|total=47|uptime=01:00:21|cache_hits=12|cache_misses=5
```

---

#### `HELP`

Request a list of available commands.

**Request:**
```
HELP
```

**Response:** Multiple `INFO` lines:
```
INFO|Available commands:
INFO|  LOGIN|<user>|<pass>                            - Authenticate
INFO|  LOGOUT                                         - End session
INFO|  QUERY|<code>                                   - Search by course code
INFO|  SEARCH_INSTRUCTOR|<name>                       - Search by instructor
INFO|  SEARCH_TIME|<day>|<time>                       - Search by time slot
INFO|  SEARCH_ADVANCED|<key>=<val>...                - Multi-field search
INFO|  LIST_ALL[|<semester>]                          - List all courses
INFO|  STATUS                                         - Server statistics
INFO|  ADD|<9 fields>                                 - Add course   [admin]
INFO|  UPDATE|<code>|<sec>|<field>|<val>              - Update field [admin]
INFO|  DELETE|<code>|<section>                        - Delete course [admin]
INFO|  HELP                                           - Show this help
INFO|  QUIT                                           - Disconnect
```

---

#### `QUIT`

Gracefully close the connection.

**Request:**
```
QUIT
```

**Response:**
```
BYE
```

The server closes the TCP connection immediately after sending `BYE`.

---

## 6. Server-Initiated Messages

#### `WELCOME`

Sent by the server immediately upon a new connection, before any client request.

```
WELCOME|Timetable Inquiry System v2.4|Type HELP for commands
```

---

#### `NOTIFY` (v2.3)

The server pushes a `NOTIFY` line to every connected client **except the originator** whenever an admin write (`ADD` / `UPDATE` / `DELETE`) succeeds. This satisfies the assignment requirement that *"changes must be reflected immediately for all connected clients"*: receivers may show a notification and/or re-issue their last list query to refresh their view.

**Format:**

```
NOTIFY|<op>|<code>|<section>
```

| Field    | Values                              | Description                            |
|----------|-------------------------------------|----------------------------------------|
| `op`     | `ADDED` / `UPDATED` / `DELETED`     | The kind of mutation                   |
| `code`   | string                              | Course code of the affected record     |
| `section`| string                              | Section identifier of the affected record |

**Encryption:** `NOTIFY` is **always plaintext**, even on a session that has enabled the `ENC|` layer. This avoids per-session encryption-state lookup inside the server's broadcast path; the NOTIFY payload contains no secrets (only the identifier of a public course record).

**Receiver behaviour:** clients should treat `NOTIFY` as **non-terminal** — it may interleave with regular responses. Clients that do not implement live UI updates should silently drop the line so it does not corrupt parsing of the next response.

**Example:**

```
S: NOTIFY|UPDATED|COMP3003|S1
S: NOTIFY|ADDED|COMP4999|S1
S: NOTIFY|DELETED|MATH2001|S2
```

---

## 6.2 Query Result Cache (v2.4 — server-side)

The server memoizes responses to the read-only commands `QUERY`, `SEARCH_INSTRUCTOR`, `SEARCH_TIME`, `SEARCH_ADVANCED`, and `LIST_ALL`. A repeated request with the exact same command line is served from a small in-process `std::map<line, response>` without re-running the linear scan or taking the database mutex. This benefits **every** client uniformly — the C++ CLI, the Python desktop GUI, and the browser SPA all see lower latency on hot queries.

**Invalidation:** every successful `ADD` / `UPDATE` / `DELETE` clears the entire cache before broadcasting `NOTIFY`, so post-write reads always reflect the new state. A 10-second per-entry TTL acts as a backstop if the data file is edited out-of-band.

**Layering:** the browser path has a second, closer cache in `web/bridge.py` (5-second TTL on `LIST_ALL` only). The two caches compose: a browser `LIST_ALL` may hit the bridge cache (no TCP) → falls through to the server cache (no DB scan) → only finally falls through to the in-memory `vector<Course>` scan.

**Observability:** server cache hit/miss counters are surfaced through `STATUS` as `cache_hits` and `cache_misses` (see §5.4). The server also prints `[cache] HIT  <cmd>` / `[cache] CLEAR (<reason>, N entries dropped)` to stdout for live demo purposes.

The cache is opaque to clients — they never need to be aware of it; correctness is guaranteed by write-invalidation. NOTIFY broadcasts are unaffected (always recomputed and never cached).

---

## 7. Response Status Keywords Summary

| Keyword       | Direction | Meaning                                      |
|---------------|-----------|----------------------------------------------|
| `WELCOME`     | S→C       | Connection established banner                |
| `SUCCESS`     | S→C       | Authentication operation succeeded           |
| `FAILURE`     | S→C       | Authentication operation failed              |
| `RESULT_BEGIN`| S→C       | Start of a multi-record result block         |
| `RESULT`      | S→C       | One course record (pipe-separated fields)    |
| `RESULT_END`  | S→C       | End of a multi-record result block           |
| `RESULT_NONE` | S→C       | Query succeeded but no records matched       |
| `STATUS_INFO` | S→C       | Server statistics (response to STATUS)       |
| `OK`          | S→C       | Admin write operation succeeded              |
| `ERROR`       | S→C       | Command failed (see error code)              |
| `INFO`        | S→C       | Informational text (used by HELP)            |
| `BYE`         | S→C       | Server acknowledges QUIT; connection closing |
| `NOTIFY`      | S→C       | Server-initiated asynchronous change notification (v2.3) |

---

## 8. Error Codes

| Code   | Meaning                                        |
|--------|------------------------------------------------|
| `E001` | Invalid username or password                   |
| `E101` | Missing or malformed parameters                |
| `E102` | Unknown command                                |
| `E201` | Not authenticated                              |
| `E202` | Insufficient privileges (admin required)       |
| `E301` | Duplicate resource (course already exists)     |
| `E302` | Resource not found                             |
| `E303` | Invalid field name in UPDATE                   |
| `E500` | Internal server error                          |

**Error response format:**
```
ERROR|<code>|<human-readable message>
```

---

## 9. Course Record Format

A `RESULT` line always carries exactly 9 pipe-separated fields in this order:

```
RESULT|<code>|<title>|<section>|<instructor>|<day>|<time>|<duration>|<classroom>|<semester>
```

| Position | Field        | Type   | Example           |
|----------|--------------|--------|-------------------|
| 1        | `code`       | string | `COMP3003`        |
| 2        | `title`      | string | `Computer Networks` |
| 3        | `section`    | string | `S1`              |
| 4        | `instructor` | string | `Dr. Chan`        |
| 5        | `day`        | enum   | `Mon`             |
| 6        | `time`       | HH:MM  | `10:00`           |
| 7        | `duration`   | string | `2h`              |
| 8        | `classroom`  | string | `A101`            |
| 9        | `semester`   | string | `2026S1`          |

Valid values for `day`: `Mon` `Tue` `Wed` `Thu` `Fri`

---

## 10. Complete Exchange Example

```
[TCP connection on port 50000]

S: WELCOME|Timetable Inquiry System v2.4|Type HELP for commands

C: STATUS
S: STATUS_INFO|active=1|total=2|uptime=00:00:08|cache_hits=0|cache_misses=0

C: LOGIN|student1|pass1234
S: SUCCESS|student

C: STATUS
S: STATUS_INFO|active=1|total=12|uptime=00:05:05|cache_hits=4|cache_misses=3

C: QUERY|COMP3003
S: RESULT_BEGIN
S: RESULT|COMP3003|Computer Networks|S1|Dr. Chan|Mon|10:00|2h|A101|2026S1
S: RESULT|COMP3003|Computer Networks|S2|Dr. Chan|Wed|14:00|2h|B202|2026S1
S: RESULT_END

C: SEARCH_ADVANCED|keyword=COMP|day=Mon|semester=2026S1
S: RESULT_BEGIN
S: RESULT|COMP3003|Data Communications|S1|Dr. Chan|Mon|10:00|2h|A101|2026S1
S: RESULT_END

C: SEARCH_TIME|Fri|09:00
S: RESULT_NONE|No courses found at Fri 09:00

C: DELETE|COMP3003|S1
S: ERROR|E202|Insufficient privileges (admin required)

C: LOGOUT
S: SUCCESS|Logged out

C: LOGIN|admin|adminpass
S: SUCCESS|admin

C: ADD|COMP4999|Special Topics|S1|Dr. Wu|Thu|16:00|2h|E501|2026S1
S: OK|Course added: COMP4999|S1

C: UPDATE|COMP4999|S1|CLASSROOM|F601
S: OK|Updated COMP4999|S1: CLASSROOM -> F601

C: DELETE|COMP4999|S1
S: OK|Deleted COMP4999|S1

C: ENC|<hex-encoded ciphertext of "LIST_ALL">
S: ENC|<hex-encoded ciphertext of "RESULT_BEGIN\n...RESULT_END">

C: QUIT
S: BYE

[TCP connection closed]
```

---

## 11. Protocol Version History

| Version | Date       | Changes                                                   |
|---------|------------|-----------------------------------------------------------|
| 1.0     | 2026-04    | Initial implementation (space-separated, port 8888)       |
| 2.0     | 2026-04-28 | Unified `\|` separator; `RESULT_BEGIN`/`RESULT_END`; TCP port 50000; structured error codes |
| 2.1     | 2026-04-29 | Add STATUS command; add SEARCH_ADVANCED command; add ENC\| XOR encryption layer; add SHA-256 password hashing for LOGIN |
| 2.2     | 2026-05-02 | Documentation corrections only (no wire-protocol change): SEARCH_ADVANCED updated to key=value format; STATUS uptime documented as HH:MM:SS; admin auth error code corrected to E202 in §5.3 |
| 2.3     | 2026-05-11 | Add `NOTIFY` server-initiated broadcast for admin writes (assignment IV.4 compliance); NOTIFY is always plaintext (skips ENC layer). WELCOME banner version bumped. |
| 2.4     | 2026-05-13 | Add server-side query-result cache (`QUERY`/`SEARCH_*`/`LIST_ALL`) with TTL = 10 s, invalidated on every successful write (§6.2). `STATUS` response now carries `cache_hits` and `cache_misses` fields (existing clients ignore unknown fields), and `active` is now reported as "other connected clients excluding the requester" so a single-tab browser sees a sensible `active=1`. `SEARCH_ADVANCED` keyword search extended to all 9 course fields (was missing `section` and `duration`, contradicting the UI's "searches across all fields" hint). `QUERY` switched from exact-only to exact-OR-prefix matching to honour the §5.2 contract. Doc-only fixes: `§2` transport table corrected to TCP/50000 + HTTP/50002 (no more WebSocket/50001 placeholder), `§5.1` password storage wording aligned with code, `§5.4` `total` clarified as commands not connections. `LOGIN` failure code reverted from `E201` to `E001` in code. WELCOME banner bumped to `v2.4`. |

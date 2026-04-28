# Timetable Inquiry System — Application-Layer Protocol Specification

**Version:** 2.0  
**Date:** 2026-04-28  
**Authors:** DCN Project Group

---

## 1. Overview

This document defines the application-layer protocol used between clients and the Timetable Inquiry System server. The protocol is a line-oriented, text-based protocol running over TCP. A WebSocket transport variant is also specified for browser-based clients.

All messages are **UTF-8 encoded** and terminated with a single newline character (`\n`).

---

## 2. Transport

| Transport | Port  | Description                          |
|-----------|-------|--------------------------------------|
| TCP       | 50000 | C++ CLI client and general use       |
| WebSocket | 50001 | Python GUI client and browser access |

The server supports up to **64 simultaneous client connections**, each handled in a dedicated thread.

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

**Success response:**
```
SUCCESS|<role>
```
where `<role>` is `student` or `admin`.

**Failure response:**
```
FAILURE|E001|Invalid username or password
```

**Example:**
```
C: LOGIN|admin|secret123
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
ERROR|E201|Unauthorized. Admin role required.
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

#### `HELP`

Request a list of available commands.

**Request:**
```
HELP
```

**Response:** Multiple `INFO` lines:
```
INFO|Available commands:
INFO|  LOGIN|<user>|<pass>             - Authenticate
INFO|  LOGOUT                          - End session
INFO|  QUERY|<code>                    - Search by course code
INFO|  SEARCH_INSTRUCTOR|<name>        - Search by instructor
INFO|  SEARCH_TIME|<day>|<time>        - Search by time slot
INFO|  LIST_ALL[|<semester>]           - List all courses
INFO|  ADD|<9 fields>                  - Add course   [admin]
INFO|  UPDATE|<code>|<sec>|<field>|<val> - Update field [admin]
INFO|  DELETE|<code>|<section>         - Delete course [admin]
INFO|  HELP                            - Show this help
INFO|  QUIT                            - Disconnect
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
WELCOME|Timetable Inquiry System v2.0|Type HELP for commands
```

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
| `OK`          | S→C       | Admin write operation succeeded              |
| `ERROR`       | S→C       | Command failed (see error code)              |
| `INFO`        | S→C       | Informational text (used by HELP)            |
| `BYE`         | S→C       | Server acknowledges QUIT; connection closing |

---

## 8. Error Codes

| Code   | Meaning                                        |
|--------|------------------------------------------------|
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

S: WELCOME|Timetable Inquiry System v2.0|Type HELP for commands

C: LOGIN|student1|pass1234
S: SUCCESS|student

C: QUERY|COMP3003
S: RESULT_BEGIN
S: RESULT|COMP3003|Computer Networks|S1|Dr. Chan|Mon|10:00|2h|A101|2026S1
S: RESULT|COMP3003|Computer Networks|S2|Dr. Chan|Wed|14:00|2h|B202|2026S1
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

C: QUIT
S: BYE

[TCP connection closed]
```

---

## 11. Protocol Version History

| Version | Date       | Changes                                                   |
|---------|------------|-----------------------------------------------------------|
| 1.0     | 2026-04    | Initial implementation (space-separated, port 8888)       |
| 2.0     | 2026-04-28 | Unified `\|` separator; `RESULT_BEGIN`/`RESULT_END`; ports 50000/50001; structured error codes |

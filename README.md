# Course Timetable Inquiry System
## Data Communications and Networking — Assignment 2

---

## Project Structure

```
timetable_system/
├── server/
│   ├── server.cpp      # Main server: Winsock, multithreading, request handling
│   ├── database.h      # Database module: CSV storage, CRUD operations
│   ├── logger.h        # Thread-safe logging module
│   └── protocol.h      # Protocol constants and documentation
├── client/
│   ├── src/
│   │   └── client.cpp  # Interactive client with menu UI
│   └── timetable_gui.py
├── data/
│   ├── timetable.csv
│   ├── users.csv
│   ├── server.log
│   └── schema.sql
├── docs/
│   ├── Assignment2_2026.pdf
│   ├── Assignment_2_Compilation.pdf
│   └── protocol.md
├── CMakeLists.txt
├── build.bat
└── README.md
```

---

## How to Compile

### Requirements
- Windows
- Visual Studio Build Tools 2022
- Workload: **Desktop development with C++**

### Recommended build method
Open **Developer Command Prompt for VS 2022** in the project root and run:

```bat
build.bat
```

The script also calls `vcvars64.bat` automatically, so it can prepare the x64 MSVC environment for teammates who run it from a normal command prompt.

### Toolchain note
Use **MSVC (`cl`)** for this project. Do not use `g++` or MinGW in team builds; this is only to keep everyone on the same compiler and reduce environment differences.

### CMake
If you want to build with CMake, use a Visual Studio generator:

```bat
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

---

## Running the System

1. **Start the server** (in one terminal window):
   ```
   server.exe
   ```
   The server listens on port **8888** and auto-creates timetable/user CSV files if they don't exist.

2. **Start one or more clients** (each in a separate terminal):
   ```
   client.exe
   ```
   Or connect to a remote server:
   ```
   client.exe 127.0.0.1 8888
   ```

---

## Application-Layer Protocol

All messages are **newline-terminated text lines**.

### Client → Server Commands

| Command | Format | Description |
|---------|--------|-------------|
| LOGIN   | `LOGIN <user> <pass>` | Authenticate user |
| LOGOUT  | `LOGOUT` | End session |
| QUERY   | `QUERY <code>` | Search by course code |
| SEARCH_INSTRUCTOR | `SEARCH_INSTRUCTOR <name>` | Search by instructor |
| SEARCH_TIME | `SEARCH_TIME <day> <HH:MM>` | Search by time slot |
| LIST_ALL | `LIST_ALL [semester]` | List all / filter by semester |
| ADD     | `ADD code\|title\|section\|instructor\|day\|time\|duration\|room\|semester` | Add course (admin only) |
| UPDATE  | `UPDATE <code> <section> <field> <value>` | Modify a field (admin only) |
| DELETE  | `DELETE <code> <section>` | Remove course (admin only) |
| HELP    | `HELP` | Show available commands |
| QUIT    | `QUIT` | Disconnect |

### Server → Client Responses

| Response | Meaning |
|----------|---------|
| `WELCOME ...` | Initial connection banner |
| `SUCCESS ...` | Operation successful |
| `FAILURE ...` | Authentication or operation failed |
| `RESULT BEGIN` | Start of multi-line result |
| `RESULT <data>` | One result record |
| `RESULT END` | End of results |
| `RESULT NONE ...` | No matching records |
| `OK ...` | Admin operation confirmed |
| `ERROR ...` | Bad request or unauthorized |
| `BYE` | Disconnect acknowledgment |

---

## Default Credentials

| Username | Password | Role |
|----------|----------|------|
| admin    | admin123 | Administrator |
| student  | stu123   | Student |

---

## Functional Modules

### (1) Database Module (`server/database.h`)
- File-based CSV storage for portability
- Auto-creates sample data on first run
- Full CRUD: add, query, update, delete
- Multiple search modes: by code, instructor, time slot, semester

### (2) Query Module (`client/src/client.cpp`)
- Interactive numbered menu
- Searches: by course code, instructor name, time slot, semester
- Formatted colored console output

### (3) User Management Module
- Two roles: **student** (query-only) and **admin** (full access)
- Simple username/password authentication per session
- Admin-only commands return `ERROR Unauthorized` for unauthenticated clients

### (4) Information Update Module
- `ADD`: insert new course (duplicate code+section rejected)
- `UPDATE`: modify individual field (time, classroom, instructor, etc.)
- `DELETE`: remove course by code + section
- All changes written immediately to CSV (persist across restarts)

### (5) Networking & Concurrency Module
- **Windows Sockets (Winsock 2.2)**
- Each client handled in a **separate `std::thread`**
- Supports multiple concurrent clients
- Graceful handling of disconnects and malformed input

---

## Known Issues
- [ ] Server data and log paths are not fixed to `data/`; they currently depend on the startup working directory.

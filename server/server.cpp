#define _WIN32_WINNT 0x0600
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <map>
#include <vector>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <ctime>
#include <atomic>
#include "database.h"
#include "protocol.h"
#include "logger.h"
#include "crypto.h"

#pragma comment(lib, "ws2_32.lib")

#define PORT 50000
#define MAX_CLIENTS 64
#define BUFFER_SIZE 4096

Database db;
Logger logger("logs/server.log");
std::mutex db_mutex;

// Connected-client registry for NOTIFY broadcast (protocol v2.3, assignment IV.4).
// A separate mutex from db_mutex to avoid lock-ordering issues — broadcastNotify()
// must never run while db_mutex is held.
std::vector<SOCKET> g_clients;
std::mutex          g_clients_mutex;

std::atomic<int>  active_clients{0};
std::atomic<long> total_requests{0};
std::time_t       server_start_time = 0;

// ---- Server-side query result cache (v2.4) ----
// Memoizes responses to read-only commands (QUERY / SEARCH_* / LIST_ALL) so
// repeated queries skip the O(n) scan of `courses` and the db_mutex round trip.
// Benefits ALL clients (C++ CLI, Python GUI, web SPA) — the bridge's LIST_ALL
// cache (web/bridge.py) is a second, closer layer that further saves a TCP
// hop for browser sessions.
//
// Invalidation: any successful ADD/UPDATE/DELETE clears the whole cache, so
// after a write the next read pays one full miss and re-populates with fresh
// data. TTL = 10 s tombstone catches the rare case where data files were
// edited out-of-band (e.g. someone hand-edited timetable.csv on disk).
struct QueryCacheEntry {
    std::time_t timestamp;
    std::string response;
};
std::map<std::string, QueryCacheEntry> g_query_cache;
std::mutex                             g_query_cache_mutex;
std::atomic<long>                      g_cache_hits{0};
std::atomic<long>                      g_cache_misses{0};
static const long QUERY_CACHE_TTL_SEC = 10;

static bool isCacheableCmd(const std::string& cmd) {
    return cmd == "QUERY"             || cmd == "SEARCH_INSTRUCTOR" ||
           cmd == "SEARCH_TIME"       || cmd == "SEARCH_ADVANCED"   ||
           cmd == "LIST_ALL";
}

static bool cacheLookup(const std::string& key, std::string& out) {
    std::lock_guard<std::mutex> lk(g_query_cache_mutex);
    auto it = g_query_cache.find(key);
    if (it == g_query_cache.end()) return false;
    if (std::time(nullptr) - it->second.timestamp > QUERY_CACHE_TTL_SEC) {
        g_query_cache.erase(it);
        return false;
    }
    out = it->second.response;
    return true;
}

static void cacheStore(const std::string& key, const std::string& resp) {
    std::lock_guard<std::mutex> lk(g_query_cache_mutex);
    g_query_cache[key] = QueryCacheEntry{ std::time(nullptr), resp };
}

static void cacheClear(const char* reason) {
    std::lock_guard<std::mutex> lk(g_query_cache_mutex);
    if (!g_query_cache.empty()) {
        std::cout << "[cache] CLEAR (" << reason << ", "
                  << g_query_cache.size() << " entries dropped)\n";
        g_query_cache.clear();
    }
}

// Session state per client
struct ClientSession {
    SOCKET sock;
    bool authenticated;
    std::string role; // "student" or "admin"
    std::string username;
};

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end   = s.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return s.substr(start, end - start + 1);
}

std::vector<std::string> split(const std::string& s, char delim = '|') {
    std::vector<std::string> tokens;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, delim)) {
        tok = trim(tok);
        if (!tok.empty()) tokens.push_back(tok);
    }
    return tokens;
}

// Broadcast a NOTIFY line to every connected client except `origin`.
// Always sends plaintext (the ENC layer is per-message and opt-in; NOTIFY skips it
// per protocol.md §6 — the payload carries no secrets and this avoids per-session
// state lookup in the broadcast hot path).
//
// Snapshot pattern: copy g_clients under g_clients_mutex, release the lock, then
// send outside the lock so a slow socket can't stall broadcasts to others.
// Sockets that fail to send are removed from g_clients proactively; the actual
// closesocket() still happens in the owning clientHandler thread when its recv
// returns 0 / RST.
static void broadcastNotify(const std::string& line, SOCKET origin) {
    std::vector<SOCKET> snap;
    {
        std::lock_guard<std::mutex> lk(g_clients_mutex);
        snap = g_clients;
    }

    std::vector<SOCKET> failed;
    for (SOCKET s : snap) {
        if (s == origin) continue;
        int n = send(s, line.c_str(), (int)line.size(), 0);
        if (n == SOCKET_ERROR) failed.push_back(s);
    }

    if (!failed.empty()) {
        std::lock_guard<std::mutex> lk(g_clients_mutex);
        for (SOCKET dead : failed) {
            g_clients.erase(std::remove(g_clients.begin(), g_clients.end(), dead),
                            g_clients.end());
        }
    }
}

// Inner dispatch: receives a trimmed plaintext command line, returns a plaintext response.
static std::string dispatchRequest(const std::string& line, ClientSession& session) {
    ++total_requests;
    std::vector<std::string> parts = split(line, '|');
    std::string cmd = parts[0];
    for (auto& c : cmd) c = toupper(c);

    // v2.4: server-side query cache. Skip the work entirely if we've answered
    // this exact line within QUERY_CACHE_TTL_SEC and nothing has been written
    // since. The cache key is the full line so `QUERY|COMP3003` and
    // `SEARCH_INSTRUCTOR|Chan` stay distinct, and RESULT_NONE answers are
    // cached too (a stable "nothing matches" is just as expensive to recompute).
    bool cacheable = isCacheableCmd(cmd);
    if (cacheable) {
        std::string cached;
        if (cacheLookup(line, cached)) {
            ++g_cache_hits;
            std::cout << "[cache] HIT  " << line << "\n";
            return cached;
        }
        ++g_cache_misses;
    }

    // LOGIN|username|password
    if (cmd == "LOGIN") {
        if (parts.size() < 3) return "FAILURE|E101|Missing credentials\n";
        std::string user = parts[1], pass = parts[2];
        std::lock_guard<std::mutex> lock(db_mutex);
        std::string role = db.authenticate(user, pass);
        if (role.empty()) {
            logger.log("Failed login attempt: " + user);
            return "FAILURE|E001|Invalid username or password\n";
        }
        session.authenticated = true;
        session.role = role;
        session.username = user;
        logger.log("User logged in: " + user + " [" + role + "]");
        return "SUCCESS|" + role + "\n";
    }

    // LOGOUT
    if (cmd == "LOGOUT") {
        session.authenticated = false;
        session.role = "student";
        session.username = "";
        return "SUCCESS|Logged out\n";
    }

    // QUERY|course_code
    if (cmd == "QUERY") {
        if (parts.size() < 2) return "ERROR|E101|Missing course code\n";
        std::string code = parts[1];
        for (auto& c : code) c = toupper(c);
        std::string resp;
        {
            std::lock_guard<std::mutex> lock(db_mutex);
            auto results = db.queryByCode(code);
            if (results.empty()) {
                resp = "RESULT_NONE|No courses found for " + code + "\n";
            } else {
                resp = "RESULT_BEGIN\n";
                for (auto& r : results) resp += r.toProtocol() + "\n";
                resp += "RESULT_END\n";
            }
        }
        cacheStore(line, resp);
        return resp;
    }

    // SEARCH_INSTRUCTOR|name
    if (cmd == "SEARCH_INSTRUCTOR") {
        if (parts.size() < 2) return "ERROR|E101|Missing instructor name\n";
        std::string name = parts[1];
        std::string resp;
        {
            std::lock_guard<std::mutex> lock(db_mutex);
            auto results = db.queryByInstructor(name);
            if (results.empty()) {
                resp = "RESULT_NONE|No courses found for instructor: " + name + "\n";
            } else {
                resp = "RESULT_BEGIN\n";
                for (auto& r : results) resp += r.toProtocol() + "\n";
                resp += "RESULT_END\n";
            }
        }
        cacheStore(line, resp);
        return resp;
    }

    // LIST_ALL[|semester]
    if (cmd == "LIST_ALL") {
        std::string sem = (parts.size() >= 2) ? parts[1] : "";
        std::string resp;
        {
            std::lock_guard<std::mutex> lock(db_mutex);
            auto results = db.listAll(sem);
            if (results.empty()) {
                resp = "RESULT_NONE|No courses found\n";
            } else {
                resp = "RESULT_BEGIN\n";
                for (auto& r : results) resp += r.toProtocol() + "\n";
                resp += "RESULT_END\n";
            }
        }
        cacheStore(line, resp);
        return resp;
    }

    // SEARCH_TIME|day|time
    if (cmd == "SEARCH_TIME") {
        if (parts.size() < 3) return "ERROR|E101|Usage: SEARCH_TIME|<day>|<time>\n";
        std::string resp;
        {
            std::lock_guard<std::mutex> lock(db_mutex);
            auto results = db.queryByTime(parts[1], parts[2]);
            if (results.empty()) {
                resp = "RESULT_NONE|No courses found at " + parts[1] + " " + parts[2] + "\n";
            } else {
                resp = "RESULT_BEGIN\n";
                for (auto& r : results) resp += r.toProtocol() + "\n";
                resp += "RESULT_END\n";
            }
        }
        cacheStore(line, resp);
        return resp;
    }

    // SEARCH_ADVANCED|keyword=...|day=...|semester=...|time_range=...
    if (cmd == "SEARCH_ADVANCED") {
        std::string keyword, day, semester, timeRange;
        for (size_t i = 1; i < parts.size(); ++i) {
            const std::string& p = parts[i];
            size_t eq = p.find('=');
            if (eq == std::string::npos) continue;
            std::string k = p.substr(0, eq);
            std::string v = p.substr(eq + 1);
            for (auto& ch : k) ch = tolower(ch);
            if      (k == "keyword")    keyword   = v;
            else if (k == "day")        day       = v;
            else if (k == "semester")   semester  = v;
            else if (k == "time_range") timeRange = v;
        }
        std::string resp;
        {
            std::lock_guard<std::mutex> lock(db_mutex);
            auto results = db.queryAdvanced(keyword, day, semester, timeRange);
            if (results.empty()) {
                resp = "RESULT_NONE|No courses match the criteria\n";
            } else {
                resp = "RESULT_BEGIN\n";
                for (auto& r : results) resp += r.toProtocol() + "\n";
                resp += "RESULT_END\n";
            }
        }
        cacheStore(line, resp);
        return resp;
    }

    // --- Admin-only guard ---
    if (cmd == "ADD" || cmd == "UPDATE" || cmd == "DELETE") {
        if (!session.authenticated || session.role != "admin") {
            return "ERROR|E202|Insufficient privileges (admin required)\n";
        }
    }

    // ADD|code|title|section|instructor|day|time|duration|classroom|semester
    if (cmd == "ADD") {
        if (parts.size() < 10) return "ERROR|E101|ADD requires 9 fields: code|title|section|instructor|day|time|duration|classroom|semester\n";
        Course c;
        c.code       = parts[1];
        c.title      = parts[2];
        c.section    = parts[3];
        c.instructor = parts[4];
        c.day        = parts[5];
        c.time       = parts[6];
        c.duration   = parts[7];
        c.classroom  = parts[8];
        c.semester   = parts[9];
        bool ok;
        {   // Scope db_mutex so it's released before broadcasting.
            std::lock_guard<std::mutex> lock(db_mutex);
            ok = db.addCourse(c);
        }
        if (ok) {
            cacheClear("ADD");
            logger.log("Admin " + session.username + " added course: " + c.code);
            broadcastNotify("NOTIFY|ADDED|" + c.code + "|" + c.section + "\n",
                            session.sock);
            return "OK|Course added: " + c.code + "|" + c.section + "\n";
        }
        return "ERROR|E301|Duplicate course: " + c.code + " section " + c.section + " already exists\n";
    }

    // UPDATE|code|section|field|value
    if (cmd == "UPDATE") {
        if (parts.size() < 5) return "ERROR|E101|Usage: UPDATE|<code>|<section>|<field>|<value>\n";
        std::string code    = parts[1];
        std::string section = parts[2];
        std::string field   = parts[3];
        std::string value   = parts[4];
        for (auto& c2 : field) c2 = toupper(c2);
        bool ok;
        {   // Scope db_mutex so it's released before broadcasting.
            std::lock_guard<std::mutex> lock(db_mutex);
            ok = db.updateCourse(code, section, field, value);
        }
        if (ok) {
            cacheClear("UPDATE");
            logger.log("Admin " + session.username + " updated " + code + "/" + section + " " + field + "=" + value);
            broadcastNotify("NOTIFY|UPDATED|" + code + "|" + section + "\n",
                            session.sock);
            return "OK|Updated " + code + "|" + section + ": " + field + " -> " + value + "\n";
        }
        return "ERROR|E302|Course not found: " + code + " section " + section + "\n";
    }

    // DELETE|code|section
    if (cmd == "DELETE") {
        if (parts.size() < 3) return "ERROR|E101|Usage: DELETE|<code>|<section>\n";
        std::string code    = parts[1];
        std::string section = parts[2];
        bool ok;
        {   // Scope db_mutex so it's released before broadcasting.
            std::lock_guard<std::mutex> lock(db_mutex);
            ok = db.deleteCourse(code, section);
        }
        if (ok) {
            cacheClear("DELETE");
            logger.log("Admin " + session.username + " deleted " + code + "/" + section);
            broadcastNotify("NOTIFY|DELETED|" + code + "|" + section + "\n",
                            session.sock);
            return "OK|Deleted " + code + "|" + section + "\n";
        }
        return "ERROR|E302|Course not found: " + code + " section " + section + "\n";
    }

    // STATUS
    if (cmd == "STATUS") {
        int  a = active_clients.load();
        long t = total_requests.load();
        long ch = g_cache_hits.load();
        long cm = g_cache_misses.load();
        std::time_t now = std::time(nullptr);
        long secs = (long)(now - server_start_time);
        long hh = secs / 3600;
        long mm = (secs % 3600) / 60;
        long ss = secs % 60;
        char tbuf[16];
        snprintf(tbuf, sizeof(tbuf), "%02ld:%02ld:%02ld", hh, mm, ss);
        return "STATUS_INFO|active="    + std::to_string(a)  +
               "|total="                + std::to_string(t)  +
               "|uptime="               + std::string(tbuf)  +
               "|cache_hits="           + std::to_string(ch) +
               "|cache_misses="         + std::to_string(cm) + "\n";
    }

    // HELP
    if (cmd == "HELP") {
        return
            "INFO|Available commands:\n"
            "INFO|  LOGIN|<user>|<pass>                     - Authenticate\n"
            "INFO|  LOGOUT                                  - End session\n"
            "INFO|  QUERY|<code>                            - Search by course code\n"
            "INFO|  SEARCH_INSTRUCTOR|<name>                - Search by instructor\n"
            "INFO|  SEARCH_TIME|<day>|<time>                - Search by time slot\n"
            "INFO|  SEARCH_ADVANCED|<key=val>...            - Combined filter search\n"
            "INFO|  LIST_ALL[|<semester>]                   - List all courses\n"
            "INFO|  ADD|<code>|<title>|<sec>|<instr>|<day>|<time>|<dur>|<room>|<sem>  [admin]\n"
            "INFO|  UPDATE|<code>|<sec>|<field>|<val>       [admin]\n"
            "INFO|  DELETE|<code>|<section>                 [admin]\n"
            "INFO|  STATUS                                  - Server status (active/total/uptime)\n"
            "INFO|  HELP                                    - Show this help\n"
            "INFO|  QUIT                                    - Disconnect\n";
    }

    if (cmd == "QUIT") return "BYE\n";

    return "ERROR|E102|Unknown command: " + cmd + "\n";
}

// Public entry point: handles ENC| unwrapping and re-wrapping transparently.
std::string handleRequest(const std::string& raw, ClientSession& session) {
    std::string line = trim(raw);
    if (line.empty()) return "ERROR|E102|Empty request\n";

    bool wasEncrypted = false;
    std::string actualLine = line;

    if (line.rfind("ENC|", 0) == 0) {
        std::string hexCipher = line.substr(4);
        std::string cipher = hexDecode(hexCipher);
        if (cipher.empty())
            return "ERROR|E101|Invalid ENC payload (hex decode failed)\n";
        actualLine = trim(xorDecrypt(cipher, XOR_KEY));
        if (actualLine.empty())
            return "ERROR|E101|Invalid ENC payload (empty after decrypt)\n";
        wasEncrypted = true;
    }

    std::string response = dispatchRequest(actualLine, session);

    if (wasEncrypted) {
        // Strip trailing \n before encrypting, then re-add a single \n outside.
        std::string payload = response;
        if (!payload.empty() && payload.back() == '\n')
            payload.pop_back();
        return "ENC|" + hexEncode(xorEncrypt(payload, XOR_KEY)) + "\n";
    }
    return response;
}

void clientHandler(SOCKET clientSock, std::string clientAddr) {
    logger.log("Client connected: " + clientAddr);
    ++active_clients;
    ClientSession session;
    session.sock = clientSock;
    session.authenticated = false;
    session.role = "student";

    // Disable Nagle's algorithm so small interactive responses are flushed
    // immediately. Without this, the OS may delay sending up to ~200ms while
    // it waits to coalesce more bytes — felt as sluggish queries on the UI.
    BOOL noDelay = TRUE;
    setsockopt(clientSock, IPPROTO_TCP, TCP_NODELAY,
               (const char*)&noDelay, sizeof(noDelay));

    // Register this socket in the broadcast list (v2.3 NOTIFY).
    {
        std::lock_guard<std::mutex> lk(g_clients_mutex);
        g_clients.push_back(clientSock);
    }

    // Send welcome banner
    std::string welcome = "WELCOME|Timetable Inquiry System v2.4|Type HELP for commands\n";
    send(clientSock, welcome.c_str(), (int)welcome.size(), 0);

    char buf[BUFFER_SIZE];
    std::string recvBuffer;

    while (true) {
        int bytes = recv(clientSock, buf, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) break;
        buf[bytes] = '\0';
        recvBuffer += buf;

        // Process line by line
        size_t pos;
        while ((pos = recvBuffer.find('\n')) != std::string::npos) {
            std::string line = recvBuffer.substr(0, pos);
            recvBuffer.erase(0, pos + 1);
            line = trim(line);
            if (line.empty()) continue;

            std::string response = handleRequest(line, session);
            send(clientSock, response.c_str(), (int)response.size(), 0);

            if (response == "BYE\n") goto done;
        }
    }
done:
    --active_clients;
    {
        std::lock_guard<std::mutex> lk(g_clients_mutex);
        g_clients.erase(std::remove(g_clients.begin(), g_clients.end(), clientSock),
                        g_clients.end());
    }
    logger.log("Client disconnected: " + clientAddr);
    closesocket(clientSock);
}

int main() {
    server_start_time = std::time(nullptr);

    // Init DB
    db.init("data/timetable.csv", "data/users.csv");

    // Init Winsock
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }

    SOCKET serverSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSock == INVALID_SOCKET) {
        std::cerr << "Socket creation failed\n";
        WSACleanup();
        return 1;
    }

    // Allow address reuse
    int opt = 1;
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(serverSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed\n";
        closesocket(serverSock);
        WSACleanup();
        return 1;
    }

    if (listen(serverSock, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed\n";
        closesocket(serverSock);
        WSACleanup();
        return 1;
    }

    std::cout << "=== Timetable Server started on port " << PORT << " ===\n";
    logger.log("Server started on port " + std::to_string(PORT));

    while (true) {
        sockaddr_in clientAddr{};
        int addrLen = sizeof(clientAddr);
        SOCKET clientSock = accept(serverSock, (sockaddr*)&clientAddr, &addrLen);
        if (clientSock == INVALID_SOCKET) continue;

        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
        std::string addrStr = std::string(ipStr) + ":" + std::to_string(ntohs(clientAddr.sin_port));

        std::thread(clientHandler, clientSock, addrStr).detach();
    }

    closesocket(serverSock);
    WSACleanup();
    return 0;
}

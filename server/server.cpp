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
#include "database.h"
#include "protocol.h"
#include "logger.h"

#pragma comment(lib, "ws2_32.lib")

#define PORT 50000
#define MAX_CLIENTS 64
#define BUFFER_SIZE 4096

Database db;
Logger logger("logs/server.log");
std::mutex db_mutex;

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

std::string handleRequest(const std::string& raw, ClientSession& session) {
    std::string line = trim(raw);
    if (line.empty()) return "ERROR|E102|Empty request\n";

    std::vector<std::string> parts = split(line, '|');
    std::string cmd = parts[0];
    for (auto& c : cmd) c = toupper(c);

    // LOGIN|username|password
    if (cmd == "LOGIN") {
        if (parts.size() < 3) return "FAILURE|E101|Missing credentials\n";
        std::string user = parts[1], pass = parts[2];
        std::lock_guard<std::mutex> lock(db_mutex);
        std::string role = db.authenticate(user, pass);
        if (role.empty()) {
            logger.log("Failed login attempt: " + user);
            return "FAILURE|E201|Invalid username or password\n";
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
        std::lock_guard<std::mutex> lock(db_mutex);
        auto results = db.queryByCode(code);
        if (results.empty()) return "RESULT_NONE|No courses found for " + code + "\n";
        std::string resp = "RESULT_BEGIN\n";
        for (auto& r : results) resp += r.toProtocol() + "\n";
        resp += "RESULT_END\n";
        return resp;
    }

    // SEARCH_INSTRUCTOR|name
    if (cmd == "SEARCH_INSTRUCTOR") {
        if (parts.size() < 2) return "ERROR|E101|Missing instructor name\n";
        std::string name = parts[1];
        std::lock_guard<std::mutex> lock(db_mutex);
        auto results = db.queryByInstructor(name);
        if (results.empty()) return "RESULT_NONE|No courses found for instructor: " + name + "\n";
        std::string resp = "RESULT_BEGIN\n";
        for (auto& r : results) resp += r.toProtocol() + "\n";
        resp += "RESULT_END\n";
        return resp;
    }

    // LIST_ALL[|semester]
    if (cmd == "LIST_ALL") {
        std::string sem = (parts.size() >= 2) ? parts[1] : "";
        std::lock_guard<std::mutex> lock(db_mutex);
        auto results = db.listAll(sem);
        if (results.empty()) return "RESULT_NONE|No courses found\n";
        std::string resp = "RESULT_BEGIN\n";
        for (auto& r : results) resp += r.toProtocol() + "\n";
        resp += "RESULT_END\n";
        return resp;
    }

    // SEARCH_TIME|day|time
    if (cmd == "SEARCH_TIME") {
        if (parts.size() < 3) return "ERROR|E101|Usage: SEARCH_TIME|<day>|<time>\n";
        std::lock_guard<std::mutex> lock(db_mutex);
        auto results = db.queryByTime(parts[1], parts[2]);
        if (results.empty()) return "RESULT_NONE|No courses found at " + parts[1] + " " + parts[2] + "\n";
        std::string resp = "RESULT_BEGIN\n";
        for (auto& r : results) resp += r.toProtocol() + "\n";
        resp += "RESULT_END\n";
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
        std::lock_guard<std::mutex> lock(db_mutex);
        if (db.addCourse(c)) {
            logger.log("Admin " + session.username + " added course: " + c.code);
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
        std::lock_guard<std::mutex> lock(db_mutex);
        if (db.updateCourse(code, section, field, value)) {
            logger.log("Admin " + session.username + " updated " + code + "/" + section + " " + field + "=" + value);
            return "OK|Updated " + code + "|" + section + ": " + field + " -> " + value + "\n";
        }
        return "ERROR|E302|Course not found: " + code + " section " + section + "\n";
    }

    // DELETE|code|section
    if (cmd == "DELETE") {
        if (parts.size() < 3) return "ERROR|E101|Usage: DELETE|<code>|<section>\n";
        std::lock_guard<std::mutex> lock(db_mutex);
        if (db.deleteCourse(parts[1], parts[2])) {
            logger.log("Admin " + session.username + " deleted " + parts[1] + "/" + parts[2]);
            return "OK|Deleted " + parts[1] + "|" + parts[2] + "\n";
        }
        return "ERROR|E302|Course not found: " + parts[1] + " section " + parts[2] + "\n";
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
            "INFO|  LIST_ALL[|<semester>]                   - List all courses\n"
            "INFO|  ADD|<code>|<title>|<sec>|<instr>|<day>|<time>|<dur>|<room>|<sem>  [admin]\n"
            "INFO|  UPDATE|<code>|<sec>|<field>|<val>       [admin]\n"
            "INFO|  DELETE|<code>|<section>                 [admin]\n"
            "INFO|  HELP                                    - Show this help\n"
            "INFO|  QUIT                                    - Disconnect\n";
    }

    if (cmd == "QUIT") return "BYE\n";

    return "ERROR|E102|Unknown command: " + cmd + "\n";
}

void clientHandler(SOCKET clientSock, std::string clientAddr) {
    logger.log("Client connected: " + clientAddr);
    ClientSession session;
    session.sock = clientSock;
    session.authenticated = false;
    session.role = "student";

    // Send welcome banner
    std::string welcome = "WELCOME|Timetable Inquiry System v2.0|Type HELP for commands\n";
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
    logger.log("Client disconnected: " + clientAddr);
    closesocket(clientSock);
}

int main() {
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

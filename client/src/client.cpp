#define _WIN32_WINNT 0x0600
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT  50000
#define BUFFER_SIZE   8192

SOCKET sock = INVALID_SOCKET;

// ---- Color helpers (Windows Console) ----
void setColor(int color) { SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), color); }
void resetColor()        { setColor(7); }
void printGreen(const std::string& s) { setColor(10); std::cout << s; resetColor(); }
void printRed(const std::string& s)   { setColor(12); std::cout << s; resetColor(); }
void printCyan(const std::string& s)  { setColor(11); std::cout << s; resetColor(); }
void printYellow(const std::string& s){ setColor(14); std::cout << s; resetColor(); }

// ---- Send a line to server ----
bool sendLine(const std::string& line) {
    std::string msg = line + "\n";
    int sent = send(sock, msg.c_str(), (int)msg.size(), 0);
    return sent != SOCKET_ERROR;
}

// ---- Receive full response (line-by-line, stop on terminal line) ----
static std::string recvLeftover; // buffer surviving across calls

static bool isTerminalLine(const std::string& line) {
    if (line == "RESULT_END") return true;
    if (line == "BYE")        return true;
    if (line.substr(0, 3) == "OK|")          return true;
    if (line.substr(0, 6) == "ERROR|")       return true;
    if (line.substr(0, 8) == "SUCCESS|")     return true;
    if (line.substr(0, 8) == "FAILURE|")     return true;
    if (line.substr(0, 12) == "RESULT_NONE|") return true;
    if (line.substr(0, 8) == "WELCOME|")     return true;
    // HELP response ends when last INFO line seen (last line of HELP block)
    if (line.find("QUIT") != std::string::npos &&
        line.substr(0, 5) == "INFO|") return true;
    return false;
}

std::string recvResponse() {
    std::string result;
    char buf[BUFFER_SIZE];

    while (true) {
        // Process any lines already buffered
        size_t pos;
        while ((pos = recvLeftover.find('\n')) != std::string::npos) {
            std::string line = recvLeftover.substr(0, pos);
            recvLeftover.erase(0, pos + 1);
            // strip \r
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            result += line + "\n";
            if (isTerminalLine(line)) return result;
        }
        // Need more data
        int bytes = recv(sock, buf, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) return result;
        buf[bytes] = '\0';
        recvLeftover += buf;
    }
}

// ---- Split a string by delimiter ----
std::vector<std::string> splitFields(const std::string& s, char delim = '|') {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, delim)) out.push_back(tok);
    return out;
}

// ---- Pretty-print server response ----
void printResponse(const std::string& resp) {
    std::istringstream ss(resp);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        if (line.substr(0, 8) == "WELCOME|") {
            auto f = splitFields(line);
            printCyan(f.size() > 1 ? f[1] + "\n" : line + "\n");
        }
        else if (line.substr(0, 8) == "SUCCESS|") {
            auto f = splitFields(line);
            printGreen("SUCCESS: " + (f.size() > 1 ? f[1] : "") + "\n");
        }
        else if (line.substr(0, 8) == "FAILURE|") {
            auto f = splitFields(line);
            printRed("FAILURE: " + (f.size() > 2 ? f[2] : (f.size() > 1 ? f[1] : "")) + "\n");
        }
        else if (line.substr(0, 6) == "ERROR|") {
            auto f = splitFields(line);
            std::string code = f.size() > 1 ? f[1] : "";
            std::string msg  = f.size() > 2 ? f[2] : "";
            printRed("ERROR [" + code + "]: " + msg + "\n");
        }
        else if (line.substr(0, 3) == "OK|") {
            auto f = splitFields(line);
            printGreen("OK: " + (f.size() > 1 ? f[1] : "") + "\n");
        }
        else if (line == "BYE") {
            printYellow("BYE - Connection closing.\n");
        }
        else if (line == "RESULT_BEGIN") {
            printYellow("--- Results ---\n");
        }
        else if (line == "RESULT_END") {
            printYellow("--- End ---\n");
        }
        else if (line.substr(0, 12) == "RESULT_NONE|") {
            auto f = splitFields(line);
            printYellow("(no results) " + (f.size() > 1 ? f[1] : "") + "\n");
        }
        else if (line.substr(0, 7) == "RESULT|") {
            // RESULT|code|title|section|instructor|day|time|duration|classroom|semester
            auto f = splitFields(line);
            if (f.size() >= 10) {
                printf("  %-10s %-30s Sec:%-4s %-15s %s %s (%s) Room:%-6s %s\n",
                    f[1].c_str(), f[2].c_str(), f[3].c_str(), f[4].c_str(),
                    f[5].c_str(), f[6].c_str(), f[7].c_str(), f[8].c_str(), f[9].c_str());
            } else {
                std::cout << "  " << line.substr(7) << "\n";
            }
        }
        else if (line.substr(0, 5) == "INFO|") {
            std::cout << line.substr(5) << "\n";
        }
        else {
            std::cout << line << "\n";
        }
    }
}

// ---- Interactive menu ----
void showMenu(bool isAdmin) {
    std::cout << "\n";
    printCyan("=== Timetable Inquiry System ===\n");
    std::cout << "  1. Query by Course Code\n";
    std::cout << "  2. Search by Instructor\n";
    std::cout << "  3. List All Courses\n";
    std::cout << "  4. Search by Time Slot\n";
    if (isAdmin) {
        printYellow("  5. Add Course       [Admin]\n");
        printYellow("  6. Update Course    [Admin]\n");
        printYellow("  7. Delete Course    [Admin]\n");
    }
    std::cout << "  8. Login / Logout\n";
    std::cout << "  9. Help\n";
    std::cout << "  0. Quit\n";
    std::cout << "Choose: ";
}

std::string promptInput(const std::string& prompt) {
    std::cout << prompt;
    std::string val;
    std::getline(std::cin, val);
    return val;
}

bool connectToHost(const std::string& host, int port) {
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port   = htons(port);

    if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) == 1) {
        return connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) != SOCKET_ERROR;
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || result == nullptr) {
        return false;
    }

    bool connected = false;
    for (addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
        auto* ipv4 = (sockaddr_in*)ptr->ai_addr;
        ipv4->sin_port = htons(port);
        if (connect(sock, (sockaddr*)ipv4, sizeof(sockaddr_in)) != SOCKET_ERROR) {
            connected = true;
            break;
        }
    }

    freeaddrinfo(result);
    return connected;
}

int main(int argc, char* argv[]) {
    std::string host = DEFAULT_HOST;
    int port = DEFAULT_PORT;
    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = std::stoi(argv[2]);

    // Init Winsock
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Socket creation failed\n";
        WSACleanup();
        return 1;
    }

    std::cout << "Connecting to " << host << ":" << port << " ...\n";
    if (!connectToHost(host, port)) {
        printRed("Connection failed! Is the server running?\n");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    // Receive welcome
    std::string welcome = recvResponse();
    printResponse(welcome);

    bool loggedIn = false;
    bool isAdmin  = false;

    while (true) {
        showMenu(isAdmin);
        std::string choice;
        std::getline(std::cin, choice);

        if (choice == "0") {
            sendLine("QUIT");
            std::string r = recvResponse();
            printResponse(r);
            break;
        }
        else if (choice == "1") {
            std::string code = promptInput("Enter course code (e.g. COMP3003): ");
            sendLine("QUERY|" + code);
            printResponse(recvResponse());
        }
        else if (choice == "2") {
            std::string name = promptInput("Enter instructor name (partial ok): ");
            sendLine("SEARCH_INSTRUCTOR|" + name);
            printResponse(recvResponse());
        }
        else if (choice == "3") {
            std::string sem = promptInput("Enter semester (leave blank for all): ");
            sendLine(sem.empty() ? "LIST_ALL" : "LIST_ALL|" + sem);
            printResponse(recvResponse());
        }
        else if (choice == "4") {
            std::string day  = promptInput("Enter day (e.g. Mon): ");
            std::string time = promptInput("Enter time (e.g. 10:00): ");
            sendLine("SEARCH_TIME|" + day + "|" + time);
            printResponse(recvResponse());
        }
        else if (choice == "5" && isAdmin) {
            std::cout << "Enter course details (press Enter after each):\n";
            std::string code  = promptInput("  Course Code:   ");
            std::string title = promptInput("  Title:         ");
            std::string sec   = promptInput("  Section:       ");
            std::string instr = promptInput("  Instructor:    ");
            std::string day   = promptInput("  Day (Mon/Tue/Wed/Thu/Fri): ");
            std::string time  = promptInput("  Time (HH:MM):  ");
            std::string dur   = promptInput("  Duration (e.g. 2h): ");
            std::string room  = promptInput("  Classroom:     ");
            std::string sem   = promptInput("  Semester:      ");
            sendLine("ADD|" + code+"|"+title+"|"+sec+"|"+instr+"|"+day+"|"+time+"|"+dur+"|"+room+"|"+sem);
            printResponse(recvResponse());
        }
        else if (choice == "6" && isAdmin) {
            std::string code  = promptInput("Course Code:   ");
            std::string sec   = promptInput("Section:       ");
            std::cout << "Field to update: TITLE / INSTRUCTOR / DAY / TIME / DURATION / CLASSROOM / SEMESTER\n";
            std::string field = promptInput("Field:         ");
            std::string val   = promptInput("New Value:     ");
            sendLine("UPDATE|" + code + "|" + sec + "|" + field + "|" + val);
            printResponse(recvResponse());
        }
        else if (choice == "7" && isAdmin) {
            std::string code = promptInput("Course Code:   ");
            std::string sec  = promptInput("Section:       ");
            sendLine("DELETE|" + code + "|" + sec);
            printResponse(recvResponse());
        }
        else if (choice == "8") {
            if (loggedIn) {
                sendLine("LOGOUT");
                printResponse(recvResponse());
                loggedIn = false;
                isAdmin  = false;
            } else {
                std::string user = promptInput("Username: ");
                std::string pass = promptInput("Password: ");
                sendLine("LOGIN|" + user + "|" + pass);
                std::string resp = recvResponse();
                printResponse(resp);
                // SUCCESS|admin  or  SUCCESS|student
                if (resp.find("SUCCESS|") != std::string::npos) {
                    loggedIn = true;
                    isAdmin  = (resp.find("SUCCESS|admin") != std::string::npos);
                }
            }
        }
        else if (choice == "9") {
            sendLine("HELP");
            printResponse(recvResponse());
        }
        else {
            printRed("Invalid choice.\n");
        }
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}

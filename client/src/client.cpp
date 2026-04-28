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
#define DEFAULT_PORT  8888
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

// ---- Receive full response (until RESULT END, OK, ERROR, SUCCESS, FAILURE, BYE) ----
std::string recvResponse() {
    std::string result;
    char buf[BUFFER_SIZE];
    while (true) {
        int bytes = recv(sock, buf, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) return result;
        buf[bytes] = '\0';
        result += buf;
        // Check if response is complete
        if (result.find("RESULT END\n") != std::string::npos) break;
        if (result.find("RESULT NONE") != std::string::npos) break;
        if (result.find("SUCCESS")     != std::string::npos) break;
        if (result.find("FAILURE")     != std::string::npos) break;
        if (result.find("ERROR")       != std::string::npos) break;
        if (result.find("OK ")         != std::string::npos) break;
        if (result.find("BYE")         != std::string::npos) break;
        if (result.find("WELCOME")     != std::string::npos) break;
        if (result.find("HELP ")       != std::string::npos &&
            result.find("QUIT")        != std::string::npos) break;
    }
    return result;
}

// ---- Pretty-print server response ----
void printResponse(const std::string& resp) {
    std::istringstream ss(resp);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        if (line.substr(0, 7) == "WELCOME") { printCyan(line + "\n"); }
        else if (line.substr(0, 7) == "SUCCESS") { printGreen(line + "\n"); }
        else if (line.substr(0, 7) == "FAILURE") { printRed(line + "\n"); }
        else if (line.substr(0, 5) == "ERROR")   { printRed(line + "\n"); }
        else if (line.substr(0, 2) == "OK")       { printGreen(line + "\n"); }
        else if (line.substr(0, 3) == "BYE")      { printYellow(line + "\n"); }
        else if (line.substr(0, 6) == "RESULT")   {
            if (line == "RESULT BEGIN" || line == "RESULT END") { printYellow(line + "\n"); }
            else { std::cout << "  " << line.substr(7) << "\n"; }
        }
        else { std::cout << line << "\n"; }
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
            sendLine("QUERY " + code);
            printResponse(recvResponse());
        }
        else if (choice == "2") {
            std::string name = promptInput("Enter instructor name (partial ok): ");
            sendLine("SEARCH_INSTRUCTOR " + name);
            printResponse(recvResponse());
        }
        else if (choice == "3") {
            std::string sem = promptInput("Enter semester (leave blank for all): ");
            sendLine("LIST_ALL " + sem);
            printResponse(recvResponse());
        }
        else if (choice == "4") {
            std::string day  = promptInput("Enter day (e.g. Mon): ");
            std::string time = promptInput("Enter time (e.g. 10:00): ");
            sendLine("SEARCH_TIME " + day + " " + time);
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
            sendLine("ADD " + code+"|"+title+"|"+sec+"|"+instr+"|"+day+"|"+time+"|"+dur+"|"+room+"|"+sem);
            printResponse(recvResponse());
        }
        else if (choice == "6" && isAdmin) {
            std::string code  = promptInput("Course Code:   ");
            std::string sec   = promptInput("Section:       ");
            std::cout << "Field to update: TITLE / INSTRUCTOR / DAY / TIME / DURATION / CLASSROOM / SEMESTER\n";
            std::string field = promptInput("Field:         ");
            std::string val   = promptInput("New Value:     ");
            sendLine("UPDATE " + code + " " + sec + " " + field + " " + val);
            printResponse(recvResponse());
        }
        else if (choice == "7" && isAdmin) {
            std::string code = promptInput("Course Code:   ");
            std::string sec  = promptInput("Section:       ");
            sendLine("DELETE " + code + " " + sec);
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
                sendLine("LOGIN " + user + " " + pass);
                std::string resp = recvResponse();
                printResponse(resp);
                if (resp.find("SUCCESS") != std::string::npos) {
                    loggedIn = true;
                    isAdmin  = (resp.find("admin") != std::string::npos);
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

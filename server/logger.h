#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include <ctime>
#include <iostream>

class Logger {
public:
    Logger(const std::string& filename) : logFile(filename) {}

    void log(const std::string& msg) {
        std::lock_guard<std::mutex> lock(mtx);
        std::string ts = timestamp();
        std::string entry = "[" + ts + "] " + msg;
        std::cout << entry << "\n";
        std::ofstream f(logFile, std::ios::app);
        if (f.is_open()) f << entry << "\n";
    }

private:
    std::string logFile;
    std::mutex  mtx;

    std::string timestamp() {
        time_t now = time(nullptr);
        char buf[32];
        struct tm t;
        localtime_s(&t, &now);
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
        return buf;
    }
};

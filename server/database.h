#pragma once
#include "crypto.h"
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>

// ---- Course Record ----
struct Course {
    std::string code;
    std::string title;
    std::string section;
    std::string instructor;
    std::string day;
    std::string time;
    std::string duration;
    std::string classroom;
    std::string semester;

    std::string toString() const {
        return "[" + code + "] " + title +
               " | Sec: " + section +
               " | Instr: " + instructor +
               " | " + day + " " + time + " (" + duration + ")" +
               " | Room: " + classroom +
               " | Sem: " + semester;
    }

    std::string toProtocol() const {
        return "RESULT|" + code + "|" + title + "|" + section + "|" +
               instructor + "|" + day + "|" + time + "|" +
               duration + "|" + classroom + "|" + semester;
    }

    std::string toCSV() const {
        return code + "," + title + "," + section + "," +
               instructor + "," + day + "," + time + "," +
               duration + "," + classroom + "," + semester;
    }
};

// ---- User Record ----
struct User {
    std::string username;
    std::string password;
    std::string role; // "admin" or "student"
};

// ---- Database ----
class Database {
public:
    std::vector<Course> courses;
    std::vector<User>   users;
    std::string courseFile;
    std::string userFile;

    void init(const std::string& cf, const std::string& uf) {
        courseFile = cf;
        userFile   = uf;
        loadCourses();
        loadUsers();
    }

    // ---- Auth ----
    std::string authenticate(const std::string& user, const std::string& pass) {
        // If pass is exactly 64 lowercase hex chars, treat it as a SHA-256 hash.
        bool isHash = (pass.size() == 64);
        if (isHash) {
            for (char ch : pass) {
                if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) {
                    isHash = false;
                    break;
                }
            }
        }
        for (auto& u : users) {
            if (u.username != user) continue;
            if (isHash) {
                if (sha256Hex(u.password) == pass) return u.role;
            } else {
                if (u.password == pass) return u.role;
            }
        }
        return "";
    }

    // ---- Query ----
    std::vector<Course> queryByCode(const std::string& code) {
        std::vector<Course> res;
        std::string upperCode = code;
        for (auto& c : upperCode) c = toupper(c);
        for (auto& c : courses) {
            std::string uc = c.code;
            for (auto& ch : uc) ch = toupper(ch);
            if (uc == upperCode) res.push_back(c);
        }
        return res;
    }

    std::vector<Course> queryByInstructor(const std::string& name) {
        std::vector<Course> res;
        std::string lower = name;
        for (auto& c : lower) c = tolower(c);
        for (auto& c : courses) {
            std::string li = c.instructor;
            for (auto& ch : li) ch = tolower(ch);
            if (li.find(lower) != std::string::npos) res.push_back(c);
        }
        return res;
    }

    std::vector<Course> listAll(const std::string& semester = "") {
        if (semester.empty()) return courses;
        std::vector<Course> res;
        for (auto& c : courses) {
            if (c.semester == semester) res.push_back(c);
        }
        return res;
    }

    std::vector<Course> queryByTime(const std::string& day, const std::string& time) {
        std::vector<Course> res;
        std::string ld = day, lt = time;
        for (auto& c : ld) c = tolower(c);
        for (auto& c : lt) c = tolower(c);
        for (auto& c : courses) {
            std::string cd = c.day, ct = c.time;
            for (auto& ch : cd) ch = tolower(ch);
            for (auto& ch : ct) ch = tolower(ch);
            if (cd == ld && ct == lt) res.push_back(c);
        }
        return res;
    }

    // ---- Advanced Query ----
    // Parses "HH:MM" to minutes since midnight; returns -1 on failure.
    int timeToMinutes(const std::string& t) {
        if (t.size() < 4) return -1;
        size_t colon = t.find(':');
        if (colon == std::string::npos) return -1;
        try {
            int h = std::stoi(t.substr(0, colon));
            int m = std::stoi(t.substr(colon + 1));
            if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
            return h * 60 + m;
        } catch (...) {
            return -1;
        }
    }

    // Filters courses by keyword (substring across multiple fields), day (exact),
    // semester (exact), and time_range ("morning"/"afternoon"/"evening"/"all"/empty).
    // All non-empty, non-"all" filters must match (AND logic).
    std::vector<Course> queryAdvanced(const std::string& keyword,
                                      const std::string& day,
                                      const std::string& semester,
                                      const std::string& timeRange) {
        std::vector<Course> res;

        std::string kw = keyword, dy = day, sem = semester, tr = timeRange;
        for (auto& c : kw)  c = tolower(c);
        for (auto& c : dy)  c = tolower(c);
        for (auto& c : sem) c = tolower(c);
        for (auto& c : tr)  c = tolower(c);

        bool hasKw  = !kw.empty();
        bool hasDay = !dy.empty()  && dy  != "all";
        bool hasSem = !sem.empty() && sem != "all";
        bool hasTr  = !tr.empty()  && tr  != "all";

        for (auto& c : courses) {
            if (hasDay) {
                std::string cd = c.day;
                for (auto& ch : cd) ch = tolower(ch);
                if (cd != dy) continue;
            }
            if (hasSem) {
                std::string cs = c.semester;
                for (auto& ch : cs) ch = tolower(ch);
                if (cs != sem) continue;
            }
            if (hasTr) {
                int courseMin = timeToMinutes(c.time);
                if (courseMin < 0) continue;
                bool inRange = false;
                if      (tr == "morning")   inRange = (courseMin >= 480  && courseMin < 720);
                else if (tr == "afternoon") inRange = (courseMin >= 720  && courseMin < 1080);
                else if (tr == "evening")   inRange = (courseMin >= 1080);
                else continue;
                if (!inRange) continue;
            }
            if (hasKw) {
                auto contains = [&](const std::string& field) {
                    std::string lf = field;
                    for (auto& ch : lf) ch = tolower(ch);
                    return lf.find(kw) != std::string::npos;
                };
                if (!contains(c.code) && !contains(c.title) &&
                    !contains(c.instructor) && !contains(c.classroom) &&
                    !contains(c.day) && !contains(c.time) && !contains(c.semester))
                    continue;
            }
            res.push_back(c);
        }
        return res;
    }

    // ---- Modify ----
    bool addCourse(const Course& c) {
        // Check duplicate (same code + section)
        for (auto& existing : courses) {
            if (existing.code == c.code && existing.section == c.section) return false;
        }
        courses.push_back(c);
        saveCourses();
        return true;
    }

    bool updateCourse(const std::string& code, const std::string& section,
                      const std::string& field, const std::string& value) {
        for (auto& c : courses) {
            if (c.code == code && c.section == section) {
                std::string f = field;
                for (auto& ch : f) ch = toupper(ch);
                if      (f == "TITLE")      c.title      = value;
                else if (f == "INSTRUCTOR") c.instructor = value;
                else if (f == "DAY")        c.day        = value;
                else if (f == "TIME")       c.time       = value;
                else if (f == "DURATION")   c.duration   = value;
                else if (f == "CLASSROOM")  c.classroom  = value;
                else if (f == "SEMESTER")   c.semester   = value;
                else return false;
                saveCourses();
                return true;
            }
        }
        return false;
    }

    bool deleteCourse(const std::string& code, const std::string& section) {
        for (auto it = courses.begin(); it != courses.end(); ++it) {
            if (it->code == code && it->section == section) {
                courses.erase(it);
                saveCourses();
                return true;
            }
        }
        return false;
    }

private:
    std::string trim(const std::string& s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        size_t b = s.find_last_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        return s.substr(a, b - a + 1);
    }

    void loadCourses() {
        std::ifstream f(courseFile);
        if (!f.is_open()) {
            std::cerr << "[DB] Cannot open " << courseFile << ", starting empty.\n";
            createSampleCourses();
            return;
        }
        std::string line;
        std::getline(f, line); // skip header
        while (std::getline(f, line)) {
            line = trim(line);
            if (line.empty()) continue;
            std::stringstream ss(line);
            Course c;
            std::getline(ss, c.code,       ',');
            std::getline(ss, c.title,      ',');
            std::getline(ss, c.section,    ',');
            std::getline(ss, c.instructor, ',');
            std::getline(ss, c.day,        ',');
            std::getline(ss, c.time,       ',');
            std::getline(ss, c.duration,   ',');
            std::getline(ss, c.classroom,  ',');
            std::getline(ss, c.semester,   ',');
            courses.push_back(c);
        }
        std::cout << "[DB] Loaded " << courses.size() << " courses.\n";
    }

    void saveCourses() {
        std::ofstream f(courseFile);
        f << "code,title,section,instructor,day,time,duration,classroom,semester\n";
        for (auto& c : courses) f << c.toCSV() << "\n";
    }

    void loadUsers() {
        std::ifstream f(userFile);
        if (!f.is_open()) {
            std::cerr << "[DB] Cannot open " << userFile << ", creating defaults.\n";
            createDefaultUsers();
            return;
        }
        std::string line;
        std::getline(f, line); // skip header
        while (std::getline(f, line)) {
            line = trim(line);
            if (line.empty()) continue;
            std::stringstream ss(line);
            User u;
            std::getline(ss, u.username, ',');
            std::getline(ss, u.password, ',');
            std::getline(ss, u.role,     ',');
            users.push_back(u);
        }
        std::cout << "[DB] Loaded " << users.size() << " users.\n";
    }

    void createDefaultUsers() {
        users.push_back({"admin",   "admin123", "admin"});
        users.push_back({"student", "stu123",   "student"});
        std::ofstream f(userFile);
        f << "username,password,role\n";
        for (auto& u : users) f << u.username << "," << u.password << "," << u.role << "\n";
        std::cout << "[DB] Created default users file.\n";
    }

    void createSampleCourses() {
        courses = {
            {"COMP3003","Data Communications","S1","Dr. Chan","Mon","10:00","2h","A101","2026S1"},
            {"COMP3003","Data Communications","S2","Dr. Chan","Wed","14:00","2h","A101","2026S1"},
            {"COMP3001","Operating Systems",  "S1","Dr. Lee", "Tue","09:00","2h","B202","2026S1"},
            {"COMP3001","Operating Systems",  "S2","Dr. Lee", "Thu","13:00","2h","B202","2026S1"},
            {"COMP2001","Data Structures",    "S1","Dr. Wong","Mon","08:00","2h","C303","2026S1"},
            {"COMP2001","Data Structures",    "S2","Dr. Wong","Fri","10:00","2h","C303","2026S1"},
            {"COMP4001","Machine Learning",   "S1","Dr. Lam", "Tue","14:00","2h","D404","2026S1"},
            {"COMP4002","Computer Networks",  "S1","Dr. Chan","Thu","10:00","2h","A101","2026S1"},
            {"MATH2001","Discrete Mathematics","S1","Dr. Ng", "Wed","08:00","2h","E505","2026S1"},
            {"MATH2001","Discrete Mathematics","S2","Dr. Ng", "Fri","14:00","2h","E505","2026S1"},
        };
        std::ofstream f(courseFile);
        f << "code,title,section,instructor,day,time,duration,classroom,semester\n";
        for (auto& c : courses) f << c.toCSV() << "\n";
        std::cout << "[DB] Created sample timetable.csv.\n";
    }
};

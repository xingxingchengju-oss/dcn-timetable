# Timetable Inquiry System

DCN 课程作业 2，4 人项目。当前为 C++ Winsock TCP 服务器 + C++ CLI 客户端 + Python GUI 客户端（customtkinter）。

## 技术栈
- 后端：C++17, Winsock2, CSV 文件存储
- 客户端：C++17 CLI（`client/src/client.cpp`）；Python GUI（`client/timetable_gui.py`）
- 构建：CMake、build.bat（MinGW g++）

## 当前结构
- 服务端：`server/server.cpp` + `server/database.h` `logger.h` `protocol.h`
- 数据：`data/timetable.csv` `data/users.csv` `data/schema.sql`
- 文档：`docs/protocol.md` 与作业 PDF

## 当前实现
- 已完成：TCP 连接、多客户端线程、登录/登出、按课程号/教师/时间查询、列出课程、管理员增删改、日志、CLI 与 GUI 连接
- 待办：构建脚本路径未同步（仍指向 `client/client.cpp`）；CMake 同样错误。协议文档和 schema 目前仍是占位，尚未实现 SQLite / Web GUI。

## 约定
- 改协议先更新 `docs/protocol.md`
- 不提交 `*.exe` `*.db` `*.log` `build/` `.vs/`
- 注释与 commit 信息用英文


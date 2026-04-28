@echo off
cd /d "%~dp0"
echo Starting Timetable Server...
echo Working directory: %CD%
echo.
if not exist "logs" mkdir logs
server.exe
pause

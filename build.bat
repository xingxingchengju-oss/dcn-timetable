@echo off
REM ============================================================
REM  Build script for Timetable System (MSVC / cl)
REM  Run from the project root directory
REM  Use Developer Command Prompt for VS 2022, or let this script
REM  call vcvars64.bat automatically to enter the x64 MSVC environment
REM ============================================================

setlocal
REM Try common VS install locations in order. VS 2022 = product version 17,
REM VS 2026 = 18. Order: newest BuildTools first, then older, then full IDE editions.
set "VCVARS64="
for %%V in (18 17) do (
    for %%E in (BuildTools Community Professional Enterprise) do (
        if not defined VCVARS64 (
            if exist "C:\Program Files (x86)\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat" (
                set "VCVARS64=C:\Program Files (x86)\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat"
            )
            if exist "C:\Program Files\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat" (
                set "VCVARS64=C:\Program Files\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat"
            )
        )
    )
)

if not defined VCVARS64 (
    echo [FAIL] vcvars64.bat not found in any standard VS 2022/2026 location.
    echo Please install Visual Studio Build Tools with the Desktop C++ workload.
    pause
    exit /b 1
)

echo [INFO] Using %VCVARS64%
call "%VCVARS64%" amd64 >nul
if errorlevel 1 (
    echo [FAIL] Failed to initialize MSVC environment.
    pause
    exit /b 1
)

if not exist "build" mkdir "build"

echo === Building Server ===
cl /nologo /std:c++17 /EHsc /W3 /O2 /DNDEBUG /MT /I"server" /Fo"build\server.obj" /Fe"server.exe" "server\server.cpp" ws2_32.lib
if errorlevel 1 (
    echo [FAIL] Server build failed!
    pause
    exit /b 1
)
echo [OK] server.exe built.

echo === Building Client ===
cl /nologo /std:c++17 /EHsc /W3 /O2 /DNDEBUG /MT /Fo"build\client.obj" /Fe"client.exe" "client\src\client.cpp" ws2_32.lib
if errorlevel 1 (
    echo [FAIL] Client build failed!
    pause
    exit /b 1
)
echo [OK] client.exe built.

echo.
echo === Build Successful! ===
echo Run:  server.exe          (in one window)
echo Run:  client.exe          (in other windows)
echo Run:  client.exe ^<host^> ^<port^>  (to connect to remote server)
pause
endlocal

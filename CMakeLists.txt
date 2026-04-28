cmake_minimum_required(VERSION 3.15)
project(TimetableSystem CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Server ----
add_executable(server
    server/server.cpp
)
target_include_directories(server PRIVATE server)
target_link_libraries(server PRIVATE ws2_32)

# ---- Client ----
add_executable(client
    client/src/client.cpp
)
target_link_libraries(client PRIVATE ws2_32)

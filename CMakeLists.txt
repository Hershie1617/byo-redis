cmake_minimum_required(VERSION 3.12)
project(redis_server LANGUAGES CXX)

# Enforce C++17 standard as specified
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler warnings for high code quality
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -pedantic -Wconversion -Wno-sign-conversion)
endif()

# Main server executable
add_executable(redis-server
    src/server.cpp
    src/resp.cpp
    src/store.cpp
)

# Unit test executable for RESP parser and KVStore
add_executable(test_resp
    tests/test_resp.cpp
    src/resp.cpp
    src/store.cpp
)

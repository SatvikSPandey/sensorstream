cmake_minimum_required(VERSION 3.24...4.3)
project(SensorStream VERSION 1.0.0 LANGUAGES CXX)

# C++20 — required for std::jthread, std::stop_token, concepts
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Threading — pthreads on Linux (Docker), no-op on Windows
find_package(Threads REQUIRED)

# Windows 10 minimum — required by cpp-httplib (CreateFile2, modern socket APIs)
add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00)

# ── Dependencies via FetchContent ──────────────────────────────────────────
include(FetchContent)

# nlohmann/json — header-only JSON library
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

# Google Test — unit testing framework
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)

# Prevent nlohmann from building its own 1000+ tests
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)

# Force GTest to use the same CRT as this project on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(nlohmann_json googletest)

# ── Main executable ────────────────────────────────────────────────────────
add_executable(sensorstream
    src/main.cpp
    src/sensor_simulator.cpp
    src/processing_engine.cpp
    src/data_store.cpp
    src/api_server.cpp
)

target_include_directories(sensorstream PRIVATE
    src
    third_party
)

target_link_libraries(sensorstream PRIVATE
    nlohmann_json::nlohmann_json
    Threads::Threads
)

if(WIN32)
    target_link_libraries(sensorstream PRIVATE ws2_32)
endif()

target_compile_options(sensorstream PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

# ── Testing ────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)

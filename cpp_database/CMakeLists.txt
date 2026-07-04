cmake_minimum_required(VERSION 3.14)
project(VectorDatabaseNode VERSION 1.0)

# ==========================================
# 1. Compiler Configuration
# ==========================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==========================================
# 2. Dependencies
# ==========================================
# Required for std::thread and mutexes (Week 2)
find_package(Threads REQUIRED)

# ==========================================
# 3. Core Executable Definition
# ==========================================
# List the source files. 
# NOTE: storage.cpp and vector_math.cpp are commented out to prevent 
# compilation errors in Week 1. Uncomment them as you reach Weeks 4 & 5.
set(SOURCES
    src/main.cpp
    src/server.cpp
    # src/storage.cpp
    # src/vector_math.cpp
)

add_executable(database_server ${SOURCES})

# Tell CMake where your header files live. 
# This allows you to write #include "server.hpp" instead of #include "../include/server.hpp"
target_include_directories(database_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Link the threading library to your executable
target_link_libraries(database_server PRIVATE Threads::Threads)

# ==========================================
# 4. Testing Framework (GoogleTest)
# ==========================================
# This automatically downloads and configures GTest for WSL
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/5376968f6948923e2411081fd9372e71a59d8e77.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

# ------------------------------------------
# Test Setup (Commented out until your test files exist)
# ------------------------------------------
# add_executable(run_tests 
#     tests/test_vector_math.cpp 
#     tests/test_storage.cpp
#     src/storage.cpp
#     src/vector_math.cpp
# )
# target_include_directories(run_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
# target_link_libraries(run_tests PRIVATE GTest::gtest_main Threads::Threads)
# add_test(NAME DatabaseTests COMMAND run_tests)
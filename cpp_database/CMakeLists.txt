cmake_minimum_required(VERSION 3.14)
project(VectorDatabaseNode VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -O3 -march=native")
endif()

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPQXX REQUIRED libpqxx)
pkg_check_modules(LIBPQ REQUIRED libpq)

option(BUILD_TESTS "Build unit tests" ON)

include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 10.2.1
)
FetchContent_MakeAvailable(fmt)

if(BUILD_TESTS)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/609281088cfefc76f9d0ce82e1ff6c30cc3591e5.zip
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
endif()

set(SOURCES
    src/main.cpp
    src/server.cpp
    src/postgres_storage.cpp
    src/vector_math.cpp
)

add_executable(database_server ${SOURCES})
target_include_directories(database_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(database_server PRIVATE
    ${LIBPQXX_LIBRARIES}
    ${LIBPQ_LIBRARIES}
    Threads::Threads
    nlohmann_json::nlohmann_json
    fmt::fmt
)
target_include_directories(database_server PRIVATE ${LIBPQXX_INCLUDE_DIRS} ${LIBPQ_INCLUDE_DIRS})
target_compile_options(database_server PRIVATE ${LIBPQXX_CFLAGS_OTHER} ${LIBPQ_CFLAGS_OTHER})

if(BUILD_TESTS)
    add_executable(run_tests
        tests/test_vector_math.cpp
        tests/test_storage.cpp
        src/postgres_storage.cpp
        src/vector_math.cpp
    )
    target_include_directories(run_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(run_tests PRIVATE
        GTest::gtest_main
        Threads::Threads
        ${LIBPQXX_LIBRARIES}
        ${LIBPQ_LIBRARIES}
        nlohmann_json::nlohmann_json
        fmt::fmt
    )
    target_include_directories(run_tests PRIVATE ${LIBPQXX_INCLUDE_DIRS} ${LIBPQ_INCLUDE_DIRS})
    target_compile_options(run_tests PRIVATE ${LIBPQXX_CFLAGS_OTHER} ${LIBPQ_CFLAGS_OTHER})
    add_test(NAME DatabaseTests COMMAND run_tests)
endif()
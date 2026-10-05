cmake_minimum_required(VERSION 3.14)
project(Router VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -O3 -march=native")
endif()

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(OPENSSL REQUIRED openssl)

option(BUILD_TESTS "Build unit tests" ON)

include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(nlohmann_json)

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
    src/router.cpp
    src/hash_ring.cpp
)

add_executable(router ${SOURCES})
target_include_directories(router PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(router PRIVATE
    Threads::Threads
    ${OPENSSL_LIBRARIES}
    nlohmann_json::nlohmann_json
)
target_include_directories(router PRIVATE ${OPENSSL_INCLUDE_DIRS})
target_compile_options(router PRIVATE ${OPENSSL_CFLAGS_OTHER})

if(BUILD_TESTS)
    add_executable(run_tests
        tests/test_hashing.cpp
        src/hash_ring.cpp
    )
    target_include_directories(run_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(run_tests PRIVATE
        GTest::gtest_main
        Threads::Threads
        ${OPENSSL_LIBRARIES}
        nlohmann_json::nlohmann_json
    )
    target_include_directories(run_tests PRIVATE ${OPENSSL_INCLUDE_DIRS})
    target_compile_options(run_tests PRIVATE ${OPENSSL_CFLAGS_OTHER})
    add_test(NAME RouterTests COMMAND run_tests)
endif()
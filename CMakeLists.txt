cmake_minimum_required(VERSION 3.20)

project(SyncVault VERSION 0.1.0 LANGUAGES CXX)

option(SYNCVAULT_BUILD_TESTS "Build SyncVault tests" ON)

add_library(syncvault_core
    src/chunker.cpp
    src/repository.cpp
    src/scanner.cpp
    src/sha256.cpp
)

target_include_directories(syncvault_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(syncvault_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(syncvault_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(syncvault_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(syncvault src/main.cpp)
target_link_libraries(syncvault PRIVATE syncvault_core)

if(MSVC)
    target_compile_options(syncvault PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(syncvault PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SYNCVAULT_BUILD_TESTS)
    enable_testing()

    add_executable(syncvault_tests tests/repository_test.cpp)
    target_link_libraries(syncvault_tests PRIVATE syncvault_core)

    if(MSVC)
        target_compile_options(syncvault_tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(syncvault_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME syncvault_tests COMMAND syncvault_tests)

    add_executable(syncvault_scanner_tests tests/scanner_test.cpp)
    target_link_libraries(syncvault_scanner_tests PRIVATE syncvault_core)

    if(MSVC)
        target_compile_options(syncvault_scanner_tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(syncvault_scanner_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME syncvault_scanner_tests COMMAND syncvault_scanner_tests)

    add_executable(syncvault_chunker_tests tests/chunker_test.cpp)
    target_link_libraries(syncvault_chunker_tests PRIVATE syncvault_core)

    if(MSVC)
        target_compile_options(syncvault_chunker_tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(syncvault_chunker_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME syncvault_chunker_tests COMMAND syncvault_chunker_tests)
endif()

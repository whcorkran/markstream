cmake_minimum_required(VERSION 3.16)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
project(Markstream LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


# ---- Fetch dependencies ----
include(FetchContent)

FetchContent_Declare(
  cmark
  GIT_REPOSITORY https://github.com/commonmark/cmark.git
  GIT_TAG        master
)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)

# Prevent GoogleTest from overriding compiler/linker options
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(cmark googletest)

# ---- Library for core functionality (shared between main and tests) ----
add_library(markstream_lib STATIC
        src/parser.cpp
        src/node.cpp
        src/scanners.cpp
        src/stream_handlers.cpp
        src/ast_node.cpp
)

target_include_directories(markstream_lib PUBLIC
  include
  ${cmark_SOURCE_DIR}/include
  ${cmark_SOURCE_DIR}/src
  ${cmark_BINARY_DIR}/src
)

target_link_libraries(markstream_lib PUBLIC cmark)

# ---- Main executable ----
add_executable(markstream src/main.cpp)

target_link_libraries(markstream PRIVATE markstream_lib)

# ---- Testing ----
enable_testing()

add_executable(markstream_tests
        tests/test_scanners.cpp
        tests/test_ast_node.cpp
)

target_link_libraries(markstream_tests PRIVATE
        markstream_lib
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(markstream_tests)

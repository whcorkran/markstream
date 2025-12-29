cmake_minimum_required(VERSION 3.16)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
project(Markstream LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


# ---- Fetch cmark from GitHub ----
include(FetchContent)

FetchContent_Declare(
  cmark
  GIT_REPOSITORY https://github.com/commonmark/cmark.git
  GIT_TAG        master   # Stable release; use "master" if you want latest
)
FetchContent_MakeAvailable(cmark)

add_executable(markstream
        src/main.cpp
        src/parser.cpp
)

target_include_directories(markstream PRIVATE
  include
  ${cmark_SOURCE_DIR}/include
  ${cmark_SOURCE_DIR}/src
  ${cmark_BINARY_DIR}/src
)

target_link_libraries(markstream PRIVATE cmark)

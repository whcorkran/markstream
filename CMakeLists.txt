cmake_minimum_required(VERSION 3.16)
project(Markstream LANGUAGES CXX)

# ---- Ensure C++17 or later ----
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# ---- Fetch cmark from GitHub ----
include(FetchContent)

FetchContent_Declare(
  cmark
  GIT_REPOSITORY https://github.com/commonmark/cmark.git
  GIT_TAG        0.30.3   # Stable release; use "master" if you want latest
)
FetchContent_MakeAvailable(cmark)

add_executable(markstream
        src/main.cpp
        src/parser.cpp
)

target_link_libraries(markstream PRIVATE cmark)
target_include_directories(markstream PRIVATE include)


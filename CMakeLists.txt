cmake_minimum_required(VERSION 3.22)
project(obs-playlist-deck VERSION 1.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC reads a UTF-8 source file as the system ANSI code page unless told
# otherwise, and re-encodes narrow literals to that same page: the Cyrillic,
# Chinese, Japanese and Korean language names in the Settings dialog would come
# out as "?" on a Windows build. /utf-8 sets both the source and the execution
# charset to UTF-8, which is what GCC and Clang already do.
if(MSVC)
  add_compile_options(/utf-8)
endif()

option(BUILD_PLUGIN "Build the OBS plugin (needs libobs + Qt6)" ON)
option(BUILD_TESTS "Build unit tests" ON)

add_subdirectory(src/core)

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(BUILD_PLUGIN AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/CMakeLists.txt")
  add_subdirectory(src/plugin)
endif()

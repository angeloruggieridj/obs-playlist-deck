cmake_minimum_required(VERSION 3.22)
project(obs-playlist-deck VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

add_executable(playlist-deck-tests
  test_main.cpp
  test_smoke.cpp
  test_playlist.cpp
  test_mediapath.cpp
  test_playlistio.cpp
  test_format.cpp
  test_version.cpp
)
target_link_libraries(playlist-deck-tests PRIVATE playlist-deck-core)
target_include_directories(playlist-deck-tests PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
add_test(NAME core COMMAND playlist-deck-tests)

add_library(playlist-deck-core STATIC
  Playlist.cpp
  MediaPath.cpp
  PlaylistIO.cpp
)
target_include_directories(playlist-deck-core
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PUBLIC ${CMAKE_SOURCE_DIR}/third_party
)
target_compile_features(playlist-deck-core PUBLIC cxx_std_17)

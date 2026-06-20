add_library(playlist-deck-core STATIC
  Playlist.cpp
  MediaPath.cpp
  PlaylistIO.cpp
  Format.cpp
)
target_include_directories(playlist-deck-core
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PUBLIC ${CMAKE_SOURCE_DIR}/third_party
)
target_compile_features(playlist-deck-core PUBLIC cxx_std_17)

# The core is linked into the shared plugin module; on Linux a static lib must
# be position-independent to be linked into a .so.
set_target_properties(playlist-deck-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

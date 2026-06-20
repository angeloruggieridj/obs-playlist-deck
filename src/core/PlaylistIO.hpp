#pragma once
#include <string>
#include <vector>
#include "Playlist.hpp"

namespace pld::io {

std::string toJson(const std::string& name, const std::vector<PlaylistItem>& items);
bool fromJson(const std::string& text, std::string& nameOut, std::vector<PlaylistItem>& itemsOut);
std::string toM3u(const std::vector<PlaylistItem>& items);
std::vector<PlaylistItem> parseM3u(const std::string& text);

} // namespace pld::io

#pragma once
#include <string>

namespace pld::mediapath {

std::string extensionLower(const std::string& path);
bool isMediaFile(const std::string& path);
std::string fileStem(const std::string& path);

} // namespace pld::mediapath

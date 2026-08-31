// SPDX-License-Identifier: MIT
#include "SourceKind.hpp"
#include <cstring>

namespace pld {

SourceKind sourceKindFromId(const char* id) {
    if (!id) return SourceKind::Unsupported;
    if (std::strcmp(id, "ffmpeg_source") == 0) return SourceKind::Ffmpeg;
    if (std::strcmp(id, "vlc_source") == 0) return SourceKind::Vlc;
    return SourceKind::Unsupported;
}

bool isBindableSourceId(const char* id) { return sourceKindFromId(id) != SourceKind::Unsupported; }

} // namespace pld

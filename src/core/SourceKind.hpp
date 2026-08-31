// SPDX-License-Identifier: MIT
#pragma once
#include <string>

namespace pld {

// Which media source implementation the dock is driving. The two OBS sources it
// can bind take the file to play through completely different settings, so the
// controller has to know which one it holds:
//
//   ffmpeg_source  ->  is_local_file=true + local_file="<path>"
//   vlc_source     ->  playlist=[{ "value": "<path>" }]   (obs_data_array)
//
// Writing local_file on a VLC source is silently ignored by it — the bug this
// enum exists to make impossible.
enum class SourceKind { Unsupported = 0, Ffmpeg, Vlc };

// Maps an obs_source_get_id() string to the kind. nullptr / unknown ids are
// Unsupported, which callers treat as "not bindable".
SourceKind sourceKindFromId(const char* id);

// True when the id names a source this plugin can drive (used by the source
// picker to decide what to list).
bool isBindableSourceId(const char* id);

} // namespace pld

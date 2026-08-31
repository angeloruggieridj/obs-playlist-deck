// SPDX-License-Identifier: MIT
#pragma once
#include <obs.h>

namespace pld {

// Emits an obs-websocket vendor event under this plugin's name, if
// obs-websocket is installed and the vendor registered. Implemented in
// plugin-main.cpp, which owns the vendor handle; the dock calls it so remote
// clients can follow playback instead of polling GetStatus.
//
// Call from the UI thread. Ownership of `data` stays with the caller.
void emitVendorEvent(const char* name, obs_data_t* data);

} // namespace pld

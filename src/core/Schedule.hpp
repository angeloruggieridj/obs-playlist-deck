// SPDX-License-Identifier: MIT
#pragma once

namespace pld {

// A playlist can be told to start at a wall-clock time. What the deck does
// about that is decided here, from the clock alone, so every edge — the time
// already passed, the warning window, the moment itself — is testable without
// waiting for it.
enum class ScheduleState {
    None,     // nothing scheduled
    Waiting,  // scheduled, and further away than the warning window
    Warning,  // inside the warning window: the operator should be told
    Due,      // the moment arrived (or passed while OBS was closed): start now
};

struct ScheduleStatus {
    ScheduleState state = ScheduleState::None;
    long long remainingMs = 0; // until the start; 0 once due
};

// `startMs` and `nowMs` are milliseconds since the epoch; a negative `startMs`
// means nothing is scheduled. `warnSeconds` is how long before the start the
// deck begins saying so.
//
// A time that has already passed is Due rather than ignored: OBS may have been
// closed, or the operator may have set 21:30 at 21:31 meaning "now".
inline ScheduleStatus scheduleStatus(long long startMs, long long nowMs, int warnSeconds = 10) {
    ScheduleStatus out;
    if (startMs < 0) return out;
    const long long remaining = startMs - nowMs;
    out.remainingMs = remaining > 0 ? remaining : 0;
    if (remaining <= 0) {
        out.state = ScheduleState::Due;
    } else if (remaining <= static_cast<long long>(warnSeconds) * 1000) {
        out.state = ScheduleState::Warning;
    } else {
        out.state = ScheduleState::Waiting;
    }
    return out;
}

// Guard for the one thing a schedule must never do: fire twice. The caller
// keeps the time it last fired for, and a schedule that has already been
// honoured stays quiet until it is set to something else.
inline bool shouldFireSchedule(long long startMs, long long nowMs, long long lastFiredStartMs,
                               int warnSeconds = 10) {
    if (startMs < 0 || startMs == lastFiredStartMs) return false;
    return scheduleStatus(startMs, nowMs, warnSeconds).state == ScheduleState::Due;
}

} // namespace pld

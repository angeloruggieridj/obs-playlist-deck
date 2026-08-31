// SPDX-License-Identifier: MIT
#pragma once
#include <QColor>
#include <QPalette>
#include <QString>

// Design tokens for the dock.
//
// The dock is a narrow panel inside OBS, read at a glance during a live show,
// usually on a dark theme, by someone who is looking somewhere else. So: follow
// the host theme rather than inventing one, keep every colour semantic and in
// one place, and derive what can be derived from the palette OBS hands us —
// OBS 31 can switch theme while running, and hardcoded colours do not follow.
//
// Before this, one hardcoded "#e06c75" was scattered through three call sites
// and served as both "file missing" and "error", at a contrast ratio of about
// 3.9:1 on the default dark theme — below the 4.5:1 that makes text readable.
namespace pld::style {

// A missing file is a warning, not a failure: amber reads as "attention" and
// clears 7:1 on OBS dark, while red is kept for things that actually failed.
inline QColor warning(const QPalette& pal) {
    return pal.color(QPalette::Window).lightness() < 128 ? QColor("#e5a00d") : QColor("#9a6700");
}

inline QColor error(const QPalette& pal) {
    return pal.color(QPalette::Window).lightness() < 128 ? QColor("#f07c7c") : QColor("#b42318");
}

inline QColor success(const QPalette& pal) {
    return pal.color(QPalette::Window).lightness() < 128 ? QColor("#6fcf8d") : QColor("#137333");
}

// Actions, focus and "now playing" all share one accent, from the same blue
// family OBS uses for selection.
inline QColor accent(const QPalette& pal) {
    return pal.color(QPalette::Window).lightness() < 128 ? QColor("#4f8cff") : QColor("#1d5fd8");
}

// Secondary text: the same ink as the primary, dimmed rather than a separate
// grey, so it stays legible in whatever theme is loaded.
inline QColor textSecondary(const QPalette& pal) {
    QColor c = pal.color(QPalette::WindowText);
    c.setAlpha(160);
    return c;
}

inline bool isDark(const QPalette& pal) { return pal.color(QPalette::Window).lightness() < 128; }

// Raised surface for the now-playing card: a translucent lift off the window
// colour, which works on any theme without picking a colour of its own.
inline QString raisedSurface(const QPalette& pal) {
    return isDark(pal) ? QStringLiteral("rgba(255,255,255,0.05)")
                       : QStringLiteral("rgba(0,0,0,0.04)");
}

inline QString subtleBorder(const QPalette& pal) {
    return isDark(pal) ? QStringLiteral("rgba(255,255,255,0.10)")
                       : QStringLiteral("rgba(0,0,0,0.12)");
}

// Metrics, in logical pixels. 28 px is the smallest comfortable click target;
// everything else is squeezed so the list — the only part whose height is worth
// anything — keeps the rest.
inline constexpr int kIconPx = 16;
inline constexpr int kButtonPx = 28;
inline constexpr int kRowPx = 26;
inline constexpr int kRadius = 5;

// The whole stylesheet for the dock, built from the live palette so a theme
// switch can simply re-apply it. Deliberately small: only the selectors the
// restyle needs, so OBS's own theme keeps control of everything else.
QString sheet(const QPalette& pal);

} // namespace pld::style

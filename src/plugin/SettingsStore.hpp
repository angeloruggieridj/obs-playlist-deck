// SPDX-License-Identifier: MIT
#pragma once
#include <QByteArray>
#include <QString>
#include <vector>
#include "EndMode.hpp"
#include "Playlist.hpp"

// Everything the deck remembers between sessions, in one struct.
struct DeckSettings {
    pld::EndMode mode = pld::EndMode::PlayNext;
    // The media source the user chose. Survives scene collection switches where
    // the incoming collection has no source by that name, so the binding comes
    // back when they switch to a collection that does.
    QString source;
    bool enableProbe = true;
    bool autoRestore = false;
    bool relativePaths = false;
    // Whether a clip that starts should clear the source's mute. Off by
    // default: the mute stays where the operator put it, which is what a live
    // tool should do unless told otherwise.
    bool unmuteOnStart = false;
    QString language = "auto"; // "auto" (follow OBS) | "en-US" | "it-IT" | ...
    QString lastDir;           // last folder used in a file dialog
};

// The restored playlist, plus what the caller needs to know about how it came
// back.
struct SessionData {
    std::vector<pld::PlaylistItem> items;
    QString loadedPath;
    bool ok = false;
    // The file was written by a newer version of the plugin and was set aside
    // rather than half-understood; the caller tells the user.
    bool fromNewerVersion = false;
};

// Reads and writes the two files the plugin owns in its OBS config folder.
//
// Separated from the dock because persistence has nothing to do with widgets,
// and because every write here has to be atomic: a truncate-then-write leaves a
// half-written file behind if anything interrupts it, and truncated JSON does
// not parse — the whole saved playlist would be silently gone at the next
// start.
class SettingsStore {
public:
    // Writes a temporary alongside the target and renames it into place, which
    // is atomic on NTFS, ext4 and APFS alike. Public because saving a playlist
    // the user named deserves the same treatment.
    static bool writeAtomically(const QString& path, const QByteArray& bytes);

    QString settingsPath() const;
    QString sessionPath() const;

    DeckSettings loadSettings() const;
    bool saveSettings(const DeckSettings& settings) const;

    SessionData loadSession() const;
    bool saveSession(const std::vector<pld::PlaylistItem>& items, const QString& loadedPath) const;
};

// SPDX-License-Identifier: MIT
#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <vector>
#include "EndMode.hpp"
#include "Library.hpp"
#include "Playlist.hpp"

// Everything the deck remembers between sessions, apart from the playlists
// themselves (those are the library).
struct DeckSettings {
    pld::EndMode mode = pld::EndMode::PlayNext;
    // The media source the user chose. Survives scene collection switches where
    // the incoming collection has no source by that name, so the binding comes
    // back when they switch to a collection that does.
    QString source;
    bool enableProbe = true;
    bool relativePaths = false;
    // Whether a clip that starts should clear the source's mute. Off by
    // default: the mute stays where the operator put it, which is what a live
    // tool should do unless told otherwise.
    bool unmuteOnStart = false;
    // Scene the panic button cuts to. Empty means panic only stops playback.
    QString panicScene;
    QString language = "auto"; // "auto" (follow OBS) | "en-US" | "it-IT" | ...
    QString lastDir;           // last folder used in a file dialog
};

// The restored library, plus what the caller needs to know about how it came
// back.
struct LibraryData {
    std::vector<pld::PlaylistEntry> entries;
    int active = 0;
    bool ok = false;
    // The file came from a newer version of the plugin and was set aside rather
    // than half-understood; the caller tells the user.
    bool fromNewerVersion = false;
    // It was a 1.3.x session file, migrated into a one-playlist library.
    bool migrated = false;
};

// Reads and writes the files the plugin owns in its OBS config folder.
//
// Separated from the dock because persistence has nothing to do with widgets,
// and because every write here has to be atomic: a truncate-then-write leaves a
// half-written file behind if anything interrupts it, and truncated JSON does
// not parse — the whole library would be silently gone at the next start.
class SettingsStore {
public:
    // Writes a temporary alongside the target and renames it into place, which
    // is atomic on NTFS, ext4 and APFS alike. Public because saving a playlist
    // the user named deserves the same treatment.
    static bool writeAtomically(const QString& path, const QByteArray& bytes);

    QString settingsPath() const;
    QString libraryPath() const;
    QString backupDir() const;

    DeckSettings loadSettings() const;
    bool saveSettings(const DeckSettings& settings) const;

    // The library is not optional state: a deck that forgot the sets someone
    // named would be broken, not configurable. It is always saved and always
    // restored. A 1.3.x session.json is migrated on first read and left in
    // place, so downgrading loses nothing.
    LibraryData loadLibrary() const;
    bool saveLibrary(const std::vector<pld::PlaylistEntry>& entries, int active);

    // Copies the current library aside. Rate-limited internally, because the
    // library is saved on every edit and twenty backups of the same minute are
    // worth nothing; `force` overrides that, for shutdown.
    void backupLibrary(bool force = false);
    // Newest first, absolute paths.
    QStringList backups() const;
    // Reads one back. `ok` is false when the file is unreadable or not a library.
    LibraryData readBackup(const QString& path) const;

private:
    // Epoch ms of the last backup written, so the rate limit survives a burst of
    // edits but not a restart.
    qint64 lastBackupMs_ = 0;
};

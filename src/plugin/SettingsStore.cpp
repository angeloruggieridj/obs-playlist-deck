// SPDX-License-Identifier: MIT
#include "SettingsStore.hpp"
#include "PlaylistIO.hpp"

#include <obs-module.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {
// Resolves a file inside the plugin's own OBS config folder, creating the
// folder if this is the first run.
QString configPath(const char* name) {
    char* p = obs_module_config_path(name);
    QString s = p ? QString::fromUtf8(p) : QString();
    bfree(p);
    if (!s.isEmpty()) QDir().mkpath(QFileInfo(s).absolutePath());
    return s;
}

// How many copies of the library to keep, and how often to take one. Twenty
// backups of the same minute are worth nothing; twenty spread over a show are
// worth a great deal.
constexpr int kMaxBackups = 20;
constexpr qint64 kBackupIntervalMs = 10 * 60 * 1000;
} // namespace

bool SettingsStore::writeAtomically(const QString& path, const QByteArray& bytes) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    if (f.write(bytes) != bytes.size()) {
        f.cancelWriting();
        return false;
    }
    return f.commit();
}

QString SettingsStore::settingsPath() const { return configPath("settings.json"); }
QString SettingsStore::libraryPath() const { return configPath("library.json"); }

QString SettingsStore::backupDir() const {
    const QString marker = configPath("backups/.keep");
    if (marker.isEmpty()) return {};
    return QFileInfo(marker).absolutePath();
}

DeckSettings SettingsStore::loadSettings() const {
    DeckSettings out;
    QFile f(settingsPath());
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) return out;
    const QJsonObject o = doc.object();
    out.mode = pld::endModeFromInt(o.value("mode").toInt(0));
    out.source = o.value("source").toString();
    out.enableProbe = o.value("enableProbe").toBool(true);
    out.relativePaths = o.value("relativePaths").toBool(false);
    out.unmuteOnStart = o.value("unmuteOnStart").toBool(false);
    out.panicScene = o.value("panicScene").toString();
    out.language = o.value("language").toString("auto");
    out.lastDir = o.value("lastDir").toString();
    return out;
}

bool SettingsStore::saveSettings(const DeckSettings& settings) const {
    const QString path = settingsPath();
    if (path.isEmpty()) return false;
    QJsonObject o;
    o["mode"] = static_cast<int>(settings.mode);
    // The remembered choice, never the combo's current text: that reads as the
    // "no source configured" placeholder whenever the active scene collection
    // does not contain the chosen source.
    o["source"] = settings.source;
    o["enableProbe"] = settings.enableProbe;
    o["relativePaths"] = settings.relativePaths;
    o["unmuteOnStart"] = settings.unmuteOnStart;
    o["panicScene"] = settings.panicScene;
    o["language"] = settings.language;
    o["lastDir"] = settings.lastDir;
    return writeAtomically(path, QJsonDocument(o).toJson(QJsonDocument::Compact));
}

bool SettingsStore::saveLibrary(const std::vector<pld::PlaylistEntry>& entries, int active) {
    const QString path = libraryPath();
    if (path.isEmpty()) return false;
    return writeAtomically(path,
                           QByteArray::fromStdString(pld::io::toLibraryJson(entries, active)));
}

LibraryData SettingsStore::loadLibrary() const {
    LibraryData out;
    const QString path = libraryPath();

    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        const QByteArray raw = f.readAll();
        f.close();
        // The schema version is read, not just written: a file from a future
        // version is kept aside rather than half-understood.
        const QJsonDocument doc = QJsonDocument::fromJson(raw);
        if (doc.isObject() && doc.object().value("version").toInt(1) > 2) {
            QFile::remove(path + ".bak");
            QFile::rename(path, path + ".bak");
            out.fromNewerVersion = true;
            return out;
        }
        out.ok = pld::io::fromLibraryJson(raw.toStdString(), out.entries, out.active);
        if (out.ok) return out;
    }

    // No library yet: a session file from 1.3.x becomes the first playlist.
    // The old file is left where it is, so downgrading loses nothing.
    QFile session(configPath("session.json"));
    if (session.open(QIODevice::ReadOnly)) {
        const QByteArray raw = session.readAll();
        session.close();
        if (pld::io::fromLibraryJson(raw.toStdString(), out.entries, out.active)) {
            const QJsonDocument doc = QJsonDocument::fromJson(raw);
            if (doc.isObject() && !out.entries.empty()) {
                const QString loaded = doc.object().value("loadedPath").toString();
                out.entries.front().sourcePath = loaded.toStdString();
            }
            out.ok = true;
            out.migrated = true;
        }
    }
    return out;
}

void SettingsStore::backupLibrary(bool force) {
    const QString source = libraryPath();
    const QString dir = backupDir();
    if (source.isEmpty() || dir.isEmpty() || !QFile::exists(source)) return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!force && lastBackupMs_ != 0 && now - lastBackupMs_ < kBackupIntervalMs) return;
    lastBackupMs_ = now;

    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    QFile in(source);
    if (!in.open(QIODevice::ReadOnly)) return;
    const QByteArray raw = in.readAll();
    in.close();
    if (raw.isEmpty()) return;
    writeAtomically(dir + "/library-" + stamp + ".json", raw);

    // Oldest first, so the tail beyond the cap is what goes.
    QDir folder(dir);
    const QFileInfoList files =
        folder.entryInfoList({"library-*.json"}, QDir::Files, QDir::Name | QDir::Reversed);
    for (int i = kMaxBackups; i < files.size(); ++i) QFile::remove(files[i].absoluteFilePath());
}

QStringList SettingsStore::backups() const {
    const QString dir = backupDir();
    if (dir.isEmpty()) return {};
    QStringList out;
    // The names sort chronologically, so newest first is a reversed name sort.
    for (const QFileInfo& info :
         QDir(dir).entryInfoList({"library-*.json"}, QDir::Files, QDir::Name | QDir::Reversed))
        out << info.absoluteFilePath();
    return out;
}

LibraryData SettingsStore::readBackup(const QString& path) const {
    LibraryData out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QByteArray raw = f.readAll();
    f.close();
    out.ok = pld::io::fromLibraryJson(raw.toStdString(), out.entries, out.active);
    return out;
}

// SPDX-License-Identifier: MIT
#include "SettingsStore.hpp"
#include "PlaylistIO.hpp"

#include <obs-module.h>

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
QString SettingsStore::sessionPath() const { return configPath("session.json"); }

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
    out.autoRestore = o.value("autoRestore").toBool(false);
    out.relativePaths = o.value("relativePaths").toBool(false);
    out.unmuteOnStart = o.value("unmuteOnStart").toBool(false);
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
    o["autoRestore"] = settings.autoRestore;
    o["relativePaths"] = settings.relativePaths;
    o["unmuteOnStart"] = settings.unmuteOnStart;
    o["language"] = settings.language;
    o["lastDir"] = settings.lastDir;
    return writeAtomically(path, QJsonDocument(o).toJson(QJsonDocument::Compact));
}

bool SettingsStore::saveSession(const std::vector<pld::PlaylistItem>& items,
                                const QString& loadedPath) const {
    const QString path = sessionPath();
    if (path.isEmpty()) return false;
    // Reuse the playlist serializer, then add what only a session carries.
    QJsonDocument doc =
        QJsonDocument::fromJson(QByteArray::fromStdString(pld::io::toJson("session", items)));
    if (!doc.isObject()) return false;
    QJsonObject o = doc.object();
    // Restoring the playlist but not which file it came from left the label
    // claiming nothing was loaded.
    o["loadedPath"] = loadedPath;
    return writeAtomically(path, QJsonDocument(o).toJson(QJsonDocument::Compact));
}

SessionData SettingsStore::loadSession() const {
    SessionData out;
    const QString path = sessionPath();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QByteArray raw = f.readAll();
    f.close();

    // The schema version was written from the first release but never read. A
    // file from a future version is kept aside rather than half-understood.
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (doc.isObject() && doc.object().value("version").toInt(1) > 1) {
        QFile::remove(path + ".bak");
        QFile::rename(path, path + ".bak");
        out.fromNewerVersion = true;
        return out;
    }

    std::string name;
    if (!pld::io::fromJson(raw.toStdString(), name, out.items).ok) return out;
    if (doc.isObject()) out.loadedPath = doc.object().value("loadedPath").toString();
    out.ok = true;
    return out;
}

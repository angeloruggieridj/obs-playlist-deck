#include "PlaylistDock.hpp"
#include "PlaylistIO.hpp"
#include "MediaPath.hpp"
#include "MediaProbe.hpp"
#include "Format.hpp"
#include "Version.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QAbstractItemView>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSvgRenderer>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

#ifndef PLD_VERSION
#define PLD_VERSION "0.0.0"
#endif

using namespace pld;

namespace {
const char* kReleasesUrl = "https://github.com/angeloruggieridj/obs-playlist-deck/releases";
const char* kLatestApi =
    "https://api.github.com/repos/angeloruggieridj/obs-playlist-deck/releases/latest";
}

PlaylistDock::PlaylistDock(QWidget* parent) : QDockWidget(parent) {
    setWindowTitle(QString::fromUtf8(obs_module_text("PlaylistDeck")));
    setObjectName("obs-playlist-deck-dock");
    buildUi();
    refreshSources();
    refreshPlaylistCombo();
    controller_.setOnMediaEnded([this]() {
        QMetaObject::invokeMethod(this, "onMediaEnded", Qt::QueuedConnection);
    });
    registerHotkeys();
    checkForUpdate();
}

PlaylistDock::~PlaylistDock() {
    unregisterHotkeys();
    controller_.setOnMediaEnded(nullptr);
    controller_.unbind();
}

QIcon PlaylistDock::tintedIcon(const QString& resource) const {
    QSvgRenderer renderer(resource);
    const int logical = 16;
    const qreal dpr = devicePixelRatioF();
    QPixmap pm(static_cast<int>(logical * dpr), static_cast<int>(logical * dpr));
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        renderer.render(&p);
    }
    // Tint the rendered shape with the current theme's button text color.
    QColor c = palette().color(QPalette::ButtonText);
    {
        QPainter p(&pm);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(pm.rect(), c);
    }
    pm.setDevicePixelRatio(dpr);
    return QIcon(pm);
}

void PlaylistDock::buildUi() {
    auto* root = new QWidget(this);
    auto* col = new QVBoxLayout(root);
    col->setContentsMargins(6, 6, 6, 6);
    col->setSpacing(5);

    auto sectionLabel = [](const QString& text) {
        auto* l = new QLabel(text);
        QFont f = l->font();
        f.setBold(true);
        l->setFont(f);
        return l;
    };

    // Text + icon button.
    auto mk = [this](const QString& icon, const QString& text, const QString& tip) {
        auto* b = new QToolButton();
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setIcon(tintedIcon(icon));
        b->setText(text);
        b->setToolTip(tip);
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };

    // ---- Media source -----------------------------------------------------
    col->addWidget(sectionLabel("Media source"));
    sourceCombo_ = new QComboBox();
    sourceCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    sourceCombo_->setMinimumHeight(26);
    auto* refreshBtn = mk(":/icons/refresh.svg", "Refresh", "Rescan media sources in the current setup");
    auto* srcRow = new QHBoxLayout();
    srcRow->addWidget(sourceCombo_, 1);
    srcRow->addWidget(refreshBtn);
    col->addLayout(srcRow);

    // ---- Playlist ---------------------------------------------------------
    col->addWidget(sectionLabel("Playlist"));
    list_ = new QListWidget();
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    col->addWidget(list_, 1);

    auto* opsRow = new QHBoxLayout();
    opsRow->setSpacing(3);
    auto* addBtn = mk(":/icons/plus.svg", "Add", "Add media files");
    auto* rmBtn = mk(":/icons/minus.svg", "Remove", "Remove selected item");
    auto* upBtn = mk(":/icons/chevron-up.svg", "Up", "Move selected item up");
    auto* downBtn = mk(":/icons/chevron-down.svg", "Down", "Move selected item down");
    auto* clrBtn = mk(":/icons/x.svg", "Clear", "Clear the playlist");
    for (auto* b : {addBtn, rmBtn, upBtn, downBtn, clrBtn}) opsRow->addWidget(b);
    opsRow->addStretch(1);
    col->addLayout(opsRow);

    auto* trRow = new QHBoxLayout();
    trRow->setSpacing(3);
    auto* playSelBtn = mk(":/icons/play.svg", "Play", "Play the selected item");
    auto* prevBtn = mk(":/icons/skip-back.svg", "Prev", "Previous item");
    auto* playPauseBtn = mk(":/icons/pause.svg", "Pause", "Play / pause the bound source");
    auto* stopBtn = mk(":/icons/stop.svg", "Stop", "Stop playback");
    auto* nextBtn = mk(":/icons/skip-forward.svg", "Next", "Next item");
    for (auto* b : {playSelBtn, prevBtn, playPauseBtn, stopBtn, nextBtn}) trRow->addWidget(b);
    trRow->addStretch(1);
    col->addLayout(trRow);

    auto* endRow = new QHBoxLayout();
    endRow->addWidget(new QLabel("On end:"));
    endCombo_ = new QComboBox();
    endCombo_->addItem("Play next");
    endCombo_->addItem("Loop");
    endCombo_->addItem("Load next (paused)");
    endCombo_->addItem("Stop");
    endCombo_->setCurrentIndex(static_cast<int>(mode_));
    endCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    endRow->addWidget(endCombo_, 1);
    col->addLayout(endRow);

    // ---- Saved playlists --------------------------------------------------
    col->addWidget(sectionLabel("Saved playlists"));
    playlistCombo_ = new QComboBox();
    playlistCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    playlistCombo_->setMinimumHeight(26);
    col->addWidget(playlistCombo_);

    auto* plRow = new QHBoxLayout();
    plRow->setSpacing(3);
    auto* saveBtn = mk(":/icons/save.svg", "Save", "Save current playlist with a new name");
    auto* loadBtn = mk(":/icons/folder-open.svg", "Load", "Load the selected saved playlist");
    auto* renameBtn = mk(":/icons/pencil.svg", "Rename", "Rename the selected saved playlist");
    auto* delBtn = mk(":/icons/trash.svg", "Delete", "Delete the selected saved playlist");
    for (auto* b : {saveBtn, loadBtn, renameBtn, delBtn}) plRow->addWidget(b);
    plRow->addStretch(1);
    col->addLayout(plRow);

    auto* ioRow = new QHBoxLayout();
    ioRow->setSpacing(3);
    auto* impBtn = mk(":/icons/download.svg", "Import", "Import a playlist file (.json / .m3u)");
    auto* expBtn = mk(":/icons/upload.svg", "Export", "Export the playlist to a file (.json / .m3u)");
    for (auto* b : {impBtn, expBtn}) ioRow->addWidget(b);
    ioRow->addStretch(1);
    col->addLayout(ioRow);

    // ---- Status + version -------------------------------------------------
    status_ = new QLabel("");
    status_->setWordWrap(true);
    col->addWidget(status_);

    versionLabel_ = new QLabel(QStringLiteral("v%1").arg(PLD_VERSION));
    versionLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    versionLabel_->setTextFormat(Qt::RichText);
    versionLabel_->setOpenExternalLinks(true);
    versionLabel_->setStyleSheet("color: palette(mid);");
    auto* verRow = new QHBoxLayout();
    verRow->addStretch(1);
    verRow->addWidget(versionLabel_);
    col->addLayout(verRow);

    setWidget(root);

    connect(refreshBtn, &QToolButton::clicked, this, &PlaylistDock::refreshSources);
    connect(addBtn, &QToolButton::clicked, this, &PlaylistDock::onAddFiles);
    connect(rmBtn, &QToolButton::clicked, this, &PlaylistDock::onRemove);
    connect(upBtn, &QToolButton::clicked, this, &PlaylistDock::onUp);
    connect(downBtn, &QToolButton::clicked, this, &PlaylistDock::onDown);
    connect(clrBtn, &QToolButton::clicked, this, &PlaylistDock::onClear);
    connect(playSelBtn, &QToolButton::clicked, this, &PlaylistDock::onPlaySelected);
    connect(prevBtn, &QToolButton::clicked, this, &PlaylistDock::onPrev);
    connect(playPauseBtn, &QToolButton::clicked, this, &PlaylistDock::onTogglePlayPause);
    connect(stopBtn, &QToolButton::clicked, this, &PlaylistDock::onStop);
    connect(nextBtn, &QToolButton::clicked, this, &PlaylistDock::onNext);
    connect(saveBtn, &QToolButton::clicked, this, &PlaylistDock::onSavePlaylist);
    connect(loadBtn, &QToolButton::clicked, this, &PlaylistDock::onLoadPlaylist);
    connect(renameBtn, &QToolButton::clicked, this, &PlaylistDock::onRenamePlaylist);
    connect(delBtn, &QToolButton::clicked, this, &PlaylistDock::onDeletePlaylist);
    connect(impBtn, &QToolButton::clicked, this, &PlaylistDock::onImport);
    connect(expBtn, &QToolButton::clicked, this, &PlaylistDock::onExport);
    connect(list_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { onPlaySelected(); });
    connect(sourceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PlaylistDock::onSourceChanged);
    connect(endCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int i) { mode_ = static_cast<EndMode>(i); });
}

QString PlaylistDock::itemText(int row) const {
    const auto& it = playlist_.items()[row];
    QString label = QString::fromStdString(it.title);
    std::string dur = formatDuration(it.durationMs);
    if (!dur.empty()) label += QStringLiteral(" (%1)").arg(QString::fromStdString(dur));
    return label;
}

void PlaylistDock::rebuildList() {
    int sel = list_->currentRow();
    list_->clear();
    QIcon playing = tintedIcon(":/icons/play.svg");
    for (int i = 0; i < playlist_.size(); ++i) {
        auto* item = new QListWidgetItem(itemText(i));
        item->setToolTip(QString::fromStdString(playlist_.items()[i].path));
        if (i == playlist_.currentIndex()) item->setIcon(playing);
        list_->addItem(item);
    }
    if (playlist_.currentIndex() >= 0)
        list_->setCurrentRow(playlist_.currentIndex());
    else if (sel >= 0 && sel < playlist_.size())
        list_->setCurrentRow(sel);
}

void PlaylistDock::playIndex(int row) {
    if (!playlist_.setCurrent(row)) return;
    const auto* it = playlist_.current();
    if (!it) return;
    if (!controller_.isBound()) {
        setStatus("No media source bound.", true);
        rebuildList();
        return;
    }
    if (controller_.setFileAndRestart(it->path)) {
        setStatus(QStringLiteral("Playing: %1").arg(QString::fromStdString(it->title)));
        QTimer::singleShot(700, this, &PlaylistDock::captureCurrentDuration);
    } else {
        setStatus("Failed to set media source.", true);
    }
    rebuildList();
}

void PlaylistDock::loadIndex(int row) {
    if (!playlist_.setCurrent(row)) return;
    const auto* it = playlist_.current();
    if (!it) return;
    if (!controller_.isBound()) {
        setStatus("No media source bound.", true);
        rebuildList();
        return;
    }
    if (controller_.setFileLoadOnly(it->path)) {
        setStatus(QStringLiteral("Loaded (paused): %1").arg(QString::fromStdString(it->title)));
        // The media source starts decoding asynchronously; re-issue the pause a
        // few times so it reliably stays paused on the first frame even though
        // the scene is live in program.
        QTimer::singleShot(120, this, [this]() { controller_.pause(); });
        QTimer::singleShot(400, this, [this]() { controller_.pause(); });
        QTimer::singleShot(700, this, &PlaylistDock::captureCurrentDuration);
    } else {
        setStatus("Failed to set media source.", true);
    }
    rebuildList();
}

void PlaylistDock::captureCurrentDuration() {
    int row = playlist_.currentIndex();
    if (row < 0 || row >= playlist_.size()) return;
    if (playlist_.items()[row].durationMs >= 0) return; // already known
    long long d = controller_.currentDurationMs();
    if (d < 0) return;
    auto items = playlist_.items();
    items[row].durationMs = d;
    int cur = playlist_.currentIndex();
    playlist_.setItems(std::move(items));
    playlist_.setCurrent(cur);
    rebuildList();
}

void PlaylistDock::onMediaEnded() {
    switch (mode_) {
    case PlayNext: {
        int i = playlist_.next(false);
        if (i >= 0) playIndex(i);
        else controller_.stop();
        break;
    }
    case Loop: {
        int i = playlist_.next(true);
        if (i >= 0) playIndex(i);
        break;
    }
    case LoadNext: {
        int i = playlist_.next(false);
        if (i >= 0) loadIndex(i);
        else controller_.stop();
        break;
    }
    case StopAtEnd:
        controller_.stop();
        break;
    }
}

void PlaylistDock::onAddFiles() {
    QStringList files = QFileDialog::getOpenFileNames(this, "Add media files");
    int added = 0;
    for (const auto& f : files) {
        std::string p = f.toStdString();
        if (!mediapath::isMediaFile(p)) continue;
        PlaylistItem it{p, mediapath::fileStem(p), pld::probeDurationMs(p)};
        playlist_.add(it);
        ++added;
    }
    rebuildList();
    if (added) setStatus(QStringLiteral("Added %1 file(s).").arg(added));
}

void PlaylistDock::onRemove() {
    int row = list_->currentRow();
    if (row >= 0) {
        playlist_.removeAt(row);
        rebuildList();
    }
}

void PlaylistDock::onUp() {
    int r = list_->currentRow();
    if (playlist_.moveUp(r)) {
        rebuildList();
        list_->setCurrentRow(r - 1);
    }
}

void PlaylistDock::onDown() {
    int r = list_->currentRow();
    if (playlist_.moveDown(r)) {
        rebuildList();
        list_->setCurrentRow(r + 1);
    }
}

void PlaylistDock::onClear() {
    playlist_.clear();
    rebuildList();
}

void PlaylistDock::onPlaySelected() {
    int r = list_->currentRow();
    if (r >= 0) playIndex(r);
}

void PlaylistDock::onTogglePlayPause() { controller_.togglePlayPause(); }
void PlaylistDock::onStop() { controller_.stop(); }

void PlaylistDock::onNext() {
    int i = playlist_.next(wrapEnabled());
    if (i >= 0) playIndex(i);
}

void PlaylistDock::onPrev() {
    int i = playlist_.prev(wrapEnabled());
    if (i >= 0) playIndex(i);
}

void PlaylistDock::onSourceChanged(int) {
    QString name = sourceCombo_->currentText();
    if (name.isEmpty()) {
        controller_.unbind();
        return;
    }
    controller_.bind(name.toStdString());
    setStatus(QStringLiteral("Bound to: %1").arg(name));
}

void PlaylistDock::refreshSources() {
    QString prev = sourceCombo_ ? sourceCombo_->currentText() : QString();
    sourceCombo_->blockSignals(true);
    sourceCombo_->clear();
    for (const auto& n : MediaSourceController::listMediaSources())
        sourceCombo_->addItem(QString::fromStdString(n));
    int idx = sourceCombo_->findText(prev);
    if (idx >= 0) sourceCombo_->setCurrentIndex(idx);
    sourceCombo_->blockSignals(false);
    onSourceChanged(sourceCombo_->currentIndex());
}

std::string PlaylistDock::configDir() const {
    char* p = obs_module_config_path("playlists");
    std::string s = p ? p : "";
    bfree(p);
    if (!s.empty()) QDir().mkpath(QString::fromStdString(s));
    return s;
}

void PlaylistDock::refreshPlaylistCombo() {
    QString prev = playlistCombo_->currentText();
    playlistCombo_->clear();
    QDir dir(QString::fromStdString(configDir()));
    const auto entries = dir.entryInfoList(QStringList() << "*.json", QDir::Files, QDir::Name);
    for (const auto& fi : entries) playlistCombo_->addItem(fi.completeBaseName());
    int idx = playlistCombo_->findText(prev);
    if (idx >= 0) playlistCombo_->setCurrentIndex(idx);
}

void PlaylistDock::onSavePlaylist() {
    bool ok = false;
    QString name =
        QInputDialog::getText(this, "Save playlist", "Name:", QLineEdit::Normal, "", &ok);
    if (!ok || name.isEmpty()) return;
    std::string text = io::toJson(name.toStdString(), playlist_.items());
    QString path = QString::fromStdString(configDir()) + "/" + name + ".json";
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(text.c_str());
        f.close();
    }
    refreshPlaylistCombo();
    setStatus(QStringLiteral("Saved playlist: %1").arg(name));
}

void PlaylistDock::onLoadPlaylist() {
    QString name = playlistCombo_->currentText();
    if (name.isEmpty()) return;
    QString path = QString::fromStdString(configDir()) + "/" + name + ".json";
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setStatus("Cannot open playlist.", true);
        return;
    }
    std::string text = f.readAll().toStdString();
    f.close();
    std::string n;
    std::vector<PlaylistItem> items;
    if (!io::fromJson(text, n, items)) {
        setStatus("Invalid playlist file.", true);
        return;
    }
    playlist_.setItems(std::move(items));
    rebuildList();
    setStatus(QStringLiteral("Loaded playlist: %1").arg(name));
}

void PlaylistDock::onRenamePlaylist() {
    QString oldName = playlistCombo_->currentText();
    if (oldName.isEmpty()) return;
    bool ok = false;
    QString newName =
        QInputDialog::getText(this, "Rename playlist", "New name:", QLineEdit::Normal, oldName, &ok);
    if (!ok || newName.isEmpty() || newName == oldName) return;
    QString dir = QString::fromStdString(configDir());
    QString oldPath = dir + "/" + oldName + ".json";
    QString newPath = dir + "/" + newName + ".json";
    if (QFile::exists(newPath)) {
        setStatus("A playlist with that name already exists.", true);
        return;
    }
    if (!QFile::rename(oldPath, newPath)) {
        setStatus("Rename failed.", true);
        return;
    }
    // Keep the stored "name" field in sync with the new file name.
    QFile f(newPath);
    if (f.open(QIODevice::ReadOnly)) {
        std::string text = f.readAll().toStdString();
        f.close();
        std::string n;
        std::vector<PlaylistItem> items;
        if (io::fromJson(text, n, items)) {
            std::string out = io::toJson(newName.toStdString(), items);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                f.write(out.c_str());
                f.close();
            }
        }
    }
    refreshPlaylistCombo();
    int idx = playlistCombo_->findText(newName);
    if (idx >= 0) playlistCombo_->setCurrentIndex(idx);
    setStatus(QStringLiteral("Renamed to: %1").arg(newName));
}

void PlaylistDock::onDeletePlaylist() {
    QString name = playlistCombo_->currentText();
    if (name.isEmpty()) return;
    QFile::remove(QString::fromStdString(configDir()) + "/" + name + ".json");
    refreshPlaylistCombo();
    setStatus(QStringLiteral("Deleted playlist: %1").arg(name));
}

void PlaylistDock::onImport() {
    QString path = QFileDialog::getOpenFileName(this, "Import playlist", "",
                                                "Playlists (*.json *.m3u *.m3u8)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setStatus("Cannot open file.", true);
        return;
    }
    std::string text = f.readAll().toStdString();
    f.close();
    std::vector<PlaylistItem> items;
    if (path.endsWith(".json", Qt::CaseInsensitive)) {
        std::string n;
        if (!io::fromJson(text, n, items)) {
            setStatus("Invalid JSON playlist.", true);
            return;
        }
    } else {
        items = io::parseM3u(text);
    }
    for (auto& it : items)
        if (it.durationMs < 0) it.durationMs = pld::probeDurationMs(it.path);
    playlist_.setItems(std::move(items));
    rebuildList();
    setStatus("Imported playlist.");
}

void PlaylistDock::onExport() {
    QString path = QFileDialog::getSaveFileName(this, "Export playlist", "",
                                                "JSON (*.json);;M3U (*.m3u)");
    if (path.isEmpty()) return;
    std::string text = path.endsWith(".m3u", Qt::CaseInsensitive)
                           ? io::toM3u(playlist_.items())
                           : io::toJson("playlist", playlist_.items());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(text.c_str());
        f.close();
    }
    setStatus("Exported playlist.");
}

void PlaylistDock::setStatus(const QString& msg, bool error) {
    status_->setStyleSheet(error ? "color:#e06c75;" : "");
    status_->setText(msg);
}

void PlaylistDock::checkForUpdate() {
    net_ = new QNetworkAccessManager(this);
    QNetworkRequest req((QUrl(QString::fromUtf8(kLatestApi))));
    req.setHeader(QNetworkRequest::UserAgentHeader, "obs-playlist-deck");
    QNetworkReply* reply = net_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) return;
        QString tag = doc.object().value("tag_name").toString();
        if (tag.isEmpty()) return;
        if (pld::isNewerVersion(tag.toStdString(), PLD_VERSION)) {
            versionLabel_->setText(
                QStringLiteral("v%1 — <a href=\"%2\">update to %3 \xE2\x86\x97</a>")
                    .arg(PLD_VERSION)
                    .arg(QString::fromUtf8(kReleasesUrl))
                    .arg(tag));
        }
    });
}

// ---- Hotkeys -------------------------------------------------------------

namespace {
struct HotkeyTarget {
    PlaylistDock* dock;
    void (PlaylistDock::*method)();
};

void hotkeyCallback(void* data, obs_hotkey_id, obs_hotkey_t*, bool pressed) {
    if (!pressed) return;
    auto* t = static_cast<HotkeyTarget*>(data);
    PlaylistDock* dock = t->dock;
    auto method = t->method;
    QMetaObject::invokeMethod(
        dock, [dock, method]() { (dock->*method)(); }, Qt::QueuedConnection);
}
} // namespace

void PlaylistDock::registerHotkeys() {
    auto reg = [this](const char* id, const char* desc, void (PlaylistDock::*fn)()) {
        return obs_hotkey_register_frontend(id, desc, hotkeyCallback,
                                            new HotkeyTarget{this, fn});
    };
    hkNext_ = reg("obs-playlist-deck.next", "Playlist Deck: Next", &PlaylistDock::onNext);
    hkPrev_ = reg("obs-playlist-deck.prev", "Playlist Deck: Previous", &PlaylistDock::onPrev);
    hkPlayPause_ = reg("obs-playlist-deck.playpause", "Playlist Deck: Play/Pause",
                       &PlaylistDock::onTogglePlayPause);
    hkStop_ = reg("obs-playlist-deck.stop", "Playlist Deck: Stop", &PlaylistDock::onStop);
}

void PlaylistDock::unregisterHotkeys() {
    for (auto id : {hkNext_, hkPrev_, hkPlayPause_, hkStop_})
        if (id != OBS_INVALID_HOTKEY_ID) obs_hotkey_unregister(id);
}

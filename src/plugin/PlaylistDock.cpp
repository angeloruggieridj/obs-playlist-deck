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
#include <QPushButton>
#include <QSvgRenderer>
#include <QTimer>
#include <QUrl>
#include <QVariant>
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
    loadSettings();
    buildUi();
    refreshSources();
    controller_.setOnMediaEnded([this]() {
        QMetaObject::invokeMethod(this, "onMediaEnded", Qt::QueuedConnection);
    });
    controller_.setOnDeactivated([this]() {
        QMetaObject::invokeMethod(this, "onSourceDeactivated", Qt::QueuedConnection);
    });
    registerHotkeys();
    checkForUpdate();
}

PlaylistDock::~PlaylistDock() {
    // Most obs cleanup happens in shutdown() (called on EXIT). Guard against the
    // case where the dtor runs without a prior shutdown.
    if (!obsShutdown_) shutdown();
}

void PlaylistDock::shutdown() {
    if (obsShutdown_) return;
    saveSettings();
    obsShutdown_ = true;
    unregisterHotkeys();
    controller_.setOnMediaEnded(nullptr);
    controller_.setOnDeactivated(nullptr);
    controller_.unbind();
    if (net_) net_->disconnect();
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

    // Real push button with a tinted icon beside its text.
    auto mk = [this](const QString& icon, const QString& text, const QString& tip) {
        auto* b = new QPushButton(tintedIcon(icon), " " + text);
        b->setToolTip(tip);
        b->setFocusPolicy(Qt::NoFocus);
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };

    // ---- Media source -----------------------------------------------------
    col->addWidget(sectionLabel("Media source"));
    sourceCombo_ = new QComboBox();
    sourceCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    sourceCombo_->setMinimumHeight(26);
    auto* refreshBtn = mk(":/icons/refresh.svg", "Refresh", "Rescan media sources");
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

    // ---- Playlist file ----------------------------------------------------
    col->addWidget(sectionLabel("Playlist file"));
    auto* plRow = new QHBoxLayout();
    plRow->setSpacing(3);
    auto* saveBtn = mk(":/icons/save.svg", "Save", "Save the playlist to a file you choose");
    auto* openBtn = mk(":/icons/folder-open.svg", "Open", "Open a playlist file (.json / .m3u)");
    for (auto* b : {saveBtn, openBtn}) plRow->addWidget(b);
    plRow->addStretch(1);
    col->addLayout(plRow);

    loadedLabel_ = new QLabel("No playlist loaded");
    loadedLabel_->setWordWrap(true);
    col->addWidget(loadedLabel_);

    // ---- Status + version -------------------------------------------------
    status_ = new QLabel("");
    status_->setWordWrap(true);
    col->addWidget(status_);

    versionLabel_ = new QLabel(QStringLiteral("v%1").arg(PLD_VERSION));
    versionLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    versionLabel_->setTextFormat(Qt::RichText);
    versionLabel_->setOpenExternalLinks(true);
    versionLabel_->setToolTip("Installed Playlist Deck version");
    auto* verRow = new QHBoxLayout();
    verRow->addStretch(1);
    verRow->addWidget(versionLabel_);
    col->addLayout(verRow);

    setWidget(root);

    connect(refreshBtn, &QPushButton::clicked, this, &PlaylistDock::refreshSources);
    connect(addBtn, &QPushButton::clicked, this, &PlaylistDock::onAddFiles);
    connect(rmBtn, &QPushButton::clicked, this, &PlaylistDock::onRemove);
    connect(upBtn, &QPushButton::clicked, this, &PlaylistDock::onUp);
    connect(downBtn, &QPushButton::clicked, this, &PlaylistDock::onDown);
    connect(clrBtn, &QPushButton::clicked, this, &PlaylistDock::onClear);
    connect(playSelBtn, &QPushButton::clicked, this, &PlaylistDock::onPlaySelected);
    connect(prevBtn, &QPushButton::clicked, this, &PlaylistDock::onPrev);
    connect(playPauseBtn, &QPushButton::clicked, this, &PlaylistDock::onTogglePlayPause);
    connect(stopBtn, &QPushButton::clicked, this, &PlaylistDock::onStop);
    connect(nextBtn, &QPushButton::clicked, this, &PlaylistDock::onNext);
    connect(saveBtn, &QPushButton::clicked, this, &PlaylistDock::onSavePlaylist);
    connect(openBtn, &QPushButton::clicked, this, &PlaylistDock::onOpenPlaylist);
    connect(list_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { onPlaySelected(); });
    connect(sourceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PlaylistDock::onSourceChanged);
    connect(endCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        mode_ = static_cast<EndMode>(i);
        saveSettings();
    });
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
    pendingStageNext_ = false;
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
    if (playlist_.items()[row].durationMs >= 0) return;
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
    case LoadNext:
        // Hold the finished clip's last frame on program. Defer loading the next
        // clip until the source leaves program (program -> preview), so the next
        // clip's first frame never goes live.
        pendingStageNext_ = true;
        setStatus("Clip ended — next will load when this source leaves program.");
        break;
    case StopAtEnd:
        controller_.stop();
        break;
    }
}

void PlaylistDock::onSourceDeactivated() {
    if (!pendingStageNext_ || mode_ != LoadNext) return;
    pendingStageNext_ = false;
    int i = playlist_.next(false);
    if (i >= 0) loadIndex(i); // sets next file paused while off-air (in preview)
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
    int i = playlist_.next(mode_ == Loop);
    if (i >= 0) playIndex(i);
}

void PlaylistDock::onPrev() {
    int i = playlist_.prev(mode_ == Loop);
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
    saveSettings();
}

void PlaylistDock::refreshSources() {
    QString prev = sourceCombo_->currentText();
    if (prev.isEmpty() && !pendingSource_.isEmpty()) prev = pendingSource_;
    sourceCombo_->blockSignals(true);
    sourceCombo_->clear();
    for (const auto& n : MediaSourceController::listMediaSources())
        sourceCombo_->addItem(QString::fromStdString(n));
    int idx = sourceCombo_->findText(prev);
    if (idx >= 0) sourceCombo_->setCurrentIndex(idx);
    sourceCombo_->blockSignals(false);
    onSourceChanged(sourceCombo_->currentIndex());
}

// ---- Saved-playlist file handling ----------------------------------------

bool PlaylistDock::loadPlaylistFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setStatus("Cannot open playlist file.", true);
        return false;
    }
    std::string text = f.readAll().toStdString();
    f.close();
    std::vector<PlaylistItem> items;
    if (path.endsWith(".json", Qt::CaseInsensitive)) {
        std::string n;
        if (!io::fromJson(text, n, items)) {
            setStatus("Invalid JSON playlist.", true);
            return false;
        }
    } else {
        items = io::parseM3u(text);
    }
    for (auto& it : items)
        if (it.durationMs < 0) it.durationMs = pld::probeDurationMs(it.path);
    playlist_.setItems(std::move(items));
    rebuildList();
    return true;
}

void PlaylistDock::setLoadedPlaylist(const QString& path) {
    if (path.isEmpty()) {
        loadedLabel_->setText("No playlist loaded");
        return;
    }
    loadedLabel_->setText(QStringLiteral("Loaded: %1").arg(QFileInfo(path).fileName()));
    loadedLabel_->setToolTip(path);
}

void PlaylistDock::onSavePlaylist() {
    QString path = QFileDialog::getSaveFileName(this, "Save playlist", "",
                                                "JSON (*.json);;M3U (*.m3u)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".json", Qt::CaseInsensitive) && !path.endsWith(".m3u", Qt::CaseInsensitive))
        path += ".json";
    std::string text = path.endsWith(".m3u", Qt::CaseInsensitive)
                           ? io::toM3u(playlist_.items())
                           : io::toJson(QFileInfo(path).completeBaseName().toStdString(),
                                        playlist_.items());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setStatus("Cannot write playlist file.", true);
        return;
    }
    f.write(text.c_str());
    f.close();
    setLoadedPlaylist(path);
    setStatus(QStringLiteral("Saved: %1").arg(QFileInfo(path).fileName()));
}

void PlaylistDock::onOpenPlaylist() {
    QString path = QFileDialog::getOpenFileName(this, "Open playlist", "",
                                                "Playlists (*.json *.m3u *.m3u8)");
    if (path.isEmpty()) return;
    if (loadPlaylistFile(path)) {
        setLoadedPlaylist(path);
        setStatus(QStringLiteral("Opened: %1").arg(QFileInfo(path).fileName()));
    }
}

void PlaylistDock::setStatus(const QString& msg, bool error) {
    status_->setStyleSheet(error ? "color:#e06c75;" : "");
    status_->setText(msg);
}

// ---- Settings persistence (mode + bound source) --------------------------

QString PlaylistDock::settingsPath() const {
    char* p = obs_module_config_path("settings.json");
    QString s = p ? QString::fromUtf8(p) : QString();
    bfree(p);
    if (!s.isEmpty()) QDir().mkpath(QFileInfo(s).absolutePath());
    return s;
}

void PlaylistDock::saveSettings() const {
    QString path = settingsPath();
    if (path.isEmpty()) return;
    QJsonObject o;
    o["mode"] = static_cast<int>(mode_);
    o["source"] = sourceCombo_ ? sourceCombo_->currentText() : QString();
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        f.close();
    }
}

void PlaylistDock::loadSettings() {
    QString path = settingsPath();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) return;
    QJsonObject o = doc.object();
    int m = o.value("mode").toInt(static_cast<int>(PlayNext));
    if (m >= PlayNext && m <= StopAtEnd) mode_ = static_cast<EndMode>(m);
    pendingSource_ = o.value("source").toString();
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
    hkNext_ = hkPrev_ = hkPlayPause_ = hkStop_ = OBS_INVALID_HOTKEY_ID;
}

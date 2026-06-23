#include "PlaylistDock.hpp"
#include "PlaylistIO.hpp"
#include "MediaPath.hpp"
#include "MediaProbe.hpp"
#include "Format.hpp"
#include "Version.hpp"
#include "Shuffle.hpp"
#include "PlaylistListWidget.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QAbstractItemView>
#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QPointer>
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
#include <QProgressBar>
#include <QPushButton>
#include <QSvgRenderer>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

#include <thread>
#include <utility>

#ifndef PLD_VERSION
#define PLD_VERSION "0.0.0"
#endif

using namespace pld;

namespace {
const char* kReleasesUrl = "https://github.com/angeloruggieridj/obs-playlist-deck/releases";
const char* kLatestApi =
    "https://api.github.com/repos/angeloruggieridj/obs-playlist-deck/releases/latest";

// Localized string lookup (falls back to the key if a translation is missing).
QString T(const char* key) { return QString::fromUtf8(obs_module_text(key)); }
}

PlaylistDock::PlaylistDock(QWidget* parent) : QDockWidget(parent) {
    setWindowTitle(QString::fromUtf8(obs_module_text("PlaylistDeck")));
    setObjectName("obs-playlist-deck-dock");
    loadSettings();
    applyLocale();
    buildUi();
    refreshSources();
    if (autoRestore_) loadSession();
    // The bound media source persists its previous file across OBS restarts; if
    // no playlist item is loaded, clear it so sending the source to Program does
    // not replay the clip from before the last shutdown.
    if (playlist_.currentIndex() < 0 && controller_.isBound()) controller_.clearFile();
    controller_.setOnMediaEnded([this]() {
        QMetaObject::invokeMethod(this, "onMediaEnded", Qt::QueuedConnection);
    });
    controller_.setOnDeactivated([this]() {
        QMetaObject::invokeMethod(this, "onSourceDeactivated", Qt::QueuedConnection);
    });
    registerHotkeys();
    checkForUpdate();

    uiTimer_ = new QTimer(this);
    uiTimer_->setInterval(500);
    connect(uiTimer_, &QTimer::timeout, this, &PlaylistDock::onTick);
    uiTimer_->start();
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
    col->addWidget(sectionLabel(T("Section.MediaSource")));
    sourceCombo_ = new QComboBox();
    sourceCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    sourceCombo_->setMinimumHeight(26);
    auto* refreshBtn = mk(":/icons/refresh.svg", T("Btn.Refresh"), T("Tip.Refresh"));
    auto* srcRow = new QHBoxLayout();
    srcRow->addWidget(sourceCombo_, 1);
    srcRow->addWidget(refreshBtn);
    col->addLayout(srcRow);

    // ---- Playlist ---------------------------------------------------------
    col->addWidget(sectionLabel(T("Section.Playlist")));
    filterEdit_ = new QLineEdit();
    filterEdit_->setClearButtonEnabled(true);
    filterEdit_->setPlaceholderText(T("Filter.Placeholder"));
    col->addWidget(filterEdit_);
    list_ = new PlaylistListWidget();
    col->addWidget(list_, 1);

    auto* opsRow = new QHBoxLayout();
    opsRow->setSpacing(3);
    auto* addBtn = mk(":/icons/plus.svg", T("Btn.Add"), T("Tip.Add"));
    auto* rmBtn = mk(":/icons/minus.svg", T("Btn.Remove"), T("Tip.Remove"));
    auto* upBtn = mk(":/icons/chevron-up.svg", T("Btn.Up"), T("Tip.Up"));
    auto* downBtn = mk(":/icons/chevron-down.svg", T("Btn.Down"), T("Tip.Down"));
    auto* clrBtn = mk(":/icons/x.svg", T("Btn.Clear"), T("Tip.Clear"));
    for (auto* b : {addBtn, rmBtn, upBtn, downBtn, clrBtn}) opsRow->addWidget(b);
    opsRow->addStretch(1);
    col->addLayout(opsRow);

    auto* trRow = new QHBoxLayout();
    trRow->setSpacing(3);
    auto* playSelBtn = mk(":/icons/play.svg", T("Btn.Play"), T("Tip.Play"));
    auto* prevBtn = mk(":/icons/skip-back.svg", T("Btn.Prev"), T("Tip.Prev"));
    auto* playPauseBtn = mk(":/icons/pause.svg", T("Btn.Pause"), T("Tip.Pause"));
    auto* stopBtn = mk(":/icons/stop.svg", T("Btn.Stop"), T("Tip.Stop"));
    auto* nextBtn = mk(":/icons/skip-forward.svg", T("Btn.Next"), T("Tip.Next"));
    for (auto* b : {playSelBtn, prevBtn, playPauseBtn, stopBtn, nextBtn}) trRow->addWidget(b);
    trRow->addStretch(1);
    col->addLayout(trRow);

    auto* endRow = new QHBoxLayout();
    endRow->addWidget(new QLabel(T("OnEnd.Label")));
    endCombo_ = new QComboBox();
    endCombo_->addItem(T("OnEnd.PlayNext"));
    endCombo_->addItem(T("OnEnd.Loop"));
    endCombo_->addItem(T("OnEnd.LoadNext"));
    endCombo_->addItem(T("OnEnd.Stop"));
    endCombo_->addItem(T("OnEnd.Shuffle"));
    endCombo_->addItem(T("OnEnd.RepeatOne"));
    endCombo_->setCurrentIndex(static_cast<int>(mode_));
    endCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    endRow->addWidget(endCombo_, 1);
    col->addLayout(endRow);

    // ---- Now playing progress --------------------------------------------
    progress_ = new QProgressBar();
    progress_->setRange(0, 1000);
    progress_->setValue(0);
    progress_->setTextVisible(false);
    progress_->setFixedHeight(6);
    col->addWidget(progress_);
    timeLabel_ = new QLabel("");
    timeLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    col->addWidget(timeLabel_);

    // ---- Playlist file ----------------------------------------------------
    col->addWidget(sectionLabel(T("Section.PlaylistFile")));
    auto* plRow = new QHBoxLayout();
    plRow->setSpacing(3);
    auto* saveBtn = mk(":/icons/save.svg", T("Btn.Save"), T("Tip.Save"));
    auto* openBtn = mk(":/icons/folder-open.svg", T("Btn.Open"), T("Tip.Open"));
    for (auto* b : {saveBtn, openBtn}) plRow->addWidget(b);
    plRow->addStretch(1);
    col->addLayout(plRow);

    loadedLabel_ = new QLabel(T("NoPlaylist"));
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
    versionLabel_->setToolTip(T("Tip.Version"));
    auto* settingsBtn = mk(":/icons/settings.svg", T("Btn.Settings"), T("Tip.Settings"));
    auto* verRow = new QHBoxLayout();
    verRow->addWidget(settingsBtn);
    verRow->addStretch(1);
    verRow->addWidget(versionLabel_);
    col->addLayout(verRow);

    setWidget(root);

    connect(settingsBtn, &QPushButton::clicked, this, &PlaylistDock::onOpenSettings);

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
    connect(list_, &PlaylistListWidget::filesDropped, this, &PlaylistDock::onFilesDropped);
    connect(list_, &PlaylistListWidget::reordered, this, &PlaylistDock::onListReordered);
    connect(filterEdit_, &QLineEdit::textChanged, this, &PlaylistDock::onFilterChanged);
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
    list_->blockSignals(true);
    list_->clear();
    QIcon playing = tintedIcon(":/icons/play.svg");
    for (int i = 0; i < playlist_.size(); ++i) {
        const auto& pi = playlist_.items()[i];
        QString qpath = QString::fromStdString(pi.path);
        bool missing = !QFileInfo::exists(qpath);
        QString label = itemText(i);
        if (missing) label += "  \xE2\x9A\xA0 " + T("FileNotFound"); // ⚠
        auto* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, i); // model index, used to sync drag-reorder
        item->setToolTip(missing ? qpath + "  (" + T("FileNotFound") + ")" : qpath);
        if (missing) item->setForeground(QBrush(QColor("#e06c75")));
        if (i == playlist_.currentIndex()) item->setIcon(playing);
        list_->addItem(item);
    }
    // Preserve the user's selection across rebuilds — do NOT force it onto the
    // currently-playing item (that's marked with the ▶ icon). Reordering while
    // another item plays must keep the selection where the user put it.
    if (sel >= 0 && sel < playlist_.size()) list_->setCurrentRow(sel);
    list_->blockSignals(false);
    applyFilter();
    if (autoRestore_) saveSession();
}

void PlaylistDock::applyFilter() {
    QString f = filterEdit_ ? filterEdit_->text().trimmed() : QString();
    for (int row = 0; row < list_->count(); ++row) {
        auto* item = list_->item(row);
        bool match = f.isEmpty() || item->text().contains(f, Qt::CaseInsensitive);
        item->setHidden(!match);
    }
}

void PlaylistDock::onFilterChanged(const QString&) { applyFilter(); }

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
    case Shuffle: {
        int i = pld::randomIndex(playlist_.size(), playlist_.currentIndex(), rng_);
        if (i >= 0) playIndex(i);
        break;
    }
    case RepeatOne: {
        int i = playlist_.currentIndex();
        if (i >= 0) playIndex(i);
        break;
    }
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

void PlaylistDock::addPaths(const QStringList& paths) {
    QStringList added;
    for (const auto& f : paths) {
        std::string p = f.toStdString();
        if (!mediapath::isMediaFile(p)) continue;
        playlist_.add(PlaylistItem{p, mediapath::fileStem(p), -1}); // duration probed async
        added << f;
    }
    rebuildList();
    if (!added.isEmpty()) {
        setStatus(QStringLiteral("Added %1 file(s).").arg(added.size()));
        startBackgroundProbe(added);
    }
}

void PlaylistDock::startBackgroundProbe(const QStringList& paths) {
    if (!enableProbe_) return;
    QPointer<PlaylistDock> self(this);
    std::thread([self, paths]() {
        for (const QString& path : paths) {
            long long d = pld::probeDurationMs(path.toStdString());
            if (d < 0) continue;
            QString p = path;
            QMetaObject::invokeMethod(
                qApp, [self, p, d]() { if (self) self->applyProbedDuration(p, d); },
                Qt::QueuedConnection);
        }
    }).detach();
}

void PlaylistDock::applyProbedDuration(const QString& path, long long durationMs) {
    if (durationMs < 0) return;
    std::string p = path.toStdString();
    auto items = playlist_.items();
    bool changed = false;
    for (auto& it : items)
        if (it.path == p && it.durationMs < 0) {
            it.durationMs = durationMs;
            changed = true;
        }
    if (!changed) return;
    int cur = playlist_.currentIndex();
    playlist_.setItems(std::move(items));
    playlist_.setCurrent(cur);
    rebuildList();
}

void PlaylistDock::onAddFiles() {
    addPaths(QFileDialog::getOpenFileNames(this, "Add media files"));
}

void PlaylistDock::onFilesDropped(const QStringList& paths) { addPaths(paths); }

void PlaylistDock::onListReordered() {
    // The widget rows were reordered by drag; rebuild the model from the new
    // order using each item's stored original index.
    std::vector<PlaylistItem> reordered;
    reordered.reserve(playlist_.size());
    int newCurrent = -1;
    for (int row = 0; row < list_->count(); ++row) {
        int orig = list_->item(row)->data(Qt::UserRole).toInt();
        if (orig < 0 || orig >= playlist_.size()) continue;
        if (orig == playlist_.currentIndex()) newCurrent = static_cast<int>(reordered.size());
        reordered.push_back(playlist_.items()[orig]);
    }
    if (static_cast<int>(reordered.size()) != playlist_.size()) {
        rebuildList(); // safety: row count mismatch, just resync
        return;
    }
    playlist_.setItems(std::move(reordered));
    playlist_.setCurrent(newCurrent);
    rebuildList();
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

// ---- Remote control (obs-websocket vendor) -------------------------------
void PlaylistDock::wsNext() { onNext(); }
void PlaylistDock::wsPrev() { onPrev(); }
void PlaylistDock::wsStop() { onStop(); }
void PlaylistDock::wsPlayPause() { onTogglePlayPause(); }
void PlaylistDock::wsPlayIndex(int index) {
    if (index >= 0 && index < playlist_.size()) playIndex(index);
}
void PlaylistDock::wsLoad(const QString& path) {
    if (loadPlaylistFile(path)) setLoadedPlaylist(path);
}

void PlaylistDock::onNext() {
    int i = (mode_ == Shuffle)
                ? pld::randomIndex(playlist_.size(), playlist_.currentIndex(), rng_)
                : playlist_.next(mode_ == Loop);
    if (i >= 0) playIndex(i);
}

void PlaylistDock::onPrev() {
    int i = playlist_.prev(mode_ == Loop);
    if (i >= 0) playIndex(i);
}

void PlaylistDock::onTick() {
    if (!progress_) return;
    // Only show progress for a clip this session has actually selected. Without
    // this, on startup the bound media source still holds the file from the
    // previous session and the counter would show that stale clip even though
    // no playlist item is loaded.
    if (playlist_.currentIndex() < 0) {
        progress_->setValue(0);
        timeLabel_->clear();
        return;
    }
    long long dur = controller_.currentDurationMs();
    long long cur = controller_.currentTimeMs();
    if (dur > 0 && cur >= 0) {
        progress_->setValue(static_cast<int>(1000.0 * cur / dur));
        long long remaining = dur - cur;
        if (remaining < 0) remaining = 0;
        timeLabel_->setText(QStringLiteral("%1 / %2   -%3")
                                .arg(QString::fromStdString(pld::formatDuration(cur)))
                                .arg(QString::fromStdString(pld::formatDuration(dur)))
                                .arg(QString::fromStdString(pld::formatDuration(remaining))));
    } else {
        progress_->setValue(0);
        timeLabel_->clear();
    }
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
    QStringList toProbe;
    for (const auto& it : items)
        if (it.durationMs < 0) toProbe << QString::fromStdString(it.path);
    playlist_.setItems(std::move(items));
    rebuildList();
    startBackgroundProbe(toProbe);
    return true;
}

void PlaylistDock::setLoadedPlaylist(const QString& path) {
    loadedPath_ = path;
    if (path.isEmpty()) {
        loadedLabel_->setText(T("NoPlaylist"));
        loadedLabel_->setToolTip("");
        return;
    }
    loadedLabel_->setText(T("Loaded").arg(QFileInfo(path).fileName()));
    loadedLabel_->setToolTip(path);
}

void PlaylistDock::onSavePlaylist() {
    QString selectedFilter;
    QString path = QFileDialog::getSaveFileName(this, "Save playlist", "",
                                                "JSON (*.json);;M3U (*.m3u)", &selectedFilter);
    if (path.isEmpty()) return;
    // Honor the chosen filter for the format (the dialog doesn't always append
    // the extension), so picking "M3U" actually writes .m3u — not .json.
    bool m3u = path.endsWith(".m3u", Qt::CaseInsensitive) ||
               (!path.endsWith(".json", Qt::CaseInsensitive) &&
                selectedFilter.contains("m3u", Qt::CaseInsensitive));
    if (!path.endsWith(".json", Qt::CaseInsensitive) && !path.endsWith(".m3u", Qt::CaseInsensitive))
        path += m3u ? ".m3u" : ".json";
    std::string text = m3u ? io::toM3u(playlist_.items())
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
    o["enableProbe"] = enableProbe_;
    o["autoRestore"] = autoRestore_;
    o["language"] = language_;
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
    if (m >= PlayNext && m <= RepeatOne) mode_ = static_cast<EndMode>(m);
    pendingSource_ = o.value("source").toString();
    enableProbe_ = o.value("enableProbe").toBool(true);
    autoRestore_ = o.value("autoRestore").toBool(false);
    language_ = o.value("language").toString("auto");
}

void PlaylistDock::applyLocale() {
    if (language_ != "auto" && !language_.isEmpty())
        obs_module_set_locale(language_.toUtf8().constData());
}

void PlaylistDock::applyLocaleAndRebuild() {
    applyLocale();
    QString loaded = loadedPath_;
    buildUi(); // recreates all widgets with the new language
    setWindowTitle(QString::fromUtf8(obs_module_text("PlaylistDeck")));
    refreshSources();
    rebuildList();
    setLoadedPlaylist(loaded);
}

QString PlaylistDock::sessionPath() const {
    char* p = obs_module_config_path("session.json");
    QString s = p ? QString::fromUtf8(p) : QString();
    bfree(p);
    if (!s.isEmpty()) QDir().mkpath(QFileInfo(s).absolutePath());
    return s;
}

void PlaylistDock::saveSession() const {
    QString path = sessionPath();
    if (path.isEmpty()) return;
    std::string text = io::toJson("session", playlist_.items());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(text.c_str());
        f.close();
    }
}

void PlaylistDock::loadSession() {
    QString path = sessionPath();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    std::string text = f.readAll().toStdString();
    f.close();
    std::string n;
    std::vector<PlaylistItem> items;
    if (!io::fromJson(text, n, items)) return;
    QStringList toProbe;
    for (const auto& it : items)
        if (it.durationMs < 0) toProbe << QString::fromStdString(it.path);
    playlist_.setItems(std::move(items));
    rebuildList();
    startBackgroundProbe(toProbe);
}

void PlaylistDock::onOpenSettings() {
    QDialog dlg(this);
    dlg.setWindowTitle(T("Settings.Title"));
    auto* form = new QFormLayout(&dlg);

    auto* probeChk = new QCheckBox(T("Settings.Probe"));
    probeChk->setChecked(enableProbe_);
    form->addRow(probeChk);

    auto* restoreChk = new QCheckBox(T("Settings.AutoRestore"));
    restoreChk->setChecked(autoRestore_);
    form->addRow(restoreChk);

    auto* langCombo = new QComboBox();
    langCombo->addItem(T("Lang.Auto"), "auto");
    langCombo->addItem("English", "en-US");
    langCombo->addItem("Italiano", "it-IT");
    langCombo->addItem("Español", "es-ES");
    langCombo->addItem("Français", "fr-FR");
    langCombo->addItem("Deutsch", "de-DE");
    langCombo->addItem("Português (BR)", "pt-BR");
    langCombo->addItem("Русский", "ru-RU");
    langCombo->addItem("简体中文", "zh-CN");
    langCombo->addItem("日本語", "ja-JP");
    langCombo->addItem("한국어", "ko-KR");
    int li = langCombo->findData(language_);
    langCombo->setCurrentIndex(li >= 0 ? li : 0);
    form->addRow(new QLabel(T("Settings.Language")), langCombo);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;
    enableProbe_ = probeChk->isChecked();
    autoRestore_ = restoreChk->isChecked();
    QString newLang = langCombo->currentData().toString();
    bool langChanged = (newLang != language_);
    language_ = newLang;
    saveSettings();
    if (autoRestore_) saveSession();
    if (langChanged) applyLocaleAndRebuild();
    setStatus(T("Settings.Saved"));
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

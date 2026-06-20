#include "PlaylistDock.hpp"
#include "PlaylistIO.hpp"
#include "MediaPath.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

using namespace pld;

PlaylistDock::PlaylistDock(QWidget* parent) : QDockWidget(parent) {
    setWindowTitle(QString::fromUtf8(obs_module_text("PlaylistDeck")));
    setObjectName("obs-playlist-deck-dock");
    buildUi();
    refreshSources();
    refreshPlaylistCombo();
    controller_.setOnMediaEnded([this]() {
        QMetaObject::invokeMethod(this, "advance", Qt::QueuedConnection);
    });
    registerHotkeys();
}

PlaylistDock::~PlaylistDock() {
    unregisterHotkeys();
    controller_.setOnMediaEnded(nullptr);
    controller_.unbind();
}

static QPushButton* btn(const char* text) { return new QPushButton(QString::fromUtf8(text)); }

void PlaylistDock::buildUi() {
    auto* root = new QWidget(this);
    auto* col = new QVBoxLayout(root);

    auto* srcRow = new QHBoxLayout();
    sourceCombo_ = new QComboBox();
    auto* refreshBtn = btn("Refresh");
    srcRow->addWidget(new QLabel("Media source:"));
    srcRow->addWidget(sourceCombo_, 1);
    srcRow->addWidget(refreshBtn);
    col->addLayout(srcRow);

    list_ = new QListWidget();
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    col->addWidget(list_, 1);

    auto* editRow = new QHBoxLayout();
    auto* addBtn = btn("Add files");
    auto* rmBtn = btn("Remove");
    auto* upBtn = btn("Up");
    auto* downBtn = btn("Down");
    auto* clrBtn = btn("Clear");
    for (auto* b : {addBtn, rmBtn, upBtn, downBtn, clrBtn}) editRow->addWidget(b);
    col->addLayout(editRow);

    auto* trRow = new QHBoxLayout();
    auto* playSelBtn = btn("Play selected");
    auto* playBtn = btn("Play");
    auto* pauseBtn = btn("Pause");
    auto* stopBtn = btn("Stop");
    auto* prevBtn = btn("Prev");
    auto* nextBtn = btn("Next");
    for (auto* b : {playSelBtn, playBtn, pauseBtn, stopBtn, prevBtn, nextBtn}) trRow->addWidget(b);
    col->addLayout(trRow);

    wrapCheck_ = new QCheckBox("Loop playlist (wrap)");
    wrapCheck_->setChecked(wrap_);
    col->addWidget(wrapCheck_);

    auto* plRow = new QHBoxLayout();
    playlistCombo_ = new QComboBox();
    auto* saveBtn = btn("Save");
    auto* loadBtn = btn("Load");
    auto* delBtn = btn("Delete");
    auto* impBtn = btn("Import");
    auto* expBtn = btn("Export");
    plRow->addWidget(new QLabel("Playlists:"));
    plRow->addWidget(playlistCombo_, 1);
    for (auto* b : {saveBtn, loadBtn, delBtn, impBtn, expBtn}) plRow->addWidget(b);
    col->addLayout(plRow);

    status_ = new QLabel("");
    status_->setWordWrap(true);
    col->addWidget(status_);

    setWidget(root);

    connect(refreshBtn, &QPushButton::clicked, this, &PlaylistDock::refreshSources);
    connect(addBtn, &QPushButton::clicked, this, &PlaylistDock::onAddFiles);
    connect(rmBtn, &QPushButton::clicked, this, &PlaylistDock::onRemove);
    connect(upBtn, &QPushButton::clicked, this, &PlaylistDock::onUp);
    connect(downBtn, &QPushButton::clicked, this, &PlaylistDock::onDown);
    connect(clrBtn, &QPushButton::clicked, this, &PlaylistDock::onClear);
    connect(playSelBtn, &QPushButton::clicked, this, &PlaylistDock::onPlaySelected);
    connect(playBtn, &QPushButton::clicked, this, &PlaylistDock::onPlay);
    connect(pauseBtn, &QPushButton::clicked, this, &PlaylistDock::onPause);
    connect(stopBtn, &QPushButton::clicked, this, &PlaylistDock::onStop);
    connect(prevBtn, &QPushButton::clicked, this, &PlaylistDock::onPrev);
    connect(nextBtn, &QPushButton::clicked, this, &PlaylistDock::advance);
    connect(saveBtn, &QPushButton::clicked, this, &PlaylistDock::onSavePlaylist);
    connect(loadBtn, &QPushButton::clicked, this, &PlaylistDock::onLoadPlaylist);
    connect(delBtn, &QPushButton::clicked, this, &PlaylistDock::onDeletePlaylist);
    connect(impBtn, &QPushButton::clicked, this, &PlaylistDock::onImport);
    connect(expBtn, &QPushButton::clicked, this, &PlaylistDock::onExport);
    connect(list_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { onPlaySelected(); });
    connect(sourceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &PlaylistDock::onSourceChanged);
    connect(wrapCheck_, &QCheckBox::toggled, this, [this](bool v) { wrap_ = v; });
}

void PlaylistDock::rebuildList() {
    list_->clear();
    for (int i = 0; i < playlist_.size(); ++i) {
        const auto& it = playlist_.items()[i];
        QString label = QString::fromStdString(it.title);
        if (i == playlist_.currentIndex()) label = QStringLiteral("▶ ") + label;
        auto* item = new QListWidgetItem(label);
        item->setToolTip(QString::fromStdString(it.path));
        list_->addItem(item);
    }
    if (playlist_.currentIndex() >= 0) list_->setCurrentRow(playlist_.currentIndex());
}

void PlaylistDock::selectAndPlay(int row) {
    if (!playlist_.setCurrent(row)) return;
    const auto* it = playlist_.current();
    if (!it) return;
    if (!controller_.isBound()) {
        setStatus("No media source bound.", true);
        rebuildList();
        return;
    }
    if (controller_.setFileAndRestart(it->path))
        setStatus(QStringLiteral("Playing: %1").arg(QString::fromStdString(it->title)));
    else
        setStatus("Failed to set media source.", true);
    rebuildList();
}

void PlaylistDock::advance() {
    int i = playlist_.next(wrap_);
    if (i >= 0) {
        selectAndPlay(i);
    } else {
        controller_.stop();
        setStatus("Playlist finished.");
    }
}

void PlaylistDock::onAddFiles() {
    QStringList files = QFileDialog::getOpenFileNames(this, "Add media files");
    int added = 0;
    for (const auto& f : files) {
        std::string p = f.toStdString();
        if (!mediapath::isMediaFile(p)) continue;
        playlist_.add(PlaylistItem{p, mediapath::fileStem(p)});
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
    if (r >= 0) selectAndPlay(r);
}

void PlaylistDock::onPlay() { controller_.play(); }
void PlaylistDock::onPause() { controller_.pause(); }
void PlaylistDock::onTogglePlayPause() { controller_.togglePlayPause(); }
void PlaylistDock::onStop() { controller_.stop(); }

void PlaylistDock::onPrev() {
    int i = playlist_.prev(wrap_);
    if (i >= 0) selectAndPlay(i);
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
    playlistCombo_->clear();
    QDir dir(QString::fromStdString(configDir()));
    const auto entries = dir.entryInfoList(QStringList() << "*.json", QDir::Files, QDir::Name);
    for (const auto& fi : entries) playlistCombo_->addItem(fi.completeBaseName());
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
    hkNext_ = reg("obs-playlist-deck.next", "Playlist Deck: Next", &PlaylistDock::advance);
    hkPrev_ = reg("obs-playlist-deck.prev", "Playlist Deck: Previous", &PlaylistDock::onPrev);
    hkPlayPause_ = reg("obs-playlist-deck.playpause", "Playlist Deck: Play/Pause",
                       &PlaylistDock::onTogglePlayPause);
    hkStop_ = reg("obs-playlist-deck.stop", "Playlist Deck: Stop", &PlaylistDock::onStop);
}

void PlaylistDock::unregisterHotkeys() {
    for (auto id : {hkNext_, hkPrev_, hkPlayPause_, hkStop_})
        if (id != OBS_INVALID_HOTKEY_ID) obs_hotkey_unregister(id);
}

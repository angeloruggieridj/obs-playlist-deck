// SPDX-License-Identifier: MIT
#include "PlaylistDock.hpp"
#include "DeckStyle.hpp"
#include "Format.hpp"
#include "Heal.hpp"
#include "MediaPath.hpp"
#include "PlaylistIO.hpp"
#include "PlaylistModel.hpp"
#include "PlaylistView.hpp"
#include "Schedule.hpp"
#include "UpdateChecker.hpp"
#include "VendorBridge.hpp"
#include "Version.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QAbstractItemView>
#include <QAction>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QCursor>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QShortcut>
#include <QSize>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QSvgRenderer>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <set>
#include <utility>

#ifndef PLD_VERSION
#define PLD_VERSION "0.0.0"
#endif

using namespace pld;

namespace {
const char* kReleasesUrl = "https://github.com/angeloruggieridj/obs-playlist-deck/releases";

// Localized string lookup (falls back to the key if a translation is missing).
QString T(const char* key) { return QString::fromUtf8(obs_module_text(key)); }

// Toolbar metrics, in logical pixels. The glyph matches what tintedIcon()
// rasterizes; the square keeps a row of icon-only buttons as narrow as the dock
// allows while staying a comfortable click target.
constexpr int kIconPx = pld::style::kIconPx;
constexpr int kButtonPx = pld::style::kButtonPx;

// Timings, named rather than sprinkled through the file as bare numbers.
constexpr int kTickMs = 500;             // playback position refresh
constexpr int kStatusFadeMs = 6000;      // how long a transient status stays up
constexpr int kLibraryDebounceMs = 800;  // coalesces bursts of library writes
constexpr int kWatchDebounceMs = 1200;   // lets a file finish being copied in
constexpr int kDurationRetryMs = 400;    // re-read a duration the decoder had not filled in
constexpr int kDurationRetries = 4;
constexpr int kScheduleWarnSeconds = 10; // how early the countdown appears
constexpr qint64 kStateEventMs = 1000;   // vendor playback-state event rate
constexpr qint64 kMaxPlaylistBytes = 10ll * 1024 * 1024; // refuse absurd playlist files

// Draws a row as three zones — index, title, duration — so durations line up in
// a column that can be scanned, and the playing item is marked by an accent bar
// rather than by a full-width highlight that fights the OBS theme.
class ItemDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QSize s = QStyledItemDelegate::sizeHint(option, index);
        s.setHeight(std::max(s.height(), pld::style::kRowPx));
        return s;
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const QPalette& pal = opt.palette;
        const bool missing = index.data(PlaylistModel::MissingRole).toBool();
        const bool current = index.data(PlaylistModel::CurrentRole).toBool();
        const QString duration = index.data(PlaylistModel::DurationRole).toString();
        const int number = index.row() + 1;

        painter->save();
        // Background: selection and hover come from the style so the dock keeps
        // looking like the rest of OBS.
        opt.text.clear();
        if (opt.widget)
            opt.widget->style()->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

        QRect r = opt.rect.adjusted(4, 0, -4, 0);
        if (current) {
            QRect bar(opt.rect.left() + 1, opt.rect.top() + 3, 2, opt.rect.height() - 6);
            painter->fillRect(bar, pld::style::accent(pal));
        }

        const QFontMetrics fm(opt.font);
        // Index column, dimmed: position matters in a set list.
        const QString num = QString::number(number);
        const int numW =
            std::max(fm.horizontalAdvance(QStringLiteral("99")), fm.horizontalAdvance(num)) + 6;
        QRect numRect(r.left() + 4, r.top(), numW, r.height());
        painter->setPen(pld::style::textSecondary(pal));
        painter->drawText(numRect, Qt::AlignLeft | Qt::AlignVCenter, num);

        // Duration column, right aligned.
        int durW = 0;
        if (!duration.isEmpty()) {
            durW = fm.horizontalAdvance(duration) + 8;
            QRect durRect(r.right() - durW, r.top(), durW, r.height());
            painter->setPen(pld::style::textSecondary(pal));
            painter->drawText(durRect, Qt::AlignRight | Qt::AlignVCenter, duration);
        }

        // Title, elided in the middle so both the name and its ending survive.
        QRect titleRect(numRect.right() + 2, r.top(), r.width() - numW - durW - 6, r.height());
        QColor ink = pal.color(QPalette::Text);
        if (opt.state & QStyle::State_Selected) ink = pal.color(QPalette::HighlightedText);
        if (missing) ink = pld::style::warning(pal);
        else if (current) ink = pld::style::accent(pal);
        painter->setPen(ink);
        QFont f = opt.font;
        f.setBold(current);
        painter->setFont(f);
        painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                          fm.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideMiddle,
                                        titleRect.width()));
        painter->restore();
    }
};

// Media files in a folder, sorted the way people read numbers.
QStringList mediaFilesIn(const QString& dir, bool recursive) {
    QStringList found;
    if (dir.isEmpty() || !QFileInfo::exists(dir)) return found;
    QDirIterator it(dir, QDir::Files | QDir::Readable,
                    recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
    while (it.hasNext()) {
        const QString path = it.next();
        if (mediapath::isMediaFile(path.toStdString())) found << path;
    }
    std::sort(found.begin(), found.end(), [](const QString& a, const QString& b) {
        return mediapath::naturalLess(a.toStdString(), b.toStdString());
    });
    return found;
}
} // namespace

QString PlaylistDock::dockTitle() {
    return QString::fromUtf8(obs_module_text("PlaylistDeck"));
}

// The constructor runs during obs_module_load(), before any scene collection
// exists. Keep it to settings and UI only; source-, library- and hotkey-related
// setup happens in frontendLoaded().
PlaylistDock::PlaylistDock(QWidget* parent) : QWidget(parent) {
    setObjectName("obs-playlist-deck-widget");
    loadSettings();
    applyLocale();

    scanner_ = new MediaScanner(this);
    connect(scanner_, &MediaScanner::resultsReady, this, &PlaylistDock::onScanResults);
    updateChecker_ = new UpdateChecker(this);
    connect(updateChecker_, &UpdateChecker::resultReady, this, &PlaylistDock::onUpdateResult);

    watcher_ = new QFileSystemWatcher(this);
    connect(watcher_, &QFileSystemWatcher::directoryChanged, this,
            [this](const QString&) { onWatchedFolderChanged(); });

    buildUi();

    controller_.setOnMediaEnded([this]() {
        QMetaObject::invokeMethod(this, "onMediaEnded", Qt::QueuedConnection);
    });
    controller_.setOnMediaStarted([this]() {
        QMetaObject::invokeMethod(this, "onMediaStarted", Qt::QueuedConnection);
    });
    controller_.setOnDeactivated([this]() {
        QMetaObject::invokeMethod(this, "onSourceDeactivated", Qt::QueuedConnection);
    });
    controller_.setOnMuteChanged([this](bool muted) {
        QMetaObject::invokeMethod(this, "onMuteChanged", Qt::QueuedConnection, Q_ARG(bool, muted));
    });

    uiTimer_ = new QTimer(this);
    uiTimer_->setInterval(kTickMs);
    connect(uiTimer_, &QTimer::timeout, this, &PlaylistDock::onTick);
    uiTimer_->start();

    statusTimer_ = new QTimer(this);
    statusTimer_->setSingleShot(true);
    connect(statusTimer_, &QTimer::timeout, this, [this]() {
        if (status_) status_->clear();
    });

    // The library used to be written to disk on every rebuild — that is once per
    // added file, per probed duration, per reorder. Debounced, a burst costs one
    // write; shutdown() flushes whatever is still pending.
    libraryTimer_ = new QTimer(this);
    libraryTimer_->setSingleShot(true);
    libraryTimer_->setInterval(kLibraryDebounceMs);
    connect(libraryTimer_, &QTimer::timeout, this, [this]() { saveLibraryNow(); });

    // A file copied into a watched folder appears the moment it is created and
    // grows for as long as the copy takes. Waiting a moment before looking is
    // the difference between adding a clip and adding a broken one.
    watchTimer_ = new QTimer(this);
    watchTimer_->setSingleShot(true);
    watchTimer_->setInterval(kWatchDebounceMs);
    connect(watchTimer_, &QTimer::timeout, this, [this]() { scanWatchFolder(); });
}

void PlaylistDock::frontendLoaded() {
    refreshSources();
    loadLibrary();
    // The bound media source persists its previous file across OBS restarts; if
    // no playlist item is loaded, clear it so sending the source to Program does
    // not replay the clip from before the last shutdown.
    clearStalePluginFile();
    registerHotkeys();
    // Defer the request so the asynchronous reply lands on a settled event loop.
    QTimer::singleShot(0, this, [this]() {
        if (updateChecker_) updateChecker_->check(false);
    });
}

PlaylistDock::~PlaylistDock() {
    // Most obs cleanup happens in shutdown() (called on EXIT). Guard against the
    // case where the dtor runs without a prior shutdown.
    if (!obsShutdown_) shutdown();
}

void PlaylistDock::shutdown() {
    if (obsShutdown_) return;
    obsShutdown_ = true;
    saveSettings();
    // Flush a debounced write rather than losing the last edits, and keep one
    // backup of how the library looked at the end of the show.
    if (libraryTimer_) libraryTimer_->stop();
    commitToLibrary();
    store_.saveLibrary(library_.entries(), library_.activeIndex());
    store_.backupLibrary(/*force=*/true);
    unregisterHotkeys();
    controller_.setOnMediaEnded(nullptr);
    controller_.setOnMediaStarted(nullptr);
    controller_.setOnDeactivated(nullptr);
    controller_.setOnMuteChanged(nullptr);
    controller_.unbind();
    // Stop the worker threads while there is still an event loop to stop them
    // with, and wait: a thread still running into OBS's teardown is the crash
    // this replaced.
    if (scanner_) scanner_->shutdown();
    if (updateChecker_) updateChecker_->shutdown();
}

void PlaylistDock::releaseSource() {
    engine_.playlistChanged(); // drops a staged clip whose binding is going away
    // Keep the user's choice across the collection swap so it is restored when
    // they switch back to the collection that owns this source.
    if (controller_.isBound()) settings_.source = QString::fromStdString(controller_.boundName());
    controller_.unbind();
    snapshotStatus();
}

void PlaylistDock::clearStalePluginFile() {
    if (!controller_.isBound() || playlist_.currentIndex() >= 0) return;
    const std::string current = controller_.currentFile();
    if (current.empty()) return;
    // Only wipe a file this plugin loaded from the playlist. Anything else was
    // configured by the user in OBS itself, and emptying it would silently
    // destroy their scene setup.
    for (const auto& it : playlist_.items()) {
        if (it.path == current) {
            controller_.clearFile();
            return;
        }
    }
}

// ---- Icons ---------------------------------------------------------------

QIcon PlaylistDock::tintedIcon(const QString& resource) const {
    const QColor c = palette().color(QPalette::ButtonText);
    // Rasterizing an SVG twice per button is not free, and a theme switch
    // re-tints every one of them: cache by resource and ink colour.
    const QString key = resource + QLatin1Char('|') + c.name(QColor::HexArgb);
    auto cached = iconCache_.constFind(key);
    if (cached != iconCache_.constEnd()) return cached.value();

    QSvgRenderer renderer(resource);
    const int logical = kIconPx;
    const qreal dpr = devicePixelRatioF();
    QPixmap pm(static_cast<int>(logical * dpr), static_cast<int>(logical * dpr));
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        renderer.render(&p);
    }
    {
        QPainter p(&pm);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(pm.rect(), c);
    }
    pm.setDevicePixelRatio(dpr);
    const QIcon icon(pm);
    iconCache_.insert(key, icon);
    return icon;
}

void PlaylistDock::retintIcons() {
    lastTransportState_ = -1; // force the transport glyph to be re-tinted too
    // OBS 31 switches theme without restarting. Icons tinted once at build time
    // stayed dark-on-dark (or light-on-light) from that moment on.
    for (const auto& entry : iconButtons_) {
        if (!entry.first) continue;
        entry.first->setIcon(tintedIcon(entry.second));
    }
    updateTransportIcons();
    updateMuteButton();
}

void PlaylistDock::applyTheme() {
    if (content_) content_->setStyleSheet(pld::style::sheet(palette()));
    retintIcons();
    rebuildList(); // missing/current colours are palette-derived too
    updateNowPlaying();
}

void PlaylistDock::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange ||
        event->type() == QEvent::ApplicationPaletteChange) {
        applyTheme();
    }
}

void PlaylistDock::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    // The 500 ms tick used to run for the entire OBS session even with the dock
    // closed, polling the source and repainting widgets nobody could see.
    if (uiTimer_) uiTimer_->start();
    onTick();
}

void PlaylistDock::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    if (uiTimer_) uiTimer_->stop();
}

// ---- UI ------------------------------------------------------------------

void PlaylistDock::buildUi() {
    iconButtons_.clear();
    auto* root = new QWidget(this);
    root->setObjectName("pldRoot");
    auto* col = new QVBoxLayout(root);
    col->setContentsMargins(6, 6, 6, 6);
    col->setSpacing(5);

    auto inlineLabel = [this](const QString& text) {
        auto* l = new QLabel(text);
        QPalette pal = l->palette();
        pal.setColor(QPalette::WindowText, pld::style::textSecondary(palette()));
        l->setPalette(pal);
        return l;
    };

    // Icon-only push button. Dropping the caption is what lets a full row of
    // controls fit a narrow dock; the localized description moves to the
    // tooltip, and the equally localized name stays available to screen readers
    // through accessibleName. Buttons are keyboard-reachable: they used to be
    // Qt::NoFocus, which made every one of them unreachable by Tab while the
    // accessible names claimed otherwise.
    auto mk = [this](const QString& icon, const QString& name, const QString& tip) {
        auto* b = new QPushButton(tintedIcon(icon), QString());
        b->setToolTip(tip);
        b->setAccessibleName(name);
        b->setAccessibleDescription(tip);
        b->setIconSize(QSize(kIconPx, kIconPx));
        b->setFixedSize(kButtonPx, kButtonPx);
        b->setProperty("pldIcon", true);
        b->setCursor(Qt::PointingHandCursor);
        iconButtons_.append({QPointer<QPushButton>(b), icon});
        return b;
    };

    // ---- Playlist library --------------------------------------------------
    // A show is rarely one list: a warm-up set, the main set, a folder of
    // stingers. The picker is one row; everything else about a playlist lives
    // behind its menu, so a narrow dock does not grow a panel for it.
    auto* libRow = new QHBoxLayout();
    libRow->setSpacing(4);
    libRow->addWidget(inlineLabel(T("Section.Playlists")));
    playlistCombo_ = new QComboBox();
    playlistCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    playlistCombo_->setMinimumHeight(kButtonPx);
    playlistCombo_->setAccessibleName(T("Section.Playlists"));
    auto* libMenuBtn = mk(":/icons/library.svg", T("Btn.PlaylistMenu"), T("Tip.PlaylistMenu"));
    libRow->addWidget(playlistCombo_, 1);
    libRow->addWidget(libMenuBtn);
    col->addLayout(libRow);

    // ---- Media source (one row: inline label + picker + rescan) -----------
    auto* srcRow = new QHBoxLayout();
    srcRow->setSpacing(4);
    srcRow->addWidget(inlineLabel(T("Section.MediaSource")));
    sourceCombo_ = new QComboBox();
    sourceCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    sourceCombo_->setMinimumHeight(kButtonPx);
    sourceCombo_->setAccessibleName(T("Section.MediaSource"));
    auto* refreshBtn = mk(":/icons/refresh.svg", T("Btn.Refresh"), T("Tip.Refresh"));
    srcRow->addWidget(sourceCombo_, 1);
    srcRow->addWidget(refreshBtn);
    col->addLayout(srcRow);

    // ---- Now playing card -------------------------------------------------
    // Everything about the clip on air in one block: title, position, transport.
    // It used to be spread over three widgets in three parts of the dock.
    card_ = new QFrame();
    card_->setObjectName("pldCard");
    auto* cardCol = new QVBoxLayout(card_);
    cardCol->setContentsMargins(8, 6, 8, 6);
    cardCol->setSpacing(3);

    auto* titleRow = new QHBoxLayout();
    titleRow->setSpacing(6);
    auto* nowIcon = new QLabel();
    nowIcon->setPixmap(tintedIcon(":/icons/music.svg").pixmap(kIconPx, kIconPx));
    nowTitle_ = new QLabel(T("Card.Idle"));
    QFont titleFont = nowTitle_->font();
    titleFont.setBold(true);
    nowTitle_->setFont(titleFont);
    nowTitle_->setAccessibleName(T("Card.NowPlaying"));
    nowMeta_ = new QLabel();
    nowMeta_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    titleRow->addWidget(nowIcon);
    titleRow->addWidget(nowTitle_, 1);
    titleRow->addWidget(nowMeta_);
    cardCol->addLayout(titleRow);

    seek_ = new QSlider(Qt::Horizontal);
    seek_->setObjectName("pldSeek");
    seek_->setRange(0, 1000);
    seek_->setValue(0);
    seek_->setAccessibleName(T("Card.Progress"));
    seek_->setToolTip(T("Card.Progress"));
    cardCol->addWidget(seek_);

    auto* timeRow = new QHBoxLayout();
    timeLabel_ = new QLabel();
    // Tabular figures: a proportional font makes the counter jitter as digits
    // change, which is exactly what peripheral vision notices.
    timeLabel_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    upNext_ = new QLabel();
    upNext_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    timeRow->addWidget(timeLabel_);
    timeRow->addStretch(1);
    timeRow->addWidget(upNext_);
    cardCol->addLayout(timeRow);

    auto* trRow = new QHBoxLayout();
    trRow->setSpacing(3);
    auto* prevBtn = mk(":/icons/skip-back.svg", T("Btn.Prev"), T("Tip.Prev"));
    playPauseBtn_ = mk(":/icons/play.svg", T("Btn.Pause"), T("Tip.Pause"));
    auto* stopBtn = mk(":/icons/stop.svg", T("Btn.Stop"), T("Tip.Stop"));
    auto* nextBtn = mk(":/icons/skip-forward.svg", T("Btn.Next"), T("Tip.Next"));
    muteBtn_ = mk(":/icons/volume.svg", T("Btn.Mute"), T("Tip.Mute"));
    muteBtn_->setCheckable(true);
    auto* panicBtn = mk(":/icons/panic.svg", T("Btn.Panic"), T("Tip.Panic"));
    auto* playSelBtn = mk(":/icons/play.svg", T("Btn.Play"), T("Tip.Play"));
    for (auto* b : {prevBtn, playPauseBtn_, stopBtn, nextBtn, muteBtn_}) trRow->addWidget(b);
    trRow->addStretch(1);
    trRow->addWidget(panicBtn);
    trRow->addWidget(playSelBtn);
    cardCol->addLayout(trRow);
    col->addWidget(card_);

    // ---- Filter -----------------------------------------------------------
    auto* filterRow = new QHBoxLayout();
    filterRow->setSpacing(4);
    filterEdit_ = new QLineEdit();
    filterEdit_->setClearButtonEnabled(true);
    filterEdit_->setPlaceholderText(T("Filter.Placeholder"));
    filterEdit_->setAccessibleName(T("Filter.Placeholder"));
    filterEdit_->addAction(tintedIcon(":/icons/search.svg"), QLineEdit::LeadingPosition);
    filterCount_ = new QLabel();
    filterCount_->setAccessibleName(T("Filter.Matches.Name"));
    filterRow->addWidget(filterEdit_, 1);
    filterRow->addWidget(filterCount_);
    col->addLayout(filterRow);

    // ---- The list, which is what the dock is for --------------------------
    model_ = new PlaylistModel(this);
    list_ = new PlaylistView();
    list_->setModel(model_);
    list_->setItemDelegate(new ItemDelegate(list_));
    list_->setPlaceholder(T("Empty.Title"), T("Empty.Hint"));
    col->addWidget(list_, 1);

    // ---- Ops toolbar, attached under the thing it edits --------------------
    auto* opsRow = new QHBoxLayout();
    opsRow->setSpacing(3);
    auto* addBtn = mk(":/icons/plus.svg", T("Btn.Add"), T("Tip.Add"));
    auto* rmBtn = mk(":/icons/minus.svg", T("Btn.Remove"), T("Tip.Remove"));
    auto* upBtn = mk(":/icons/chevron-up.svg", T("Btn.Up"), T("Tip.Up"));
    auto* downBtn = mk(":/icons/chevron-down.svg", T("Btn.Down"), T("Tip.Down"));
    auto* renameBtn = mk(":/icons/pencil.svg", T("Btn.Rename"), T("Tip.Rename"));
    auto* clrBtn = mk(":/icons/trash.svg", T("Btn.Clear"), T("Tip.Clear"));
    undoBtn_ = mk(":/icons/undo.svg", T("Btn.Undo"), T("Tip.Undo"));
    for (auto* b : {addBtn, rmBtn, upBtn, downBtn, renameBtn, clrBtn, undoBtn_})
        opsRow->addWidget(b);
    opsRow->addStretch(1);
    totalsLabel_ = new QLabel();
    opsRow->addWidget(totalsLabel_);
    col->addLayout(opsRow);

    // ---- End-of-clip mode and playlist files, one row ---------------------
    auto* endRow = new QHBoxLayout();
    endRow->setSpacing(4);
    endRow->addWidget(inlineLabel(T("OnEnd.Label")));
    endCombo_ = new QComboBox();
    endCombo_->setAccessibleName(T("OnEnd.Label"));
    // The mapping is the table, not the row order: inserting a mode in the
    // middle of the menu used to silently reassign every mode after it.
    for (const auto& m : pld::kEndModes) {
        endCombo_->addItem(T(m.key), static_cast<int>(m.mode));
        endCombo_->setItemData(endCombo_->count() - 1, T(m.tipKey), Qt::ToolTipRole);
    }
    const int modeIdx = endCombo_->findData(static_cast<int>(engine_.mode()));
    endCombo_->setCurrentIndex(modeIdx >= 0 ? modeIdx : 0);
    endCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* saveBtn = mk(":/icons/save.svg", T("Btn.Save"), T("Tip.Save"));
    auto* openBtn = mk(":/icons/folder-open.svg", T("Btn.Open"), T("Tip.Open"));
    endRow->addWidget(endCombo_, 1);
    endRow->addWidget(saveBtn);
    endRow->addWidget(openBtn);
    col->addLayout(endRow);

    loadedLabel_ = new QLabel(T("NoPlaylist"));
    loadedLabel_->setWordWrap(true);
    col->addWidget(loadedLabel_);

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

    // buildUi() runs again on a language change, so swap the previous contents
    // out rather than stacking a second widget in the layout.
    auto* outer = qobject_cast<QVBoxLayout*>(layout());
    if (!outer) {
        outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
    }
    if (content_) {
        outer->removeWidget(content_);
        content_->hide();
        content_->deleteLater();
    }
    outer->addWidget(root);
    content_ = root;
    root->setStyleSheet(pld::style::sheet(palette()));

    connect(settingsBtn, &QPushButton::clicked, this, &PlaylistDock::onOpenSettings);
    connect(libMenuBtn, &QPushButton::clicked, this, &PlaylistDock::onPlaylistMenu);
    connect(refreshBtn, &QPushButton::clicked, this, &PlaylistDock::refreshSources);
    connect(addBtn, &QPushButton::clicked, this, &PlaylistDock::onAddFiles);
    connect(rmBtn, &QPushButton::clicked, this, &PlaylistDock::onRemove);
    connect(upBtn, &QPushButton::clicked, this, &PlaylistDock::onUp);
    connect(downBtn, &QPushButton::clicked, this, &PlaylistDock::onDown);
    connect(renameBtn, &QPushButton::clicked, this, &PlaylistDock::onRename);
    connect(clrBtn, &QPushButton::clicked, this, &PlaylistDock::onClear);
    connect(undoBtn_, &QPushButton::clicked, this, &PlaylistDock::onUndo);
    connect(playSelBtn, &QPushButton::clicked, this, &PlaylistDock::onPlaySelected);
    connect(prevBtn, &QPushButton::clicked, this, &PlaylistDock::onPrev);
    connect(playPauseBtn_, &QPushButton::clicked, this, &PlaylistDock::onTogglePlayPause);
    connect(stopBtn, &QPushButton::clicked, this, &PlaylistDock::onStop);
    connect(nextBtn, &QPushButton::clicked, this, &PlaylistDock::onNext);
    connect(muteBtn_, &QPushButton::clicked, this, &PlaylistDock::onToggleMute);
    connect(panicBtn, &QPushButton::clicked, this, &PlaylistDock::onPanic);
    connect(saveBtn, &QPushButton::clicked, this, &PlaylistDock::onSavePlaylist);
    connect(openBtn, &QPushButton::clicked, this, &PlaylistDock::onOpenPlaylist);

    connect(list_, &QAbstractItemView::doubleClicked, this,
            [this](const QModelIndex&) { onPlaySelected(); });
    connect(list_, &PlaylistView::filesDropped, this, &PlaylistDock::onFilesDropped);
    connect(list_, &PlaylistView::playRequested, this, &PlaylistDock::onPlaySelected);
    connect(list_, &PlaylistView::removeRequested, this, &PlaylistDock::onRemove);
    connect(list_, &PlaylistView::renameRequested, this, &PlaylistDock::onRename);
    connect(list_, &QWidget::customContextMenuRequested, this, &PlaylistDock::onContextMenu);
    connect(model_, &PlaylistModel::renameRequested, this, &PlaylistDock::onItemRenamed);
    connect(model_, &PlaylistModel::moveRequested, this, &PlaylistDock::onRowsMoved);
    connect(filterEdit_, &QLineEdit::textChanged, this, &PlaylistDock::onFilterChanged);
    connect(seek_, &QSlider::sliderReleased, this, &PlaylistDock::onSeekReleased);
    connect(sourceCombo_, &QComboBox::currentIndexChanged, this, &PlaylistDock::onSourceChanged);
    connect(playlistCombo_, &QComboBox::currentIndexChanged, this,
            &PlaylistDock::onPlaylistSelected);
    connect(endCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        engine_.setMode(pld::endModeFromInt(endCombo_->currentData().toInt()));
        saveSettings();
        updateNowPlaying();
        snapshotStatus();
    });

    // Keyboard: the dock is fully operable without a mouse. Scoped to this
    // widget so none of it fights OBS's own shortcuts.
    auto shortcut = [this](QKeySequence::StandardKey key, void (PlaylistDock::*fn)()) {
        auto* s = new QShortcut(QKeySequence(key), this);
        s->setContext(Qt::WidgetWithChildrenShortcut);
        connect(s, &QShortcut::activated, this, fn);
    };
    shortcut(QKeySequence::Undo, &PlaylistDock::onUndo);
    shortcut(QKeySequence::Redo, &PlaylistDock::onRedo);
    auto* findShortcut = new QShortcut(QKeySequence(QKeySequence::Find), this);
    findShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(findShortcut, &QShortcut::activated, this, [this]() {
        if (filterEdit_) filterEdit_->setFocus(Qt::ShortcutFocusReason);
    });

    lastTransportState_ = -1; // the button is new; whatever it showed is gone
    updatePlaylistCombo();
    rebuildList();
    updateNowPlaying();
    updateTransportIcons();
    updateMuteButton();
}

QString PlaylistDock::itemText(int row) const {
    return QString::fromStdString(playlist_.items()[row].title);
}

void PlaylistDock::rebuildList() {
    if (!model_ || !list_) return;
    // A model reset drops the selection, so it is put back afterwards: the user
    // chose those rows, and a duration arriving is no reason to lose them.
    const int currentRow = list_->currentIndex().row();
    const std::vector<int> selected = selectedRows();

    QVector<PlaylistRow> rows;
    rows.reserve(playlist_.size());
    for (int i = 0; i < playlist_.size(); ++i) {
        const auto& pi = playlist_.items()[i];
        PlaylistRow row;
        row.path = QString::fromStdString(pi.path);
        // The existence check comes from the cache the scanner fills. It used to
        // be a synchronous stat() per item on every rebuild — on a network share
        // that is a visible freeze on every single click.
        const auto cached = existsCache_.constFind(row.path);
        row.missing = (cached != existsCache_.constEnd()) && !cached.value();
        row.title = QString::fromStdString(pi.title);
        if (row.missing) row.title += QStringLiteral("  \u26A0 ") + T("FileNotFound");
        row.duration = QString::fromStdString(formatDuration(pi.durationMs));
        row.current = (i == playlist_.currentIndex());
        rows.append(row);
    }
    model_->setRows(std::move(rows));

    if (currentRow >= 0 && currentRow < playlist_.size())
        list_->setCurrentIndex(model_->index(currentRow));
    if (auto* selection = list_->selectionModel()) {
        for (int row : selected) {
            if (row < 0 || row >= playlist_.size()) continue;
            selection->select(model_->index(row), QItemSelectionModel::Select);
        }
    }

    applyFilter();
    updateTotals();
    if (undoBtn_) undoBtn_->setEnabled(history_.canUndo());
    saveLibrarySoon();
    snapshotStatus();
}

std::vector<int> PlaylistDock::selectedRows() const {
    std::vector<int> rows;
    if (!list_ || !list_->selectionModel()) return rows;
    for (const QModelIndex& index : list_->selectionModel()->selectedIndexes())
        if (index.isValid()) rows.push_back(index.row());
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    return rows;
}

void PlaylistDock::applyFilter() {
    if (!list_ || !model_) return;
    const QString f = filterEdit_ ? filterEdit_->text().trimmed() : QString();
    int shown = 0;
    for (int row = 0; row < model_->rowCount(); ++row) {
        const bool match = f.isEmpty() || model_->rowAt(row).title.contains(f, Qt::CaseInsensitive);
        list_->setRowHidden(row, !match);
        if (match) ++shown;
    }
    if (filterCount_) {
        // The count only means something while a filter is active.
        filterCount_->setText(
            f.isEmpty() ? QString() : T("Filter.Matches").arg(shown).arg(model_->rowCount()));
    }
}

void PlaylistDock::onFilterChanged(const QString&) { applyFilter(); }

void PlaylistDock::updateTotals() {
    if (!totalsLabel_) return;
    if (playlist_.empty()) {
        totalsLabel_->clear();
        return;
    }
    QString total = QString::fromStdString(formatDuration(playlist_.totalDurationMs()));
    if (playlist_.unknownDurationCount() > 0) total = QStringLiteral("~") + total;
    totalsLabel_->setText(T("Totals").arg(playlist_.size()).arg(total));
    QPalette pal = totalsLabel_->palette();
    pal.setColor(QPalette::WindowText, pld::style::textSecondary(palette()));
    totalsLabel_->setPalette(pal);
}

void PlaylistDock::updateTransportIcons() {
    if (!playPauseBtn_) return;
    // One button, two meanings: it used to always show a pause glyph, so nothing
    // on screen said whether the clip was running. Called from the twice-a-second
    // tick, so it only touches the widget when the state actually changed.
    const int playing = controller_.isPlaying() ? 1 : 0;
    if (playing == lastTransportState_) return;
    lastTransportState_ = playing;
    playPauseBtn_->setIcon(tintedIcon(playing ? QStringLiteral(":/icons/pause.svg")
                                              : QStringLiteral(":/icons/play.svg")));
    playPauseBtn_->setToolTip(T("Tip.Pause"));
}

void PlaylistDock::updateMuteButton() {
    if (!muteBtn_) return;
    const bool bound = controller_.isBound();
    const bool muted = bound && controller_.isMuted();
    muteBtn_->setEnabled(bound);
    muteBtn_->setChecked(muted);
    muteBtn_->setIcon(tintedIcon(muted ? QStringLiteral(":/icons/volume-x.svg")
                                       : QStringLiteral(":/icons/volume.svg")));
}

void PlaylistDock::updateNowPlaying() {
    if (!nowTitle_) return;

    // A scheduled start outranks everything else in that line: it is the only
    // thing on screen that says the deck is about to act on its own.
    const long long startMs = library_.active().scheduledStartMs;
    const auto schedule =
        pld::scheduleStatus(startMs, QDateTime::currentMSecsSinceEpoch(), kScheduleWarnSeconds);

    const auto* it = playlist_.current();
    if (!it) {
        nowTitle_->setText(T("Card.Idle"));
        nowMeta_->clear();
        if (seek_ && !seek_->isSliderDown()) seek_->setValue(0);
        if (timeLabel_) timeLabel_->clear();
    } else {
        const QString title = QString::fromStdString(it->title);
        const QFontMetrics fm(nowTitle_->font());
        const int w = std::max(60, nowTitle_->width());
        nowTitle_->setText(fm.elidedText(title, Qt::ElideMiddle, w));
        nowTitle_->setToolTip(QString::fromStdString(it->path));
        nowMeta_->setText(QString::fromStdString(formatDuration(it->durationMs)));
    }

    // What happens next, spelled out: in a live show the operator has to know
    // what the deck will do on its own.
    const bool waiting = engine_.stageIsWaiting();
    bool alarming = waiting;
    if (schedule.state == pld::ScheduleState::Warning ||
        schedule.state == pld::ScheduleState::Waiting) {
        upNext_->setText(T("Card.StartingIn")
                             .arg(QString::fromStdString(formatDuration(schedule.remainingMs))));
        alarming = (schedule.state == pld::ScheduleState::Warning);
    } else if (waiting) {
        upNext_->setText(T("Card.Pending"));
    } else {
        const int nextIdx = engine_.upNextIndex();
        upNext_->setText(nextIdx >= 0 && nextIdx < playlist_.size()
                             ? T("Card.UpNext").arg(itemText(nextIdx))
                             : QString());
    }
    QPalette pal = upNext_->palette();
    pal.setColor(QPalette::WindowText,
                 alarming ? pld::style::warning(palette()) : pld::style::textSecondary(palette()));
    upNext_->setPalette(pal);
}

// ---- Playback ------------------------------------------------------------

void PlaylistDock::playIndex(int row) {
    stagePauseWanted_ = false;
    applyPlayback(engine_.play(row));
}

// One place where an engine decision becomes something the operator can see.
// The engine says what happened; the status line, the duration capture, the
// remote event and the redraw all follow from that, rather than being repeated
// at every call site the way they were when the dock did the deciding itself.
void PlaylistDock::applyPlayback(const pld::PlaybackResult& result) {
    const int row = result.index;
    const QString title = (row >= 0 && row < playlist_.size()) ? itemText(row) : QString();

    switch (result.outcome) {
    case pld::PlaybackOutcome::Started:
        setStatus(T("Status.Playing").arg(title), StatusKind::Success);
        captureRow_ = row;
        capturePath_ = row >= 0 ? QString::fromStdString(playlist_.items()[row].path) : QString();
        captureRetries_ = 0;
        emitVendorItemStarted();
        break;
    case pld::PlaybackOutcome::Staged:
        setStatus(T("Status.StagedLoaded").arg(title), StatusKind::Info);
        // The pause is repeated when the source reports it started, because a
        // large or remote file may not have opened yet. Waiting for the signal
        // replaces a pair of guessed timers that let heavy clips slip on air.
        stagePauseWanted_ = true;
        captureRow_ = row;
        capturePath_ = row >= 0 ? QString::fromStdString(playlist_.items()[row].path) : QString();
        captureRetries_ = 0;
        break;
    case pld::PlaybackOutcome::StagePending:
        setStatus(T("Status.ClipEndedStaged"), StatusKind::Info);
        break;
    case pld::PlaybackOutcome::Stopped: {
        // Only the engine stops on its own, and only at the end of the list.
        obs_data_t* d = obs_data_create();
        pld::emitVendorEvent("playlist-completed", d);
        obs_data_release(d);
        break;
    }
    case pld::PlaybackOutcome::NoSource:
        setStatus(T("Status.NoSourceConfigured"), StatusKind::Error);
        break;
    case pld::PlaybackOutcome::Failed:
        setStatus(T("Status.FailedSetSource"), StatusKind::Error);
        break;
    case pld::PlaybackOutcome::Nothing:
        break;
    }

    rebuildList();
    // Playback moved on its own: show where it went, without taking the
    // selection away from wherever the operator put it.
    if (row >= 0 && row < playlist_.size() && list_ && model_)
        list_->scrollTo(model_->index(row), QAbstractItemView::EnsureVisible);
    updateNowPlaying();
    updateTransportIcons();
}

void PlaylistDock::captureDuration() {
    if (captureRow_ < 0 || captureRow_ >= playlist_.size()) return;
    // The item must still be the one this capture was scheduled for. A fixed
    // 700 ms timer had no way to know that, so pressing Next quickly wrote the
    // new clip's duration onto the previous item.
    if (playlist_.currentIndex() != captureRow_) return;
    if (QString::fromStdString(playlist_.items()[captureRow_].path) != capturePath_) return;
    if (playlist_.items()[captureRow_].durationMs >= 0) return;

    const long long d = controller_.currentDurationMs();
    if (d <= 0) {
        // Streaming or slow-opening files report nothing yet; try a few times,
        // then leave it to the background probe.
        if (++captureRetries_ <= kDurationRetries)
            QTimer::singleShot(kDurationRetryMs, this, [this]() { captureDuration(); });
        return;
    }
    auto items = playlist_.items();
    items[captureRow_].durationMs = d;
    playlist_.setItemsKeepCurrent(std::move(items));
    rebuildList();
    updateNowPlaying();
}

void PlaylistDock::onMediaStarted() {
    // Tells the engine the clip it handed over is really playing, which is what
    // lets it tell a real end from the outgoing clip's (see mediaEnded()).
    engine_.mediaStarted();
    if (stagePauseWanted_) {
        controller_.pause();
        stagePauseWanted_ = false;
    }
    // Opt-in, and off by default: a deck that silently un-mutes itself would put
    // audio on air that the operator had deliberately taken off it. With the
    // setting on, every clip starts audible; with it off, the mute stays exactly
    // where it was put, across clips and across restarts (OBS owns it).
    if (settings_.unmuteOnStart && controller_.isMuted()) controller_.setMuted(false);
    captureDuration();
    updateTransportIcons();
}

void PlaylistDock::onMediaEnded() {
    applyPlayback(engine_.mediaEnded());
    snapshotStatus();
}

void PlaylistDock::programLayoutChanged() {
    // Program or preview moved: a clip held behind "Load next (paused)" loads
    // as soon as the bound source is no longer on air.
    const auto result = engine_.programLayoutChanged();
    if (result.outcome == pld::PlaybackOutcome::Nothing) {
        updateNowPlaying(); // the "staged, waiting" note may have changed
        return;
    }
    applyPlayback(result);
    snapshotStatus();
}

void PlaylistDock::onSourceDeactivated() {
    // Kept as a fallback: a source removed from every scene raises this and no
    // scene change. The primary trigger is programLayoutChanged().
    programLayoutChanged();
}

// ---- Playlist edits ------------------------------------------------------

void PlaylistDock::recordUndo(const QString& label) {
    history_.push(playlist_.items(), playlist_.currentIndex(), label.toStdString());
    if (undoBtn_) undoBtn_->setEnabled(true);
}

void PlaylistDock::applyHistoryState(std::vector<pld::PlaylistItem> items, int current) {
    playlist_.setItems(std::move(items));
    playlist_.setCurrent(current);
    engine_.playlistChanged();
    rebuildList();
    updateNowPlaying();
}

void PlaylistDock::onUndo() {
    auto items = playlist_.items();
    int current = playlist_.currentIndex();
    std::string label;
    if (!history_.undo(items, current, label)) {
        setStatus(T("Status.NothingToUndo"), StatusKind::Info);
        return;
    }
    applyHistoryState(std::move(items), current);
    setStatus(T("Status.Undone").arg(QString::fromStdString(label)), StatusKind::Info);
}

void PlaylistDock::onRedo() {
    auto items = playlist_.items();
    int current = playlist_.currentIndex();
    std::string label;
    if (!history_.redo(items, current, label)) return;
    applyHistoryState(std::move(items), current);
    setStatus(T("Status.Redone").arg(QString::fromStdString(label)), StatusKind::Info);
}

void PlaylistDock::addPaths(const QStringList& paths, bool undoable) {
    QStringList added;
    std::set<std::string> known;
    for (const auto& it : playlist_.items()) known.insert(it.path);
    for (const auto& f : paths) {
        const std::string p = f.toStdString();
        if (!mediapath::isMediaFile(p)) continue;
        // The same file twice in one playlist helps nobody, and a watched folder
        // would otherwise add its contents again on every change.
        if (known.count(p)) continue;
        known.insert(p);
        added << f;
    }
    if (added.isEmpty()) return;
    if (undoable) recordUndo(T("Edit.Add"));
    for (const auto& f : added)
        playlist_.add(PlaylistItem{f.toStdString(), mediapath::fileStem(f.toStdString()), -1});
    engine_.playlistChanged();
    rebuildList();
    setStatus(T("Status.Added").arg(added.size()), StatusKind::Success);
    startScan(added);
}

void PlaylistDock::startScan(const QStringList& paths, bool replacesPlaylist) {
    if (!scanner_ || paths.isEmpty()) return;
    scanner_->submit(paths, settings_.enableProbe, replacesPlaylist);
}

void PlaylistDock::rescanAll() {
    QStringList all;
    all.reserve(playlist_.size());
    for (const auto& it : playlist_.items()) all << QString::fromStdString(it.path);
    existsCache_.clear();
    startScan(all, /*replacesPlaylist=*/true);
}

void PlaylistDock::onScanResults(const QList<ScanResult>& results) {
    auto items = playlist_.items();
    bool changed = false;
    for (const auto& r : results) {
        existsCache_.insert(r.path, r.exists);
        if (r.durationMs < 0) continue;
        const std::string p = r.path.toStdString();
        for (auto& it : items)
            if (it.path == p && it.durationMs < 0) {
                it.durationMs = r.durationMs;
                changed = true;
            }
    }
    if (changed) playlist_.setItemsKeepCurrent(std::move(items));

    // One row at a time rather than a full reset: a batch arriving while the
    // operator is choosing clips must not throw away their selection, their
    // scroll position or an open rename. This is what the model is for.
    if (model_ && model_->rowCount() == playlist_.size()) {
        for (int i = 0; i < playlist_.size(); ++i) {
            const auto& pi = playlist_.items()[i];
            PlaylistRow row;
            row.path = QString::fromStdString(pi.path);
            const auto cached = existsCache_.constFind(row.path);
            row.missing = (cached != existsCache_.constEnd()) && !cached.value();
            row.title = QString::fromStdString(pi.title);
            if (row.missing) row.title += QStringLiteral("  \u26A0 ") + T("FileNotFound");
            row.duration = QString::fromStdString(formatDuration(pi.durationMs));
            row.current = (i == playlist_.currentIndex());
            const PlaylistRow& shown = model_->rowAt(i);
            if (row.title != shown.title || row.duration != shown.duration ||
                row.missing != shown.missing)
                model_->updateRow(i, row);
        }
        applyFilter();
        updateTotals();
        saveLibrarySoon();
        snapshotStatus();
    } else {
        rebuildList();
    }
    updateNowPlaying();
}

void PlaylistDock::onRecheckFiles() {
    setStatus(T("Status.Rechecking"), StatusKind::Info);
    rescanAll();
}

// NF-10. Files get reorganised between shows, and repairing a playlist full of
// red rows by hand is miserable. The deck knows each file's name and where its
// other files live, which is usually enough to find it again.
void PlaylistDock::onFindMoved() {
    QStringList missing;
    for (const auto& it : playlist_.items()) {
        const QString path = QString::fromStdString(it.path);
        const auto cached = existsCache_.constFind(path);
        if (cached != existsCache_.constEnd() && !cached.value()) missing << path;
    }
    if (missing.isEmpty()) {
        setStatus(T("Status.AllFilesPresent"), StatusKind::Info);
        return;
    }

    // Where to look: the folders the surviving files live in, the watched
    // folder, the folder the playlist file came from, and the last one used.
    QStringList folders;
    for (const auto& it : playlist_.items()) {
        const QString path = QString::fromStdString(it.path);
        const auto cached = existsCache_.constFind(path);
        if (cached != existsCache_.constEnd() && cached.value())
            folders << QFileInfo(path).absolutePath();
    }
    const QString watched = QString::fromStdString(library_.active().watchFolder);
    if (!watched.isEmpty()) folders << watched;
    const QString source = QString::fromStdString(library_.active().sourcePath);
    if (!source.isEmpty()) folders << QFileInfo(source).absolutePath();
    if (!settings_.lastDir.isEmpty()) folders << settings_.lastDir;
    folders.removeDuplicates();

    std::vector<std::string> candidates;
    for (const QString& folder : folders)
        for (const QString& file : mediaFilesIn(folder, /*recursive=*/true))
            candidates.push_back(file.toStdString());

    auto items = playlist_.items();
    int healed = 0;
    int ambiguous = 0;
    for (auto& it : items) {
        if (!missing.contains(QString::fromStdString(it.path))) continue;
        const auto match = pld::findMoved(it.path, candidates);
        if (match.ambiguous) {
            ++ambiguous;
            continue;
        }
        if (match.path.empty()) continue;
        it.path = match.path;
        ++healed;
    }
    if (healed == 0) {
        setStatus(ambiguous > 0 ? T("Status.HealAmbiguous").arg(ambiguous)
                                : T("Status.NoMatchFound"),
                  StatusKind::Warning);
        return;
    }
    recordUndo(T("Edit.Heal"));
    playlist_.setItemsKeepCurrent(std::move(items));
    engine_.playlistChanged();
    rescanAll();
    rebuildList();
    setStatus(T("Status.Healed").arg(healed), StatusKind::Success);
}

void PlaylistDock::onAddFiles() {
    const QStringList files = QFileDialog::getOpenFileNames(
        this, T("Dlg.AddFiles"), settings_.lastDir,
        T("Dlg.MediaFilter") +
            QStringLiteral(" (*.mp4 *.mov *.mkv *.avi *.webm *.m4v *.mpg *.mpeg *.ts *.flv *.wmv "
                           "*.mp3 *.m4a *.aac *.wav *.flac *.ogg *.opus *.3gp);;All files (*)"));
    if (files.isEmpty()) return;
    settings_.lastDir = QFileInfo(files.first()).absolutePath();
    saveSettings();
    addPaths(files);
}

void PlaylistDock::onAddFolder() {
    const QString dir =
        QFileDialog::getExistingDirectory(this, T("Dlg.AddFolder"), settings_.lastDir);
    if (dir.isEmpty()) return;
    settings_.lastDir = dir;
    saveSettings();
    addPaths(mediaFilesIn(dir, /*recursive=*/true));
}

void PlaylistDock::onFilesDropped(const QStringList& paths) { addPaths(paths); }

void PlaylistDock::onRowsMoved(const QVector<int>& rows, int destination) {
    if (rows.isEmpty()) return;
    std::vector<PlaylistItem> moved;
    std::vector<PlaylistItem> rest;
    const int current = playlist_.currentIndex();
    std::string currentPath;
    if (current >= 0 && current < playlist_.size()) currentPath = playlist_.items()[current].path;

    // Rebuild the order explicitly: the dragged rows come out, and go back in at
    // the destination, counted among the rows that stayed put.
    int insertAt = destination;
    for (int i = 0; i < playlist_.size(); ++i) {
        const bool dragged = rows.contains(i);
        if (dragged && i < destination) --insertAt;
        (dragged ? moved : rest).push_back(playlist_.items()[i]);
    }
    if (moved.empty()) return;
    if (insertAt < 0) insertAt = 0;
    if (insertAt > static_cast<int>(rest.size())) insertAt = static_cast<int>(rest.size());

    recordUndo(T("Edit.Reorder"));
    std::vector<PlaylistItem> reordered;
    reordered.reserve(playlist_.items().size());
    reordered.insert(reordered.end(), rest.begin(), rest.begin() + insertAt);
    reordered.insert(reordered.end(), moved.begin(), moved.end());
    reordered.insert(reordered.end(), rest.begin() + insertAt, rest.end());

    playlist_.setItems(std::move(reordered));
    // The item that was playing keeps playing: it is found again by path, not by
    // the position it used to occupy.
    if (!currentPath.empty()) {
        for (int i = 0; i < playlist_.size(); ++i) {
            if (playlist_.items()[i].path == currentPath) {
                playlist_.setCurrent(i);
                break;
            }
        }
    }
    engine_.playlistChanged();
    rebuildList();
    updateNowPlaying();
}

void PlaylistDock::onRemove() {
    std::vector<int> rows = selectedRows();
    if (rows.empty()) {
        const int row = list_ ? list_->currentIndex().row() : -1;
        if (row < 0) return;
        rows.push_back(row);
    }
    removeRows(std::move(rows));
}

void PlaylistDock::removeRows(std::vector<int> rows) {
    if (rows.empty()) return;
    const bool hitCurrent =
        std::find(rows.begin(), rows.end(), playlist_.currentIndex()) != rows.end();
    recordUndo(T("Edit.Remove"));
    const int removed = playlist_.removeMany(rows);
    if (removed <= 0) return;
    if (hitCurrent) {
        // The item that was playing is gone, so nothing is current any more —
        // and leaving the source playing a clip the playlist no longer knows
        // about is exactly the confusion this avoids.
        controller_.stop();
        setStatus(T("Status.StoppedItemRemoved"), StatusKind::Warning);
    } else {
        setStatus(T("Status.Removed").arg(removed), StatusKind::Info);
    }
    engine_.playlistChanged();
    rebuildList();
    updateNowPlaying();
}

void PlaylistDock::onUp() {
    const int r = list_ ? list_->currentIndex().row() : -1;
    if (playlist_.moveUp(r)) {
        recordUndo(T("Edit.Move"));
        rebuildList();
        list_->setCurrentIndex(model_->index(r - 1));
        updateNowPlaying();
    }
}

void PlaylistDock::onDown() {
    const int r = list_ ? list_->currentIndex().row() : -1;
    if (playlist_.moveDown(r)) {
        recordUndo(T("Edit.Move"));
        rebuildList();
        list_->setCurrentIndex(model_->index(r + 1));
        updateNowPlaying();
    }
}

void PlaylistDock::onClear() {
    if (playlist_.empty()) return;
    // No confirmation dialog: an undo that actually works is better than a
    // prompt people learn to dismiss.
    recordUndo(T("Edit.Clear"));
    playlist_.clear();
    engine_.playlistChanged();
    existsCache_.clear();
    rebuildList();
    updateNowPlaying();
    setStatus(T("Status.Cleared"), StatusKind::Info);
}

void PlaylistDock::onRename() {
    if (!list_ || !model_) return;
    const QModelIndex index = list_->currentIndex();
    if (!index.isValid()) return;
    list_->edit(index);
}

void PlaylistDock::onItemRenamed(int index, const QString& title) {
    if (index < 0 || index >= playlist_.size() || title.isEmpty()) return;
    recordUndo(T("Edit.Rename"));
    if (playlist_.setTitle(index, title.toStdString()))
        setStatus(T("Status.Renamed").arg(title), StatusKind::Info);
    rebuildList();
    updateNowPlaying();
}

void PlaylistDock::onContextMenu(const QPoint& pos) {
    if (!list_ || !model_) return;
    QMenu menu(this);
    const QModelIndex at = list_->indexAt(pos);
    const int idx = at.isValid() ? at.row() : -1;

    auto add = [this, &menu](const QString& icon, const QString& text, auto&& handler) {
        QAction* action =
            icon.isEmpty() ? menu.addAction(text) : menu.addAction(tintedIcon(icon), text);
        connect(action, &QAction::triggered, this, handler);
        return action;
    };

    if (idx >= 0) {
        add(":/icons/play.svg", T("Menu.Play"), &PlaylistDock::onPlaySelected);
        add(":/icons/pencil.svg", T("Menu.Rename"), &PlaylistDock::onRename);
        add(QString(), T("Menu.ResetName"), [this, idx]() {
            if (idx < 0 || idx >= playlist_.size()) return;
            recordUndo(T("Edit.Rename"));
            playlist_.setTitle(idx, mediapath::fileStem(playlist_.items()[idx].path));
            rebuildList();
        });
        add(":/icons/minus.svg", T("Menu.Remove"), &PlaylistDock::onRemove);
        menu.addSeparator();
    }
    add(":/icons/plus.svg", T("Menu.AddFiles"), &PlaylistDock::onAddFiles);
    add(":/icons/download.svg", T("Menu.AddFolder"), &PlaylistDock::onAddFolder);
    add(QString(), T("Menu.ImportFromSource"), &PlaylistDock::onImportFromSource);
    add(":/icons/upload.svg", T("Menu.ExportCsv"), &PlaylistDock::onExportCsv);
    menu.addSeparator();
    add(":/icons/refresh.svg", T("Menu.Recheck"), &PlaylistDock::onRecheckFiles);
    add(QString(), T("Menu.FindMoved"), &PlaylistDock::onFindMoved);
    add(QString(), T("Menu.Undo"), &PlaylistDock::onUndo)->setEnabled(history_.canUndo());
    add(QString(), T("Menu.Redo"), &PlaylistDock::onRedo)->setEnabled(history_.canRedo());
    menu.exec(list_->viewport()->mapToGlobal(pos));
}

void PlaylistDock::onPlaySelected() {
    if (!list_) return;
    const QModelIndex index = list_->currentIndex();
    if (index.isValid()) playIndex(index.row());
}

void PlaylistDock::onTogglePlayPause() {
    controller_.togglePlayPause();
    // The source reports its new state a moment later; ask again shortly so the
    // glyph does not lie in the meantime.
    updateTransportIcons();
    QTimer::singleShot(kDurationRetryMs, this, [this]() { updateTransportIcons(); });
}

void PlaylistDock::onStop() {
    controller_.stop();
    updateTransportIcons();
}

void PlaylistDock::onToggleMute() {
    if (!controller_.isBound()) {
        setStatus(T("Status.NoSourceConfigured"), StatusKind::Error);
        updateMuteButton();
        return;
    }
    controller_.toggleMute();
    // The button is not updated here: OBS raises the source's "mute" signal and
    // onMuteChanged() draws the result, so a change made in the audio mixer and
    // one made here end up on screen by the same path.
}

void PlaylistDock::onMuteChanged(bool muted) {
    updateMuteButton();
    setStatus(muted ? T("Status.Muted") : T("Status.Unmuted"), StatusKind::Info);
    obs_data_t* d = obs_data_create();
    obs_data_set_bool(d, "muted", muted);
    pld::emitVendorEvent("mute-changed", d);
    obs_data_release(d);
    snapshotStatus();
}

// NF-8. The button for the moment something is on air that should not be.
// Stopping is not enough on its own: a stopped media source holds its last
// frame, so the deck also cuts to a scene the operator nominated.
void PlaylistDock::onPanic() {
    controller_.stop();
    engine_.playlistChanged(); // whatever was staged is not what anyone wants now
    updateTransportIcons();

    if (settings_.panicScene.isEmpty()) {
        setStatus(T("Status.PanicStopped"), StatusKind::Warning);
        return;
    }
    obs_source_t* scene = obs_get_source_by_name(settings_.panicScene.toUtf8().constData());
    if (!scene) {
        setStatus(T("Status.PanicSceneMissing").arg(settings_.panicScene), StatusKind::Error);
        return;
    }
    obs_frontend_set_current_scene(scene);
    obs_source_release(scene);
    setStatus(T("Status.Panic").arg(settings_.panicScene), StatusKind::Warning);
}

void PlaylistDock::onSeekReleased() {
    if (!seek_) return;
    const long long dur = controller_.currentDurationMs();
    if (dur <= 0) return;
    controller_.seekMs(dur * seek_->value() / 1000);
}

// ---- Playlist library ----------------------------------------------------

void PlaylistDock::loadLibrary() {
    LibraryData data = store_.loadLibrary();
    if (data.fromNewerVersion) {
        setStatus(T("Status.LibraryReset"), StatusKind::Warning);
    } else if (data.ok) {
        library_.setEntries(std::move(data.entries), data.active);
        if (data.migrated) setStatus(T("Status.LibraryMigrated"), StatusKind::Info);
    }
    updatePlaylistCombo();
    activateLibraryEntry(library_.activeIndex());
    // One copy of how the library looked when this session started, before any
    // edit of this run can spoil it.
    store_.backupLibrary(/*force=*/true);
}

void PlaylistDock::commitToLibrary() { library_.setActiveItems(playlist_.items()); }

void PlaylistDock::activateLibraryEntry(int index) {
    library_.setActive(index);
    const auto& entry = library_.active();
    QStringList toScan;
    for (const auto& it : entry.items) toScan << QString::fromStdString(it.path);
    playlist_.setItems(entry.items);
    engine_.playlistChanged();
    history_.clear(); // undo does not reach across playlists
    existsCache_.clear();
    lastScheduleFiredMs_ = -1;
    rebuildList();
    if (loadedLabel_) {
        const QString path = QString::fromStdString(entry.sourcePath);
        if (path.isEmpty()) {
            loadedLabel_->setText(T("NoPlaylist"));
            loadedLabel_->setToolTip("");
        } else {
            loadedLabel_->setText(T("Loaded").arg(QFileInfo(path).fileName()));
            loadedLabel_->setToolTip(path);
        }
    }
    updateNowPlaying();
    applyWatchFolder();
    startScan(toScan, /*replacesPlaylist=*/true);
}

void PlaylistDock::updatePlaylistCombo() {
    if (!playlistCombo_) return;
    switchingPlaylist_ = true;
    playlistCombo_->blockSignals(true);
    playlistCombo_->clear();
    for (const auto& entry : library_.entries()) {
        QString label = QString::fromStdString(entry.name);
        // The two properties that make a playlist act on its own are worth
        // seeing in the picker, not only in its dialog.
        if (!entry.watchFolder.empty()) label += QStringLiteral(" \u25CF");
        if (entry.scheduledStartMs >= 0) label += QStringLiteral(" \u23F1");
        playlistCombo_->addItem(label);
    }
    playlistCombo_->setCurrentIndex(library_.activeIndex());
    playlistCombo_->blockSignals(false);
    switchingPlaylist_ = false;
}

void PlaylistDock::onPlaylistSelected(int index) {
    if (switchingPlaylist_ || index < 0 || index == library_.activeIndex()) return;
    commitToLibrary();
    activateLibraryEntry(index);
    saveLibraryNow();
    setStatus(T("Status.PlaylistSwitched").arg(QString::fromStdString(library_.active().name)),
              StatusKind::Info);
}

void PlaylistDock::onPlaylistMenu() {
    QMenu menu(this);
    auto add = [this, &menu](const QString& text, auto&& handler) {
        QAction* action = menu.addAction(text);
        connect(action, &QAction::triggered, this, handler);
        return action;
    };

    add(T("Menu.PlaylistNew"), [this]() {
        commitToLibrary();
        const int index = library_.add(std::string());
        updatePlaylistCombo();
        activateLibraryEntry(index);
        saveLibraryNow();
    });
    add(T("Menu.PlaylistRename"), [this]() {
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, T("Dlg.PlaylistRename"), T("Dlg.PlaylistName"), QLineEdit::Normal,
            QString::fromStdString(library_.active().name), &ok);
        if (!ok || name.trimmed().isEmpty()) return;
        library_.rename(library_.activeIndex(), name.trimmed().toStdString());
        updatePlaylistCombo();
        saveLibraryNow();
    });
    add(T("Menu.PlaylistDuplicate"), [this]() {
        commitToLibrary();
        const int index = library_.duplicate(library_.activeIndex());
        if (index < 0) return;
        updatePlaylistCombo();
        activateLibraryEntry(index);
        saveLibraryNow();
    });
    add(T("Menu.PlaylistDelete"), [this]() {
        const QString name = QString::fromStdString(library_.active().name);
        library_.remove(library_.activeIndex());
        updatePlaylistCombo();
        activateLibraryEntry(library_.activeIndex());
        saveLibraryNow();
        setStatus(T("Status.PlaylistDeleted").arg(name), StatusKind::Info);
    });
    menu.addSeparator();
    add(T("Menu.PlaylistProperties"), &PlaylistDock::onPlaylistProperties);
    menu.exec(QCursor::pos());
}

// Watch folder and scheduled start belong to one playlist, not to the deck, so
// they are edited where the playlist is — and in a dialog, because a narrow
// dock has no room for two more rows nobody looks at during a show.
void PlaylistDock::onPlaylistProperties() {
    QDialog dlg(this);
    dlg.setWindowTitle(T("Dlg.PlaylistProps"));
    auto* form = new QFormLayout(&dlg);

    auto* watchEdit = new QLineEdit(QString::fromStdString(library_.active().watchFolder));
    watchEdit->setPlaceholderText(T("Props.WatchNone"));
    auto* browse = new QPushButton(T("Btn.Browse"));
    auto* watchRow = new QHBoxLayout();
    watchRow->addWidget(watchEdit, 1);
    watchRow->addWidget(browse);
    connect(browse, &QPushButton::clicked, &dlg, [this, watchEdit, &dlg]() {
        const QString dir =
            QFileDialog::getExistingDirectory(&dlg, T("Dlg.AddFolder"), watchEdit->text());
        if (!dir.isEmpty()) watchEdit->setText(dir);
    });
    form->addRow(new QLabel(T("Props.WatchFolder")), watchRow);
    auto* watchHint = new QLabel(T("Props.WatchHint"));
    watchHint->setWordWrap(true);
    form->addRow(watchHint);

    auto* scheduleChk = new QCheckBox(T("Props.Schedule"));
    auto* scheduleEdit = new QDateTimeEdit();
    scheduleEdit->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    scheduleEdit->setCalendarPopup(true);
    const long long start = library_.active().scheduledStartMs;
    scheduleChk->setChecked(start >= 0);
    scheduleEdit->setEnabled(start >= 0);
    scheduleEdit->setDateTime(start >= 0 ? QDateTime::fromMSecsSinceEpoch(start)
                                         : QDateTime::currentDateTime().addSecs(300));
    connect(scheduleChk, &QCheckBox::toggled, scheduleEdit, &QWidget::setEnabled);
    form->addRow(scheduleChk, scheduleEdit);
    auto* scheduleHint = new QLabel(T("Props.ScheduleHint"));
    scheduleHint->setWordWrap(true);
    form->addRow(scheduleHint);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;

    auto& entry = library_.mutableActive();
    entry.watchFolder = watchEdit->text().trimmed().toStdString();
    entry.scheduledStartMs =
        scheduleChk->isChecked() ? scheduleEdit->dateTime().toMSecsSinceEpoch() : -1;
    lastScheduleFiredMs_ = -1; // a new time is a new appointment
    applyWatchFolder();
    updatePlaylistCombo();
    updateNowPlaying();
    saveLibraryNow();
    setStatus(T("Settings.Saved"), StatusKind::Success);
}

void PlaylistDock::applyWatchFolder() {
    if (!watcher_) return;
    const QStringList watched = watcher_->directories();
    if (!watched.isEmpty()) watcher_->removePaths(watched);
    const QString folder = QString::fromStdString(library_.active().watchFolder);
    if (folder.isEmpty()) return;
    if (!QFileInfo(folder).isDir()) {
        // A folder on a disk that is not mounted is worth saying out loud; it is
        // not worth refusing to work over.
        setStatus(T("Status.WatchMissing").arg(folder), StatusKind::Warning);
        return;
    }
    watcher_->addPath(folder);
    // Pick up whatever landed there while the deck was not watching.
    scanWatchFolder();
}

void PlaylistDock::onWatchedFolderChanged() {
    if (watchTimer_) watchTimer_->start();
}

void PlaylistDock::scanWatchFolder() {
    const QString folder = QString::fromStdString(library_.active().watchFolder);
    if (folder.isEmpty()) return;
    const QStringList found = mediaFilesIn(folder, /*recursive=*/false);
    if (found.isEmpty()) return;

    std::set<std::string> known;
    for (const auto& it : playlist_.items()) known.insert(it.path);
    QStringList fresh;
    for (const QString& path : found)
        if (!known.count(path.toStdString())) fresh << path;
    if (fresh.isEmpty()) return;

    addPaths(fresh);
    setStatus(T("Status.WatchAdded").arg(fresh.size()), StatusKind::Success);
}

// NF-6. The deck starting on its own is the one thing that must never be a
// surprise, which is why the card counts down to it out loud first.
void PlaylistDock::checkSchedule() {
    const long long start = library_.active().scheduledStartMs;
    if (start < 0) return;
    const long long now = QDateTime::currentMSecsSinceEpoch();
    if (!pld::shouldFireSchedule(start, now, lastScheduleFiredMs_, kScheduleWarnSeconds)) return;
    lastScheduleFiredMs_ = start;
    if (playlist_.empty()) {
        setStatus(T("Status.ScheduleEmpty"), StatusKind::Warning);
        return;
    }
    setStatus(T("Status.ScheduleFired"), StatusKind::Success);
    playIndex(playlist_.currentIndex() >= 0 ? playlist_.currentIndex() : 0);
}

void PlaylistDock::saveLibrarySoon() {
    if (!libraryTimer_ || obsShutdown_) return;
    libraryTimer_->start();
}

void PlaylistDock::saveLibraryNow() {
    if (libraryTimer_) libraryTimer_->stop();
    commitToLibrary();
    store_.saveLibrary(library_.entries(), library_.activeIndex());
    store_.backupLibrary();
}

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
void PlaylistDock::wsSetMode(int mode) {
    if (!pld::isValidEndMode(mode)) return;
    engine_.setMode(pld::endModeFromInt(mode));
    if (endCombo_) {
        const int idx = endCombo_->findData(mode);
        // The combo's own signal would set the mode again, harmlessly, but the
        // engine is the one that decides — the widget only reflects it.
        if (idx >= 0) endCombo_->setCurrentIndex(idx);
    }
    saveSettings();
    updateNowPlaying();
    snapshotStatus();
}
void PlaylistDock::wsSeek(int ms) {
    if (controller_.seekMs(ms)) onTick();
}
void PlaylistDock::wsClear() { onClear(); }
void PlaylistDock::wsAddPaths(const QStringList& paths) { addPaths(paths); }
void PlaylistDock::wsSetMute(bool muted) { controller_.setMuted(muted); }
void PlaylistDock::wsToggleMute() { onToggleMute(); }
void PlaylistDock::wsSave(const QString& path) {
    if (path.isEmpty()) return;
    if (writePlaylistTo(path)) {
        setLoadedPlaylist(path);
        setStatus(T("Status.Saved").arg(QFileInfo(path).fileName()), StatusKind::Success);
    } else {
        setStatus(T("Status.CannotWrite"), StatusKind::Error);
    }
}
void PlaylistDock::wsMove(int from, int to) {
    recordUndo(T("Edit.Move"));
    if (!playlist_.move(from, to)) return;
    engine_.playlistChanged();
    rebuildList();
    updateNowPlaying();
}
void PlaylistDock::wsRemove(int index) { removeRows({index}); }
void PlaylistDock::wsSwitchPlaylist(const QString& name) {
    const int index = library_.indexOfName(name.toStdString());
    if (index < 0 || index == library_.activeIndex()) return;
    commitToLibrary();
    activateLibraryEntry(index);
    updatePlaylistCombo();
    saveLibraryNow();
}
void PlaylistDock::wsPanic() { onPanic(); }

DeckStatus PlaylistDock::status() const {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    return snapshot_;
}

void PlaylistDock::snapshotStatus() {
    DeckStatus s;
    s.count = playlist_.size();
    s.currentIndex = playlist_.currentIndex();
    if (const auto* it = playlist_.current()) {
        s.currentTitle = QString::fromStdString(it->title);
        s.currentPath = QString::fromStdString(it->path);
    }
    s.positionMs = controller_.currentTimeMs();
    s.durationMs = controller_.currentDurationMs();
    s.playing = controller_.isPlaying();
    s.paused = controller_.isPaused();
    s.sourceBound = controller_.isBound();
    s.muted = s.sourceBound && controller_.isMuted();
    s.sourceName = QString::fromStdString(controller_.boundName());
    s.mode = static_cast<int>(engine_.mode());
    for (const auto& m : pld::kEndModes)
        if (m.mode == engine_.mode())
            s.modeName = QString::fromUtf8(m.key).mid(6); // drop "OnEnd."
    s.playlistName = QString::fromStdString(library_.active().name);
    s.playlistIndex = library_.activeIndex();
    for (const auto& entry : library_.entries())
        s.playlists << QString::fromStdString(entry.name);
    s.scheduledStartMs = library_.active().scheduledStartMs;
    s.upNextIndex = engine_.upNextIndex();
    if (s.upNextIndex >= 0 && s.upNextIndex < s.count)
        s.upNextTitle = QString::fromStdString(playlist_.items()[s.upNextIndex].title);
    else
        s.upNextIndex = -1;
    s.items.reserve(playlist_.size());
    for (const auto& it : playlist_.items())
        s.items.append({QString::fromStdString(it.title), QString::fromStdString(it.path)});

    std::lock_guard<std::mutex> lock(snapshotMutex_);
    snapshot_ = std::move(s);
}

void PlaylistDock::snapshotPlayback() {
    // The tick runs twice a second: it refreshes what actually changes, not the
    // whole item list.
    const long long position = controller_.currentTimeMs();
    const long long duration = controller_.currentDurationMs();
    const bool playing = controller_.isPlaying();
    const bool paused = controller_.isPaused();
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    snapshot_.positionMs = position;
    snapshot_.durationMs = duration;
    snapshot_.playing = playing;
    snapshot_.paused = paused;
}

void PlaylistDock::emitVendorItemStarted() {
    const auto* it = playlist_.current();
    if (!it) return;
    obs_data_t* d = obs_data_create();
    obs_data_set_int(d, "index", playlist_.currentIndex());
    obs_data_set_string(d, "title", it->title.c_str());
    obs_data_set_string(d, "path", it->path.c_str());
    obs_data_set_int(d, "durationMs", it->durationMs);
    pld::emitVendorEvent("item-started", d);
    obs_data_release(d);
}

void PlaylistDock::onNext() { applyPlayback(engine_.next()); }

void PlaylistDock::onPrev() { applyPlayback(engine_.prev()); }

void PlaylistDock::onTick() {
    if (!seek_) return;
    checkSchedule();
    // Only show progress for a clip this session has actually selected. Without
    // this, on startup the bound media source still holds the file from the
    // previous session and the counter would show that stale clip even though
    // no playlist item is loaded.
    if (playlist_.currentIndex() < 0) {
        if (!seek_->isSliderDown()) seek_->setValue(0);
        timeLabel_->clear();
    } else {
        const long long dur = controller_.currentDurationMs();
        const long long cur = controller_.currentTimeMs();
        if (dur > 0 && cur >= 0) {
            if (!seek_->isSliderDown()) seek_->setValue(static_cast<int>(1000.0 * cur / dur));
            long long remaining = dur - cur;
            if (remaining < 0) remaining = 0;
            const QString text = QStringLiteral("%1 / %2   -%3")
                                     .arg(QString::fromStdString(pld::formatDuration(cur)),
                                          QString::fromStdString(pld::formatDuration(dur)),
                                          QString::fromStdString(pld::formatDuration(remaining)));
            // setText on an unchanged string still invalidates and repaints.
            if (timeLabel_->text() != text) timeLabel_->setText(text);
        } else {
            if (!seek_->isSliderDown()) seek_->setValue(0);
            timeLabel_->clear();
        }
    }
    // The countdown to a scheduled start lives in the same line as "up next".
    if (library_.active().scheduledStartMs >= 0) updateNowPlaying();
    updateTransportIcons();
    snapshotPlayback();

    // Remote clients follow playback through this event instead of polling.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastStateEventMs_ >= kStateEventMs) {
        lastStateEventMs_ = now;
        obs_data_t* d = obs_data_create();
        obs_data_set_bool(d, "playing", controller_.isPlaying());
        obs_data_set_int(d, "positionMs", controller_.currentTimeMs());
        obs_data_set_int(d, "durationMs", controller_.currentDurationMs());
        obs_data_set_int(d, "index", playlist_.currentIndex());
        pld::emitVendorEvent("playback-state", d);
        obs_data_release(d);
    }
}

void PlaylistDock::onSourceChanged(int) {
    // The name lives in the item data, not the text: index 0 is the "no source
    // configured" placeholder, whose data is deliberately empty.
    const QString name = sourceCombo_->currentData().toString();
    if (name.isEmpty()) {
        controller_.unbind();
        setStatus(T("Status.NoSourceConfigured"), StatusKind::Warning);
        updateMuteButton();
        snapshotStatus();
        return;
    }
    controller_.bind(name.toStdString());
    setStatus(T("Status.BoundTo").arg(name), StatusKind::Info);
    updateMuteButton();
    // Only a deliberate pick updates the remembered source. A programmatic
    // refresh must never overwrite it, or a scene collection that happens to
    // lack the chosen source would erase the user's configuration.
    if (!refreshing_) {
        settings_.source = name;
        saveSettings();
    }
    snapshotStatus();
}

void PlaylistDock::refreshSources() {
    // Track what the user asked for, not what the combo currently shows.
    QString wanted = sourceCombo_->currentData().toString();
    if (wanted.isEmpty()) wanted = settings_.source;

    refreshing_ = true;
    sourceCombo_->blockSignals(true);
    sourceCombo_->clear();
    // Index 0 is always the explicit "nothing configured" entry. Without it,
    // QComboBox auto-selects the first real source as soon as one is added, so
    // switching to a scene collection that lacks the configured source would
    // silently bind an arbitrary media source and then write over its file.
    sourceCombo_->addItem(T("NoSourceConfigured"), QString());
    for (const auto& n : MediaSourceController::listMediaSources()) {
        const QString name = QString::fromStdString(n);
        sourceCombo_->addItem(name, name);
    }
    const int idx = wanted.isEmpty() ? 0 : sourceCombo_->findData(wanted);
    sourceCombo_->setCurrentIndex(idx >= 0 ? idx : 0);
    sourceCombo_->blockSignals(false);
    onSourceChanged(sourceCombo_->currentIndex());
    refreshing_ = false;
}

// ---- Saved-playlist file handling ----------------------------------------

bool PlaylistDock::loadPlaylistFile(const QString& path) {
    const QFileInfo info(path);
    // A playlist is text with one line per clip; anything this big is not one,
    // and reading it whole would freeze OBS. The remote Load request can name
    // any path on disk, so the ceiling belongs here rather than at the caller.
    if (info.size() > kMaxPlaylistBytes) {
        setStatus(T("Status.TooLarge").arg(kMaxPlaylistBytes / (1024 * 1024)), StatusKind::Error);
        return false;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setStatus(T("Status.CannotOpen"), StatusKind::Error);
        return false;
    }
    const std::string text = f.readAll().toStdString();
    f.close();
    // Relative paths in a playlist are relative to the playlist's own folder.
    const std::string baseDir = info.absolutePath().toStdString();

    std::vector<PlaylistItem> items;
    size_t skipped = 0;
    if (path.endsWith(".json", Qt::CaseInsensitive)) {
        std::string n;
        const auto res = io::fromJson(text, n, items, baseDir);
        if (!res.ok) {
            setStatus(T("Status.InvalidPlaylist"), StatusKind::Error);
            return false;
        }
        skipped = res.skipped;
    } else {
        items = io::parseM3u(text, baseDir);
    }

    recordUndo(T("Edit.Open"));
    QStringList toScan;
    for (const auto& it : items) toScan << QString::fromStdString(it.path);
    playlist_.setItems(std::move(items));
    engine_.playlistChanged();
    existsCache_.clear();
    rebuildList();
    updateNowPlaying();
    startScan(toScan, /*replacesPlaylist=*/true);
    if (skipped > 0)
        setStatus(T("Status.SkippedItems").arg(static_cast<int>(skipped)), StatusKind::Warning);
    return true;
}

void PlaylistDock::setLoadedPlaylist(const QString& path) {
    library_.mutableActive().sourcePath = path.toStdString();
    if (!loadedLabel_) return;
    if (path.isEmpty()) {
        loadedLabel_->setText(T("NoPlaylist"));
        loadedLabel_->setToolTip("");
        return;
    }
    loadedLabel_->setText(T("Loaded").arg(QFileInfo(path).fileName()));
    loadedLabel_->setToolTip(path);
    saveLibrarySoon();
}

void PlaylistDock::onSavePlaylist() {
    QString selectedFilter;
    QString path = QFileDialog::getSaveFileName(this, T("Dlg.SavePlaylist"), settings_.lastDir,
                                                "JSON (*.json);;M3U (*.m3u)", &selectedFilter);
    if (path.isEmpty()) return;
    // Honor the chosen filter for the format (the dialog doesn't always append
    // the extension), so picking "M3U" actually writes .m3u — not .json.
    const bool m3u = path.endsWith(".m3u", Qt::CaseInsensitive) ||
                     (!path.endsWith(".json", Qt::CaseInsensitive) &&
                      selectedFilter.contains("m3u", Qt::CaseInsensitive));
    if (!path.endsWith(".json", Qt::CaseInsensitive) && !path.endsWith(".m3u", Qt::CaseInsensitive))
        path += m3u ? ".m3u" : ".json";

    settings_.lastDir = QFileInfo(path).absolutePath();
    if (!writePlaylistTo(path)) {
        setStatus(T("Status.CannotWrite"), StatusKind::Error);
        return;
    }
    setLoadedPlaylist(path);
    saveSettings();
    setStatus(T("Status.Saved").arg(QFileInfo(path).fileName()), StatusKind::Success);
}

bool PlaylistDock::writePlaylistTo(const QString& path) {
    const QFileInfo info(path);
    const std::string baseDir = info.absolutePath().toStdString();
    const bool m3u =
        path.endsWith(".m3u", Qt::CaseInsensitive) || path.endsWith(".m3u8", Qt::CaseInsensitive);
    const std::string text =
        m3u ? io::toM3u(playlist_.items(), baseDir, settings_.relativePaths)
            : io::toJson(info.completeBaseName().toStdString(), playlist_.items(), baseDir,
                         settings_.relativePaths);
    // The user's own playlist file is the most valuable thing this plugin
    // writes; it gets the atomic path too.
    return SettingsStore::writeAtomically(path, QByteArray::fromStdString(text));
}

void PlaylistDock::onOpenPlaylist() {
    const QString path = QFileDialog::getOpenFileName(
        this, T("Dlg.OpenPlaylist"), settings_.lastDir,
        T("Dlg.PlaylistFilter") + QStringLiteral(" (*.json *.m3u *.m3u8)"));
    if (path.isEmpty()) return;
    settings_.lastDir = QFileInfo(path).absolutePath();
    saveSettings();
    if (loadPlaylistFile(path)) {
        setLoadedPlaylist(path);
        setStatus(T("Status.Opened").arg(QFileInfo(path).fileName()), StatusKind::Success);
    }
}

void PlaylistDock::onExportCsv() {
    if (playlist_.empty()) return;
    QString path =
        QFileDialog::getSaveFileName(this, T("Dlg.ExportCsv"), settings_.lastDir, "CSV (*.csv)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".csv", Qt::CaseInsensitive)) path += ".csv";
    if (!SettingsStore::writeAtomically(path,
                                        QByteArray::fromStdString(io::toCsv(playlist_.items())))) {
        setStatus(T("Status.CannotWrite"), StatusKind::Error);
        return;
    }
    settings_.lastDir = QFileInfo(path).absolutePath();
    saveSettings();
    setStatus(T("Status.Exported").arg(QFileInfo(path).fileName()), StatusKind::Success);
}

// NF-13. Other sources hold playlists too — a VLC source, OBS's own playlist
// source, whatever a plugin adds. Reading one is generic: find the array of
// paths in its settings. Driving one is not, so the deck imports rather than
// writing settings it has never seen — which is exactly the mistake that left
// VLC support broken for three releases.
void PlaylistDock::onImportFromSource() {
    const auto sources = MediaSourceController::listPlaylistCapableSources();
    if (sources.empty()) {
        setStatus(T("Status.NoImportSource"), StatusKind::Warning);
        return;
    }
    QStringList names;
    for (const auto& n : sources) names << QString::fromStdString(n);
    bool ok = false;
    const QString chosen = QInputDialog::getItem(this, T("Dlg.ImportFromSource"),
                                                 T("Dlg.ImportSourceLabel"), names, 0, false, &ok);
    if (!ok || chosen.isEmpty()) return;

    const auto paths = MediaSourceController::readSourcePlaylist(chosen.toStdString());
    QStringList media;
    for (const auto& p : paths) media << QString::fromStdString(p);
    if (media.isEmpty()) {
        setStatus(T("Status.ImportEmpty").arg(chosen), StatusKind::Warning);
        return;
    }
    addPaths(media);
}

void PlaylistDock::setStatus(const QString& msg, StatusKind kind) {
    if (!status_) return;
    QColor color;
    switch (kind) {
    case StatusKind::Error:
        color = pld::style::error(palette());
        break;
    case StatusKind::Warning:
        color = pld::style::warning(palette());
        break;
    case StatusKind::Success:
        color = pld::style::success(palette());
        break;
    case StatusKind::Info:
        color = pld::style::textSecondary(palette());
        break;
    }
    status_->setStyleSheet(QStringLiteral("color:%1;").arg(color.name()));
    status_->setText(msg);
    // Transient feedback clears itself; things that went wrong stay up until
    // something else happens, because they are what the operator needs to read.
    if (statusTimer_) {
        statusTimer_->stop();
        if (kind == StatusKind::Info || kind == StatusKind::Success)
            statusTimer_->start(kStatusFadeMs);
    }
}

// ---- Settings persistence ------------------------------------------------

void PlaylistDock::saveSettings() const {
    DeckSettings out = settings_;
    // The engine owns the mode; the struct only carries it to disk.
    out.mode = engine_.mode();
    store_.saveSettings(out);
}

void PlaylistDock::loadSettings() {
    settings_ = store_.loadSettings();
    engine_.setMode(settings_.mode);
}

void PlaylistDock::applyLocale() {
    if (settings_.language != "auto" && !settings_.language.isEmpty())
        obs_module_set_locale(settings_.language.toUtf8().constData());
}

void PlaylistDock::applyLocaleAndRebuild() {
    applyLocale();
    const QString loaded = QString::fromStdString(library_.active().sourcePath);
    buildUi(); // recreates all widgets with the new language
    // OBS owns the QDockWidget this widget lives in; retitle it through the parent.
    if (auto* dock = qobject_cast<QDockWidget*>(parentWidget()))
        dock->setWindowTitle(dockTitle());
    refreshSources();
    rebuildList();
    setLoadedPlaylist(loaded);
}

void PlaylistDock::onOpenSettings() {
    QDialog dlg(this);
    dlg.setWindowTitle(T("Settings.Title"));
    auto* form = new QFormLayout(&dlg);

    auto* probeChk = new QCheckBox(T("Settings.Probe"));
    probeChk->setChecked(settings_.enableProbe);
    form->addRow(probeChk);

    auto* relativeChk = new QCheckBox(T("Settings.RelativePaths"));
    relativeChk->setChecked(settings_.relativePaths);
    relativeChk->setToolTip(T("Settings.RelativePathsTip"));
    form->addRow(relativeChk);

    auto* unmuteChk = new QCheckBox(T("Settings.UnmuteOnStart"));
    unmuteChk->setChecked(settings_.unmuteOnStart);
    unmuteChk->setToolTip(T("Settings.UnmuteOnStartTip"));
    form->addRow(unmuteChk);

    auto* panicEdit = new QLineEdit(settings_.panicScene);
    panicEdit->setPlaceholderText(T("Settings.PanicSceneNone"));
    panicEdit->setToolTip(T("Settings.PanicSceneTip"));
    form->addRow(new QLabel(T("Settings.PanicScene")), panicEdit);

    auto* langCombo = new QComboBox();
    langCombo->addItem(T("Lang.Auto"), "auto");
    // Endonyms, not translations: they have to name the language to someone who
    // cannot read the current UI. fromUtf8() states the source encoding rather
    // than trusting the compiler's default narrow-literal charset (see the
    // /utf-8 flag in CMakeLists.txt).
    langCombo->addItem(QStringLiteral("English"), "en-US");
    langCombo->addItem(QStringLiteral("Italiano"), "it-IT");
    langCombo->addItem(QString::fromUtf8("Español"), "es-ES");
    langCombo->addItem(QString::fromUtf8("Français"), "fr-FR");
    langCombo->addItem(QStringLiteral("Deutsch"), "de-DE");
    langCombo->addItem(QString::fromUtf8("Português (BR)"), "pt-BR");
    langCombo->addItem(QString::fromUtf8("Русский"), "ru-RU");
    langCombo->addItem(QString::fromUtf8("简体中文"), "zh-CN");
    langCombo->addItem(QString::fromUtf8("日本語"), "ja-JP");
    langCombo->addItem(QString::fromUtf8("한국어"), "ko-KR");
    const int li = langCombo->findData(settings_.language);
    langCombo->setCurrentIndex(li >= 0 ? li : 0);
    form->addRow(new QLabel(T("Settings.Language")), langCombo);

    auto* restoreBtn = new QPushButton(T("Btn.RestoreBackup"));
    connect(restoreBtn, &QPushButton::clicked, &dlg, [this, &dlg]() {
        const QStringList list = store_.backups();
        if (list.isEmpty()) {
            setStatus(T("Status.NoBackups"), StatusKind::Info);
            return;
        }
        QStringList labels;
        for (const QString& path : list)
            labels << QFileInfo(path).completeBaseName().mid(
                static_cast<int>(QStringLiteral("library-").size()));
        bool ok = false;
        const QString chosen = QInputDialog::getItem(
            &dlg, T("Dlg.RestoreBackup"), T("Dlg.RestoreBackupLabel"), labels, 0, false, &ok);
        if (!ok) return;
        const int index = labels.indexOf(chosen);
        if (index < 0) return;
        LibraryData data = store_.readBackup(list.at(index));
        if (!data.ok) {
            setStatus(T("Status.BackupUnreadable"), StatusKind::Error);
            return;
        }
        // The library as it stands is backed up first: restoring an old copy
        // must not be the thing that loses today's work.
        saveLibraryNow();
        store_.backupLibrary(/*force=*/true);
        library_.setEntries(std::move(data.entries), data.active);
        updatePlaylistCombo();
        activateLibraryEntry(library_.activeIndex());
        saveLibraryNow();
        setStatus(T("Status.BackupRestored"), StatusKind::Success);
    });
    form->addRow(restoreBtn);

    // The automatic check runs once at OBS startup; this is the way to ask again
    // without restarting, and it reports the outcome in the status line.
    auto* updateBtn = new QPushButton(T("Btn.CheckUpdates"));
    connect(updateBtn, &QPushButton::clicked, this, [this]() {
        if (!updateChecker_) return;
        if (updateChecker_->check(true)) setStatus(T("Status.UpdateChecking"), StatusKind::Info);
    });
    form->addRow(updateBtn);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;
    settings_.enableProbe = probeChk->isChecked();
    settings_.relativePaths = relativeChk->isChecked();
    settings_.unmuteOnStart = unmuteChk->isChecked();
    settings_.panicScene = panicEdit->text().trimmed();
    const QString newLang = langCombo->currentData().toString();
    const bool langChanged = (newLang != settings_.language);
    settings_.language = newLang;
    saveSettings();
    if (langChanged) applyLocaleAndRebuild();
    setStatus(T("Settings.Saved"), StatusKind::Success);
}

void PlaylistDock::onUpdateResult(const QString& body, const QString& error, bool manual) {
    if (!error.isEmpty()) {
        blog(LOG_WARNING, "[obs-playlist-deck] update check failed: %s", qPrintable(error));
        if (manual) setStatus(T("Status.UpdateCheckFailed").arg(error), StatusKind::Error);
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(body.toUtf8());
    if (!doc.isObject()) {
        blog(LOG_WARNING, "[obs-playlist-deck] update check: unexpected response");
        if (manual) setStatus(T("Status.UpdateCheckFailed").arg("bad response"), StatusKind::Error);
        return;
    }
    const QString tag = doc.object().value("tag_name").toString();
    if (tag.isEmpty()) {
        if (manual) setStatus(T("Status.UpdateCheckFailed").arg("no tag_name"), StatusKind::Error);
        return;
    }
    if (pld::isNewerVersion(tag.toStdString(), PLD_VERSION)) {
        // Non-ASCII inside a QStringLiteral must be written as a universal
        // character name. QStringLiteral expands to a UTF-16 u"" literal, where
        // "\xE2\x86\x97" is three code units (U+00E2 U+0086 U+0097 - an accented
        // "a" plus two invisible controls), not the three UTF-8 bytes of one
        // arrow. "↗" is transcoded by the compiler and is correct on every
        // platform and toolchain.
        versionLabel_->setText(QStringLiteral("v%1 \u2014 <a href=\"%2\">%3 \u2197</a>")
                                   .arg(PLD_VERSION)
                                   .arg(QString::fromUtf8(kReleasesUrl))
                                   .arg(T("Link.UpdateTo").arg(tag.toHtmlEscaped())));
        setStatus(T("Status.UpdateAvailable").arg(tag), StatusKind::Info);
    } else if (manual) {
        setStatus(T("Status.UpToDate").arg(PLD_VERSION), StatusKind::Success);
    }
}

// ---- Hotkeys -------------------------------------------------------------

namespace {
void hotkeyCallback(void* data, obs_hotkey_id, obs_hotkey_t*, bool pressed) {
    if (!pressed) return;
    auto* t = static_cast<PlaylistDock::HotkeyTarget*>(data);
    if (!t || !t->dock) return;
    PlaylistDock* dock = t->dock;
    if (t->index >= 0) {
        const int index = t->index;
        QMetaObject::invokeMethod(
            dock, [dock, index]() { dock->wsPlayIndex(index); }, Qt::QueuedConnection);
        return;
    }
    auto method = t->method;
    if (!method) return;
    QMetaObject::invokeMethod(
        dock, [dock, method]() { (dock->*method)(); }, Qt::QueuedConnection);
}
} // namespace

void PlaylistDock::registerHotkeys() {
    auto reg = [this](const char* id, const char* desc, void (PlaylistDock::*fn)(), int index) {
        // The target used to be a bare `new` that nothing freed. It is owned
        // here and released only after every hotkey is unregistered.
        auto target = std::make_unique<HotkeyTarget>();
        target->dock = this;
        target->method = fn;
        target->index = index;
        const obs_hotkey_id id_ =
            obs_hotkey_register_frontend(id, desc, hotkeyCallback, target.get());
        hotkeyTargets_.push_back(std::move(target));
        hotkeys_.push_back(id_);
    };
    reg("obs-playlist-deck.next", "Playlist Deck: Next", &PlaylistDock::onNext, -1);
    reg("obs-playlist-deck.prev", "Playlist Deck: Previous", &PlaylistDock::onPrev, -1);
    reg("obs-playlist-deck.playpause", "Playlist Deck: Play/Pause",
        &PlaylistDock::onTogglePlayPause, -1);
    reg("obs-playlist-deck.stop", "Playlist Deck: Stop", &PlaylistDock::onStop, -1);
    reg("obs-playlist-deck.panic", "Playlist Deck: Panic (stop and cut away)",
        &PlaylistDock::onPanic, -1);
    reg("obs-playlist-deck.recheck", "Playlist Deck: Recheck missing files",
        &PlaylistDock::onRecheckFiles, -1);
    reg("obs-playlist-deck.mute", "Playlist Deck: Mute/unmute the bound source",
        &PlaylistDock::onToggleMute, -1);
    // Direct item triggers, for MIDI controllers and foot pedals mapped through
    // OBS's own hotkey system.
    for (int i = 1; i <= 9; ++i) {
        const std::string id = "obs-playlist-deck.play" + std::to_string(i);
        const std::string desc = "Playlist Deck: Play item " + std::to_string(i);
        reg(id.c_str(), desc.c_str(), nullptr, i - 1);
    }
}

void PlaylistDock::unregisterHotkeys() {
    for (auto id : hotkeys_)
        if (id != OBS_INVALID_HOTKEY_ID) obs_hotkey_unregister(id);
    hotkeys_.clear();
    // Only now can the targets go: no callback can still reach them.
    hotkeyTargets_.clear();
}

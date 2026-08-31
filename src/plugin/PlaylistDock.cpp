// SPDX-License-Identifier: MIT
#include "PlaylistDock.hpp"
#include "DeckStyle.hpp"
#include "Format.hpp"
#include "MediaPath.hpp"
#include "PlaylistIO.hpp"
#include "PlaylistListWidget.hpp"
#include "UpdateChecker.hpp"
#include "VendorBridge.hpp"
#include "Version.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QSaveFile>
#include <QShortcut>
#include <QSize>
#include <QSlider>
#include <QAction>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QSvgRenderer>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
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
constexpr int kTickMs = 500;              // playback position refresh
constexpr int kStatusFadeMs = 6000;       // how long a transient status stays up
constexpr int kSessionDebounceMs = 800;   // coalesces bursts of session writes
constexpr int kDurationRetryMs = 400;     // re-read a duration the decoder had not filled in
constexpr int kDurationRetries = 4;
constexpr qint64 kStateEventMs = 1000;    // vendor playback-state event rate
constexpr qint64 kMaxPlaylistBytes = 10ll * 1024 * 1024; // refuse absurd playlist files

// Item data roles the delegate paints from.
constexpr int kRoleModelIndex = Qt::UserRole;     // position in the playlist model
constexpr int kRoleDuration = Qt::UserRole + 1;   // formatted duration, may be empty
constexpr int kRoleMissing = Qt::UserRole + 2;    // file is known to be gone
constexpr int kRoleCurrent = Qt::UserRole + 3;    // this is the item being played

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
        const bool missing = index.data(kRoleMissing).toBool();
        const bool current = index.data(kRoleCurrent).toBool();
        const QString duration = index.data(kRoleDuration).toString();
        const int number = index.data(kRoleModelIndex).toInt() + 1;

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
        const int numW = std::max(fm.horizontalAdvance(QStringLiteral("99")),
                                  fm.horizontalAdvance(num)) + 6;
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

// Atomic file write. A truncate-then-write leaves a half-written settings or
// session file behind if anything interrupts it, and a truncated JSON file is
// not "slightly wrong" — it fails to parse, and the whole saved playlist is
// gone. QSaveFile writes a temporary alongside and renames it into place, which
// is atomic on NTFS, ext4 and APFS alike.
bool writeAtomically(const QString& path, const QByteArray& bytes) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    if (f.write(bytes) != bytes.size()) {
        f.cancelWriting();
        return false;
    }
    return f.commit();
}
} // namespace

QString PlaylistDock::dockTitle() {
    return QString::fromUtf8(obs_module_text("PlaylistDeck"));
}

void PlaylistDock::applyDockTitle() {
    // OBS owns the QDockWidget this widget lives in; retitle it through the parent.
    if (auto* dock = qobject_cast<QDockWidget*>(parentWidget()))
        dock->setWindowTitle(dockTitle());
}

// The constructor runs during obs_module_load(), before any scene collection
// exists. Keep it to settings and UI only; source-, session- and hotkey-related
// setup happens in frontendLoaded().
PlaylistDock::PlaylistDock(QWidget* parent) : QWidget(parent) {
    setObjectName("obs-playlist-deck-widget");
    loadSettings();
    applyLocale();

    scanner_ = new MediaScanner(this);
    connect(scanner_, &MediaScanner::resultsReady, this, &PlaylistDock::onScanResults);
    updateChecker_ = new UpdateChecker(this);
    connect(updateChecker_, &UpdateChecker::resultReady, this, &PlaylistDock::onUpdateResult);

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

    uiTimer_ = new QTimer(this);
    uiTimer_->setInterval(kTickMs);
    connect(uiTimer_, &QTimer::timeout, this, &PlaylistDock::onTick);
    uiTimer_->start();

    statusTimer_ = new QTimer(this);
    statusTimer_->setSingleShot(true);
    connect(statusTimer_, &QTimer::timeout, this, [this]() {
        if (status_) status_->clear();
    });

    // The session used to be written to disk on every rebuild — that is once per
    // added file, per probed duration, per reorder. Debounced, a burst costs one
    // write; shutdown() flushes whatever is still pending.
    sessionTimer_ = new QTimer(this);
    sessionTimer_->setSingleShot(true);
    sessionTimer_->setInterval(kSessionDebounceMs);
    connect(sessionTimer_, &QTimer::timeout, this, [this]() { saveSession(); });
}

void PlaylistDock::frontendLoaded() {
    refreshSources();
    if (autoRestore_) loadSession();
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
    // Flush a debounced session write rather than losing the last edits.
    if (sessionTimer_ && sessionTimer_->isActive()) {
        sessionTimer_->stop();
        saveSession();
    }
    unregisterHotkeys();
    controller_.setOnMediaEnded(nullptr);
    controller_.setOnMediaStarted(nullptr);
    controller_.setOnDeactivated(nullptr);
    controller_.unbind();
    // Stop the worker threads while there is still an event loop to stop them
    // with, and wait: a thread still running into OBS's teardown is the crash
    // this replaced.
    if (scanner_) scanner_->shutdown();
    if (updateChecker_) updateChecker_->shutdown();
}

void PlaylistDock::releaseSource() {
    pendingStageNext_ = false;
    pendingStageRow_ = -1;
    // Keep the user's choice across the collection swap so it is restored when
    // they switch back to the collection that owns this source.
    if (controller_.isBound()) pendingSource_ = QString::fromStdString(controller_.boundName());
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
    auto* playSelBtn = mk(":/icons/play.svg", T("Btn.Play"), T("Tip.Play"));
    for (auto* b : {prevBtn, playPauseBtn_, stopBtn, nextBtn}) trRow->addWidget(b);
    trRow->addStretch(1);
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
    list_ = new PlaylistListWidget();
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
    const int modeIdx = endCombo_->findData(static_cast<int>(mode_));
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
    connect(saveBtn, &QPushButton::clicked, this, &PlaylistDock::onSavePlaylist);
    connect(openBtn, &QPushButton::clicked, this, &PlaylistDock::onOpenPlaylist);

    connect(list_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { onPlaySelected(); });
    connect(list_, &PlaylistListWidget::filesDropped, this, &PlaylistDock::onFilesDropped);
    connect(list_, &PlaylistListWidget::reordered, this, &PlaylistDock::onListReordered);
    connect(list_, &PlaylistListWidget::playRequested, this, &PlaylistDock::onPlaySelected);
    connect(list_, &PlaylistListWidget::removeRequested, this, &PlaylistDock::onRemove);
    connect(list_, &PlaylistListWidget::renameRequested, this, &PlaylistDock::onRename);
    connect(list_, &QListWidget::itemChanged, this, &PlaylistDock::onItemRenamed);
    connect(list_, &QWidget::customContextMenuRequested, this, &PlaylistDock::onContextMenu);
    connect(filterEdit_, &QLineEdit::textChanged, this, &PlaylistDock::onFilterChanged);
    connect(seek_, &QSlider::sliderReleased, this, &PlaylistDock::onSeekReleased);
    connect(sourceCombo_, &QComboBox::currentIndexChanged, this, &PlaylistDock::onSourceChanged);
    connect(endCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        mode_ = pld::endModeFromInt(endCombo_->currentData().toInt());
        shuffle_.invalidate();
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
    rebuildList();
    updateNowPlaying();
    updateTransportIcons();
}

QString PlaylistDock::itemText(int row) const {
    return QString::fromStdString(playlist_.items()[row].title);
}

void PlaylistDock::rebuildList() {
    if (!list_) return;
    const int sel = list_->currentRow();
    renaming_ = false; // any open inline editor is destroyed by the rebuild
    list_->blockSignals(true);
    list_->clear();
    for (int i = 0; i < playlist_.size(); ++i) {
        const auto& pi = playlist_.items()[i];
        const QString qpath = QString::fromStdString(pi.path);
        // The existence check comes from the cache the scanner fills. It used to
        // be a synchronous stat() per item on every rebuild — on a network share
        // that is a visible freeze on every single click.
        const auto cached = existsCache_.constFind(qpath);
        const bool missing = (cached != existsCache_.constEnd()) && !cached.value();
        QString label = itemText(i);
        if (missing) label += QStringLiteral("  \u26A0 ") + T("FileNotFound");
        auto* item = new QListWidgetItem(label);
        item->setData(kRoleModelIndex, i); // model index, used to sync drag-reorder
        item->setData(kRoleDuration, QString::fromStdString(formatDuration(pi.durationMs)));
        item->setData(kRoleMissing, missing);
        item->setData(kRoleCurrent, i == playlist_.currentIndex());
        item->setToolTip(missing ? qpath + "  (" + T("FileNotFound") + ")" : qpath);
        // Editable, but only through an explicit rename: the list sets no edit
        // triggers of its own.
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        list_->addItem(item);
    }
    // Preserve the user's selection across rebuilds — do NOT force it onto the
    // currently-playing item (that one carries the accent bar). Reordering while
    // another item plays must keep the selection where the user put it.
    if (sel >= 0 && sel < playlist_.size()) list_->setCurrentRow(sel);
    list_->blockSignals(false);
    applyFilter();
    updateTotals();
    if (undoBtn_) undoBtn_->setEnabled(history_.canUndo());
    scheduleSessionSave();
    snapshotStatus();
}

void PlaylistDock::applyFilter() {
    const QString f = filterEdit_ ? filterEdit_->text().trimmed() : QString();
    int shown = 0;
    for (int row = 0; row < list_->count(); ++row) {
        auto* item = list_->item(row);
        const bool match = f.isEmpty() || item->text().contains(f, Qt::CaseInsensitive);
        item->setHidden(!match);
        if (match) ++shown;
    }
    if (filterCount_) {
        // The count only means something while a filter is active.
        filterCount_->setText(f.isEmpty() ? QString()
                                          : T("Filter.Matches")
                                                .arg(shown)
                                                .arg(list_->count()));
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

void PlaylistDock::updateNowPlaying() {
    if (!nowTitle_) return;
    const auto* it = playlist_.current();
    if (!it) {
        nowTitle_->setText(T("Card.Idle"));
        nowMeta_->clear();
        upNext_->clear();
        if (seek_ && !seek_->isSliderDown()) seek_->setValue(0);
        if (timeLabel_) timeLabel_->clear();
        return;
    }
    const QString title = QString::fromStdString(it->title);
    const QFontMetrics fm(nowTitle_->font());
    const int w = std::max(60, nowTitle_->width());
    nowTitle_->setText(fm.elidedText(title, Qt::ElideMiddle, w));
    nowTitle_->setToolTip(QString::fromStdString(it->path));
    nowMeta_->setText(QString::fromStdString(formatDuration(it->durationMs)));

    // What happens next, spelled out: in a live show the operator has to know
    // what the deck will do on its own.
    if (pendingStageNext_) {
        upNext_->setText(T("Card.Pending"));
    } else {
        int nextIdx = -1;
        if (mode_ == pld::EndMode::Shuffle) {
            nextIdx = shuffle_.peek();
        } else {
            const auto d = pld::decideOnEnd(mode_, playlist_.size(), playlist_.currentIndex(), -1);
            if (d.action == pld::EndAction::Play || d.action == pld::EndAction::StageNext)
                nextIdx = d.index;
        }
        upNext_->setText(nextIdx >= 0 && nextIdx < playlist_.size()
                             ? T("Card.UpNext").arg(itemText(nextIdx))
                             : QString());
    }
    QPalette pal = upNext_->palette();
    pal.setColor(QPalette::WindowText, pendingStageNext_ ? pld::style::warning(palette())
                                                         : pld::style::textSecondary(palette()));
    upNext_->setPalette(pal);
}

// ---- Playback ------------------------------------------------------------

void PlaylistDock::playIndex(int row) {
    pendingStageNext_ = false;
    pendingStageRow_ = -1;
    stagePauseWanted_ = false;
    if (!playlist_.setCurrent(row)) return;
    const auto* it = playlist_.current();
    if (!it) return;
    if (!controller_.isBound()) {
        setStatus(T("Status.NoSourceConfigured"), StatusKind::Error);
        rebuildList();
        return;
    }
    const QString title = QString::fromStdString(it->title);
    if (controller_.setFileAndRestart(it->path)) {
        setStatus(T("Status.Playing").arg(title), StatusKind::Success);
        captureRow_ = row;
        capturePath_ = QString::fromStdString(it->path);
        captureRetries_ = 0;
        emitVendorItemStarted();
    } else {
        setStatus(T("Status.FailedSetSource"), StatusKind::Error);
    }
    rebuildList();
    updateNowPlaying();
    updateTransportIcons();
}

void PlaylistDock::loadIndex(int row) {
    if (!playlist_.setCurrent(row)) return;
    const auto* it = playlist_.current();
    if (!it) return;
    if (!controller_.isBound()) {
        setStatus(T("Status.NoSourceConfigured"), StatusKind::Error);
        rebuildList();
        return;
    }
    const QString title = QString::fromStdString(it->title);
    if (controller_.setFileLoadOnly(it->path)) {
        setStatus(T("Status.LoadedPaused").arg(title), StatusKind::Info);
        // The pause is repeated when the source reports it started, because a
        // large or remote file may not have opened yet. Waiting for the signal
        // replaces a pair of guessed timers that let heavy clips slip on air.
        stagePauseWanted_ = true;
        captureRow_ = row;
        capturePath_ = QString::fromStdString(it->path);
        captureRetries_ = 0;
    } else {
        setStatus(T("Status.FailedSetSource"), StatusKind::Error);
    }
    rebuildList();
    updateNowPlaying();
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
    if (stagePauseWanted_) {
        controller_.pause();
        stagePauseWanted_ = false;
    }
    captureDuration();
    updateTransportIcons();
}

void PlaylistDock::onMediaEnded() {
    const int count = playlist_.size();
    const int current = playlist_.currentIndex();
    const int candidate =
        (mode_ == pld::EndMode::Shuffle) ? shuffle_.next(count, current, rng_) : -1;
    const auto decision = pld::decideOnEnd(mode_, count, current, candidate);

    switch (decision.action) {
    case pld::EndAction::Play:
        playIndex(decision.index);
        break;
    case pld::EndAction::StageNext:
        // Hold the finished clip's last frame on air and load the next one only
        // once this source is off air, so its first frame never goes live.
        pendingStageNext_ = true;
        pendingStageRow_ = decision.index;
        setStatus(T("Status.ClipEndedStaged"), StatusKind::Info);
        updateNowPlaying();
        // If the source is already off air, there is nothing to wait for.
        if (pld::shouldStageNow(pendingStageNext_, presence())) stagePendingNow();
        break;
    case pld::EndAction::Stop: {
        controller_.stop();
        obs_data_t* d = obs_data_create();
        pld::emitVendorEvent("playlist-completed", d);
        obs_data_release(d);
        updateTransportIcons();
        break;
    }
    case pld::EndAction::Nothing:
        break;
    }
    snapshotStatus();
}

pld::ProgramPresence PlaylistDock::presence() const {
    pld::ProgramPresence p;
    p.bound = controller_.isBound();
    p.inProgram = p.bound && controller_.isInProgram();
    p.studioMode = obs_frontend_preview_program_mode_active();
    return p;
}

void PlaylistDock::stagePendingNow() {
    if (!pendingStageNext_) return;
    pendingStageNext_ = false;
    int row = pendingStageRow_;
    pendingStageRow_ = -1;
    if (row < 0 || row >= playlist_.size()) return;
    loadIndex(row); // sets the next file paused while off air
    setStatus(T("Status.StagedLoaded").arg(itemText(row)), StatusKind::Info);
}

void PlaylistDock::programLayoutChanged() {
    if (pld::shouldStageNow(pendingStageNext_, presence()))
        stagePendingNow();
    else
        updateNowPlaying();
}

void PlaylistDock::onSourceDeactivated() {
    // Kept as a fallback: a source removed from every scene raises this and no
    // scene change. The primary trigger is programLayoutChanged().
    if (pld::shouldStageNow(pendingStageNext_, presence())) stagePendingNow();
}

// ---- Playlist edits ------------------------------------------------------

void PlaylistDock::recordUndo(const QString& label) {
    history_.push(playlist_.items(), playlist_.currentIndex(), label.toStdString());
    if (undoBtn_) undoBtn_->setEnabled(true);
}

void PlaylistDock::applyHistoryState(std::vector<pld::PlaylistItem> items, int current) {
    playlist_.setItems(std::move(items));
    playlist_.setCurrent(current);
    shuffle_.invalidate();
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
    for (const auto& f : paths) {
        const std::string p = f.toStdString();
        if (!mediapath::isMediaFile(p)) continue;
        added << f;
    }
    if (added.isEmpty()) return;
    if (undoable) recordUndo(T("Edit.Add"));
    for (const auto& f : added)
        playlist_.add(PlaylistItem{f.toStdString(), mediapath::fileStem(f.toStdString()), -1});
    shuffle_.invalidate();
    rebuildList();
    setStatus(T("Status.Added").arg(added.size()), StatusKind::Success);
    startScan(added);
}

void PlaylistDock::startScan(const QStringList& paths, bool replacesPlaylist) {
    if (!scanner_ || paths.isEmpty()) return;
    scanner_->submit(paths, enableProbe_, replacesPlaylist);
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
    // One rebuild per batch, not per file: a 300-file drop used to rebuild the
    // list 300 times, each rebuild stat()ing all 300 items.
    if (changed) playlist_.setItemsKeepCurrent(std::move(items));
    rebuildList();
    updateNowPlaying();
}

void PlaylistDock::onRecheckFiles() {
    setStatus(T("Status.Rechecking"), StatusKind::Info);
    rescanAll();
}

void PlaylistDock::onAddFiles() {
    const QStringList files = QFileDialog::getOpenFileNames(
        this, T("Dlg.AddFiles"), lastDir_,
        T("Dlg.MediaFilter") +
            QStringLiteral(" (*.mp4 *.mov *.mkv *.avi *.webm *.m4v *.mpg *.mpeg *.ts *.flv *.wmv "
                           "*.mp3 *.m4a *.aac *.wav *.flac *.ogg *.opus *.3gp);;All files (*)"));
    if (files.isEmpty()) return;
    lastDir_ = QFileInfo(files.first()).absolutePath();
    saveSettings();
    addPaths(files);
}

void PlaylistDock::onAddFolder() {
    const QString dir = QFileDialog::getExistingDirectory(this, T("Dlg.AddFolder"), lastDir_);
    if (dir.isEmpty()) return;
    lastDir_ = dir;
    saveSettings();
    QStringList found;
    QDirIterator it(dir, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (mediapath::isMediaFile(path.toStdString())) found << path;
    }
    // Numbers the way people read them: clip2 before clip10.
    std::sort(found.begin(), found.end(), [](const QString& a, const QString& b) {
        return mediapath::naturalLess(a.toStdString(), b.toStdString());
    });
    addPaths(found);
}

void PlaylistDock::onFilesDropped(const QStringList& paths) { addPaths(paths); }

void PlaylistDock::onListReordered() {
    // The widget rows were reordered by drag; rebuild the model from the new
    // order using each item's stored original index.
    std::vector<PlaylistItem> reordered;
    reordered.reserve(playlist_.size());
    int newCurrent = -1;
    for (int row = 0; row < list_->count(); ++row) {
        const int orig = list_->item(row)->data(kRoleModelIndex).toInt();
        if (orig < 0 || orig >= playlist_.size()) continue;
        if (orig == playlist_.currentIndex()) newCurrent = static_cast<int>(reordered.size());
        reordered.push_back(playlist_.items()[orig]);
    }
    if (static_cast<int>(reordered.size()) != playlist_.size()) {
        rebuildList(); // safety: row count mismatch, just resync
        return;
    }
    recordUndo(T("Edit.Reorder"));
    playlist_.setItems(std::move(reordered));
    playlist_.setCurrent(newCurrent);
    shuffle_.invalidate();
    rebuildList();
    updateNowPlaying();
}

void PlaylistDock::onRemove() {
    std::vector<int> rows;
    for (const auto* item : list_->selectedItems()) {
        const int idx = item->data(kRoleModelIndex).toInt();
        if (idx >= 0) rows.push_back(idx);
    }
    if (rows.empty()) {
        const int row = list_->currentRow();
        if (row < 0) return;
        rows.push_back(row);
    }
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
    shuffle_.invalidate();
    rebuildList();
    updateNowPlaying();
}

void PlaylistDock::onUp() {
    const int r = list_->currentRow();
    if (playlist_.moveUp(r)) {
        recordUndo(T("Edit.Move"));
        rebuildList();
        list_->setCurrentRow(r - 1);
        updateNowPlaying();
    }
}

void PlaylistDock::onDown() {
    const int r = list_->currentRow();
    if (playlist_.moveDown(r)) {
        recordUndo(T("Edit.Move"));
        rebuildList();
        list_->setCurrentRow(r + 1);
        updateNowPlaying();
    }
}

void PlaylistDock::onClear() {
    if (playlist_.empty()) return;
    // No confirmation dialog: an undo that actually works is better than a
    // prompt people learn to dismiss.
    recordUndo(T("Edit.Clear"));
    playlist_.clear();
    shuffle_.invalidate();
    existsCache_.clear();
    rebuildList();
    updateNowPlaying();
    setStatus(T("Status.Cleared"), StatusKind::Info);
}

void PlaylistDock::onRename() {
    auto* item = list_->currentItem();
    if (!item) return;
    const int idx = item->data(kRoleModelIndex).toInt();
    if (idx < 0 || idx >= playlist_.size()) return;
    // Edit the plain title, not the decorated label — and set it without
    // emitting itemChanged, which would otherwise look like the commit.
    list_->blockSignals(true);
    item->setText(itemText(idx));
    list_->blockSignals(false);
    renaming_ = true;
    list_->editItem(item);
}

void PlaylistDock::onItemRenamed(QListWidgetItem* item) {
    if (!renaming_ || !item) return;
    renaming_ = false;
    const int idx = item->data(kRoleModelIndex).toInt();
    const QString title = item->text().trimmed();
    if (idx < 0 || idx >= playlist_.size() || title.isEmpty()) {
        rebuildList();
        return;
    }
    recordUndo(T("Edit.Rename"));
    if (playlist_.setTitle(idx, title.toStdString()))
        setStatus(T("Status.Renamed").arg(title), StatusKind::Info);
    rebuildList();
    updateNowPlaying();
}

void PlaylistDock::onContextMenu(const QPoint& pos) {
    QMenu menu(this);
    auto* item = list_->itemAt(pos);
    const int idx = item ? item->data(kRoleModelIndex).toInt() : -1;

    auto add = [this, &menu](const QString& icon, const QString& text, auto&& handler) {
        QAction* action = icon.isEmpty() ? menu.addAction(text)
                                         : menu.addAction(tintedIcon(icon), text);
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
    add(":/icons/upload.svg", T("Menu.ExportCsv"), &PlaylistDock::onExportCsv);
    menu.addSeparator();
    add(":/icons/refresh.svg", T("Menu.Recheck"), &PlaylistDock::onRecheckFiles);
    add(QString(), T("Menu.Undo"), &PlaylistDock::onUndo)->setEnabled(history_.canUndo());
    add(QString(), T("Menu.Redo"), &PlaylistDock::onRedo)->setEnabled(history_.canRedo());
    menu.exec(list_->viewport()->mapToGlobal(pos));
}

void PlaylistDock::onPlaySelected() {
    auto* item = list_->currentItem();
    if (!item) return;
    const int idx = item->data(kRoleModelIndex).toInt();
    if (idx >= 0) playIndex(idx);
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

void PlaylistDock::onSeekReleased() {
    if (!seek_) return;
    const long long dur = controller_.currentDurationMs();
    if (dur <= 0) return;
    controller_.seekMs(dur * seek_->value() / 1000);
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
    mode_ = pld::endModeFromInt(mode);
    if (endCombo_) {
        const int idx = endCombo_->findData(mode);
        if (idx >= 0) endCombo_->setCurrentIndex(idx);
    }
    shuffle_.invalidate();
    saveSettings();
    updateNowPlaying();
    snapshotStatus();
}
void PlaylistDock::wsSeek(int ms) {
    if (controller_.seekMs(ms)) onTick();
}
void PlaylistDock::wsClear() { onClear(); }
void PlaylistDock::wsAddPaths(const QStringList& paths) { addPaths(paths); }

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
    s.sourceName = QString::fromStdString(controller_.boundName());
    s.mode = static_cast<int>(mode_);
    for (const auto& m : pld::kEndModes)
        if (m.mode == mode_) s.modeName = QString::fromUtf8(m.key).mid(6); // drop "OnEnd."
    s.playlistName = loadedPath_.isEmpty() ? QString() : QFileInfo(loadedPath_).completeBaseName();
    if (pendingStageNext_ && pendingStageRow_ >= 0 && pendingStageRow_ < playlist_.size()) {
        s.upNextIndex = pendingStageRow_;
    } else if (mode_ == pld::EndMode::Shuffle) {
        s.upNextIndex = shuffle_.peek();
    } else {
        const auto d = pld::decideOnEnd(mode_, s.count, s.currentIndex, -1);
        if (d.action == pld::EndAction::Play || d.action == pld::EndAction::StageNext)
            s.upNextIndex = d.index;
    }
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

void PlaylistDock::onNext() {
    const int candidate = (mode_ == pld::EndMode::Shuffle)
                              ? shuffle_.next(playlist_.size(), playlist_.currentIndex(), rng_)
                              : -1;
    const auto d =
        pld::decideOnNext(mode_, playlist_.size(), playlist_.currentIndex(), candidate);
    if (d.action == pld::EndAction::Play) playIndex(d.index);
}

void PlaylistDock::onPrev() {
    const auto d = pld::decideOnPrev(mode_, playlist_.size(), playlist_.currentIndex());
    if (d.action == pld::EndAction::Play) playIndex(d.index);
}

void PlaylistDock::onTick() {
    if (!seek_) return;
    // Only show progress for a clip this session has actually selected. Without
    // this, on startup the bound media source still holds the file from the
    // previous session and the counter would show that stale clip even though
    // no playlist item is loaded.
    if (playlist_.currentIndex() < 0) {
        if (!seek_->isSliderDown()) seek_->setValue(0);
        timeLabel_->clear();
        return;
    }
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
    updateTransportIcons();
    snapshotPlayback();

    // Remote clients follow playback through this event instead of polling.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastStateEventMs_ >= kStateEventMs) {
        lastStateEventMs_ = now;
        obs_data_t* d = obs_data_create();
        obs_data_set_bool(d, "playing", controller_.isPlaying());
        obs_data_set_int(d, "positionMs", cur);
        obs_data_set_int(d, "durationMs", dur);
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
        snapshotStatus();
        return;
    }
    controller_.bind(name.toStdString());
    setStatus(T("Status.BoundTo").arg(name), StatusKind::Info);
    // Only a deliberate pick updates the remembered source. A programmatic
    // refresh must never overwrite it, or a scene collection that happens to
    // lack the chosen source would erase the user's configuration.
    if (!refreshing_) {
        pendingSource_ = name;
        saveSettings();
    }
    snapshotStatus();
}

void PlaylistDock::refreshSources() {
    // Track what the user asked for, not what the combo currently shows.
    QString wanted = sourceCombo_->currentData().toString();
    if (wanted.isEmpty()) wanted = pendingSource_;

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
    shuffle_.invalidate();
    existsCache_.clear();
    rebuildList();
    updateNowPlaying();
    startScan(toScan, /*replacesPlaylist=*/true);
    if (skipped > 0)
        setStatus(T("Status.SkippedItems").arg(static_cast<int>(skipped)), StatusKind::Warning);
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
    scheduleSessionSave();
}

void PlaylistDock::onSavePlaylist() {
    QString selectedFilter;
    QString path = QFileDialog::getSaveFileName(this, T("Dlg.SavePlaylist"), lastDir_,
                                                "JSON (*.json);;M3U (*.m3u)", &selectedFilter);
    if (path.isEmpty()) return;
    // Honor the chosen filter for the format (the dialog doesn't always append
    // the extension), so picking "M3U" actually writes .m3u — not .json.
    const bool m3u = path.endsWith(".m3u", Qt::CaseInsensitive) ||
                     (!path.endsWith(".json", Qt::CaseInsensitive) &&
                      selectedFilter.contains("m3u", Qt::CaseInsensitive));
    if (!path.endsWith(".json", Qt::CaseInsensitive) && !path.endsWith(".m3u", Qt::CaseInsensitive))
        path += m3u ? ".m3u" : ".json";

    const QFileInfo info(path);
    lastDir_ = info.absolutePath();
    const std::string baseDir = lastDir_.toStdString();
    const std::string text =
        m3u ? io::toM3u(playlist_.items(), baseDir, relativePaths_)
            : io::toJson(info.completeBaseName().toStdString(), playlist_.items(), baseDir,
                         relativePaths_);
    // The user's own playlist file is the most valuable thing this plugin
    // writes; it gets the atomic path too.
    if (!writeAtomically(path, QByteArray::fromStdString(text))) {
        setStatus(T("Status.CannotWrite"), StatusKind::Error);
        return;
    }
    setLoadedPlaylist(path);
    saveSettings();
    setStatus(T("Status.Saved").arg(info.fileName()), StatusKind::Success);
}

void PlaylistDock::onOpenPlaylist() {
    const QString path = QFileDialog::getOpenFileName(
        this, T("Dlg.OpenPlaylist"), lastDir_,
        T("Dlg.PlaylistFilter") + QStringLiteral(" (*.json *.m3u *.m3u8)"));
    if (path.isEmpty()) return;
    lastDir_ = QFileInfo(path).absolutePath();
    saveSettings();
    if (loadPlaylistFile(path)) {
        setLoadedPlaylist(path);
        setStatus(T("Status.Opened").arg(QFileInfo(path).fileName()), StatusKind::Success);
    }
}

void PlaylistDock::onExportCsv() {
    if (playlist_.empty()) return;
    QString path = QFileDialog::getSaveFileName(this, T("Dlg.ExportCsv"), lastDir_, "CSV (*.csv)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".csv", Qt::CaseInsensitive)) path += ".csv";
    if (!writeAtomically(path, QByteArray::fromStdString(io::toCsv(playlist_.items())))) {
        setStatus(T("Status.CannotWrite"), StatusKind::Error);
        return;
    }
    lastDir_ = QFileInfo(path).absolutePath();
    saveSettings();
    setStatus(T("Status.Exported").arg(QFileInfo(path).fileName()), StatusKind::Success);
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

QString PlaylistDock::settingsPath() const {
    char* p = obs_module_config_path("settings.json");
    QString s = p ? QString::fromUtf8(p) : QString();
    bfree(p);
    if (!s.isEmpty()) QDir().mkpath(QFileInfo(s).absolutePath());
    return s;
}

void PlaylistDock::saveSettings() const {
    const QString path = settingsPath();
    if (path.isEmpty()) return;
    QJsonObject o;
    o["mode"] = static_cast<int>(mode_);
    // Persist the remembered choice, never the combo's current text: that is the
    // "no source configured" placeholder whenever the active scene collection
    // does not contain the chosen source.
    o["source"] = pendingSource_;
    o["enableProbe"] = enableProbe_;
    o["autoRestore"] = autoRestore_;
    o["relativePaths"] = relativePaths_;
    o["language"] = language_;
    o["lastDir"] = lastDir_;
    writeAtomically(path, QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void PlaylistDock::loadSettings() {
    const QString path = settingsPath();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) return;
    const QJsonObject o = doc.object();
    mode_ = pld::endModeFromInt(o.value("mode").toInt(0));
    pendingSource_ = o.value("source").toString();
    enableProbe_ = o.value("enableProbe").toBool(true);
    autoRestore_ = o.value("autoRestore").toBool(false);
    relativePaths_ = o.value("relativePaths").toBool(false);
    language_ = o.value("language").toString("auto");
    lastDir_ = o.value("lastDir").toString();
}

void PlaylistDock::applyLocale() {
    if (language_ != "auto" && !language_.isEmpty())
        obs_module_set_locale(language_.toUtf8().constData());
}

void PlaylistDock::applyLocaleAndRebuild() {
    applyLocale();
    const QString loaded = loadedPath_;
    buildUi(); // recreates all widgets with the new language
    applyDockTitle();
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

void PlaylistDock::scheduleSessionSave() {
    if (!autoRestore_ || !sessionTimer_ || obsShutdown_) return;
    sessionTimer_->start();
}

void PlaylistDock::saveSession() const {
    const QString path = sessionPath();
    if (path.isEmpty()) return;
    // Reuse the playlist serializer, then add what only a session carries.
    QJsonDocument doc = QJsonDocument::fromJson(
        QByteArray::fromStdString(io::toJson("session", playlist_.items())));
    if (!doc.isObject()) return;
    QJsonObject o = doc.object();
    // Restoring the playlist but not which file it came from left the label
    // claiming nothing was loaded.
    o["loadedPath"] = loadedPath_;
    writeAtomically(path, QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void PlaylistDock::loadSession() {
    const QString path = sessionPath();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QByteArray raw = f.readAll();
    f.close();

    // The schema version was written from the start but never read. A file from
    // a future version is kept aside rather than half-understood.
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (doc.isObject() && doc.object().value("version").toInt(1) > 1) {
        QFile::remove(path + ".bak");
        QFile::rename(path, path + ".bak");
        setStatus(T("Status.SessionReset"), StatusKind::Warning);
        return;
    }

    std::string n;
    std::vector<PlaylistItem> items;
    if (!io::fromJson(raw.toStdString(), n, items).ok) return;
    QStringList toScan;
    for (const auto& it : items) toScan << QString::fromStdString(it.path);
    playlist_.setItems(std::move(items));
    shuffle_.invalidate();
    rebuildList();
    updateNowPlaying();
    startScan(toScan, /*replacesPlaylist=*/true);
    if (doc.isObject()) {
        const QString loaded = doc.object().value("loadedPath").toString();
        if (!loaded.isEmpty()) setLoadedPlaylist(loaded);
    }
    // Nothing was played yet, so the history starts here.
    history_.clear();
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

    auto* relativeChk = new QCheckBox(T("Settings.RelativePaths"));
    relativeChk->setChecked(relativePaths_);
    relativeChk->setToolTip(T("Settings.RelativePathsTip"));
    form->addRow(relativeChk);

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
    const int li = langCombo->findData(language_);
    langCombo->setCurrentIndex(li >= 0 ? li : 0);
    form->addRow(new QLabel(T("Settings.Language")), langCombo);

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
    enableProbe_ = probeChk->isChecked();
    autoRestore_ = restoreChk->isChecked();
    relativePaths_ = relativeChk->isChecked();
    const QString newLang = langCombo->currentData().toString();
    const bool langChanged = (newLang != language_);
    language_ = newLang;
    saveSettings();
    if (autoRestore_) saveSession();
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
    reg("obs-playlist-deck.recheck", "Playlist Deck: Recheck missing files",
        &PlaylistDock::onRecheckFiles, -1);
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

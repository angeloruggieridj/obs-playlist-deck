// SPDX-License-Identifier: GPL-2.0-or-later
#include "SettingsDialog.hpp"
#include "DeckStyle.hpp"
#include "DeckWidgets.hpp"

#include <obs-module.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace {
QString T(const char* key) { return QString::fromUtf8(obs_module_text(key)); }

// Secondary text under an option: what it does, in the same dimmed ink the dock
// uses elsewhere, wrapping instead of widening the window.
QLabel* hintLabel(const QString& text, const QPalette& pal) {
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    QPalette p = label->palette();
    p.setColor(QPalette::WindowText, pld::style::textSecondary(pal));
    label->setPalette(p);
    return label;
}

QLabel* captionLabel(const QString& text) {
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    return label;
}
} // namespace

ToggleLabel::ToggleLabel(const QString& text, QWidget* parent) : QLabel(text, parent) {
    setWordWrap(true);
    setCursor(Qt::PointingHandCursor);
}

void ToggleLabel::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
        emit clicked();
    QLabel::mouseReleaseEvent(event);
}

SettingsDialog::SettingsDialog(const DeckSettings& current, QWidget* parent)
    : QDialog(parent), base_(current) {
    setWindowTitle(T("Settings.Title"));
    setSizeGripEnabled(true);
    setMinimumSize(300, 260);
    resize(460, 580);

    const QPalette pal = palette();

    // Check box with a wrapping caption and an optional hint beneath it.
    auto option = [&](const QString& caption, const QString& hint, bool checked,
                      QCheckBox** out) {
        auto* box = new QWidget();
        auto* col = new QVBoxLayout(box);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(2);
        auto* row = new QHBoxLayout();
        row->setSpacing(6);
        auto* check = new QCheckBox();
        check->setChecked(checked);
        check->setAccessibleName(caption);
        auto* text = new ToggleLabel(caption);
        connect(text, &ToggleLabel::clicked, check, &QCheckBox::toggle);
        row->addWidget(check, 0, Qt::AlignTop);
        row->addWidget(text, 1);
        col->addLayout(row);
        if (!hint.isEmpty()) {
            auto* h = hintLabel(hint, pal);
            h->setContentsMargins(check->sizeHint().width() + 6, 0, 0, 0);
            col->addWidget(h);
        }
        *out = check;
        return box;
    };

    // A titled group. Titles are short; everything inside wraps.
    auto group = [&](const QString& title, QVBoxLayout** inner) {
        auto* g = new QGroupBox(title);
        auto* col = new QVBoxLayout(g);
        col->setSpacing(8);
        *inner = col;
        return g;
    };

    auto* body = new QWidget();
    auto* bodyCol = new QVBoxLayout(body);
    bodyCol->setContentsMargins(12, 12, 12, 12);
    bodyCol->setSpacing(12);

    QVBoxLayout* inner = nullptr;

    // ---- Playback ---------------------------------------------------------
    bodyCol->addWidget(group(T("Settings.SectionPlayback"), &inner));
    inner->addWidget(option(T("Settings.Probe"), QString(), current.enableProbe, &probe_));
    inner->addWidget(option(T("Settings.UnmuteOnStart"), T("Settings.UnmuteOnStartTip"),
                            current.unmuteOnStart, &unmuteOnStart_));

    // ---- Scenes -----------------------------------------------------------
    bodyCol->addWidget(group(T("Settings.SectionScenes"), &inner));
    inner->addWidget(captionLabel(T("Settings.PanicScene")));
    panicScene_ = new SceneComboBox(T("Settings.PanicSceneNone"), T("Settings.SceneMissing"));
    panicScene_->setScene(current.panicScene);
    panicScene_->setAccessibleName(T("Settings.PanicScene"));
    inner->addWidget(panicScene_);
    inner->addWidget(hintLabel(T("Settings.PanicSceneTip"), pal));

    inner->addSpacing(6);
    inner->addWidget(option(T("Settings.AutoSwitch"), T("Settings.AutoSwitchTip"),
                            current.autoSwitchEnabled, &autoSwitch_));

    auto* switchOptions = new QWidget();
    auto* switchCol = new QVBoxLayout(switchOptions);
    switchCol->setContentsMargins(0, 0, 0, 0);
    switchCol->setSpacing(4);
    switchCol->addWidget(captionLabel(T("Settings.AutoSwitchScene")));
    autoSwitchScene_ =
        new SceneComboBox(T("Settings.AutoSwitchSceneNone"), T("Settings.SceneMissing"));
    autoSwitchScene_->setScene(current.autoSwitchScene);
    autoSwitchScene_->setAccessibleName(T("Settings.AutoSwitchScene"));
    switchCol->addWidget(autoSwitchScene_);
    switchCol->addWidget(captionLabel(T("Settings.AutoSwitchEffect")));
    autoSwitchEffect_ = new QComboBox();
    autoSwitchEffect_->setAccessibleName(T("Settings.AutoSwitchEffect"));
    autoSwitchEffect_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    autoSwitchEffect_->addItem(T("Effect.Cut"), static_cast<int>(pld::SwitchEffect::Cut));
    autoSwitchEffect_->addItem(T("Effect.Fade"), static_cast<int>(pld::SwitchEffect::Fade));
    autoSwitchEffect_->addItem(T("Effect.Stinger"), static_cast<int>(pld::SwitchEffect::Stinger));
    const int effectIndex =
        autoSwitchEffect_->findData(static_cast<int>(current.autoSwitchEffect));
    autoSwitchEffect_->setCurrentIndex(effectIndex >= 0 ? effectIndex : 0);
    switchCol->addWidget(autoSwitchEffect_);
    switchOptions->setEnabled(current.autoSwitchEnabled);
    connect(autoSwitch_, &QCheckBox::toggled, switchOptions, &QWidget::setEnabled);
    inner->addWidget(switchOptions);

    // ---- Files ------------------------------------------------------------
    bodyCol->addWidget(group(T("Settings.SectionFiles"), &inner));
    inner->addWidget(option(T("Settings.RelativePaths"), T("Settings.RelativePathsTip"),
                            current.relativePaths, &relativePaths_));

    // ---- Language ---------------------------------------------------------
    bodyCol->addWidget(group(T("Settings.Language"), &inner));
    language_ = new QComboBox();
    language_->setAccessibleName(T("Settings.Language"));
    language_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    language_->addItem(T("Lang.Auto"), "auto");
    // Endonyms, not translations: they have to name the language to someone who
    // cannot read the current UI. fromUtf8() states the source encoding rather
    // than trusting the compiler's default narrow-literal charset (see the
    // /utf-8 flag in CMakeLists.txt).
    language_->addItem(QStringLiteral("English"), "en-US");
    language_->addItem(QStringLiteral("Italiano"), "it-IT");
    language_->addItem(QString::fromUtf8("Español"), "es-ES");
    language_->addItem(QString::fromUtf8("Français"), "fr-FR");
    language_->addItem(QStringLiteral("Deutsch"), "de-DE");
    language_->addItem(QString::fromUtf8("Português (BR)"), "pt-BR");
    language_->addItem(QString::fromUtf8("Русский"), "ru-RU");
    language_->addItem(QString::fromUtf8("简体中文"), "zh-CN");
    language_->addItem(QString::fromUtf8("日本語"), "ja-JP");
    language_->addItem(QString::fromUtf8("한국어"), "ko-KR");
    const int languageIndex = language_->findData(current.language);
    language_->setCurrentIndex(languageIndex >= 0 ? languageIndex : 0);
    inner->addWidget(language_);

    // ---- Maintenance ------------------------------------------------------
    bodyCol->addWidget(group(T("Settings.SectionMaintenance"), &inner));
    auto* restoreBtn = new QPushButton(T("Btn.RestoreBackup"));
    auto* updateBtn = new QPushButton(T("Btn.CheckUpdates"));
    inner->addWidget(restoreBtn);
    inner->addWidget(updateBtn);
    connect(restoreBtn, &QPushButton::clicked, this, &SettingsDialog::restoreBackupRequested);
    connect(updateBtn, &QPushButton::clicked, this, &SettingsDialog::checkUpdatesRequested);

    bodyCol->addStretch(1);

    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(body);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 8);
    outer->setSpacing(6);
    outer->addWidget(scroll, 1);
    auto* buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(12, 0, 12, 0);
    buttonRow->addWidget(buttons);
    outer->addLayout(buttonRow);
}

DeckSettings SettingsDialog::result() const {
    DeckSettings out = base_;
    out.enableProbe = probe_->isChecked();
    out.relativePaths = relativePaths_->isChecked();
    out.unmuteOnStart = unmuteOnStart_->isChecked();
    out.panicScene = panicScene_->scene().trimmed();
    out.autoSwitchEnabled = autoSwitch_->isChecked();
    out.autoSwitchScene = autoSwitchScene_->scene().trimmed();
    out.autoSwitchEffect = pld::switchEffectFromInt(autoSwitchEffect_->currentData().toInt());
    out.language = language_->currentData().toString();
    return out;
}

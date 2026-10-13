// SPDX-License-Identifier: GPL-2.0-or-later
#include "DeckWidgets.hpp"

#include <obs-frontend-api.h>

#include <QFontMetrics>
#include <QPainter>
#include <QSizePolicy>
#include <QStyle>

#include <algorithm>

ElidedLabel::ElidedLabel(const QString& text, QWidget* parent) : QLabel(text, parent) {
    setWordWrap(false);
    // Free to grow into spare width, fixed in height: see the class comment.
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void ElidedLabel::setElideMode(Qt::TextElideMode mode) {
    mode_ = mode;
    update();
}

int ElidedLabel::lineHeight() const {
    return fontMetrics().lineSpacing() + 2 * (margin() + frameWidth());
}

QSize ElidedLabel::sizeHint() const {
    const int w = fontMetrics().horizontalAdvance(text()) + 2 * (margin() + frameWidth());
    return QSize(w, lineHeight());
}

QSize ElidedLabel::minimumSizeHint() const {
    // Room for an ellipsis and a few characters: enough to show that there is
    // something here, little enough not to hold the dock open.
    const int w = fontMetrics().horizontalAdvance(QStringLiteral("MMM")) +
                  2 * (margin() + frameWidth());
    return QSize(w, lineHeight());
}

void ElidedLabel::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const QRect area = contentsRect().adjusted(margin(), margin(), -margin(), -margin());
    const QString full = text();
    const QString shown = fontMetrics().elidedText(full, mode_, std::max(0, area.width()));
    if (autoToolTip_) {
        const QString wanted = (shown != full) ? full : QString();
        if (toolTip() != wanted) setToolTip(wanted);
    }
    style()->drawItemText(&painter, area, static_cast<int>(alignment()), palette(), isEnabled(),
                          shown, foregroundRole());
}

// ---- SceneComboBox ---------------------------------------------------------

SceneComboBox::SceneComboBox(const QString& noneText, const QString& missingFormat,
                             QWidget* parent)
    : QComboBox(parent), noneText_(noneText), missingFormat_(missingFormat) {
    // A long scene name must not set the width of the window it is in.
    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    setMinimumContentsLength(12);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    populate(QString());
}

QStringList SceneComboBox::liveSceneNames() {
    QStringList names;
    struct obs_frontend_source_list scenes = {};
    obs_frontend_get_scenes(&scenes);
    for (size_t i = 0; i < scenes.sources.num; ++i) {
        const char* name = obs_source_get_name(scenes.sources.array[i]);
        if (name) names << QString::fromUtf8(name);
    }
    obs_frontend_source_list_free(&scenes);
    return names;
}

void SceneComboBox::populate(const QString& keep) {
    const QSignalBlocker blocker(this);
    clear();
    addItem(noneText_, QString());
    for (const QString& name : liveSceneNames()) addItem(name, name);
    if (!keep.isEmpty() && findData(keep) < 0) addItem(missingFormat_.arg(keep), keep);
    const int index = keep.isEmpty() ? 0 : findData(keep);
    setCurrentIndex(index >= 0 ? index : 0);
}

void SceneComboBox::setScene(const QString& name) { populate(name); }

void SceneComboBox::showPopup() {
    // Scenes are added and renamed while the window is open; the list is read
    // when it is asked for, and the selection is kept across the refresh.
    populate(scene());
    QComboBox::showPopup();
}

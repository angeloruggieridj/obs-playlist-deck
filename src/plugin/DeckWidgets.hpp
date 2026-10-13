// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QComboBox>
#include <QLabel>
#include <QString>
#include <QStringList>

// A one-line label that shortens its text with an ellipsis instead of refusing
// to be narrower than it.
//
// A plain QLabel's minimum width is the width of its text, so a long clip title
// in "now playing" or "then: ..." set the floor for the whole dock: the panel
// could not be dragged narrower than the longest title it had ever shown, and
// the surplus appeared as a wide empty strip beside the list. This one reports
// a small minimum and paints whatever fits.
//
// Its height is fixed to one line whether or not there is any text, so a label
// that fills and empties (the status line) never moves what is under it.
class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(const QString& text = QString(), QWidget* parent = nullptr);

    void setElideMode(Qt::TextElideMode mode);
    // Shows the full text as a tooltip while it is shortened. Off for a label
    // that carries its own tooltip.
    void setAutoToolTip(bool on) { autoToolTip_ = on; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int lineHeight() const;

    Qt::TextElideMode mode_ = Qt::ElideRight;
    bool autoToolTip_ = false;
};

// A combo box listing the scenes of the current scene collection, read from OBS
// each time the list is opened rather than once when the window was built.
//
// The saved name is kept even when no such scene exists (another scene
// collection, a scene since renamed): it is shown as missing and stays selected,
// so merely opening Settings never erases a choice.
class SceneComboBox : public QComboBox {
public:
    // `noneText` is the first entry, whose value is the empty string.
    // `missingFormat` contains %1 for a saved name with no matching scene.
    SceneComboBox(const QString& noneText, const QString& missingFormat,
                  QWidget* parent = nullptr);

    void setScene(const QString& name);
    QString scene() const { return currentData().toString(); }

    void showPopup() override;

    static QStringList liveSceneNames();

private:
    void populate(const QString& keep);

    QString noneText_;
    QString missingFormat_;
};

// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QDialog>
#include <QLabel>
#include "SettingsStore.hpp"

class QCheckBox;
class QComboBox;
class SceneComboBox;
class QMouseEvent;

// A wrapping caption that behaves like the text of a check box. QCheckBox keeps
// its text on one line, so a long option made the whole window as wide as the
// sentence; the caption is a label instead, and a click on it toggles the box.
class ToggleLabel : public QLabel {
    Q_OBJECT
public:
    explicit ToggleLabel(const QString& text, QWidget* parent = nullptr);

signals:
    void clicked();

protected:
    void mouseReleaseEvent(QMouseEvent* event) override;
};

// The Settings window.
//
// It used to be one QFormLayout of check boxes and a free-text field: the
// widest caption set the minimum width, nothing wrapped, and a window made
// smaller simply cut the controls off. Now each group of options sits under its
// own heading inside a scroll area, captions and hints wrap, and the combo boxes
// elide their entries - the window can be dragged small and nothing is lost.
//
// It edits a copy of the settings; the caller reads result() after accept().
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(const DeckSettings& current, QWidget* parent = nullptr);

    DeckSettings result() const;

signals:
    void restoreBackupRequested();
    void checkUpdatesRequested();

private:
    DeckSettings base_;
    QCheckBox* probe_ = nullptr;
    QCheckBox* relativePaths_ = nullptr;
    QCheckBox* unmuteOnStart_ = nullptr;
    SceneComboBox* panicScene_ = nullptr;
    QCheckBox* autoSwitch_ = nullptr;
    SceneComboBox* autoSwitchScene_ = nullptr;
    QComboBox* autoSwitchEffect_ = nullptr;
    QComboBox* language_ = nullptr;
};

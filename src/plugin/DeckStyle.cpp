// SPDX-License-Identifier: MIT
#include "DeckStyle.hpp"

namespace pld::style {

QString sheet(const QPalette& pal) {
    const QString accentHex = accent(pal).name();
    const QString surface = raisedSurface(pal);
    const QString border = subtleBorder(pal);
    const QString hover = isDark(pal) ? QStringLiteral("rgba(255,255,255,0.08)")
                                      : QStringLiteral("rgba(0,0,0,0.06)");
    const QString pressed = isDark(pal) ? QStringLiteral("rgba(255,255,255,0.14)")
                                        : QStringLiteral("rgba(0,0,0,0.10)");

    // Focus is drawn with an accent outline rather than the platform's dotted
    // rectangle: the buttons are keyboard-reachable now (they used to be
    // Qt::NoFocus, which quietly made the whole dock mouse-only while it
    // advertised accessible names), and the ring has to be visible on a dark
    // theme without shouting at a mouse user.
    return QStringLiteral(R"(
#pldRoot QPushButton[pldIcon="true"] {
    border: 1px solid transparent;
    border-radius: %5px;
    background: transparent;
    padding: 0px;
}
#pldRoot QPushButton[pldIcon="true"]:hover { background: %3; }
#pldRoot QPushButton[pldIcon="true"]:pressed { background: %4; }
#pldRoot QPushButton[pldIcon="true"]:checked { background: %4; }
#pldRoot QPushButton[pldIcon="true"]:focus { border: 1px solid %1; }
#pldRoot QPushButton[pldIcon="true"]:disabled { color: palette(mid); }

#pldCard {
    background: %2;
    border: 1px solid %6;
    border-radius: %5px;
}

#pldSeek::groove:horizontal {
    height: 4px;
    border-radius: 2px;
    background: %6;
}
#pldSeek::sub-page:horizontal {
    height: 4px;
    border-radius: 2px;
    background: %1;
}
#pldSeek::handle:horizontal {
    width: 10px;
    height: 10px;
    margin: -4px 0;
    border-radius: 5px;
    background: %1;
}
#pldSeek:focus { outline: none; }

#pldRoot QLineEdit {
    border: 1px solid %6;
    border-radius: %5px;
    padding: 3px 6px;
}
#pldRoot QLineEdit:focus { border: 1px solid %1; }

#pldRoot QComboBox {
    border: 1px solid %6;
    border-radius: %5px;
    padding: 2px 6px;
}
#pldRoot QComboBox:focus { border: 1px solid %1; }

#pldList {
    border: 1px solid %6;
    border-radius: %5px;
}
#pldList::item { padding: 3px 4px; border-radius: 3px; }
#pldList::item:hover { background: %3; }
#pldList:focus { border: 1px solid %1; }
)")
        .arg(accentHex, surface, hover, pressed)
        .arg(kRadius)
        .arg(border);
}

} // namespace pld::style

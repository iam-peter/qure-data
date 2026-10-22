#pragma once

#include <QColor>

namespace qure::ui {

/// Colours used by the C++ renderers. QML reads the same values through
/// Session.theme so front ends match.
struct Theme {
    QColor background;
    QColor grid;
    QColor boxFill;
    QColor boxBorder;
    QColor messageFill;
    QColor atomFill;
    QColor text;
    QColor commentText;
    QColor selection;
    QColor brokenBorder;
    QColor controlCord;
    QColor signalCord;
    QColor controlIolet;
    QColor signalIolet;
    QColor textureCord; ///< cords and iolets of the visuals module (gfx.*)
    QColor rubberBand;
    QColor flash;
    bool dark = false;

    static Theme light() {
        Theme t;
        t.background = QColor(0xfa, 0xfa, 0xf8);
        t.grid = QColor(0xec, 0xec, 0xe8);
        t.boxFill = QColor(0xff, 0xff, 0xff);
        t.boxBorder = QColor(0x8a, 0x8d, 0x93);
        t.messageFill = QColor(0xf1, 0xf3, 0xf6);
        t.atomFill = QColor(0xf7, 0xf7, 0xf2);
        t.text = QColor(0x1f, 0x22, 0x26);
        t.commentText = QColor(0x5a, 0x5f, 0x66);
        t.selection = QColor(0x25, 0x7c, 0xf0);
        t.brokenBorder = QColor(0xd0, 0x3a, 0x3a);
        t.controlCord = QColor(0x6c, 0x70, 0x78);
        t.signalCord = QColor(0x2f, 0x33, 0x3a);
        t.controlIolet = QColor(0x8a, 0x8d, 0x93);
        t.signalIolet = QColor(0x2f, 0x33, 0x3a);
        t.textureCord = QColor(0x9b, 0x4d, 0xe0);
        t.rubberBand = QColor(0x25, 0x7c, 0xf0, 0x30);
        t.flash = QColor(0x25, 0x7c, 0xf0);
        return t;
    }

    static Theme darkTheme() {
        Theme t;
        t.dark = true;
        t.background = QColor(0x1d, 0x1f, 0x23);
        t.grid = QColor(0x25, 0x28, 0x2d);
        t.boxFill = QColor(0x2a, 0x2d, 0x33);
        t.boxBorder = QColor(0x5d, 0x63, 0x6d);
        t.messageFill = QColor(0x31, 0x36, 0x3f);
        t.atomFill = QColor(0x2e, 0x31, 0x37);
        t.text = QColor(0xdf, 0xe2, 0xe7);
        t.commentText = QColor(0x9a, 0xa1, 0xab);
        t.selection = QColor(0x4d, 0x9a, 0xff);
        t.brokenBorder = QColor(0xf0, 0x5d, 0x5d);
        t.controlCord = QColor(0x8a, 0x91, 0x9c);
        t.signalCord = QColor(0xc8, 0xcd, 0xd5);
        t.controlIolet = QColor(0x8a, 0x91, 0x9c);
        t.signalIolet = QColor(0xc8, 0xcd, 0xd5);
        t.textureCord = QColor(0xc0, 0x84, 0xff);
        t.rubberBand = QColor(0x4d, 0x9a, 0xff, 0x30);
        t.flash = QColor(0x4d, 0x9a, 0xff);
        return t;
    }
};

} // namespace qure::ui

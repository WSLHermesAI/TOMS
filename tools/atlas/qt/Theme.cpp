#include "Theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>

namespace Theme {

namespace {

Mode g_mode = Mode::Dark;

Colors makeColors(Mode m)
{
    Colors c;
    const bool dark = m == Mode::Dark;
    c.canvasBackground = dark ? QColor(0x1b, 0x1c, 0x1f) : QColor(0xd4, 0xd7, 0xdc);
    // The checkerboard stays mid-grey in both themes: sprites must read the same everywhere.
    c.checkerLight = dark ? QColor(0x4a, 0x4c, 0x50) : QColor(0xfa, 0xfa, 0xfa);
    c.checkerDark = dark ? QColor(0x3a, 0x3c, 0x40) : QColor(0xe4, 0xe6, 0xe9);
    c.pageBorder = dark ? QColor(0x70, 0x74, 0x7a) : QColor(0x8a, 0x8f, 0x96);
    c.imageOutline = dark ? QColor(0x4f, 0x9c, 0xff, 170) : QColor(0x1f, 0x6f, 0xe0, 170);
    c.childOutline = dark ? QColor(0xff, 0xa8, 0x3a, 210) : QColor(0xd9, 0x7a, 0x00, 220);
    c.aliasOutline = QColor(0xb0, 0x7c, 0xff, 200);
    c.selection = dark ? QColor(0x3d, 0xd6, 0xff) : QColor(0x00, 0x8c, 0xd6);
    c.hover = dark ? QColor(0xff, 0xff, 0xff, 40) : QColor(0x00, 0x00, 0x00, 28);
    c.pin = QColor(0xf0, 0x4a, 0x5a);
    c.pivot = QColor(0x50, 0xe0, 0x7a);
    c.slice = QColor(0xff, 0x5c, 0xc8);
    c.label = QColor(0xf2, 0xf2, 0xf2);
    c.labelBackground = QColor(0x10, 0x10, 0x12, 200);
    return c;
}

Colors g_colors = makeColors(Mode::Dark);

QPalette darkPalette()
{
    QPalette p;
    const QColor window(0x2b, 0x2d, 0x30), base(0x1f, 0x20, 0x23), text(0xdf, 0xe1, 0xe5);
    const QColor disabled(0x7a, 0x7d, 0x82), highlight(0x2f, 0x6f, 0xd6);
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, QColor(0x26, 0x28, 0x2b));
    p.setColor(QPalette::ToolTipBase, QColor(0x3a, 0x3d, 0x41));
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, disabled);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, QColor(0x35, 0x37, 0x3b));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, QColor(0xff, 0x55, 0x55));
    p.setColor(QPalette::Light, QColor(0x45, 0x48, 0x4c));
    p.setColor(QPalette::Midlight, QColor(0x3a, 0x3c, 0x40));
    p.setColor(QPalette::Mid, QColor(0x25, 0x27, 0x2a));
    p.setColor(QPalette::Dark, QColor(0x1a, 0x1b, 0x1d));
    p.setColor(QPalette::Shadow, QColor(0x0e, 0x0e, 0x10));
    p.setColor(QPalette::Highlight, highlight);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, QColor(0x6c, 0xa8, 0xff));
    p.setColor(QPalette::Disabled, QPalette::Text, disabled);
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x45, 0x48, 0x4c));
    return p;
}

}  // namespace

void apply(Mode m)
{
    g_mode = m;
    g_colors = makeColors(m);
    // Fusion draws consistently with any palette; the native Windows style ignores dark palettes.
    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        QApplication::setStyle(fusion);
    QPalette p = m == Mode::Dark ? darkPalette() : QApplication::style()->standardPalette();
    if (m == Mode::Light) p.setColor(QPalette::Highlight, QColor(0x2f, 0x6f, 0xd6));
    QApplication::setPalette(p);
}

Mode mode() { return g_mode; }

const Colors& colors() { return g_colors; }

}  // namespace Theme

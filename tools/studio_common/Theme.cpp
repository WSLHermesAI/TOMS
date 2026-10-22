#include "Theme.h"

#include <QAbstractScrollArea>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QScrollBar>
#include <QWheelEvent>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>

namespace Theme {

namespace {

// The mouse wheel changes a value field (spin box, combo box, slider) only once it has focus (clicked or tabbed
// into). Over an unfocused field the wheel scrolls the panel around it (its scroll area) instead of changing a value
// as the pointer passes over it. Every widget of the application, every editor (installed by apply()).
bool isValueField(QObject* o) {
    // A scroll bar is a slider too, but it is how a panel scrolls: it always takes the wheel (a scroll area hands it
    // the wheel; treating it as a field would hand the wheel back, round and round).
    if (qobject_cast<QScrollBar*>(o)) return false;
    return qobject_cast<QAbstractSpinBox*>(o) || qobject_cast<QComboBox*>(o) || qobject_cast<QAbstractSlider*>(o);
}
// The field the wheel is over: the widget itself, or the field whose line edit it is (a spin box's, an editable combo
// box's). Not further up: an open combo box's list is a popup whose parents lead to the combo box, and it must scroll.
QWidget* valueField(QObject* o) {
    if (isValueField(o)) return static_cast<QWidget*>(o);
    auto* w = qobject_cast<QWidget*>(o);
    if (w && !w->isWindow() && isValueField(w->parentWidget())) return w->parentWidget();
    return nullptr;
}

class WheelNeedsFocus : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::Polish) {
            // Qt gives these fields focus on a wheel turn (Qt::WheelFocus), before any filter sees it: click or Tab only.
            if (QWidget* f = valueField(o); f == o && (f->focusPolicy() & Qt::WheelFocus) == Qt::WheelFocus)
                f->setFocusPolicy(Qt::StrongFocus);
        } else if (e->type() == QEvent::Wheel) {
            QWidget* f = valueField(o);
            if (f && !f->hasFocus() && !(QApplication::focusWidget() && f->isAncestorOf(QApplication::focusWidget()))) {
                // The panel the field sits in scrolls: the nearest scroll area around it (a Properties dock), through
                // its viewport. A field in no scroll area: the wheel does nothing.
                QAbstractScrollArea* area = nullptr;
                for (QWidget* up = f->parentWidget(); up && !area; up = up->isWindow() ? nullptr : up->parentWidget())
                    area = qobject_cast<QAbstractScrollArea*>(up);
                if (area && area->viewport()) {
                    auto* w = static_cast<QWheelEvent*>(e);
                    QWheelEvent copy(area->viewport()->mapFromGlobal(w->globalPosition()), w->globalPosition(), w->pixelDelta(),
                                     w->angleDelta(), w->buttons(), w->modifiers(), w->phase(), w->inverted(), w->source());
                    QApplication::sendEvent(area->viewport(), &copy);
                }
                return true;
            }
        }
        return QObject::eventFilter(o, e);
    }
};


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
    c.grid = dark ? QColor(0xff, 0xff, 0xff, 22) : QColor(0x00, 0x00, 0x00, 24);
    c.gridAxis = dark ? QColor(0xff, 0xff, 0xff, 90) : QColor(0x00, 0x00, 0x00, 90);
    c.gizmoX = QColor(0xf0, 0x55, 0x5a);
    c.gizmoY = QColor(0x5c, 0xd0, 0x6e);
    c.gizmoRotate = QColor(0x4f, 0x9c, 0xff);
    c.key = QColor(0xf2, 0xc1, 0x3d);
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
    static WheelNeedsFocus* wheel = nullptr;   // once per application
    if (!wheel && qApp) {
        wheel = new WheelNeedsFocus(qApp);
        qApp->installEventFilter(wheel);
    }
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

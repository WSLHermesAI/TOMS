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
bool f_IsValueField(QObject* l_pO) {
    // A scroll bar is a slider too, but it is how a panel scrolls: it always takes the wheel (a scroll area hands it
    // the wheel; treating it as a field would hand the wheel back, round and round).
    if (qobject_cast<QScrollBar*>(l_pO)) return false;
    return qobject_cast<QAbstractSpinBox*>(l_pO) || qobject_cast<QComboBox*>(l_pO) || qobject_cast<QAbstractSlider*>(l_pO);
}
// The field the wheel is over: the widget itself, or the field whose line edit it is (a spin box's, an editable combo
// box's). Not further up: an open combo box's list is a popup whose parents lead to the combo box, and it must scroll.
QWidget* f_ValueField(QObject* l_pO) {
    if (f_IsValueField(l_pO)) return static_cast<QWidget*>(l_pO);
    auto* l_pW = qobject_cast<QWidget*>(l_pO);
    if (l_pW && !l_pW->isWindow() && f_IsValueField(l_pW->parentWidget())) return l_pW->parentWidget();
    return nullptr;
}

class WheelNeedsFocus : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* l_pO, QEvent* l_pE) override {
        if (l_pE->type() == QEvent::Polish) {
            // Qt gives these fields focus on a wheel turn (Qt::WheelFocus), before any filter sees it: click or Tab only.
            if (QWidget* l_pF = f_ValueField(l_pO); l_pF == l_pO && (l_pF->focusPolicy() & Qt::WheelFocus) == Qt::WheelFocus)
                l_pF->setFocusPolicy(Qt::StrongFocus);
        } else if (l_pE->type() == QEvent::Wheel) {
            QWidget* l_pF = f_ValueField(l_pO);
            if (l_pF && !l_pF->hasFocus() && !(QApplication::focusWidget() && l_pF->isAncestorOf(QApplication::focusWidget()))) {
                // The panel the field sits in scrolls: the nearest scroll area around it (a Properties dock), through
                // its viewport. A field in no scroll area: the wheel does nothing.
                QAbstractScrollArea* l_pArea = nullptr;
                for (QWidget* l_pUp = l_pF->parentWidget(); l_pUp && !l_pArea; l_pUp = l_pUp->isWindow() ? nullptr : l_pUp->parentWidget())
                    l_pArea = qobject_cast<QAbstractScrollArea*>(l_pUp);
                if (l_pArea && l_pArea->viewport()) {
                    auto* l_pW = static_cast<QWheelEvent*>(l_pE);
                    QWheelEvent l_oCopy(l_pArea->viewport()->mapFromGlobal(l_pW->globalPosition()), l_pW->globalPosition(), l_pW->pixelDelta(),
                                     l_pW->angleDelta(), l_pW->buttons(), l_pW->modifiers(), l_pW->phase(), l_pW->inverted(), l_pW->source());
                    QApplication::sendEvent(l_pArea->viewport(), &l_oCopy);
                }
                return true;
            }
        }
        return QObject::eventFilter(l_pO, l_pE);
    }
};


Mode g_eMode = Mode::Dark;

Colors f_MakeColors(Mode l_eM)
{
    Colors l_oC;
    const bool l_bDark = l_eM == Mode::Dark;
    l_oC.m_oCanvasBackground = l_bDark ? QColor(0x1b, 0x1c, 0x1f) : QColor(0xd4, 0xd7, 0xdc);
    // The checkerboard stays mid-grey in both themes: sprites must read the same everywhere.
    l_oC.m_oCheckerLight = l_bDark ? QColor(0x4a, 0x4c, 0x50) : QColor(0xfa, 0xfa, 0xfa);
    l_oC.m_oCheckerDark = l_bDark ? QColor(0x3a, 0x3c, 0x40) : QColor(0xe4, 0xe6, 0xe9);
    l_oC.m_oPageBorder = l_bDark ? QColor(0x70, 0x74, 0x7a) : QColor(0x8a, 0x8f, 0x96);
    l_oC.m_oImageOutline = l_bDark ? QColor(0x4f, 0x9c, 0xff, 170) : QColor(0x1f, 0x6f, 0xe0, 170);
    l_oC.m_oChildOutline = l_bDark ? QColor(0xff, 0xa8, 0x3a, 210) : QColor(0xd9, 0x7a, 0x00, 220);
    l_oC.m_oAliasOutline = QColor(0xb0, 0x7c, 0xff, 200);
    l_oC.m_oSelection = l_bDark ? QColor(0x3d, 0xd6, 0xff) : QColor(0x00, 0x8c, 0xd6);
    l_oC.m_oHover = l_bDark ? QColor(0xff, 0xff, 0xff, 40) : QColor(0x00, 0x00, 0x00, 28);
    l_oC.m_oPin = QColor(0xf0, 0x4a, 0x5a);
    l_oC.m_oPivot = QColor(0x50, 0xe0, 0x7a);
    l_oC.m_oSlice = QColor(0xff, 0x5c, 0xc8);
    l_oC.m_oLabel = QColor(0xf2, 0xf2, 0xf2);
    l_oC.m_oLabelBackground = QColor(0x10, 0x10, 0x12, 200);
    l_oC.m_oGrid = l_bDark ? QColor(0xff, 0xff, 0xff, 22) : QColor(0x00, 0x00, 0x00, 24);
    l_oC.m_oGridAxis = l_bDark ? QColor(0xff, 0xff, 0xff, 90) : QColor(0x00, 0x00, 0x00, 90);
    l_oC.m_oGizmoX = QColor(0xf0, 0x55, 0x5a);
    l_oC.m_oGizmoY = QColor(0x5c, 0xd0, 0x6e);
    l_oC.m_oGizmoRotate = QColor(0x4f, 0x9c, 0xff);
    l_oC.m_oKey = QColor(0xf2, 0xc1, 0x3d);
    return l_oC;
}

Colors g_oColors = f_MakeColors(Mode::Dark);

QPalette f_DarkPalette()
{
    QPalette l_oP;
    const QColor l_oWindow(0x2b, 0x2d, 0x30), l_oBase(0x1f, 0x20, 0x23), l_oText(0xdf, 0xe1, 0xe5);
    const QColor l_oDisabled(0x7a, 0x7d, 0x82), l_oHighlight(0x2f, 0x6f, 0xd6);
    l_oP.setColor(QPalette::Window, l_oWindow);
    l_oP.setColor(QPalette::WindowText, l_oText);
    l_oP.setColor(QPalette::Base, l_oBase);
    l_oP.setColor(QPalette::AlternateBase, QColor(0x26, 0x28, 0x2b));
    l_oP.setColor(QPalette::ToolTipBase, QColor(0x3a, 0x3d, 0x41));
    l_oP.setColor(QPalette::ToolTipText, l_oText);
    l_oP.setColor(QPalette::PlaceholderText, l_oDisabled);
    l_oP.setColor(QPalette::Text, l_oText);
    l_oP.setColor(QPalette::Button, QColor(0x35, 0x37, 0x3b));
    l_oP.setColor(QPalette::ButtonText, l_oText);
    l_oP.setColor(QPalette::BrightText, QColor(0xff, 0x55, 0x55));
    l_oP.setColor(QPalette::Light, QColor(0x45, 0x48, 0x4c));
    l_oP.setColor(QPalette::Midlight, QColor(0x3a, 0x3c, 0x40));
    l_oP.setColor(QPalette::Mid, QColor(0x25, 0x27, 0x2a));
    l_oP.setColor(QPalette::Dark, QColor(0x1a, 0x1b, 0x1d));
    l_oP.setColor(QPalette::Shadow, QColor(0x0e, 0x0e, 0x10));
    l_oP.setColor(QPalette::Highlight, l_oHighlight);
    l_oP.setColor(QPalette::HighlightedText, Qt::white);
    l_oP.setColor(QPalette::Link, QColor(0x6c, 0xa8, 0xff));
    l_oP.setColor(QPalette::Disabled, QPalette::Text, l_oDisabled);
    l_oP.setColor(QPalette::Disabled, QPalette::WindowText, l_oDisabled);
    l_oP.setColor(QPalette::Disabled, QPalette::ButtonText, l_oDisabled);
    l_oP.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x45, 0x48, 0x4c));
    return l_oP;
}

}  // namespace

void f_Apply(Mode l_eM)
{
    static WheelNeedsFocus* l_pWheel = nullptr;   // once per application
    if (!l_pWheel && qApp) {
        l_pWheel = new WheelNeedsFocus(qApp);
        qApp->installEventFilter(l_pWheel);
    }
    g_eMode = l_eM;
    g_oColors = f_MakeColors(l_eM);
    // Fusion draws consistently with any palette; the native Windows style ignores dark palettes.
    if (QStyle* l_pFusion = QStyleFactory::create(QStringLiteral("Fusion")))
        QApplication::setStyle(l_pFusion);
    QPalette l_oP = l_eM == Mode::Dark ? f_DarkPalette() : QApplication::style()->standardPalette();
    if (l_eM == Mode::Light) l_oP.setColor(QPalette::Highlight, QColor(0x2f, 0x6f, 0xd6));
    QApplication::setPalette(l_oP);
}

Mode f_Mode() { return g_eMode; }

const Colors& f_Colors() { return g_oColors; }

}  // namespace Theme

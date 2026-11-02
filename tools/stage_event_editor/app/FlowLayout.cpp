// FlowLayout.cpp -- see FlowLayout.h.
#include "FlowLayout.h"

#include <QWidget>

FlowLayout::FlowLayout(QWidget* l_pParent, int l_iSpacing) : QLayout(l_pParent), m_iSpacing(l_iSpacing) { setContentsMargins(0, 0, 0, 0); }

FlowLayout::~FlowLayout() {
    while (QLayoutItem* l_pItem = takeAt(0)) delete l_pItem;
}

QSize FlowLayout::minimumSize() const {
    QSize l_oSize;
    for (const QLayoutItem* l_pItem : m_oItems) l_oSize = l_oSize.expandedTo(l_pItem->minimumSize());
    const QMargins l_oM = contentsMargins();
    return l_oSize + QSize(l_oM.left() + l_oM.right(), l_oM.top() + l_oM.bottom());
}

int FlowLayout::f_DoLayout(const QRect& l_oRect, bool l_bTestOnly) const {
    const QMargins l_oM = contentsMargins();
    const QRect l_oArea = l_oRect.adjusted(l_oM.left(), l_oM.top(), -l_oM.right(), -l_oM.bottom());
    int l_iX = l_oArea.x(), l_iY = l_oArea.y(), l_iLineHeight = 0;
    for (QLayoutItem* l_pItem : m_oItems) {
        if (l_pItem->widget() && !l_pItem->widget()->isVisibleTo(l_pItem->widget()->parentWidget())) continue;
        const QSize l_oHint = l_pItem->sizeHint();
        int l_iNextX = l_iX + l_oHint.width() + m_iSpacing;
        if (l_iNextX - m_iSpacing > l_oArea.right() + 1 && l_iLineHeight > 0) {
            l_iX = l_oArea.x();
            l_iY += l_iLineHeight + m_iSpacing;
            l_iNextX = l_iX + l_oHint.width() + m_iSpacing;
            l_iLineHeight = 0;
        }
        if (!l_bTestOnly) l_pItem->setGeometry(QRect(QPoint(l_iX, l_iY), l_oHint));
        l_iX = l_iNextX;
        l_iLineHeight = std::max(l_iLineHeight, l_oHint.height());
    }
    return l_iY + l_iLineHeight - l_oRect.y() + l_oM.bottom();
}

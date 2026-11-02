// FlowLayout.h -- widgets left to right, wrapping to the next row (the palette's icons, the inspector's chips).
// After Qt's "Flow Layout" example.
#pragma once

#include <QLayout>
#include <QList>

class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget* l_pParent = nullptr, int l_iSpacing = 4);
    ~FlowLayout() override;

    void addItem(QLayoutItem* l_pItem) override { m_oItems.append(l_pItem); }
    int count() const override { return int(m_oItems.size()); }
    QLayoutItem* itemAt(int l_iI) const override { return m_oItems.value(l_iI); }
    QLayoutItem* takeAt(int l_iI) override { return l_iI >= 0 && l_iI < m_oItems.size() ? m_oItems.takeAt(l_iI) : nullptr; }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int l_iWidth) const override { return f_DoLayout(QRect(0, 0, l_iWidth, 0), true); }
    void setGeometry(const QRect& l_oR) override { QLayout::setGeometry(l_oR); f_DoLayout(l_oR, false); }
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override;

private:
    int f_DoLayout(const QRect& l_oR, bool l_bTestOnly) const;
    QList<QLayoutItem*> m_oItems;
    int m_iSpacing;
};

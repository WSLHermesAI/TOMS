// FlowLayout.h -- widgets left to right, wrapping to the next row (the palette's icons, the inspector's chips).
// After Qt's "Flow Layout" example.
#pragma once

#include <QLayout>
#include <QList>

class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget* parent = nullptr, int spacing = 4);
    ~FlowLayout() override;

    void addItem(QLayoutItem* item) override { m_items.append(item); }
    int count() const override { return int(m_items.size()); }
    QLayoutItem* itemAt(int i) const override { return m_items.value(i); }
    QLayoutItem* takeAt(int i) override { return i >= 0 && i < m_items.size() ? m_items.takeAt(i) : nullptr; }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return doLayout(QRect(0, 0, width, 0), true); }
    void setGeometry(const QRect& r) override { QLayout::setGeometry(r); doLayout(r, false); }
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override;

private:
    int doLayout(const QRect& r, bool testOnly) const;
    QList<QLayoutItem*> m_items;
    int m_spacing;
};

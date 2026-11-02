#pragma once

#include <QImage>
#include <QRect>
#include <QVector>
#include <QWidget>

// The built page (the combined picture) of font_editor: drawn to fit, every glyph outlined, the
// selected one highlighted. A click on a glyph reports its index (glyphClicked).
class PageView : public QWidget {
    Q_OBJECT
public:
    explicit PageView(QWidget* l_pParent = nullptr);

    // rects: one per glyph, in page pixels (an empty rect for a glyph with no picture, a space).
    void f_SetPage(const QImage& l_oPage, const QVector<QRect>& l_oRects);
    void f_SetHighlight(int l_iGlyph);              // -1: none
    int f_Highlight() const { return m_iHighlight; }
    // The glyph under a point in page pixels, or -1.
    int f_GlyphAt(const QPoint& l_oPagePos) const;

signals:
    void f_GlyphClicked(int l_iGlyph);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* l_pE) override;
    QSize sizeHint() const override { return {480, 220}; }

private:
    QRectF f_Target() const;                     // where the page is drawn in the widget

    QImage m_oPage;
    QVector<QRect> m_oRects;
    int m_iHighlight = -1;
};

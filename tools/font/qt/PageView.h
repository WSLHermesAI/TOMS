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
    explicit PageView(QWidget* parent = nullptr);

    // rects: one per glyph, in page pixels (an empty rect for a glyph with no picture, a space).
    void setPage(const QImage& page, const QVector<QRect>& rects);
    void setHighlight(int glyph);              // -1: none
    int highlight() const { return highlight_; }
    // The glyph under a point in page pixels, or -1.
    int glyphAt(const QPoint& pagePos) const;

signals:
    void glyphClicked(int glyph);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    QSize sizeHint() const override { return {480, 220}; }

private:
    QRectF target() const;                     // where the page is drawn in the widget

    QImage page_;
    QVector<QRect> rects_;
    int highlight_ = -1;
};

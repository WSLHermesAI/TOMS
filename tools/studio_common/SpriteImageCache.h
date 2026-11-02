#pragma once

#include <QHash>
#include <QImage>
#include <QRect>
#include <QVector>

// Pixels of sprites packed into atlas pages, for editors that draw sprites with QPainter
// (tools/studio_common). Pages are kept premultiplied; cut-outs and tinted copies are cached.
class SpriteImageCache
{
public:
    void f_SetPages(const QVector<QImage>& l_oPages);   // converted to ARGB32_Premultiplied
    void f_Clear();
    const QVector<QImage>& f_Pages() const { return m_oPages; }

    // The pixels of `rect` on `page` (empty when out of range).
    QImage f_Cut(int l_iPage, const QRect& l_oRect) const;
    // The same, every pixel multiplied by rgba (0..1) -- what a sprite shader's texture * tint
    // gives, for straight-alpha blending with the result's (premultiplied) alpha. White = cut().
    QImage f_Tinted(int l_iPage, const QRect& l_oRect, const float l_arrRgba[4]) const;

    // A square-fitting copy for list icons: never upscaled by more than `size`, pixel art kept crisp.
    static QImage f_Thumbnail(const QImage& l_oImage, int l_iSize);

private:
    QVector<QImage> m_oPages;
    mutable QHash<QString, QImage> m_oCut;
    mutable QHash<QString, QImage> m_oTinted;
};

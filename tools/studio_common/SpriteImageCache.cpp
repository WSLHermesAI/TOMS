#include "SpriteImageCache.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace {

QString f_RectKey(int l_iPage, const QRect& l_oR)
{
    return QStringLiteral("%1:%2,%3,%4,%5").arg(l_iPage).arg(l_oR.x()).arg(l_oR.y()).arg(l_oR.width()).arg(l_oR.height());
}

int f_To255(float l_fV) { return int(std::lround(std::clamp(l_fV, 0.0f, 1.0f) * 255.0f)); }

}  // namespace

void SpriteImageCache::f_SetPages(const QVector<QImage>& l_oPages)
{
    f_Clear();
    m_oPages.clear();
    for (const QImage& l_oP : l_oPages) m_oPages.push_back(l_oP.convertToFormat(QImage::Format_ARGB32_Premultiplied));
}

void SpriteImageCache::f_Clear()
{
    m_oCut.clear();
    m_oTinted.clear();
}

QImage SpriteImageCache::f_Cut(int l_iPage, const QRect& l_oRect) const
{
    if (l_iPage < 0 || l_iPage >= m_oPages.size() || l_oRect.isEmpty()) return QImage();
    const QString l_oKey = f_RectKey(l_iPage, l_oRect);
    auto l_oIt = m_oCut.find(l_oKey);
    if (l_oIt == m_oCut.end()) l_oIt = m_oCut.insert(l_oKey, m_oPages[l_iPage].copy(l_oRect));
    return *l_oIt;
}

QImage SpriteImageCache::f_Tinted(int l_iPage, const QRect& l_oRect, const float l_arrRgba[4]) const
{
    const int l_iR = f_To255(l_arrRgba[0]), l_iG = f_To255(l_arrRgba[1]), l_iB = f_To255(l_arrRgba[2]), l_iA = f_To255(l_arrRgba[3]);
    if (l_iR == 255 && l_iG == 255 && l_iB == 255 && l_iA == 255) return f_Cut(l_iPage, l_oRect);
    const QString l_oKey = f_RectKey(l_iPage, l_oRect) + QStringLiteral("#%1.%2.%3.%4").arg(l_iR).arg(l_iG).arg(l_iB).arg(l_iA);
    auto l_oIt = m_oTinted.find(l_oKey);
    if (l_oIt != m_oTinted.end()) return *l_oIt;
    QImage l_oImg = f_Cut(l_iPage, l_oRect);
    if (l_oImg.isNull()) return l_oImg;
    l_oImg.detach();
    // Premultiplied pixels: colour channels scale by tint.rgb * tint.a, alpha by tint.a.
    const int l_iMr = l_iR * l_iA, l_iMg = l_iG * l_iA, l_iMb = l_iB * l_iA;   // 0..65025
    for (int l_iY = 0; l_iY < l_oImg.height(); l_iY++) {
        QRgb* l_pLine = reinterpret_cast<QRgb*>(l_oImg.scanLine(l_iY));
        for (int l_iX = 0; l_iX < l_oImg.width(); l_iX++) {
            const QRgb l_uP = l_pLine[l_iX];
            l_pLine[l_iX] = qRgba(qRed(l_uP) * l_iMr / 65025, qGreen(l_uP) * l_iMg / 65025, qBlue(l_uP) * l_iMb / 65025, qAlpha(l_uP) * l_iA / 255);
        }
    }
    if (m_oTinted.size() > 512) m_oTinted.clear();   // colour animations make many; keep it bounded
    m_oTinted.insert(l_oKey, l_oImg);
    return l_oImg;
}

QImage SpriteImageCache::f_Thumbnail(const QImage& l_oImage, int l_iSize)
{
    if (l_oImage.isNull()) return l_oImage;
    const QSize l_oFit = l_oImage.size().scaled(l_iSize, l_iSize, Qt::KeepAspectRatio);
    const bool l_bShrink = l_oFit.width() < l_oImage.width();
    return l_oImage.scaled(l_oFit, Qt::KeepAspectRatio, l_bShrink ? Qt::SmoothTransformation : Qt::FastTransformation);
}

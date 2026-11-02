// SpriteBank.cpp -- see SpriteBank.h.
#include "SpriteBank.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cstdio>

namespace {
std::string f_ReadAll(const QString& l_oPath) {
    QFile l_oF(l_oPath);
    return l_oF.open(QIODevice::ReadOnly) ? l_oF.readAll().toStdString() : std::string();
}
}  // namespace

void SpriteBank::f_Load(const QString& l_oTomsRoot) {
    m_oRoot = l_oTomsRoot;
    m_oAtlasPath = l_oTomsRoot + QStringLiteral("/assets/media/atlas/game.atlas");
    m_oAtlas = toms::AtlasFile();
    m_oPages.clear();
    m_oCut.clear();
    m_oTinted.clear();
    m_mapAnims.clear();
    std::string l_strErr;
    if (!toms::f_ParseAtlas(f_ReadAll(m_oAtlasPath), m_oAtlas, &l_strErr)) {
        std::fprintf(stderr, "[stage] cannot read %s: %s\n", qPrintable(m_oAtlasPath), l_strErr.c_str());
        return;
    }
    const QDir l_oDir = QFileInfo(m_oAtlasPath).absoluteDir();
    for (toms::AtlasPage& l_oPg : m_oAtlas.m_vPages) {
        QImage l_oImg(l_oDir.filePath(QString::fromStdString(l_oPg.m_strFile)));
        l_oImg = l_oImg.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        l_oPg.m_iW = l_oImg.width();
        l_oPg.m_iH = l_oImg.height();
        m_oPages.push_back(l_oImg);
    }
    m_oAtlas.f_ComputeUVs();
}

QImage SpriteBank::f_Image(const QString& l_oSprite) const {
    auto l_oIt = m_oCut.find(l_oSprite);
    if (l_oIt != m_oCut.end()) return *l_oIt;
    QImage l_oOut;
    if (const toms::AtlasRegion* l_pR = m_oAtlas.f_Find(l_oSprite.toStdString()); l_pR && l_pR->m_iPage >= 0 && l_pR->m_iPage < m_oPages.size()) {
        const QImage& l_oPage = m_oPages[l_pR->m_iPage];
        l_oOut = l_pR->m_bRotated ? l_oPage.copy(l_pR->m_iX, l_pR->m_iY, l_pR->m_iH, l_pR->m_iW).transformed(QTransform().rotate(-90)) : l_oPage.copy(l_pR->m_iX, l_pR->m_iY, l_pR->m_iW, l_pR->m_iH);
    }
    m_oCut.insert(l_oSprite, l_oOut);
    return l_oOut;
}

QImage SpriteBank::f_Tinted(const QString& l_oSprite, const float l_arrRgba[4]) const {
    const QString l_oKey = l_oSprite + QStringLiteral("|%1,%2,%3,%4").arg(l_arrRgba[0]).arg(l_arrRgba[1]).arg(l_arrRgba[2]).arg(l_arrRgba[3]);
    auto l_oIt = m_oTinted.find(l_oKey);
    if (l_oIt != m_oTinted.end()) return *l_oIt;
    QImage l_oImg = f_Image(l_oSprite).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int l_iY = 0; l_iY < l_oImg.height(); l_iY++) {   // premultiplied: every channel times its factor (alpha too)
        QRgb* l_pRow = reinterpret_cast<QRgb*>(l_oImg.scanLine(l_iY));
        for (int l_iX = 0; l_iX < l_oImg.width(); l_iX++) {
            const QRgb l_uP = l_pRow[l_iX];
            l_pRow[l_iX] = qRgba(int(qRed(l_uP) * l_arrRgba[0] * l_arrRgba[3] + 0.5f), int(qGreen(l_uP) * l_arrRgba[1] * l_arrRgba[3] + 0.5f),
                           int(qBlue(l_uP) * l_arrRgba[2] * l_arrRgba[3] + 0.5f), int(qAlpha(l_uP) * l_arrRgba[3] + 0.5f));
        }
    }
    m_oTinted.insert(l_oKey, l_oImg);
    return l_oImg;
}

const SpriteBank::Anim* SpriteBank::f_Anim(const std::string& l_strFile) {
    std::unique_ptr<Anim>& l_upSlot = m_mapAnims[l_strFile];
    if (l_upSlot) return l_upSlot->m_bOk ? l_upSlot.get() : nullptr;
    l_upSlot = std::make_unique<Anim>();
    const QString l_oPath = m_oRoot + QStringLiteral("/assets/media/") + QString::fromStdString(l_strFile);
    l_upSlot->m_strPath = l_oPath.toStdString();
    std::string l_strErr;
    if (!toms::anim::f_ParseAnim(f_ReadAll(l_oPath), l_upSlot->m_oFile, &l_strErr)) {   // a bad file is remembered as bad (logged once)
        std::fprintf(stderr, "[stage] object clip file %s: %s\n", qPrintable(l_oPath), l_strErr.empty() ? "cannot read" : l_strErr.c_str());
        return nullptr;
    }
    const QDir l_oDir = QFileInfo(l_oPath).absoluteDir();
    std::vector<toms::anim::AtlasRef> l_vRefs = l_upSlot->m_oFile.m_vAtlases;
    if (l_vRefs.empty()) l_vRefs.push_back({"game", "../atlas/game.atlas"});   // no atlas named: the game's own
    for (const toms::anim::AtlasRef& l_oRef : l_vRefs) {
        const QString l_oAp = QDir::cleanPath(l_oDir.filePath(QString::fromStdString(l_oRef.m_strPath)));
        auto l_spAtlas = std::make_shared<toms::AtlasFile>();
        if (!toms::f_ParseAtlas(f_ReadAll(l_oAp), *l_spAtlas, &l_strErr)) {
            std::fprintf(stderr, "[stage] cannot load the atlas %s\n", qPrintable(l_oAp));
            continue;
        }
        l_upSlot->m_vAtlases.emplace_back(l_oRef.m_strId, l_oAp.toStdString(), l_spAtlas);
    }
    l_upSlot->m_bOk = true;
    return l_upSlot.get();
}

const toms::anim::Clip* SpriteBank::f_Clip(const std::string& l_strRef, const Anim** l_pOwner) {
    const size_t l_nHash = l_strRef.find('#');
    if (l_nHash == std::string::npos) return nullptr;
    const Anim* l_pA = f_Anim(l_strRef.substr(0, l_nHash));
    if (!l_pA) return nullptr;
    if (l_pOwner) *l_pOwner = l_pA;
    return l_pA->m_oFile.f_Find(l_strRef.substr(l_nHash + 1));
}

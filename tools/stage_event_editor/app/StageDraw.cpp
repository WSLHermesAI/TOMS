// StageDraw.cpp -- see StageDraw.h.
#include "StageDraw.h"

#include "SpriteBank.h"

#include <QPainter>

#include <algorithm>

using stage::Json;

namespace StageDraw {

QString f_SpriteName(const Json& l_jV) {
    if (!l_jV.is_string()) return QString();
    QString l_oS = QString::fromStdString(l_jV.get<std::string>());
    if (l_oS.endsWith(QStringLiteral(".png"))) l_oS.chop(4);
    return l_oS;
}

QString f_LookSprite(const stage::Project& l_oP, const SpriteBank& l_oBank, const std::string& l_strSem, const Json& l_jLook) {
    const QString l_oOwn = l_jLook.is_object() && l_jLook.contains("sprite") ? f_SpriteName(l_jLook["sprite"]) : QString();
    if (!l_oOwn.isEmpty() && l_oBank.f_Has(l_oOwn)) return l_oOwn;
    return QString::fromStdString(l_oP.f_SpriteOf(l_strSem));
}

bool f_LookTint(const Json& l_jLook, float l_arrRgba[4]) {
    l_arrRgba[0] = l_arrRgba[1] = l_arrRgba[2] = l_arrRgba[3] = 1;
    if (!l_jLook.is_object() || !l_jLook.contains("tint") || !l_jLook["tint"].is_array()) return false;
    const Json& l_jT = l_jLook["tint"];
    for (size_t l_nI = 0; l_nI < 4 && l_nI < l_jT.size(); l_nI++)
        if (l_jT[l_nI].is_number()) l_arrRgba[l_nI] = std::clamp(l_jT[l_nI].get<float>(), 0.0f, 1.0f);
    return true;
}

bool f_GateWash(const stage::Project& l_oP, const std::string& l_strSem, float l_arrRgba[4]) {
    if (l_strSem.rfind("gate:", 0) != 0) return false;
    if (const Json* l_pIt = l_oP.f_TypeRec("item:" + l_strSem.substr(5)); l_pIt && l_pIt->contains("door") && (*l_pIt)["door"].is_string()
        && !(*l_pIt)["door"].get<std::string>().empty())
        return false;   // its own door picture: no wash
    l_arrRgba[0] = 0.6f; l_arrRgba[1] = 0.3f; l_arrRgba[2] = 0.9f; l_arrRgba[3] = 0.6f;
    if (const Json* l_pIt = l_oP.f_TypeRec("item:" + l_strSem.substr(5)); l_pIt && l_pIt->contains("tint") && (*l_pIt)["tint"].is_array())
        for (size_t l_nI = 0; l_nI < 3 && l_nI < (*l_pIt)["tint"].size(); l_nI++) l_arrRgba[l_nI] = (*l_pIt)["tint"][l_nI].get<float>();
    return true;
}

QColor f_KindColor(const stage::Project& l_oP, const std::string& l_strKind) {
    if (l_oP.m_jKinds.contains(l_strKind) && l_oP.m_jKinds[l_strKind].is_object() && l_oP.m_jKinds[l_strKind].contains("color") && l_oP.m_jKinds[l_strKind]["color"].is_string())
        return QColor(QString::fromStdString(l_oP.m_jKinds[l_strKind]["color"].get<std::string>()));
    static const std::map<std::string, const char*> l_mapCss = {{"relic", "#e0b44c"}, {"whisper", "#a98bff"}, {"cache", "#3fc2b5"},
        {"trap", "#ff6b6b"}, {"rescue", "#7bd86a"}, {"merchant_echo", "#ff8fc7"}, {"memory_shard", "#74c6ff"}};
    auto l_itIt = l_mapCss.find(l_strKind);
    return QColor(l_itIt != l_mapCss.end() ? l_itIt->second : "#888888");
}

void f_DrawSem(QPainter& l_oG, const stage::Project& l_oP, const SpriteBank& l_oBank, const std::string& l_strSem, const QRectF& l_oR,
             const Json& l_jLook, qreal l_dOpacity) {
    if (l_strSem.empty()) return;
    l_oG.save();
    l_oG.setOpacity(l_oG.opacity() * l_dOpacity);
    if (l_strSem.rfind("event:", 0) == 0) {   // the game's story plate: gold with a slate inset (game_scene_draw.cpp)
        l_oG.fillRect(l_oR, QColor(219, 184, 92, 235));
        const qreal l_dI = std::min(l_oR.width(), l_oR.height()) * 0.22;
        l_oG.fillRect(l_oR.adjusted(l_dI, l_dI, -l_dI, -l_dI), QColor(71, 87, 117, 242));
        l_oG.restore();
        return;
    }
    const QString l_oSp = f_LookSprite(l_oP, l_oBank, l_strSem, l_jLook);
    float l_arrTint[4];
    const bool l_bTinted = f_LookTint(l_jLook, l_arrTint);
    const QImage l_oImg = l_oSp.isEmpty() ? QImage() : l_bTinted ? l_oBank.f_Tinted(l_oSp, l_arrTint) : l_oBank.f_Image(l_oSp);
    if (!l_oImg.isNull()) {
        l_oG.drawImage(l_oR, l_oImg);
        float l_arrWash[4];
        if (f_GateWash(l_oP, l_strSem, l_arrWash)) l_oG.fillRect(l_oR, QColor::fromRgbF(l_arrWash[0], l_arrWash[1], l_arrWash[2], l_arrWash[3]));
        l_oG.restore();
        return;
    }
    static const std::map<std::string, const char*> l_mapFallback = {{"wall", "#5b5b66"}, {"floor", "#2b2b33"}, {"stairs", "#e0a83a"},
        {"door", "#c98b2b"}, {"key", "#d8c84a"}, {"gate", "#a070e0"}, {"player", "#3aa0ff"}, {"npc", "#46c46a"},
        {"monster", "#e0473a"}, {"item", "#b46bff"}};
    const size_t l_nColon = l_strSem.find(':');
    const std::string l_strCat = l_nColon != std::string::npos ? l_strSem.substr(0, l_nColon) : l_strSem.rfind("stairs", 0) == 0 ? "stairs"
                          : l_strSem == "player_start" ? "player" : l_strSem;
    auto l_itF = l_mapFallback.find(l_strCat);
    l_oG.fillRect(l_oR.adjusted(2, 2, -2, -2), QColor(l_itF != l_mapFallback.end() ? l_itF->second : "#888888"));
    QFont l_oFont(QStringLiteral("Consolas"));
    l_oFont.setPixelSize(std::max(6, int(l_oR.height() * 0.42)));
    l_oG.setFont(l_oFont);
    l_oG.setPen(Qt::white);
    const std::string l_strName = l_nColon != std::string::npos ? l_strSem.substr(l_nColon + 1) : l_strSem;
    l_oG.drawText(l_oR, Qt::AlignCenter, QString::fromStdString(l_strName.substr(0, 1)).toUpper());
    l_oG.restore();
}

QImage f_Icon(const stage::Project& l_oP, const SpriteBank& l_oBank, const std::string& l_strSem, int l_iSize) {
    QImage l_oImg(l_iSize, l_iSize, QImage::Format_ARGB32_Premultiplied);
    l_oImg.fill(Qt::transparent);
    QPainter l_oG(&l_oImg);
    const QRectF l_oR(0, 0, l_iSize, l_iSize);
    if (stage::Project::f_IsEntity(l_strSem) && l_strSem.rfind("event:", 0) != 0) f_DrawSem(l_oG, l_oP, l_oBank, "floor", l_oR);
    f_DrawSem(l_oG, l_oP, l_oBank, l_strSem, l_oR, l_oP.f_TypeLook(l_strSem));
    return l_oImg;
}

}  // namespace StageDraw

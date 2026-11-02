// SpriteBank.h -- the game's sprites and object animations for the stage editor (CPU side).
//
// Sprites come from the game's packed atlas (assets/media/atlas/game.atlas + its pages), as the game draws them; the
// canvas uploads the same atlas to the game renderer (GameTextures). Animations are the .anim files a look names
// ("anim/<file>.anim#<clip>", under assets/media) and the monsters' idle clips (anim/monster_idle.anim, "idle_<id>"),
// parsed once with the atlases they use. QPainter images (palette icons, the dialogs' previews, the canvas without a
// GPU) are cut from the atlas pages.
#pragma once

#include "anim_clip.h"
#include "atlas_file.h"

#include <QColor>
#include <QHash>
#include <QImage>
#include <QString>
#include <QVector>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace stage { class Project; }

class SpriteBank {
public:
    struct Anim {
        bool m_bOk = false;
        std::string m_strPath;                                  // absolute .anim path
        toms::anim::AnimFile m_oFile;
        // the atlases it names: (id, absolute path, parsed)
        std::vector<std::tuple<std::string, std::string, std::shared_ptr<toms::AtlasFile>>> m_vAtlases;
    };

    void f_Load(const QString& l_oTomsRoot);
    bool f_Ok() const { return !m_oPages.isEmpty(); }
    QString f_AtlasPath() const { return m_oAtlasPath; }
    const toms::AtlasFile& f_Atlas() const { return m_oAtlas; }

    bool f_Has(const QString& l_oSprite) const { return m_oAtlas.f_Find(l_oSprite.toStdString()) != nullptr; }
    QImage f_Image(const QString& l_oSprite) const;                         // null when the atlas has no such sprite
    QImage f_Tinted(const QString& l_oSprite, const float l_arrRgba[4]) const;   // multiplied, as the game's sprite shader
    // An "anim/<file>.anim" (relative to assets/media), parsed on first use; nullptr when it cannot be read.
    const Anim* f_Anim(const std::string& l_strFile);
    const toms::anim::Clip* f_Clip(const std::string& l_strRef, const Anim** l_pOwner = nullptr);   // "anim/<file>.anim#<clip>"

private:
    QString m_oRoot, m_oAtlasPath;
    toms::AtlasFile m_oAtlas;
    QVector<QImage> m_oPages;
    mutable QHash<QString, QImage> m_oCut, m_oTinted;
    std::map<std::string, std::unique_ptr<Anim>> m_mapAnims;
};

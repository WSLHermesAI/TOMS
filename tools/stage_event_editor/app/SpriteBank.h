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
        bool ok = false;
        std::string path;                                  // absolute .anim path
        toms::anim::AnimFile file;
        // the atlases it names: (id, absolute path, parsed)
        std::vector<std::tuple<std::string, std::string, std::shared_ptr<toms::AtlasFile>>> atlases;
    };

    void load(const QString& tomsRoot);
    bool ok() const { return !m_pages.isEmpty(); }
    QString atlasPath() const { return m_atlasPath; }
    const toms::AtlasFile& atlas() const { return m_atlas; }

    bool has(const QString& sprite) const { return m_atlas.find(sprite.toStdString()) != nullptr; }
    QImage image(const QString& sprite) const;                         // null when the atlas has no such sprite
    QImage tinted(const QString& sprite, const float rgba[4]) const;   // multiplied, as the game's sprite shader
    // An "anim/<file>.anim" (relative to assets/media), parsed on first use; nullptr when it cannot be read.
    const Anim* anim(const std::string& file);
    const toms::anim::Clip* clip(const std::string& ref, const Anim** owner = nullptr);   // "anim/<file>.anim#<clip>"

private:
    QString m_root, m_atlasPath;
    toms::AtlasFile m_atlas;
    QVector<QImage> m_pages;
    mutable QHash<QString, QImage> m_cut, m_tinted;
    std::map<std::string, std::unique_ptr<Anim>> m_anims;
};

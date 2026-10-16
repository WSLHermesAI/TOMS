// SpriteBank.cpp -- see SpriteBank.h.
#include "SpriteBank.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cstdio>

namespace {
std::string readAll(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll().toStdString() : std::string();
}
}  // namespace

void SpriteBank::load(const QString& tomsRoot) {
    m_root = tomsRoot;
    m_atlasPath = tomsRoot + QStringLiteral("/assets/media/atlas/game.atlas");
    m_atlas = toms::AtlasFile();
    m_pages.clear();
    m_cut.clear();
    m_tinted.clear();
    m_anims.clear();
    std::string err;
    if (!toms::parseAtlas(readAll(m_atlasPath), m_atlas, &err)) {
        std::fprintf(stderr, "[stage] cannot read %s: %s\n", qPrintable(m_atlasPath), err.c_str());
        return;
    }
    const QDir dir = QFileInfo(m_atlasPath).absoluteDir();
    for (toms::AtlasPage& pg : m_atlas.pages) {
        QImage img(dir.filePath(QString::fromStdString(pg.file)));
        img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        pg.w = img.width();
        pg.h = img.height();
        m_pages.push_back(img);
    }
    m_atlas.computeUVs();
}

QImage SpriteBank::image(const QString& sprite) const {
    auto it = m_cut.find(sprite);
    if (it != m_cut.end()) return *it;
    QImage out;
    if (const toms::AtlasRegion* r = m_atlas.find(sprite.toStdString()); r && r->page >= 0 && r->page < m_pages.size()) {
        const QImage& page = m_pages[r->page];
        out = r->rotated ? page.copy(r->x, r->y, r->h, r->w).transformed(QTransform().rotate(-90)) : page.copy(r->x, r->y, r->w, r->h);
    }
    m_cut.insert(sprite, out);
    return out;
}

QImage SpriteBank::tinted(const QString& sprite, const float rgba[4]) const {
    const QString key = sprite + QStringLiteral("|%1,%2,%3,%4").arg(rgba[0]).arg(rgba[1]).arg(rgba[2]).arg(rgba[3]);
    auto it = m_tinted.find(key);
    if (it != m_tinted.end()) return *it;
    QImage img = image(sprite).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < img.height(); y++) {   // premultiplied: every channel times its factor (alpha too)
        QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); x++) {
            const QRgb p = row[x];
            row[x] = qRgba(int(qRed(p) * rgba[0] * rgba[3] + 0.5f), int(qGreen(p) * rgba[1] * rgba[3] + 0.5f),
                           int(qBlue(p) * rgba[2] * rgba[3] + 0.5f), int(qAlpha(p) * rgba[3] + 0.5f));
        }
    }
    m_tinted.insert(key, img);
    return img;
}

const SpriteBank::Anim* SpriteBank::anim(const std::string& file) {
    std::unique_ptr<Anim>& slot = m_anims[file];
    if (slot) return slot->ok ? slot.get() : nullptr;
    slot = std::make_unique<Anim>();
    const QString path = m_root + QStringLiteral("/assets/media/") + QString::fromStdString(file);
    slot->path = path.toStdString();
    std::string err;
    if (!toms::anim::parseAnim(readAll(path), slot->file, &err)) {   // a bad file is remembered as bad (logged once)
        std::fprintf(stderr, "[stage] object clip file %s: %s\n", qPrintable(path), err.empty() ? "cannot read" : err.c_str());
        return nullptr;
    }
    const QDir dir = QFileInfo(path).absoluteDir();
    std::vector<toms::anim::AtlasRef> refs = slot->file.atlases;
    if (refs.empty()) refs.push_back({"game", "../atlas/game.atlas"});   // no atlas named: the game's own
    for (const toms::anim::AtlasRef& ref : refs) {
        const QString ap = QDir::cleanPath(dir.filePath(QString::fromStdString(ref.path)));
        auto atlas = std::make_shared<toms::AtlasFile>();
        if (!toms::parseAtlas(readAll(ap), *atlas, &err)) {
            std::fprintf(stderr, "[stage] cannot load the atlas %s\n", qPrintable(ap));
            continue;
        }
        slot->atlases.emplace_back(ref.id, ap.toStdString(), atlas);
    }
    slot->ok = true;
    return slot.get();
}

const toms::anim::Clip* SpriteBank::clip(const std::string& ref, const Anim** owner) {
    const size_t hash = ref.find('#');
    if (hash == std::string::npos) return nullptr;
    const Anim* a = anim(ref.substr(0, hash));
    if (!a) return nullptr;
    if (owner) *owner = a;
    return a->file.find(ref.substr(hash + 1));
}

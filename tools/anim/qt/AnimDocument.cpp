#include "AnimDocument.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUndoStack>

#include <algorithm>
#include <climits>
#include <cmath>

using namespace animed;
using toms::anim::AnimFile;
using toms::anim::Clip;
using toms::anim::Ease;
using toms::anim::Node;

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }
std::string u8(const QString& s) { return s.toStdString(); }

bool sameState(const AnimState& a, const AnimState& b)
{
    return a.atlasesAbs == b.atlasesAbs && a.atlasIds == b.atlasIds && a.clip == b.clip && a.node == b.node &&
           toms::anim::animToJson(a.file) == toms::anim::animToJson(b.file);
}

// The interpolated channels a node animates already (has keys on). Key rows only ever add to
// these: a node that only switches sprites gets no position / rotation / scale / colour keys.
std::vector<Channel> animatedChannels(const Node& n)
{
    std::vector<Channel> out;
    for (Channel c : kInterpolated)
        if (keyCount(n, c) > 0) out.push_back(c);
    return out;
}

// channel >= 0: that one; < 0: the ones the node animates (position for a node without any).
std::vector<Channel> channelsFor(const Node& n, int channel)
{
    if (channel >= 0) return {Channel(channel)};
    std::vector<Channel> out = animatedChannels(n);
    if (out.empty()) out.push_back(Channel::Pos);
    return out;
}

QString uniqueClipName(const AnimFile& f, const QString& base)
{
    auto taken = [&](const QString& n) { return f.find(u8(n)) != nullptr; };
    const QString b = base.isEmpty() ? QStringLiteral("clip") : base;
    if (!taken(b)) return b;
    for (int i = 2;; i++)
        if (!taken(QStringLiteral("%1_%2").arg(b).arg(i))) return QStringLiteral("%1_%2").arg(b).arg(i);
}

}  // namespace

QString pathToString(const NodePath& p)
{
    QStringList parts;
    for (int i : p) parts << QString::number(i);
    return parts.join(QLatin1Char('/'));
}

NodePath pathFromString(const QString& s)
{
    NodePath p;
    if (s.isEmpty()) return p;
    for (const QString& part : s.split(QLatin1Char('/'))) p.push_back(part.toInt());
    return p;
}

// ---- AnimCommand --------------------------------------------------------------------------------

AnimCommand::AnimCommand(AnimDocument* doc, const QString& text, AnimState before, AnimState after, const QString& mergeKey)
    : QUndoCommand(text)
    , m_doc(doc)
    , m_before(std::move(before))
    , m_after(std::move(after))
    , m_mergeKey(mergeKey)
    , m_id(mergeKey.isEmpty() ? -1 : int(qHash(mergeKey) & INT_MAX))
    , m_time(QDateTime::currentMSecsSinceEpoch())
{
}

void AnimCommand::undo() { m_doc->restoreState(m_before); }

void AnimCommand::redo() { m_doc->restoreState(m_after); }

bool AnimCommand::mergeWith(const QUndoCommand* other)
{
    const auto* o = static_cast<const AnimCommand*>(other);
    const bool drag = m_mergeKey.startsWith(QLatin1Char('#'));
    if (o->m_mergeKey != m_mergeKey || o->m_before.clip != m_after.clip || o->m_before.node != m_after.node) return false;
    if (!drag && o->m_time - m_time > kMergeWindowMs) return false;
    m_after = o->m_after;
    m_time = o->m_time;
    setObsolete(sameState(m_before, m_after));   // dragged back to where it started
    return true;
}

// ---- AnimDocument -------------------------------------------------------------------------------

AnimDocument::AnimDocument(QObject* parent)
    : QObject(parent)
    , m_undo(new QUndoStack(this))
{
    connect(m_undo, &QUndoStack::cleanChanged, this, [this] { emit dirtyChanged(isDirty()); });
    // First of the clipChanged slots: a clip that becomes current starts unmodified.
    connect(this, &AnimDocument::clipChanged, this, &AnimDocument::markClipBase);
    // The solo is a node of the current clip: another clip, or the node gone, ends it.
    connect(this, &AnimDocument::clipChanged, this, [this] { setSolo(std::nullopt); });
    connect(this, &AnimDocument::fileChanged, this, [this] {
        const Clip* c = clip();
        if (m_solo && (!c || !nodeAt(c->root, *m_solo))) setSolo(std::nullopt);
    });
    newFile();
}

QString AnimDocument::displayName() const
{
    return m_filePath.isEmpty() ? tr("Untitled.anim") : QFileInfo(m_filePath).fileName();
}

bool AnimDocument::isDirty() const { return !m_undo->isClean(); }

QString AnimDocument::toJson() const { return qs(toms::anim::animToJson(m_state.file)); }

void AnimDocument::newFile()
{
    AnimState s;
    Clip c;
    c.name = "clip";
    c.root.name = "root";
    s.file.clips.push_back(c);
    s.atlasesAbs = m_state.atlasesAbs;   // keep working with the same sprites (absolute until saved)
    s.atlasIds = m_state.atlasIds;
    reset(s, QString());
}

bool AnimDocument::open(const QString& path, QString* error)
{
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error) *error = in.errorString();
        return false;
    }
    AnimState s;
    std::string err;
    if (!toms::anim::parseAnim(in.readAll().toStdString(), s.file, &err)) {
        if (error) *error = qs(err);
        return false;
    }
    const QString abs = QFileInfo(path).absoluteFilePath();
    const QDir dir = QFileInfo(abs).absoluteDir();
    QStringList absolute;
    for (const toms::anim::AtlasRef& a : s.file.atlases) {
        const QString p = QDir::cleanPath(dir.absoluteFilePath(qs(a.path)));   // an absolute path stays as it is
        if (isAbsoluteAtlasPath(a.path)) absolute << p;
        if (s.atlasesAbs.contains(p)) continue;
        s.atlasesAbs << p;
        s.atlasIds << qs(a.id);
    }
    reset(s, abs);
    if (!absolute.isEmpty()) {
        m_absoluteInFile = absolute;
        emit message(tr("%1 names %2 atlas(es) with an absolute path: they will be stored relative on save.")
                         .arg(displayName()).arg(absolute.size()));
        emit atlasChanged();   // the Problems dock and the Atlases list show them
    }
    return true;
}

bool AnimDocument::save(const QString& path, QString* error)
{
    const QString abs = QFileInfo(path).absoluteFilePath();
    AnimFile f = m_state.file;
    if (!relativeAtlases(abs, f.atlases, error)) return false;
    QSaveFile out(abs);
    if (!out.open(QIODevice::WriteOnly)) {
        if (error) *error = out.errorString();
        return false;
    }
    out.write(QByteArray::fromStdString(toms::anim::animToJson(f)));
    if (!out.commit()) {
        if (error) *error = out.errorString();
        return false;
    }
    const bool moved = abs != m_filePath;
    m_filePath = abs;
    m_state.file.atlases = f.atlases;
    const bool hadAbsolute = !m_absoluteInFile.isEmpty();
    m_absoluteInFile.clear();
    m_undo->setClean();
    markClipBase();   // saved: the clip's changes are kept
    if (moved) emit filePathChanged();
    if (moved || hadAbsolute) emit atlasChanged();   // the stored (relative) paths changed
    emit dirtyChanged(false);
    emit message(tr("Saved %1").arg(QDir::toNativeSeparators(abs)));
    return true;
}

bool AnimDocument::writeBackup(const QString& path, QString* error) const
{
    AnimFile f = m_state.file;
    f.atlases.clear();
    for (int i = 0; i < m_state.atlasesAbs.size(); i++) {
        toms::anim::AtlasRef r;
        r.path = u8(QDir::fromNativeSeparators(m_state.atlasesAbs[i]));
        r.id = i < m_state.atlasIds.size() ? u8(m_state.atlasIds[i]) : toms::anim::defaultAtlasId(r.path);
        f.atlases.push_back(r);
    }
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(QByteArray::fromStdString(toms::anim::animToJson(f))) < 0 || !out.commit()) {
        if (error) *error = out.errorString();
        return false;
    }
    return true;
}

void AnimDocument::reset(AnimState s, const QString& path)
{
    m_filePath = path;
    m_state = std::move(s);
    fixSelection(m_state);
    m_state.file.atlases = storedAtlases(m_state, m_filePath);
    m_undo->clear();
    m_undo->setClean();
    m_time = 0;
    m_absoluteInFile.clear();
    loadAtlases(true);
    emit fileReset();
    emit fileChanged();
    emit clipChanged();
    emit selectionChanged();
    emit timeChanged(m_time);
    emit filePathChanged();
    emit dirtyChanged(false);
}

std::vector<toms::anim::AtlasRef> AnimDocument::storedAtlases(const AnimState& s, const QString& filePath)
{
    std::vector<toms::anim::AtlasRef> out;
    const QDir dir = QFileInfo(filePath).absoluteDir();
    for (int i = 0; i < s.atlasesAbs.size(); i++) {
        const QString& a = s.atlasesAbs[i];
        toms::anim::AtlasRef r;
        r.path = u8(QDir::fromNativeSeparators(filePath.isEmpty() ? a : dir.relativeFilePath(a)));
        r.id = i < s.atlasIds.size() ? u8(s.atlasIds[i]) : toms::anim::defaultAtlasId(r.path);
        out.push_back(r);
    }
    return out;
}

bool AnimDocument::relativeAtlases(const QString& animPath, std::vector<toms::anim::AtlasRef>& out, QString* error) const
{
    out = storedAtlases(m_state, QFileInfo(animPath).absoluteFilePath());
    for (size_t i = 0; i < out.size(); i++)
        if (isAbsoluteAtlasPath(out[i].path)) {   // QDir::relativeFilePath gives up across drives / roots
            if (error)
                *error = tr("The atlas %1 cannot be stored relative to %2: they are on different drives.\n\n"
                            "Save the .anim on the same drive as its atlases, or remove / replace that atlas.")
                             .arg(QDir::toNativeSeparators(m_state.atlasesAbs[int(i)]), QDir::toNativeSeparators(animPath));
            return false;
        }
    return true;
}

void AnimDocument::fixSelection(AnimState& s) const
{
    if (s.file.clips.empty()) {
        s.clip = 0;
        s.node.clear();
        return;
    }
    s.clip = std::clamp(s.clip, 0, int(s.file.clips.size()) - 1);
    while (!s.node.empty() && !nodeAt(s.file.clips[size_t(s.clip)].root, s.node)) s.node.pop_back();
}

void AnimDocument::restoreState(const AnimState& s)
{
    const bool clipChanged_ = s.clip != m_state.clip;
    const bool selChanged = clipChanged_ || s.node != m_state.node;
    const bool atlasChanged_ = s.atlasesAbs != m_state.atlasesAbs || s.atlasIds != m_state.atlasIds;
    m_state = s;
    fixSelection(m_state);
    m_state.file.atlases = storedAtlases(m_state, m_filePath);
    if (atlasChanged_) loadAtlases(false);
    emit fileChanged();
    if (clipChanged_) emit clipChanged();
    if (selChanged) emit selectionChanged();
}

// ---- atlases ------------------------------------------------------------------------------------

namespace {

std::shared_ptr<AnimDocument::LoadedAtlas> loadOne(const QString& path)
{
    auto a = std::make_shared<AnimDocument::LoadedAtlas>();
    a->path = path;
    QFile in(path);
    std::string err;
    if (!in.open(QIODevice::ReadOnly)) {
        a->error = AnimDocument::tr("cannot read %1: %2").arg(QDir::toNativeSeparators(path), in.errorString());
        return a;
    }
    if (!toms::parseAtlas(in.readAll().toStdString(), a->atlas, &err)) {
        a->error = AnimDocument::tr("%1: %2").arg(QDir::toNativeSeparators(path), qs(err));
        return a;
    }
    QVector<QImage> pages;
    const QDir dir = QFileInfo(path).absoluteDir();
    bool sizesChanged = false;
    for (toms::AtlasPage& p : a->atlas.pages) {
        QImage img(dir.filePath(qs(p.file)));
        if (img.isNull()) a->error = AnimDocument::tr("cannot load the atlas page %1").arg(QDir::toNativeSeparators(dir.filePath(qs(p.file))));
        if (!img.isNull() && (p.w <= 0 || p.h <= 0)) {
            p.w = img.width();
            p.h = img.height();
            sizesChanged = true;
        }
        pages.push_back(img);
    }
    if (sizesChanged) a->atlas.computeUVs();
    a->images.setPages(pages);
    for (const toms::AtlasRegion& r : a->atlas.regions) a->spriteNames << qs(r.name);
    a->spriteNames.removeDuplicates();
    a->spriteNames.sort(Qt::CaseInsensitive);
    a->ok = a->error.isEmpty();
    return a;
}

}  // namespace

// force: reread every atlas; else keep the ones already loaded (undo / redo of a list change).
void AnimDocument::loadAtlases(bool force)
{
    std::vector<std::shared_ptr<LoadedAtlas>> next;
    for (const QString& path : m_state.atlasesAbs) {
        std::shared_ptr<LoadedAtlas> a;
        if (!force)
            for (const auto& old : m_atlases)
                if (old->path == path) a = old;
        if (!a) {
            a = loadOne(path);
            if (!a->error.isEmpty()) emit message(tr("Atlas: %1").arg(a->error));
        }
        next.push_back(a);
    }
    m_atlases = std::move(next);
    // The game's lookup: every page gets its own texture id, which maps back to (atlas, page).
    m_atlasSet = toms::anim::AtlasSet();
    m_textures.clear();
    m_setToDoc.clear();
    m_spriteNames.clear();
    for (size_t i = 0; i < m_atlases.size(); i++) {
        const LoadedAtlas& a = *m_atlases[i];
        if (!a.ok) continue;
        std::vector<uint16_t> pageTextures;
        for (size_t p = 0; p < a.atlas.pages.size(); p++) {
            pageTextures.push_back(uint16_t(m_textures.size()));
            m_textures.push_back({int(i), int(p)});
        }
        m_atlasSet.add(a.atlas, u8(atlasId(int(i))), std::move(pageTextures));
        m_setToDoc.push_back(int(i));
        m_spriteNames << a.spriteNames;
    }
    m_spriteNames.removeDuplicates();
    m_spriteNames.sort(Qt::CaseInsensitive);
    // Every copy, by name and then lookup order: "coin (game)", "coin (extra)", "spark (extra)".
    m_spriteChoices.clear();
    for (const QString& name : m_spriteNames)
        for (size_t i = 0; i < m_atlases.size(); i++)
            if (m_atlases[i]->ok && m_atlases[i]->atlas.find(u8(name))) m_spriteChoices << spriteDisplay(spriteRefFor(int(i), name));
    emit atlasChanged();
}

void AnimDocument::reloadAtlases()
{
    loadAtlases(true);
    emit fileChanged();   // repaint everything that shows sprites
}

QString AnimDocument::storedAtlasPath(int i) const
{
    return i >= 0 && i < int(m_state.file.atlases.size()) ? qs(m_state.file.atlases[size_t(i)].path) : QString();
}

bool AnimDocument::atlasPathIsAbsoluteInFile(int i) const
{
    return i >= 0 && i < m_state.atlasesAbs.size() && m_absoluteInFile.contains(m_state.atlasesAbs[i]);
}

bool AnimDocument::atlasLoaded() const
{
    if (m_atlases.empty()) return false;
    for (const auto& a : m_atlases)
        if (!a->ok) return false;
    return true;
}

const SpriteImageCache* AnimDocument::pageImages(uint16_t texture, int* page) const
{
    if (texture >= m_textures.size()) return nullptr;
    const auto [atlas, p] = m_textures[texture];
    if (page) *page = p;
    return &m_atlases[size_t(atlas)]->images;
}

std::vector<CheckAtlas> AnimDocument::checkAtlases() const
{
    std::vector<CheckAtlas> out;
    for (int i = 0; i < atlasCount(); i++) {
        const LoadedAtlas& a = atlasAt(i);
        CheckAtlas r;
        r.id = u8(atlasId(i));
        r.path = u8(storedAtlasPath(i));
        r.atlas = a.ok ? &a.atlas : nullptr;
        r.error = u8(a.error);
        r.absolute = atlasPathIsAbsoluteInFile(i);
        out.push_back(r);
    }
    return out;
}

const toms::AtlasRegion* AnimDocument::findSprite(const std::string& ref, int* atlasIndex) const
{
    int entry = -1;
    const toms::AtlasRegion* r = m_atlasSet.find(ref, nullptr, &entry);
    if (r && atlasIndex) *atlasIndex = entry >= 0 && entry < int(m_setToDoc.size()) ? m_setToDoc[size_t(entry)] : -1;
    return r;
}

std::string AnimDocument::spriteRefFor(int atlasIndex, const QString& name) const
{
    return toms::anim::spriteRef(u8(atlasId(atlasIndex)), u8(name));
}

QString AnimDocument::spriteDisplay(const std::string& ref) const
{
    if (ref.empty()) return tr("(none)");
    const toms::anim::SpriteRef s = toms::anim::parseSpriteRef(ref);
    if (!s.atlas.empty()) return QStringLiteral("%1 (%2)").arg(qs(s.name), qs(s.atlas));
    int i = -1;
    if (findSprite(ref, &i) && i >= 0) return QStringLiteral("%1 (auto: %2)").arg(qs(s.name), atlasId(i));
    return qs(s.name);
}

std::string AnimDocument::spriteFromText(const QString& text) const
{
    const QString t = text.trimmed();
    if (t.isEmpty() || t == tr("(none)") || t == QLatin1String("(none)")) return std::string();
    // "name (id)" / "name (auto: id)" -- what the fields show.
    static const QRegularExpression shown(QStringLiteral("^(.*\\S)\\s*\\((?:auto:\\s*)?([^()\\s]+)\\)$"));
    const QRegularExpressionMatch m = shown.match(t);
    if (m.hasMatch()) return toms::anim::spriteRef(u8(m.captured(2)), u8(m.captured(1)));
    const toms::anim::SpriteRef s = toms::anim::parseSpriteRef(u8(t));
    if (!s.atlas.empty()) return toms::anim::spriteRef(u8(qs(s.atlas).trimmed()), u8(qs(s.name).trimmed()));
    int i = -1;   // a bare name: the atlas the lookup picks now
    if (findSprite(s.name, &i) && i >= 0) return spriteRefFor(i, qs(s.name));
    return s.name;
}

QRect AnimDocument::regionRect(int atlasIndex, const toms::AtlasRegion& r) const
{
    if (atlasIndex < 0 || atlasIndex >= atlasCount()) return QRect();
    const QVector<QImage>& pages = atlasAt(atlasIndex).images.pages();
    if (r.page < 0 || r.page >= pages.size()) return QRect();
    const QSize ps = pages[r.page].size();
    const int x0 = int(std::lround(r.uv[0] * ps.width())), y0 = int(std::lround(r.uv[1] * ps.height()));
    const int x1 = int(std::lround(r.uv[2] * ps.width())), y1 = int(std::lround(r.uv[3] * ps.height()));
    return QRect(x0, y0, x1 - x0, y1 - y0);
}

QImage AnimDocument::spriteImage(int atlasIndex, const QString& name) const
{
    if (atlasIndex < 0 || atlasIndex >= atlasCount() || !atlasAt(atlasIndex).ok) return QImage();
    const toms::AtlasRegion* r = atlasAt(atlasIndex).atlas.find(u8(name));
    return r ? atlasAt(atlasIndex).images.cut(r->page, regionRect(atlasIndex, *r)) : QImage();
}

QImage AnimDocument::spriteImage(const QString& ref) const
{
    int i = -1;
    return findSprite(u8(ref), &i) ? spriteImage(i, qs(toms::anim::parseSpriteRef(u8(ref)).name)) : QImage();
}

bool AnimDocument::atlasIdUsable(const QString& id, int exceptIndex, QString* error) const
{
    if (!isAtlasId(u8(id))) {
        if (error) *error = tr("'%1' cannot be an atlas id: it must not be empty or contain ':', '/', '\\', '=' or spaces.").arg(id);
        return false;
    }
    const int other = atlasIndexOf(id);
    if (other >= 0 && other != exceptIndex) {
        if (error) *error = tr("Another atlas (%1) has the id '%2' already: ids must be unique.").arg(storedAtlasPath(other), id);
        return false;
    }
    return true;
}

QString AnimDocument::suggestAtlasId(const QString& absolutePath) const
{
    auto clean = [](QString s) {
        for (QChar& c : s)
            if (c == QLatin1Char(':') || c == QLatin1Char('/') || c == QLatin1Char('\\') || c == QLatin1Char('=') || c.isSpace())
                c = QLatin1Char('_');
        return s;
    };
    const QFileInfo fi(absolutePath);
    const QString base = clean(qs(toms::anim::defaultAtlasId(u8(fi.fileName()))));
    if (atlasIndexOf(base) < 0 && isAtlasId(u8(base))) return base;
    QDir up = fi.absoluteDir();   // styles/dark16/atlas/game.atlas -> "dark16"
    QString folder = up.cdUp() ? clean(up.dirName()) : QString();
    QString start = isAtlasId(u8(folder)) ? folder : base.isEmpty() ? QStringLiteral("atlas") : base;
    if (atlasIndexOf(start) < 0) return start;
    for (int n = 2;; n++)
        if (atlasIndexOf(QStringLiteral("%1_%2").arg(start).arg(n)) < 0) return QStringLiteral("%1_%2").arg(start).arg(n);
}

bool AnimDocument::addAtlas(const QString& absolutePath, const QString& idIn, QString* error)
{
    const QString abs = QDir::cleanPath(QFileInfo(absolutePath).absoluteFilePath());
    if (m_state.atlasesAbs.contains(abs)) {
        if (error) *error = tr("%1 is already in the list.").arg(QDir::toNativeSeparators(abs));
        return false;
    }
    const QString id = idIn.isEmpty() ? qs(toms::anim::defaultAtlasId(u8(QFileInfo(abs).fileName()))) : idIn.trimmed();
    if (!atlasIdUsable(id, -1, error)) return false;
    QFile probe(abs);
    if (!probe.open(QIODevice::ReadOnly)) {
        if (error) *error = probe.errorString();
        return false;
    }
    toms::AtlasFile test;
    std::string err;
    if (!toms::parseAtlas(probe.readAll().toStdString(), test, &err)) {
        if (error) *error = qs(err);
        return false;
    }
    if (!m_filePath.isEmpty() && isAbsoluteAtlasPath(u8(QFileInfo(m_filePath).absoluteDir().relativeFilePath(abs)))) {
        if (error)
            *error = tr("%1 is on another drive than %2: atlas paths are stored relative to the .anim file.")
                         .arg(QDir::toNativeSeparators(abs), QDir::toNativeSeparators(m_filePath));
        return false;
    }
    return edit(tr("Add atlas"), [&](AnimState& s) {
        s.atlasesAbs << abs;
        s.atlasIds << id;
        return true;
    });
}

bool AnimDocument::removeAtlas(int index)
{
    if (index < 0 || index >= m_state.atlasesAbs.size()) return false;
    return edit(tr("Remove atlas"), [&](AnimState& s) {
        s.atlasesAbs.removeAt(index);
        s.atlasIds.removeAt(index);
        return true;
    });
}

bool AnimDocument::moveAtlas(int index, int delta)
{
    const int to = index + delta;
    if (index < 0 || index >= m_state.atlasesAbs.size() || to < 0 || to >= m_state.atlasesAbs.size()) return false;
    return edit(tr("Reorder atlases"), [&](AnimState& s) {
        s.atlasesAbs.move(index, to);
        s.atlasIds.move(index, to);
        return true;
    });
}

bool AnimDocument::renameAtlasId(int index, const QString& idIn, QString* error)
{
    const QString id = idIn.trimmed();
    if (index < 0 || index >= m_state.atlasIds.size() || id == m_state.atlasIds[index]) return false;
    if (!atlasIdUsable(id, index, error)) return false;
    const std::string from = u8(m_state.atlasIds[index]), to = u8(id);
    int rewritten = 0;
    const bool ok = edit(tr("Rename atlas id"), [&](AnimState& s) {
        s.atlasIds[index] = id;
        for (Clip& c : s.file.clips)
            rewritten += rewriteSpriteRefs(c.root, [&](const std::string& r) {
                const toms::anim::SpriteRef p = toms::anim::parseSpriteRef(r);
                return p.atlas == from ? toms::anim::spriteRef(to, p.name) : r;
            });
        return true;
    });
    if (ok) emit message(tr("Atlas id '%1' is now '%2': %n sprite reference(s) rewritten.", nullptr, rewritten).arg(qs(from), id));
    return ok;
}

int AnimDocument::atlasReferenceCount(int index) const
{
    if (index < 0 || index >= atlasCount()) return 0;
    const std::string id = u8(atlasId(index));
    int n = 0;
    for (const Clip& c : m_state.file.clips) {
        std::vector<std::string> refs;
        collectSpriteRefs(c.root, refs);
        for (const std::string& r : refs) {
            const toms::anim::SpriteRef p = toms::anim::parseSpriteRef(r);
            if (!p.atlas.empty()) {
                n += p.atlas == id;
                continue;
            }
            int owner = -1, others = 0;
            for (int i = 0; i < atlasCount(); i++)
                if (atlasAt(i).ok && atlasAt(i).atlas.find(p.name)) {
                    if (i == index) owner = i;
                    else others++;
                }
            n += owner == index && others == 0;
        }
    }
    return n;
}

int AnimDocument::bareSpriteReferenceCount() const
{
    int n = 0;
    for (const Clip& c : m_state.file.clips) {
        std::vector<std::string> refs;
        collectSpriteRefs(c.root, refs);
        for (const std::string& r : refs) n += toms::anim::parseSpriteRef(r).atlas.empty() && findSprite(r);
    }
    return n;
}

int AnimDocument::qualifySpriteReferences()
{
    int changed = 0;
    edit(tr("Qualify sprite references"), [&](AnimState& s) {
        for (Clip& c : s.file.clips)
            changed += rewriteSpriteRefs(c.root, [&](const std::string& r) {
                if (!toms::anim::parseSpriteRef(r).atlas.empty()) return r;
                int i = -1;
                return findSprite(r, &i) && i >= 0 ? spriteRefFor(i, qs(r)) : r;
            });
        return changed > 0;
    });
    emit message(changed ? tr("Qualified %n sprite reference(s): each now stores its atlas (id:name).", nullptr, changed)
                         : tr("No bare sprite names to qualify (names no atlas has stay as they are)."));
    return changed;
}

// ---- current clip / node / time -----------------------------------------------------------------

const Clip* AnimDocument::clip() const
{
    if (m_state.file.clips.empty()) return nullptr;
    return &m_state.file.clips[size_t(m_state.clip)];
}

std::string AnimDocument::clipJson(const Clip& c)
{
    AnimFile f;
    f.clips.push_back(c);
    return toms::anim::animToJson(f);
}

void AnimDocument::markClipBase()
{
    const Clip* c = clip();
    m_clipBaseIndex = c ? m_state.clip : -1;
    m_clipBase = c ? *c : Clip();
    m_clipBaseJson = c ? clipJson(*c) : std::string();
}

bool AnimDocument::clipModified() const
{
    const Clip* c = clip();
    return c && m_clipBaseIndex == m_state.clip && clipJson(*c) != m_clipBaseJson;
}

bool AnimDocument::discardClipChanges()
{
    if (!clipModified()) return false;
    const Clip base = m_clipBase;
    return edit(tr("Discard changes to clip %1").arg(qs(base.name)), [&](AnimState& s) {
        s.file.clips[size_t(s.clip)] = base;
        s.node.clear();
        return true;
    });
}

void AnimDocument::setClipIndex(int index)
{
    if (index < 0 || index >= int(m_state.file.clips.size()) || index == m_state.clip) return;
    m_state.clip = index;
    m_state.node.clear();
    emit clipChanged();
    emit selectionChanged();
    if (m_time > duration()) setTime(0);
}

const Node* AnimDocument::selectedNode() const
{
    const Clip* c = clip();
    return c ? nodeAt(c->root, m_state.node) : nullptr;
}

void AnimDocument::selectNode(const NodePath& path)
{
    const Clip* c = clip();
    if (!c || !nodeAt(c->root, path) || path == m_state.node) return;
    m_state.node = path;
    emit selectionChanged();
}

bool AnimDocument::selectNodeByName(const QString& name)
{
    const Clip* c = clip();
    if (!c) return false;
    std::function<bool(const Node&, NodePath&)> find = [&](const Node& n, NodePath& p) {
        if (qs(n.name) == name) return true;
        for (size_t i = 0; i < n.children.size(); i++) {
            p.push_back(int(i));
            if (find(n.children[i], p)) return true;
            p.pop_back();
        }
        return false;
    };
    NodePath p;
    if (!find(c->root, p)) return false;
    selectNode(p);
    return true;
}

void AnimDocument::setTime(float t)
{
    if (!std::isfinite(t)) return;
    t = std::clamp(t, 0.0f, 3600.0f);
    if (t == m_time) return;
    m_time = t;
    setPreviewTime(-1);
    emit timeChanged(m_time);
}

void AnimDocument::setSolo(const std::optional<NodePath>& path)
{
    if (path == m_solo) return;
    m_solo = path;
    emit soloChanged();
}

bool AnimDocument::soloShows(const Node* n) const
{
    const Clip* c = clip();
    if (!m_solo || !c) return true;
    const Node* top = nodeAt(c->root, *m_solo);
    if (!top) return true;
    std::function<bool(const Node&)> inside = [&](const Node& x) {
        if (&x == n) return true;
        for (const Node& k : x.children)
            if (inside(k)) return true;
        return false;
    };
    return inside(*top);
}

void AnimDocument::setPreviewTime(float t)
{
    if (!std::isfinite(t) || t < 0) t = -1;
    if (t == m_preview) return;
    m_preview = t;
    emit previewTimeChanged();
}

float AnimDocument::duration() const
{
    const Clip* c = clip();
    return c ? c->duration() : 0.0f;
}

void AnimDocument::setAutoKey(bool on)
{
    if (on == m_autoKey) return;
    m_autoKey = on;
    emit autoKeyChanged(on);
}

// ---- editing ------------------------------------------------------------------------------------

bool AnimDocument::edit(const QString& text, const std::function<bool(AnimState&)>& change, const QString& mergeKey)
{
    AnimState after = m_state;
    if (!change(after)) return false;
    fixSelection(after);
    after.file.atlases = storedAtlases(after, m_filePath);
    if (sameState(after, m_state)) return false;
    if (after.atlasesAbs == m_state.atlasesAbs && after.atlasIds == m_state.atlasIds &&
        toms::anim::animToJson(after.file) == toms::anim::animToJson(m_state.file)) {
        // Only the current clip / node moved: not an edit.
        if (after.clip != m_state.clip) setClipIndex(after.clip);
        selectNode(after.node);
        return false;
    }
    m_undo->push(new AnimCommand(this, text, m_state, after, mergeKey));
    return true;
}

bool AnimDocument::editClip(const QString& text, const std::function<void(Clip&)>& change, const QString& mergeKey)
{
    if (!clip()) return false;
    return edit(text, [&](AnimState& s) {
        change(s.file.clips[size_t(s.clip)]);
        return true;
    }, mergeKey);
}

bool AnimDocument::editNodeIf(const NodePath& path, const QString& text, const std::function<bool(Node&)>& change,
                              const QString& mergeKey)
{
    if (!clip()) return false;
    return edit(text, [&](AnimState& s) {
        Node* n = nodeAt(s.file.clips[size_t(s.clip)].root, path);
        return n && change(*n);
    }, mergeKey);
}

bool AnimDocument::editNode(const NodePath& path, const QString& text, const std::function<void(Node&)>& change,
                            const QString& mergeKey)
{
    return editNodeIf(path, text, [&](Node& n) {
        change(n);
        return true;
    }, mergeKey);
}

bool AnimDocument::editSelected(const QString& text, const std::function<void(Node&)>& change, const QString& mergeKey)
{
    return editNode(m_state.node, text, change, mergeKey);
}

bool AnimDocument::setChannel(Channel c, const Value& v, const QString& mergeKey)
{
    const Node* sel = selectedNode();
    if (!sel) return false;
    const float t = m_time;
    const bool keyed = m_autoKey || c == Channel::Event || hasKeyAt(*sel, c, t);
    const bool ok = editSelected(keyed ? tr("Key %1").arg(QLatin1String(channelName(c))) : tr("Set %1").arg(QLatin1String(channelName(c))),
                                 [&](Node& n) {
                                     if (keyed) setKey(n, c, t, v);
                                     else setRest(n, c, v);
                                 }, mergeKey);
    if (ok && !keyed && keyCount(*sel, c) > 0)
        emit message(tr("Auto-key is off: the rest %1 changed, but the channel is animated, so nothing moves here.")
                         .arg(QLatin1String(channelName(c))));
    return ok;
}

bool AnimDocument::addKeyHere(Channel c)
{
    const Node* sel = selectedNode();
    if (!sel || hasKeyAt(*sel, c, m_time)) return false;
    Value v = valueAt(*sel, c, m_time);
    if (c == Channel::Event) v.s = "event";
    const float t = m_time;
    return editSelected(tr("Add %1 key").arg(QLatin1String(channelName(c))), [&](Node& n) { setKey(n, c, t, v); });
}

bool AnimDocument::removeKeyHere(Channel c)
{
    const float t = m_time;
    return editNodeIf(m_state.node, tr("Remove %1 key").arg(QLatin1String(channelName(c))), [&](Node& n) { return removeKey(n, c, t); });
}

bool AnimDocument::setEaseHere(Channel c, const Ease& e)
{
    const Node* sel = selectedNode();
    if (!sel) return false;
    Ease cur;
    float kt = 0;
    if (!easeAtOrBefore(*sel, c, m_time, cur, &kt)) {
        const std::vector<float> times = keyTimes(*sel, c);
        if (times.empty()) return false;
        kt = times.front();   // before the first key: that key's segment is the next one to play
    }
    return editNodeIf(m_state.node, tr("Set ease"), [&](Node& n) { return setEase(n, c, kt, e); });
}

bool AnimDocument::assignSprite(const QString& ref)
{
    Value v;
    v.s = u8(ref);
    return setChannel(Channel::Sprite, v);
}

// ---- key list -----------------------------------------------------------------------------------

bool AnimDocument::insertKeys(float t, int channel)
{
    return editNodeIf(m_state.node, tr("Insert key"), [&](Node& n) {
        bool any = false;
        for (Channel c : channelsFor(n, channel)) {
            if (hasKeyAt(n, c, t)) continue;
            Value v = valueAt(n, c, t);
            if (c == Channel::Event) v.s = "event";
            setKey(n, c, t, v);
            any = true;
        }
        return any;
    });
}

bool AnimDocument::deleteKeys(const std::vector<float>& times, int channel)
{
    return editNodeIf(m_state.node, tr("Delete keys"), [&](Node& n) {
        bool any = false;
        for (float t : times) {
            if (channel >= 0) any |= removeKey(n, Channel(channel), t);
            else
                for (Channel c : kAllChannels) any |= removeKey(n, c, t);
        }
        return any;
    });
}

bool AnimDocument::setCell(float t, Channel c, const Value& v, bool together)
{
    return editNodeIf(m_state.node, tr("Edit %1 key").arg(QLatin1String(channelName(c))), [&](Node& n) {
        if (together && isInterpolated(c))   // keeps the row whole, on the channels it animates already
            for (Channel o : animatedChannels(n))
                if (o != c && !hasKeyAt(n, o, t)) setKey(n, o, t, valueAt(n, o, t));
        setKey(n, c, t, v);
        return true;
    });
}

bool AnimDocument::removeCell(float t, Channel c)
{
    return editNodeIf(m_state.node, tr("Remove %1 key").arg(QLatin1String(channelName(c))), [&](Node& n) { return removeKey(n, c, t); });
}

bool AnimDocument::moveRows(const std::vector<float>& from, const std::vector<float>& to, QString* conflict, const QString& text)
{
    std::string why;
    const bool ok = editNodeIf(m_state.node, text.isEmpty() ? tr("Move keys") : text,
                               [&](Node& n) { return moveKeyTimes(n, from, to, &why); });
    if (!ok && conflict) *conflict = qs(why);
    return ok;
}

bool AnimDocument::moveChannelKey(Channel c, float from, float to, QString* conflict)
{
    const Node* sel = selectedNode();
    if (!sel) return false;
    if (to < 0) {
        if (conflict) *conflict = tr("time must not be negative");
        return false;
    }
    if (!sameTime(from, to) && hasKeyAt(*sel, c, to)) {
        if (conflict) *conflict = tr("%1 already has a key at %2").arg(QLatin1String(channelName(c))).arg(to);
        return false;
    }
    return editNodeIf(m_state.node, tr("Move %1 key").arg(QLatin1String(channelName(c))), [&](Node& n) {
        Value v;
        if (!keyAt(n, c, from, v)) return false;
        Ease e;
        const bool eased = easeAt(n, c, from, e);
        removeKey(n, c, from);
        setKey(n, c, to, v);
        if (eased) setEase(n, c, to, e);
        return true;
    });
}

bool AnimDocument::rescale(float newDuration, bool wholeClip, QString* error)
{
    const Clip* c = clip();
    const Node* sel = selectedNode();
    if (!c || !sel || !(newDuration > 0)) {
        if (error) *error = tr("the new duration must be above 0");
        return false;
    }
    float cur = 0;
    if (wholeClip) cur = c->duration();
    else {
        const std::vector<float> t = unionKeyTimes(*sel);
        cur = t.empty() ? 0 : t.back();
    }
    if (cur <= 0) {
        if (error) *error = tr("there are no key times to scale");
        return false;
    }
    const float factor = newDuration / cur;
    if (wholeClip)
        return editClip(tr("Rescale clip"), [&](Clip& clip) {
            scaleKeyTimes(clip.root, factor, true);
            if (clip.length > 0) clip.length *= factor;
        });
    return editSelected(tr("Rescale keys"), [&](Node& n) { scaleKeyTimes(n, factor, false); });
}

bool AnimDocument::ramp(const std::vector<float>& timesIn, Channel c, const Value& first, const Value& last, bool alphaOnly)
{
    if (timesIn.size() < 2 || !isInterpolated(c)) return false;
    std::vector<float> times = timesIn;
    std::sort(times.begin(), times.end());
    const float t0 = times.front(), t1 = times.back();
    return editNodeIf(m_state.node, alphaOnly ? tr("Ramp alpha") : tr("Ramp %1").arg(QLatin1String(channelName(c))), [&](Node& n) {
        // Sample before writing: the colour a row keeps must not depend on the keys just written.
        std::vector<Value> current;
        for (float t : times) current.push_back(valueAt(n, c, t));
        for (size_t i = 0; i < times.size(); i++) {
            const float f = t1 > t0 ? (times[i] - t0) / (t1 - t0) : float(i) / float(times.size() - 1);
            Value v = current[i];
            if (alphaOnly) v.v.w = first.v.x + (last.v.x - first.v.x) * f;
            else v.v = first.v + (last.v - first.v) * f;
            setKey(n, c, times[i], v);
        }
        return true;
    });
}

bool AnimDocument::fade(const std::vector<float>& times, bool in)
{
    Value a, b;
    a.v.x = in ? 0.0f : 1.0f;
    b.v.x = in ? 1.0f : 0.0f;
    return ramp(times, Channel::Color, a, b, true);
}

bool AnimDocument::spriteSequence(const std::vector<float>& timesIn, const QStringList& sprites)
{
    if (timesIn.empty() || sprites.isEmpty()) return false;
    std::vector<float> times = timesIn;
    std::sort(times.begin(), times.end());
    return editNodeIf(m_state.node, tr("Sprite sequence"), [&](Node& n) {
        for (size_t i = 0; i < times.size(); i++) {
            Value v;
            v.s = u8(sprites[int(i % size_t(sprites.size()))]);
            setKey(n, Channel::Sprite, times[i], v);
        }
        return true;
    });
}

bool AnimDocument::insertSpriteSequence(float start, float gap, const QStringList& sprites, bool extendClip)
{
    if (sprites.isEmpty() || gap <= 0.0f || !clip() || !selectedNode()) return false;
    start = std::max(0.0f, start);
    const float last = start + gap * float(sprites.size() - 1);
    return edit(tr("Insert %n sprite key(s)", nullptr, sprites.size()), [&](AnimState& s) {
        Clip& c = s.file.clips[size_t(s.clip)];
        Node* n = nodeAt(c.root, s.node);
        if (!n) return false;
        for (int i = 0; i < sprites.size(); i++) {
            Value v;
            v.s = u8(sprites[i]);
            setKey(*n, Channel::Sprite, start + gap * float(i), v);
        }
        if (extendClip && c.length > 0 && c.length < last) c.length = last;
        return true;
    });
}

bool AnimDocument::setEaseRows(const std::vector<float>& times, const Ease& e)
{
    return editNodeIf(m_state.node, tr("Set ease"), [&](Node& n) {
        bool any = false;
        for (float t : times)
            for (Channel c : kInterpolated) any |= setEase(n, c, t, e);
        return any;
    });
}

// ---- structure ----------------------------------------------------------------------------------

bool AnimDocument::addNode(const NodePath& parent, Node node, const QString& text)
{
    return edit(text, [&](AnimState& s) {
        if (s.file.clips.empty()) return false;
        Node& root = s.file.clips[size_t(s.clip)].root;
        Node* p = nodeAt(root, parent);
        if (!p) return false;
        node.name = uniqueNodeName(root, node.name);
        p->children.push_back(std::move(node));
        s.node = parent;
        s.node.push_back(int(p->children.size()) - 1);
        return true;
    });
}

bool AnimDocument::addSpriteNode(const NodePath& parent, const QString& sprite, const glm::vec2& pos)
{
    Node n;
    n.name = u8(qs(toms::anim::parseSpriteRef(u8(sprite)).name).section(QLatin1Char('/'), -1));
    n.sprite = u8(sprite);
    n.pos = pos;
    return addNode(parent, n, tr("Add sprite node"));
}

bool AnimDocument::deleteNode(const NodePath& path)
{
    if (path.empty()) {
        emit message(tr("The root node cannot be deleted (it is the clip)."));
        return false;
    }
    return edit(tr("Delete node"), [&](AnimState& s) {
        if (s.file.clips.empty()) return false;
        NodePath parent(path.begin(), path.end() - 1);
        Node* p = nodeAt(s.file.clips[size_t(s.clip)].root, parent);
        if (!p || path.back() >= int(p->children.size())) return false;
        p->children.erase(p->children.begin() + path.back());
        s.node = parent;
        return true;
    });
}

bool AnimDocument::duplicateNode(const NodePath& path)
{
    if (path.empty()) {
        emit message(tr("The root node cannot be duplicated; duplicate the clip instead."));
        return false;
    }
    return edit(tr("Duplicate node"), [&](AnimState& s) {
        if (s.file.clips.empty()) return false;
        Node& root = s.file.clips[size_t(s.clip)].root;
        NodePath parent(path.begin(), path.end() - 1);
        Node* p = nodeAt(root, parent);
        if (!p || path.back() >= int(p->children.size())) return false;
        Node copy = p->children[size_t(path.back())];
        copy.name = uniqueNodeName(root, copy.name);
        p->children.insert(p->children.begin() + path.back() + 1, std::move(copy));
        s.node = parent;
        s.node.push_back(path.back() + 1);
        return true;
    });
}

bool AnimDocument::renameNode(const NodePath& path, const QString& name)
{
    const QString n = name.trimmed();
    if (n.isEmpty()) return false;
    return editNode(path, tr("Rename node"), [&](Node& node) { node.name = u8(n); });
}

bool AnimDocument::moveNode(const NodePath& from, const NodePath& toParent, int index)
{
    if (from.empty()) return false;
    if (toParent.size() >= from.size() && std::equal(from.begin(), from.end(), toParent.begin())) {
        emit message(tr("A node cannot be moved into itself."));
        return false;
    }
    return edit(tr("Move node"), [&](AnimState& s) {
        if (s.file.clips.empty()) return false;
        Node& root = s.file.clips[size_t(s.clip)].root;
        const NodePath fromParent(from.begin(), from.end() - 1);
        Node* fp = nodeAt(root, fromParent);
        if (!fp || from.back() >= int(fp->children.size()) || !nodeAt(root, toParent)) return false;
        Node moving = std::move(fp->children[size_t(from.back())]);
        fp->children.erase(fp->children.begin() + from.back());
        NodePath tp = toParent;
        const size_t d = fromParent.size();
        if (tp.size() > d && std::equal(fromParent.begin(), fromParent.end(), tp.begin()) && tp[d] > from.back()) tp[d]--;
        if (tp == fromParent && index > from.back()) index--;
        Node* np = nodeAt(root, tp);
        index = std::clamp(index, 0, int(np->children.size()));
        np->children.insert(np->children.begin() + index, std::move(moving));
        s.node = tp;
        s.node.push_back(index);
        return true;
    });
}

bool AnimDocument::addClip(const QString& name)
{
    return edit(tr("Add clip"), [&](AnimState& s) {
        Clip c;
        c.name = u8(uniqueClipName(s.file, name));
        c.root.name = "root";
        s.file.clips.push_back(c);
        s.clip = int(s.file.clips.size()) - 1;
        s.node.clear();
        return true;
    });
}

bool AnimDocument::duplicateClip(int index)
{
    return edit(tr("Duplicate clip"), [&](AnimState& s) {
        if (index < 0 || index >= int(s.file.clips.size())) return false;
        Clip c = s.file.clips[size_t(index)];
        c.name = u8(uniqueClipName(s.file, qs(c.name)));
        s.file.clips.insert(s.file.clips.begin() + index + 1, c);
        s.clip = index + 1;
        s.node.clear();
        return true;
    });
}

bool AnimDocument::renameClip(int index, const QString& name)
{
    const QString n = name.trimmed();
    if (n.isEmpty() || index < 0 || index >= int(m_state.file.clips.size())) return false;
    const Clip* other = m_state.file.find(u8(n));
    if (other && other != &m_state.file.clips[size_t(index)]) {
        emit message(tr("There is already a clip called '%1'.").arg(n));
        return false;
    }
    return edit(tr("Rename clip"), [&](AnimState& s) {
        s.file.clips[size_t(index)].name = u8(n);
        return true;
    });
}

bool AnimDocument::deleteClip(int index)
{
    if (m_state.file.clips.size() <= 1) {
        emit message(tr("A file keeps at least one clip."));
        return false;
    }
    return edit(tr("Delete clip"), [&](AnimState& s) {
        if (index < 0 || index >= int(s.file.clips.size())) return false;
        s.file.clips.erase(s.file.clips.begin() + index);
        if (index < s.clip) s.clip--;   // another clip: the current one stays current
        else if (index == s.clip) {
            s.clip = std::min(index, int(s.file.clips.size()) - 1);
            s.node.clear();
        }
        return true;
    });
}

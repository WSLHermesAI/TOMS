#include "ParticleDocument.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUndoCommand>
#include <QUndoStack>

using toms::fx::Effect;
using toms::fx::Emitter;

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }
std::string u8(const QString& s) { return s.toStdString(); }

bool sameState(const ParticleState& a, const ParticleState& b)
{
    return a.effect == b.effect && a.emitter == b.emitter && a.atlasesAbs == b.atlasesAbs && a.atlasIds == b.atlasIds &&
           toms::fx::particlesToJson(a.file) == toms::fx::particlesToJson(b.file);
}

std::shared_ptr<ParticleDocument::LoadedAtlas> loadOne(const QString& path)
{
    auto a = std::make_shared<ParticleDocument::LoadedAtlas>();
    a->path = path;
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        a->error = ParticleDocument::tr("cannot read %1: %2").arg(QDir::toNativeSeparators(path), in.errorString());
        return a;
    }
    std::string err;
    if (!toms::parseAtlas(in.readAll().toStdString(), a->atlas, &err)) {
        a->error = QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(path), qs(err));
        return a;
    }
    QVector<QImage> pages;
    const QDir dir = QFileInfo(path).absoluteDir();
    bool sizes = false;
    for (toms::AtlasPage& p : a->atlas.pages) {
        QImage img(dir.filePath(qs(p.file)));
        if (img.isNull()) a->error = ParticleDocument::tr("cannot load the atlas page %1").arg(QDir::toNativeSeparators(dir.filePath(qs(p.file))));
        if (!img.isNull() && (p.w <= 0 || p.h <= 0)) { p.w = img.width(); p.h = img.height(); sizes = true; }
        pages.push_back(img);
    }
    if (sizes) a->atlas.computeUVs();
    a->images.setPages(pages);
    for (const toms::AtlasRegion& r : a->atlas.regions) a->spriteNames << qs(r.name);
    a->spriteNames.removeDuplicates();
    a->spriteNames.sort(Qt::CaseInsensitive);
    a->ok = a->error.isEmpty();
    return a;
}

}  // namespace

// One undo step: the state before and after. Consecutive edits with the same merge key merge.
class ParticleCommand : public QUndoCommand
{
public:
    ParticleCommand(ParticleDocument* doc, const QString& text, ParticleState before, ParticleState after, const QString& mergeKey)
        : QUndoCommand(text), m_doc(doc), m_before(std::move(before)), m_after(std::move(after)), m_key(mergeKey)
    {
    }
    void undo() override { m_doc->setState(m_before); }
    void redo() override
    {
        if (m_first) { m_first = false; return; }   // the edit already applied it
        m_doc->setState(m_after);
    }
    int id() const override { return m_key.isEmpty() ? -1 : 1; }
    bool mergeWith(const QUndoCommand* other) override
    {
        const auto* o = static_cast<const ParticleCommand*>(other);
        if (o->m_key != m_key) return false;
        m_after = o->m_after;
        return true;
    }

private:
    ParticleDocument* m_doc;
    ParticleState m_before, m_after;
    QString m_key;
    bool m_first = true;
};

ParticleDocument::ParticleDocument(QObject* parent)
    : QObject(parent)
    , m_undo(new QUndoStack(this))
{
    connect(m_undo, &QUndoStack::cleanChanged, this, [this] { emit fileChanged(); });
}

QString ParticleDocument::defaultFxAtlas()
{
#ifdef TOMS_FX_ATLAS
    const QString p = QString::fromUtf8(TOMS_FX_ATLAS);
    if (QFileInfo::exists(p)) return QFileInfo(p).absoluteFilePath();
#endif
    return QString();
}

QString ParticleDocument::displayName() const
{
    return m_filePath.isEmpty() ? tr("untitled.particle") : QFileInfo(m_filePath).fileName();
}

bool ParticleDocument::isDirty() const { return !m_undo->isClean(); }

QString ParticleDocument::toJson() const { return qs(toms::fx::particlesToJson(m_state.file)); }

void ParticleDocument::syncStoredAtlases(ParticleState& s) const
{
    s.file.atlases.clear();
    const QDir dir = m_filePath.isEmpty() ? QDir() : QFileInfo(m_filePath).absoluteDir();
    for (int i = 0; i < s.atlasesAbs.size(); i++) {
        toms::anim::AtlasRef r;
        r.path = u8(m_filePath.isEmpty() ? s.atlasesAbs[i] : dir.relativeFilePath(s.atlasesAbs[i]));
        r.id = u8(s.atlasIds.value(i));
        s.file.atlases.push_back(r);
    }
}

void ParticleDocument::newFile()
{
    ParticleState s;
    const QString fx = defaultFxAtlas();
    if (!fx.isEmpty()) {
        s.atlasesAbs << fx;
        s.atlasIds << QStringLiteral("fx");
    }
    Effect e;
    e.name = "effect";
    Emitter m;
    m.name = "emitter";
    m.sprite = fx.isEmpty() ? std::string() : "fx:dot";
    m.blend = toms::fx::Blend::add();
    m.life = toms::fx::Range(0.6f, 1.0f);
    m.speed = toms::fx::Range(40, 80);
    m.spread = 20;
    m.size = toms::fx::Range(6, 10);
    toms::fx::CurveKey<glm::vec4> a, b;
    a.t = 0; a.v = glm::vec4(1);
    b.t = 1; b.v = glm::vec4(1, 1, 1, 0);
    m.colorOverLife.keys = {a, b};
    e.emitters.push_back(m);
    s.file.effects.push_back(e);
    s.emitter = 0;
    m_filePath.clear();
    syncStoredAtlases(s);
    m_state = s;
    m_undo->clear();
    loadAtlases();
    emit fileChanged();
    emit selectionChanged();
}

bool ParticleDocument::open(const QString& path, QString* error)
{
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error) *error = in.errorString();
        return false;
    }
    ParticleState s;
    std::string err;
    if (!toms::fx::parseParticles(in.readAll().toStdString(), s.file, &err)) {
        if (error) *error = qs(err);
        return false;
    }
    const QDir dir = QFileInfo(path).absoluteDir();
    for (const toms::anim::AtlasRef& r : s.file.atlases) {
        s.atlasesAbs << QDir::cleanPath(dir.absoluteFilePath(qs(r.path)));
        s.atlasIds << qs(r.id);
    }
    s.effect = 0;
    s.emitter = s.file.effects.empty() || s.file.effects[0].emitters.empty() ? -1 : 0;
    m_filePath = QFileInfo(path).absoluteFilePath();
    m_state = s;
    m_undo->clear();
    loadAtlases();
    emit fileChanged();
    emit selectionChanged();
    return true;
}

bool ParticleDocument::saveTo(const QString& path, QString* error)
{
    const QDir dir = QFileInfo(path).absoluteDir();
    for (const QString& a : m_state.atlasesAbs)
        if (QDir::isAbsolutePath(dir.relativeFilePath(a))) {
            if (error)
                *error = tr("The atlas %1 cannot be stored relative to %2: they are on different drives.")
                             .arg(QDir::toNativeSeparators(a), QDir::toNativeSeparators(path));
            return false;
        }
    const QString old = m_filePath;
    m_filePath = QFileInfo(path).absoluteFilePath();
    ParticleState s = m_state;
    syncStoredAtlases(s);
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(toms::fx::particlesToJson(s.file).c_str()) < 0 || !out.commit()) {
        if (error) *error = out.errorString();
        m_filePath = old;
        return false;
    }
    m_state = s;
    m_undo->setClean();
    emit fileChanged();
    return true;
}

const Effect* ParticleDocument::effect() const
{
    const auto& es = m_state.file.effects;
    return m_state.effect >= 0 && m_state.effect < int(es.size()) ? &es[size_t(m_state.effect)] : nullptr;
}

const Emitter* ParticleDocument::emitter() const
{
    const Effect* e = effect();
    return e && m_state.emitter >= 0 && m_state.emitter < int(e->emitters.size()) ? &e->emitters[size_t(m_state.emitter)] : nullptr;
}

void ParticleDocument::select(int effect, int emitter)
{
    const auto& es = m_state.file.effects;
    effect = es.empty() ? 0 : std::clamp(effect, 0, int(es.size()) - 1);
    if (!es.empty()) emitter = std::clamp(emitter, -1, int(es[size_t(effect)].emitters.size()) - 1);
    else emitter = -1;
    if (effect == m_state.effect && emitter == m_state.emitter) return;
    m_state.effect = effect;
    m_state.emitter = emitter;
    emit selectionChanged();
}

void ParticleDocument::setState(const ParticleState& s)
{
    const bool atlases = s.atlasesAbs != m_state.atlasesAbs || s.atlasIds != m_state.atlasIds;
    const bool sel = s.effect != m_state.effect || s.emitter != m_state.emitter;
    m_state = s;
    if (atlases) loadAtlases();
    emit fileChanged();
    if (sel) emit selectionChanged();
}

bool ParticleDocument::edit(const QString& text, const std::function<bool(ParticleState&)>& change, const QString& mergeKey)
{
    ParticleState after = m_state;
    if (!change(after)) return false;
    syncStoredAtlases(after);
    if (sameState(after, m_state)) return false;
    const ParticleState before = m_state;
    setState(after);
    m_undo->push(new ParticleCommand(this, text, before, after, mergeKey));
    return true;
}

bool ParticleDocument::editEffect(const QString& text, const std::function<void(Effect&)>& change, const QString& mergeKey)
{
    if (!effect()) return false;
    return edit(text, [&](ParticleState& s) {
        change(s.file.effects[size_t(s.effect)]);
        return true;
    }, mergeKey);
}

bool ParticleDocument::editEmitter(const QString& text, const std::function<void(Emitter&)>& change, const QString& mergeKey)
{
    if (!emitter()) return false;
    return edit(text, [&](ParticleState& s) {
        change(s.file.effects[size_t(s.effect)].emitters[size_t(s.emitter)]);
        return true;
    }, mergeKey);
}

QString ParticleDocument::uniqueEffectName(const QString& base) const
{
    auto taken = [&](const QString& n) { return m_state.file.find(u8(n)) != nullptr; };
    if (!taken(base)) return base;
    for (int i = 2;; i++)
        if (!taken(QStringLiteral("%1 %2").arg(base).arg(i))) return QStringLiteral("%1 %2").arg(base).arg(i);
}

QString ParticleDocument::uniqueEmitterName(const QString& base) const
{
    const Effect* e = effect();
    auto taken = [&](const QString& n) {
        if (!e) return false;
        for (const Emitter& m : e->emitters)
            if (qs(m.name) == n) return true;
        return false;
    };
    if (!taken(base)) return base;
    for (int i = 2;; i++)
        if (!taken(QStringLiteral("%1 %2").arg(base).arg(i))) return QStringLiteral("%1 %2").arg(base).arg(i);
}

bool ParticleDocument::addEffect(Effect e)
{
    if (e.name.empty() || m_state.file.find(e.name)) e.name = u8(uniqueEffectName(e.name.empty() ? QStringLiteral("effect") : qs(e.name)));
    return edit(tr("Add effect"), [&](ParticleState& s) {
        const int at = s.file.effects.empty() ? 0 : s.effect + 1;
        s.file.effects.insert(s.file.effects.begin() + at, e);
        s.effect = at;
        s.emitter = e.emitters.empty() ? -1 : 0;
        return true;
    });
}

bool ParticleDocument::removeEffect(int index)
{
    if (index < 0 || index >= int(m_state.file.effects.size())) return false;
    return edit(tr("Delete effect"), [&](ParticleState& s) {
        s.file.effects.erase(s.file.effects.begin() + index);
        s.effect = std::clamp(index, 0, std::max(0, int(s.file.effects.size()) - 1));
        s.emitter = s.file.effects.empty() || s.file.effects[size_t(s.effect)].emitters.empty() ? -1 : 0;
        return true;
    });
}

bool ParticleDocument::addEmitter(Emitter m)
{
    if (!effect()) return false;
    m.name = u8(uniqueEmitterName(m.name.empty() ? QStringLiteral("emitter") : qs(m.name)));
    return edit(tr("Add emitter"), [&](ParticleState& s) {
        auto& list = s.file.effects[size_t(s.effect)].emitters;
        const int at = s.emitter < 0 ? int(list.size()) : s.emitter + 1;
        list.insert(list.begin() + at, m);
        s.emitter = at;
        return true;
    });
}

bool ParticleDocument::removeEmitter(int index)
{
    const Effect* e = effect();
    if (!e || index < 0 || index >= int(e->emitters.size())) return false;
    return edit(tr("Delete emitter"), [&](ParticleState& s) {
        auto& list = s.file.effects[size_t(s.effect)].emitters;
        list.erase(list.begin() + index);
        s.emitter = list.empty() ? -1 : std::min(index, int(list.size()) - 1);
        return true;
    });
}

bool ParticleDocument::duplicateEmitter(int index)
{
    const Effect* e = effect();
    if (!e || index < 0 || index >= int(e->emitters.size())) return false;
    Emitter copy = e->emitters[size_t(index)];
    copy.name = u8(uniqueEmitterName(qs(copy.name)));
    return edit(tr("Duplicate emitter"), [&](ParticleState& s) {
        auto& list = s.file.effects[size_t(s.effect)].emitters;
        list.insert(list.begin() + index + 1, copy);
        s.emitter = index + 1;
        return true;
    });
}

bool ParticleDocument::moveEmitter(int index, int delta)
{
    const Effect* e = effect();
    const int to = index + delta;
    if (!e || index < 0 || index >= int(e->emitters.size()) || to < 0 || to >= int(e->emitters.size())) return false;
    return edit(tr("Move emitter"), [&](ParticleState& s) {
        auto& list = s.file.effects[size_t(s.effect)].emitters;
        std::swap(list[size_t(index)], list[size_t(to)]);
        s.emitter = to;
        return true;
    });
}

// ---- atlases --------------------------------------------------------------------------------------

void ParticleDocument::loadAtlases()
{
    std::vector<std::shared_ptr<LoadedAtlas>> next;
    for (const QString& path : m_state.atlasesAbs) {
        std::shared_ptr<LoadedAtlas> a;
        for (const auto& old : m_atlases)
            if (old->path == path) a = old;
        if (!a) {
            a = loadOne(path);
            if (!a->error.isEmpty()) emit message(tr("Atlas: %1").arg(a->error));
        }
        next.push_back(a);
    }
    m_atlases = std::move(next);
    m_atlasSet = toms::anim::AtlasSet();
    m_textures.clear();
    for (size_t i = 0; i < m_atlases.size(); i++) {
        const LoadedAtlas& a = *m_atlases[i];
        if (!a.ok) continue;
        std::vector<uint16_t> pages;
        for (size_t p = 0; p < a.atlas.pages.size(); p++) {
            pages.push_back(uint16_t(m_textures.size()));
            m_textures.push_back({int(i), int(p)});
        }
        m_atlasSet.add(a.atlas, u8(atlasId(int(i))), std::move(pages));
    }
    emit atlasChanged();
}

bool ParticleDocument::addAtlas(const QString& pathIn, QString* error)
{
    const QString path = QFileInfo(pathIn).absoluteFilePath();
    if (m_state.atlasesAbs.contains(path, Qt::CaseInsensitive)) {
        if (error) *error = tr("%1 is in the list already").arg(QDir::toNativeSeparators(path));
        return false;
    }
    if (!m_filePath.isEmpty() && QDir::isAbsolutePath(QFileInfo(m_filePath).absoluteDir().relativeFilePath(path))) {
        if (error) *error = tr("%1 is on another drive than the .particle file: it cannot be stored relative to it")
                                .arg(QDir::toNativeSeparators(path));
        return false;
    }
    QString id = qs(toms::anim::defaultAtlasId(u8(QFileInfo(path).fileName())));
    const QString base = id;
    for (int n = 2; m_state.atlasIds.contains(id); n++) id = QStringLiteral("%1%2").arg(base).arg(n);
    return edit(tr("Add atlas"), [&](ParticleState& s) {
        s.atlasesAbs << path;
        s.atlasIds << id;
        return true;
    });
}

bool ParticleDocument::removeAtlas(int index)
{
    if (index < 0 || index >= m_state.atlasesAbs.size()) return false;
    return edit(tr("Remove atlas"), [&](ParticleState& s) {
        s.atlasesAbs.removeAt(index);
        s.atlasIds.removeAt(index);
        return true;
    });
}

const SpriteImageCache* ParticleDocument::pageImages(uint16_t texture, int* page) const
{
    if (texture >= m_textures.size()) return nullptr;
    const auto [atlas, p] = m_textures[texture];
    if (page) *page = p;
    return &m_atlases[size_t(atlas)]->images;
}

QStringList ParticleDocument::spriteRefs() const
{
    QStringList out;
    for (int i = 0; i < atlasCount(); i++)
        if (atlasAt(i).ok)
            for (const QString& n : atlasAt(i).spriteNames) out << atlasId(i) + QLatin1Char(':') + n;
    return out;
}

QImage ParticleDocument::spriteImage(const QString& ref) const
{
    uint16_t tex = 0;
    const toms::AtlasRegion* r = m_atlasSet.find(u8(ref), &tex);
    int page = 0;
    const SpriteImageCache* img = r ? pageImages(tex, &page) : nullptr;
    return img ? img->cut(page, QRect(r->x, r->y, r->w, r->h)) : QImage();
}

std::vector<toms::fx::Problem> ParticleDocument::problems() const
{
    std::vector<toms::fx::Problem> out = toms::fx::checkParticles(m_state.file, &m_atlasSet);
    for (int i = 0; i < atlasCount(); i++)
        if (!atlasAt(i).ok) out.insert(out.begin(), {u8(atlasId(i)), u8(atlasAt(i).error), false});
    return out;
}

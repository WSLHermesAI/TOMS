#include "AtlasDocument.h"

#include "Commands.h"
#include "atlas_export.h"
#include "atlas_store.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QPainter>
#include <QSet>
#include <QTimer>
#include <QUndoStack>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <set>

using atlas::Project;
using atlas::SpriteDef;

namespace {

std::string u8(const QString& s) { return s.toStdString(); }
QString qs(const std::string& s) { return QString::fromStdString(s); }
QString native(const std::string& s) { return QDir::toNativeSeparators(qs(s)); }

constexpr int kRebuildDelayMs = 120;
constexpr int kThumbnailSize = 64;

// The entry for `name`, created for an image that has none yet.
SpriteDef& entryFor(Project& p, const std::string& name)
{
    if (SpriteDef* d = p.findSprite(name)) return *d;
    SpriteDef d;
    d.name = name;
    p.sprites.push_back(d);
    return p.sprites.back();
}

// A settings-only entry (no file, no parent) that holds nothing is noise in the project file.
bool isEmptySettingsEntry(const SpriteDef& d)
{
    return d.file.empty() && d.parent.empty() && !d.bake && !d.exclude && !d.hasPivot && !d.hasSplit && d.trim < 0 &&
           !d.pinned && d.tags.empty();
}

void pruneEmptyEntries(Project& p)
{
    p.sprites.erase(std::remove_if(p.sprites.begin(), p.sprites.end(), isEmptySettingsEntry), p.sprites.end());
}

// The page as a QImage: atlas pages are straight RGBA8 (premultiplied only when the project
// says so); painting wants premultiplied ARGB32.
QImage toQImage(const atlas::Image& img, bool premultiplied)
{
    if (!img.valid()) return QImage();
    QImage wrap(img.px.data(), img.w, img.h, img.w * 4,
                premultiplied ? QImage::Format_RGBA8888_Premultiplied : QImage::Format_RGBA8888);
    return wrap.convertToFormat(QImage::Format_ARGB32_Premultiplied);   // deep copy
}

// Runs on a worker thread: builds, lists the sprites and prepares images.
BuildSnapshotPtr runBuild(Project p, std::string variant, quint64 generation)
{
    QElapsedTimer timer;
    timer.start();
    auto snap = std::make_shared<BuildSnapshot>();
    snap->generation = generation;
    snap->variant = qs(variant);
    const atlas::ImageSource src = atlas::fileImageSource();   // embedded projects read no files
    snap->result = atlas::build(p, src, atlas::BuildOptions{variant, true});

    const atlas::Variant* v = variant.empty() ? nullptr : p.findVariant(variant);
    for (const atlas::ResolvedSprite& rs : atlas::resolveSprites(p, src)) {
        SpriteInfo info;
        info.name = qs(rs.def.name);
        info.parent = qs(rs.def.parent);
        info.child = rs.def.isChild();
        info.variantArt = v && v->images.count(rs.def.name) > 0;
        snap->spriteIndex.insert(info.name, int(snap->sprites.size()));
        snap->sprites.push_back(info);
    }

    for (const atlas::Page& page : snap->result.pages)
        snap->pages.push_back(toQImage(page.image, p.settings.premultiplyAlpha));
    for (const atlas::Region& r : snap->result.regions) {
        if (r.page < 0 || r.page >= snap->pages.size() || r.frame.empty()) continue;
        QImage t = snap->pages[r.page].copy(r.frame.x, r.frame.y, r.frame.w, r.frame.h);
        if (t.width() > kThumbnailSize || t.height() > kThumbnailSize)
            t = t.scaled(kThumbnailSize, kThumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        snap->thumbnails.insert(qs(r.name), t);
    }
    snap->elapsedMs = timer.elapsed();
    return snap;
}

// Export Copy: writes the atlas of the base art and/or variants to folders of the user's choice;
// runs on a worker thread. Never touches the project file or its packed atlas.
std::pair<bool, QString> runExport(Project p, std::vector<std::pair<std::string, std::string>> jobs)   // variant, dir
{
    QStringList lines;
    bool ok = true;
    for (const auto& [v, dir] : jobs) {
        const atlas::Variant* var = v.empty() ? nullptr : p.findVariant(v);
        const atlas::Output& out = var ? var->output : p.output;
        const QString label = v.empty() ? QObject::tr("base") : QObject::tr("variant %1").arg(qs(v));
        atlas::BuildResult b = atlas::build(p, atlas::BuildOptions{v, true});
        if (!b.ok()) {
            lines << QObject::tr("%1: %2 error(s), nothing written").arg(label).arg(b.errorCount());
            ok = false;
            continue;
        }
        // The Cocos .plist always goes along (Cocos Creator's Sprite Atlas), whatever the project
        // lists; the Properties dock shows that box as always on.
        std::vector<std::string> formats = out.formats;
        if (std::find(formats.begin(), formats.end(), kAlwaysExported) == formats.end()) formats.push_back(kAlwaysExported);
        std::vector<atlas::ExportFile> files;
        std::string err;
        if (!atlas::exportFiles(b, formats, files, &err) || !atlas::writeFiles(dir, files, &err)) {
            lines << QObject::tr("%1: %2").arg(label, qs(err));
            ok = false;
            continue;
        }
        QStringList names;
        for (const atlas::ExportFile& f : files) names << qs(f.name);
        lines << QObject::tr("%1: wrote %2 to %3").arg(label, names.join(QStringLiteral(", ")), native(dir));
    }
    return {ok, lines.join(QLatin1Char('\n'))};
}

struct SaveJob {
    bool ok = false;
    std::string error;
    atlas::SaveResult result;
    Project project;   // as saved (paths rebased for Save As)
};

}  // namespace

// ---- BuildSnapshot ------------------------------------------------------------------------------

const SpriteInfo* BuildSnapshot::sprite(const QString& name) const
{
    auto it = spriteIndex.find(name);
    return it == spriteIndex.end() ? nullptr : &sprites[size_t(*it)];
}

const atlas::Region* BuildSnapshot::region(const QString& name) const { return result.find(u8(name)); }

// ---- AtlasDocument ------------------------------------------------------------------------------

AtlasDocument::AtlasDocument(QObject* parent)
    : QObject(parent)
    , m_undo(new QUndoStack(this))
    , m_rebuildTimer(new QTimer(this))
{
    m_rebuildTimer->setSingleShot(true);
    m_rebuildTimer->setInterval(kRebuildDelayMs);
    connect(m_rebuildTimer, &QTimer::timeout, this, &AtlasDocument::startBuild);
    connect(&m_buildWatcher, &QFutureWatcherBase::finished, this, &AtlasDocument::onBuildDone);
    connect(&m_exportWatcher, &QFutureWatcherBase::finished, this, [this] {
        const auto r = m_exportWatcher.result();
        emit exportFinished(r.first, r.second);
    });
    connect(m_undo, &QUndoStack::cleanChanged, this, [this] { emit dirtyChanged(isDirty()); });
    newProject();
}

AtlasDocument::~AtlasDocument()
{
    // The jobs only hold copies, but finishing them here keeps the worker threads from outliving
    // the editor's state at shutdown.
    m_buildWatcher.waitForFinished();
    m_exportWatcher.waitForFinished();
}

QString AtlasDocument::filePath() const { return qs(m_project.filePath); }

QString AtlasDocument::displayName() const
{
    return m_project.filePath.empty() ? tr("Untitled") : QFileInfo(filePath()).fileName();
}

bool AtlasDocument::isDirty() const { return m_forcedDirty || !m_undo->isClean(); }

void AtlasDocument::setForcedDirty(bool on)
{
    if (m_forcedDirty == on) return;
    m_forcedDirty = on;
    emit dirtyChanged(isDirty());
}

QString AtlasDocument::storeFile(const QString& variant) const
{
    const atlas::Variant* v = variant.isEmpty() ? nullptr : m_project.findVariant(u8(variant));
    return qs(atlas::storePath(m_project, v));
}

void AtlasDocument::reset(const Project& p, bool dirty)
{
    m_project = p;
    m_changeCount++;
    m_undo->clear();
    m_forcedDirty = dirty;
    m_selection.clear();
    m_variant.clear();
    m_snapshot.reset();
    m_spriteImages.clear();
    emit projectReset();
    emit filePathChanged();
    emit variantChanged();
    emit selectionChanged();
    emit projectChanged();
    emit dirtyChanged(isDirty());
    // The first build of a project should not wait for the debounce.
    m_rebuildTimer->stop();
    m_generation++;
    startBuild();
}

void AtlasDocument::newProject()
{
    Project p;
    p.name = "atlas";
    p.embedded = true;
    p.output.dir = ".";
    reset(p, false);
}

void AtlasDocument::adopt(const Project& p)
{
    Project q = p;
    q.embedded = true;
    reset(q, true);
}

bool AtlasDocument::open(const QString& pathIn, QString* error)
{
    // Absolute: relative paths in the project are rebased against it on Save As.
    const QString path = QDir::fromNativeSeparators(QFileInfo(pathIn).absoluteFilePath());
    Project p;
    std::string err;
    if (!atlas::openProject(u8(path), p, &err)) {
        if (error) *error = qs(err);
        return false;
    }
    if (p.embedded) {
        reset(p, false);
        emit message(tr("Opened %1 (%n image(s))", nullptr, int(p.images.size())).arg(QDir::toNativeSeparators(path)));
        return true;
    }
    // A folder project (atlaspack pack, build scripts): take its images in. The folders stay as
    // references to import new or changed art from.
    std::vector<atlas::Diagnostic> diags;
    const int n = atlas::embedSources(p, atlas::fileImageSource(), &diags);
    p.output.dir = ".";
    reset(p, true);
    emit message(tr("Opened %1 (a folder project: %n image(s) taken in)", nullptr, n).arg(QDir::toNativeSeparators(path)));
    for (const atlas::Diagnostic& d : diags) emit message(qs(atlas::formatDiagnostic(d)));
    emit message(tr("Converted to an embedded project — Save writes the packed atlas next to the project; "
                    "the folders are now references."));
    return true;
}

bool AtlasDocument::writeBackup(const QString& path, QString* error) const
{
    const QFileInfo fi(path);
    const QDir dir(QDir(fi.absolutePath()).filePath(fi.completeBaseName()));
    if (!QDir().mkpath(dir.absolutePath())) {
        if (error) *error = tr("cannot create %1").arg(QDir::toNativeSeparators(dir.absolutePath()));
        return false;
    }
    Project copy = m_project;
    copy.embedded = true;
    copy.output.dir = ".";   // stays next to the project copy
    for (atlas::Variant& v : copy.variants)   // absolute: kept as they are by the save
        v.output.dir = u8(QDir::fromNativeSeparators(dir.filePath(QStringLiteral("variants/") + qs(v.id))));
    const std::string target = u8(QDir::fromNativeSeparators(dir.filePath(qs(copy.name.empty() ? std::string("atlas") : copy.name) +
                                                                             QStringLiteral(".atlasproj"))));
    (void)QtConcurrent::run([copy, target]() mutable {
        std::string err;
        if (!atlas::saveProjectAll(target, copy, {kAlwaysExported}, nullptr, &err))
            std::fprintf(stderr, "[atlas] backup failed: %s\n", err.c_str());
    });
    return true;
}

bool AtlasDocument::save(const QString& path, SaveReport* report, QString* error)
{
    if (m_saving) {
        if (error) *error = tr("A save is already running.");
        return false;
    }
    m_saving = true;
    emit saveStarted();
    // The worker saves a copy; edits made meanwhile (the event loop keeps running) are noticed
    // through m_changeCount and keep the document dirty.
    const quint64 changeAtStart = m_changeCount;
    Project copy = m_project;
    copy.embedded = true;
    const std::string target = u8(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
    QFutureWatcher<SaveJob> watcher;
    QEventLoop loop;
    connect(&watcher, &QFutureWatcherBase::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run([copy, target]() mutable {
        SaveJob job;
        job.ok = atlas::saveProjectAll(target, copy, {kAlwaysExported}, &job.result, &job.error);
        job.project = std::move(copy);
        return job;
    }));
    if (!watcher.isFinished()) loop.exec(QEventLoop::ExcludeUserInputEvents);
    SaveJob job = watcher.result();
    m_saving = false;

    if (report) {
        report->written.clear();
        for (const std::string& f : job.result.written) report->written << native(f);
        report->diagnostics = job.result.diagnostics;
    }
    if (!job.ok) {
        if (error) *error = qs(job.error);
        emit message(tr("Save failed (nothing was written): %1").arg(qs(job.error)));
        emit saveFinished(false);
        return false;
    }
    const bool moved = job.project.filePath != m_project.filePath;
    if (m_changeCount == changeAtStart) {
        // Keep the images of the live project (the same pointers) and take the rebased paths.
        m_project = std::move(job.project);
        m_undo->setClean();
        setForcedDirty(false);
    } else {
        // Edited while saving: those edits are not on disk yet.
        if (moved) atlas::rebaseProject(m_project, job.project.filePath);
        emit message(tr("The project changed while it was being saved; save again to keep those changes."));
    }
    emit filePathChanged();
    emit dirtyChanged(isDirty());
    if (moved) {
        emit projectChanged();
        scheduleRebuild();
    }
    emit message(tr("Saved %1 (%n file(s) written)", nullptr, int(job.result.written.size())).arg(QDir::toNativeSeparators(path)));
    emit saveFinished(true);
    return true;
}

void AtlasDocument::setSelection(const QStringList& names)
{
    QStringList clean;
    for (const QString& n : names)
        if (!n.isEmpty() && !clean.contains(n)) clean << n;
    if (clean == m_selection) return;
    m_selection = clean;
    emit selectionChanged();
}

void AtlasDocument::setVariant(const QString& id)
{
    if (id == m_variant) return;
    m_variant = id;
    emit variantChanged();
    scheduleRebuild();
}

const atlas::Variant* AtlasDocument::currentVariant() const
{
    return m_variant.isEmpty() ? nullptr : m_project.findVariant(u8(m_variant));
}

bool AtlasDocument::isBuilding() const { return m_buildWatcher.isRunning() || m_rebuildTimer->isActive(); }

QImage AtlasDocument::spriteImage(const QString& name) const
{
    if (!m_snapshot) return QImage();
    auto it = m_spriteImages.find(name);
    if (it != m_spriteImages.end()) return *it;
    QImage img;
    const atlas::Region* r = m_snapshot->region(name);
    if (r && r->page >= 0 && r->page < m_snapshot->pages.size()) {
        img = QImage(std::max(r->origW, 1), std::max(r->origH, 1), QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.drawImage(QPoint(r->offsetX, r->offsetY), m_snapshot->pages[r->page],
                    QRect(r->frame.x, r->frame.y, r->frame.w, r->frame.h));
    }
    m_spriteImages.insert(name, img);
    return img;
}

SpriteDef AtlasDocument::spriteDef(const QString& name) const
{
    if (const SpriteDef* d = m_project.findSprite(u8(name))) return *d;
    SpriteDef d;
    d.name = u8(name);
    return d;
}

bool AtlasDocument::isImage(const QString& name) const { return m_project.images.count(u8(name)) > 0; }

bool AtlasDocument::hasVariantArt(const QString& name, const QString& variant) const
{
    const atlas::Variant* v = variant.isEmpty() ? nullptr : m_project.findVariant(u8(variant));
    return v && v->images.count(u8(name)) > 0;
}

bool AtlasDocument::spriteExists(const QString& name) const
{
    if (isImage(name) || m_project.findSprite(u8(name))) return true;
    return m_snapshot && m_snapshot->sprite(name);
}

QString AtlasDocument::uniqueName(const QString& base) const
{
    if (!spriteExists(base)) return base;
    for (int i = 1;; i++) {
        const QString n = QStringLiteral("%1_%2").arg(base).arg(i);
        if (!spriteExists(n)) return n;
    }
}

QStringList AtlasDocument::descendants(const QString& name) const
{
    QStringList out;
    QSet<QString> roots{name};
    bool grew = true;
    while (grew) {
        grew = false;
        for (const SpriteDef& d : m_project.sprites) {
            const QString n = qs(d.name);
            if (d.isChild() && roots.contains(qs(d.parent)) && !roots.contains(n)) {
                roots.insert(n);
                out << n;
                grew = true;
            }
        }
    }
    return out;
}

QStringList AtlasDocument::deletionClosure(const QStringList& names) const
{
    QStringList out;
    for (const QString& n : names) {
        if (!out.contains(n)) out << n;
        for (const QString& c : descendants(n))
            if (!out.contains(c)) out << c;
    }
    return out;
}

QString AtlasDocument::toProjectPath(const QString& absolutePath) const
{
    if (m_project.filePath.empty()) return QDir::fromNativeSeparators(QDir::cleanPath(absolutePath));
    return qs(m_project.relativize(u8(QDir::fromNativeSeparators(absolutePath))));
}

QString AtlasDocument::resolvePath(const QString& projectPath) const { return qs(m_project.resolve(u8(projectPath))); }

// ---- editing ------------------------------------------------------------------------------------

bool AtlasDocument::edit(const QString& text, const std::function<void(Project&)>& change, const QString& mergeKey,
                         const QStringList* selectionAfter)
{
    Project p = m_project;   // cheap: images are shared
    change(p);
    if (sameProjectState(p, m_project)) {
        if (selectionAfter) setSelection(*selectionAfter);
        return false;
    }
    m_undo->push(new ProjectCommand(this, text, m_project, std::move(p), m_selection,
                                    selectionAfter ? *selectionAfter : m_selection, mergeKey));
    return true;
}

bool AtlasDocument::editSprites(const QStringList& names, const QString& text, const std::function<void(SpriteDef&)>& change,
                                const QString& mergeKey)
{
    if (names.isEmpty()) return false;
    return edit(text, [&](Project& p) {
        for (const QString& n : names) change(entryFor(p, u8(n)));
        pruneEmptyEntries(p);
    }, mergeKey.isEmpty() ? QString() : mergeKey + QLatin1Char('|') + names.join(QLatin1Char('|')));
}

void AtlasDocument::restoreState(const Project& state, const QStringList& selection)
{
    Project p = state;
    // A step recorded before Save As holds paths relative to the old place.
    if (p.filePath != m_project.filePath && !m_project.filePath.empty()) atlas::rebaseProject(p, m_project.filePath);
    p.filePath = m_project.filePath;
    m_project = std::move(p);
    m_changeCount++;
    emit projectChanged();
    scheduleRebuild();
    setSelection(selection);
}

bool AtlasDocument::addChildren(const QString& parent, const std::vector<std::pair<QString, atlas::IRect>>& children,
                                const QString& text)
{
    if (children.empty()) return false;
    QStringList names;
    for (const auto& c : children) names << c.first;
    return edit(text, [&](Project& p) {
        for (const auto& c : children) {
            SpriteDef d;
            d.name = u8(c.first);
            d.parent = u8(parent);
            d.rect = c.second;
            p.sprites.push_back(d);
        }
    }, QString(), &names);
}

bool AtlasDocument::renameSprite(const QString& from, const QString& to, QString* error)
{
    const QString name = to.trimmed();
    if (name == from) return true;
    if (name.isEmpty()) {
        if (error) *error = tr("A sprite needs a name.");
        return false;
    }
    if (spriteExists(name)) {
        if (error) *error = tr("There already is a sprite called '%1'.").arg(name);
        return false;
    }
    if (!isImage(from) && !m_project.findSprite(u8(from))) {
        if (error) *error = tr("'%1' is not in the project.").arg(from);
        return false;
    }
    QStringList sel = m_selection;
    for (QString& s : sel)
        if (s == from) s = name;
    return edit(tr("Rename %1").arg(from), [&](Project& p) {
        const std::string f = u8(from), t = u8(name);
        auto move = [&](std::map<std::string, atlas::ImagePtr>& m) {
            auto it = m.find(f);
            if (it == m.end()) return;
            atlas::ImagePtr img = it->second;
            m.erase(it);
            m[t] = img;
        };
        move(p.images);
        for (atlas::Variant& v : p.variants) move(v.images);
        for (SpriteDef& d : p.sprites) {
            if (d.name == f) d.name = t;
            if (d.parent == f) d.parent = t;
        }
        for (atlas::Animation& a : p.animations)
            for (atlas::AnimFrame& fr : a.frames)
                if (fr.sprite == f) fr.sprite = t;
    }, QString(), &sel);
}

void AtlasDocument::deleteSprites(const QStringList& names)
{
    std::set<std::string> removed;
    for (const QString& n : deletionClosure(names))
        if (spriteExists(n)) removed.insert(u8(n));
    if (removed.empty()) return;
    const QStringList none;
    edit(names.size() == 1 ? tr("Delete %1").arg(names.first()) : tr("Delete %n sprite(s)", nullptr, int(names.size())),
         [&](Project& p) {
             for (const std::string& n : removed) {
                 p.images.erase(n);
                 for (atlas::Variant& v : p.variants) v.images.erase(n);
             }
             p.sprites.erase(std::remove_if(p.sprites.begin(), p.sprites.end(),
                                            [&](const SpriteDef& d) { return removed.count(d.name) > 0; }),
                             p.sprites.end());
             for (atlas::Animation& a : p.animations)
                 a.frames.erase(std::remove_if(a.frames.begin(), a.frames.end(),
                                               [&](const atlas::AnimFrame& f) { return removed.count(f.sprite) > 0; }),
                                a.frames.end());
         }, QString(), &none);
}

// ---- images -------------------------------------------------------------------------------------

QList<ImageFile> AtlasDocument::collectPngs(const QStringList& paths)
{
    QList<ImageFile> out;
    QSet<QString> names;
    auto add = [&](const QString& name, const QString& file) {
        if (name.isEmpty() || names.contains(name)) return;
        names.insert(name);
        out.push_back({name, file});
    };
    const atlas::ImageSource src = atlas::fileImageSource();
    for (const QString& path : paths) {
        const QFileInfo fi(path);
        if (fi.isDir()) {
            const std::string dir = u8(QDir::fromNativeSeparators(fi.absoluteFilePath()));
            for (const std::string& f : src.listPngs(dir, true)) add(qs(atlas::imageNameFor(f, dir)), qs(f));
        } else if (fi.isFile() && fi.suffix().compare(QLatin1String("png"), Qt::CaseInsensitive) == 0) {
            const QString file = QDir::fromNativeSeparators(fi.absoluteFilePath());
            add(qs(atlas::imageNameFor(u8(file))), file);
        }
    }
    return out;
}

int AtlasDocument::setImages(const QList<ImageFile>& files, const QString& variant, const QString& text, QStringList* errors)
{
    // Decode first: a file that does not load leaves the project alone.
    Project p = m_project;
    QStringList added;
    for (const ImageFile& f : files) {
        atlas::Image img;
        std::string err;
        if (!atlas::loadImage(u8(f.path), img, &err) || !atlas::setImage(p, u8(f.name), img, u8(variant), &err)) {
            if (errors) *errors << tr("%1: %2").arg(QDir::toNativeSeparators(f.path), qs(err));
            continue;
        }
        added << f.name;
    }
    if (added.isEmpty()) return 0;
    edit(text.isEmpty() ? tr("Add %n image(s)", nullptr, int(added.size())) : text, [&](Project& q) { q = p; }, QString(), &added);
    return int(added.size());
}

bool AtlasDocument::removeVariantArt(const QStringList& names, const QString& variant)
{
    if (variant.isEmpty() || names.isEmpty()) return false;
    return edit(tr("Remove %1 art").arg(variant), [&](Project& p) {
        for (atlas::Variant& v : p.variants)
            if (v.id == u8(variant))
                for (const QString& n : names) v.images.erase(u8(n));
    });
}

bool AtlasDocument::saveSpriteImage(const QString& name, const QString& file, QString* error) const
{
    // An image is saved exactly as held (the variant art when the canvas shows a variant); a
    // child is cut out of the last build.
    atlas::Image img;
    const atlas::Variant* v = currentVariant();
    auto it = v ? v->images.find(u8(name)) : m_project.images.end();
    if (v && it != v->images.end() && it->second) img = *it->second;
    else if (auto b = m_project.images.find(u8(name)); b != m_project.images.end() && b->second) img = *b->second;
    else if (const atlas::Region* r = m_snapshot ? m_snapshot->region(name) : nullptr) {
        img = atlas::renderRegion(m_snapshot->result, *r);
        if (m_project.settings.premultiplyAlpha) atlas::unpremultiply(img);
    }
    if (!img.valid()) {
        if (error) *error = tr("'%1' has no pixels to save.").arg(name);
        return false;
    }
    std::string err;
    if (!atlas::savePng(u8(QDir::fromNativeSeparators(file)), img, &err)) {
        if (error) *error = qs(err);
        return false;
    }
    return true;
}

bool AtlasDocument::extractImages(const QString& dir, bool children, int* count, QString* error) const
{
    std::vector<std::string> written;
    std::string err;
    if (!atlas::extractImages(m_project, u8(QDir::fromNativeSeparators(dir)), u8(m_variant), children, &written, &err)) {
        if (error) *error = qs(err);
        return false;
    }
    if (count) *count = int(written.size());
    return true;
}

// ---- references ---------------------------------------------------------------------------------

bool AtlasDocument::addReference(const QString& dir)
{
    const QString abs = QDir::cleanPath(QFileInfo(dir).absoluteFilePath());
    for (const atlas::SourceFolder& f : m_project.references)
        if (QDir::cleanPath(QFileInfo(resolvePath(qs(f.path))).absoluteFilePath()).compare(abs, Qt::CaseInsensitive) == 0) {
            emit message(tr("%1 is already a reference folder").arg(QDir::toNativeSeparators(abs)));
            return false;
        }
    return edit(tr("Add reference folder"), [&](Project& p) {
        atlas::SourceFolder f;
        f.path = u8(toProjectPath(abs));
        p.references.push_back(f);
    });
}

bool AtlasDocument::removeReference(int index)
{
    if (index < 0 || index >= int(m_project.references.size())) return false;
    return edit(tr("Remove reference folder"), [&](Project& p) { p.references.erase(p.references.begin() + index); });
}

bool AtlasDocument::setVariantReference(const QString& variant, const QString& dir)
{
    const std::string path = dir.isEmpty() ? std::string() : u8(toProjectPath(QDir::cleanPath(QFileInfo(dir).absoluteFilePath())));
    return edit(tr("Variant reference folder"), [&](Project& p) {
        for (atlas::Variant& v : p.variants)
            if (v.id == u8(variant)) v.reference = path;
    });
}

// ---- builds -------------------------------------------------------------------------------------

void AtlasDocument::scheduleRebuild()
{
    m_generation++;
    m_rebuildTimer->start();
}

void AtlasDocument::startBuild()
{
    if (m_buildWatcher.isRunning()) {
        m_rebuildQueued = true;
        return;
    }
    emit buildStarted();
    m_buildWatcher.setFuture(QtConcurrent::run(runBuild, m_project, u8(m_variant), m_generation));
}

void AtlasDocument::onBuildDone()
{
    BuildSnapshotPtr snap = m_buildWatcher.result();
    if (m_rebuildQueued) {
        m_rebuildQueued = false;
        startBuild();
    }
    if (!snap || snap->generation != m_generation) return;   // the project changed meanwhile
    m_snapshot = std::move(snap);
    m_spriteImages.clear();
    emit buildFinished();
}

// ---- export copy --------------------------------------------------------------------------------

bool AtlasDocument::isExporting() const { return m_exportWatcher.isRunning(); }

void AtlasDocument::exportCopy(const QString& dir, bool allVariants)
{
    if (isExporting()) return;
    const std::string root = u8(QDir::fromNativeSeparators(QDir::cleanPath(dir)));
    std::vector<std::pair<std::string, std::string>> jobs;
    if (allVariants) {
        jobs.push_back({std::string(), root});
        for (const atlas::Variant& v : m_project.variants) jobs.push_back({v.id, atlas::joinPath(root, v.id)});
    } else {
        jobs.push_back({u8(m_variant), root});
    }
    m_exportWatcher.setFuture(QtConcurrent::run(runExport, m_project, jobs));
}

#pragma once

#include "atlas_build.h"

#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>
#include <vector>

class QTimer;
class QUndoStack;

// The format Save (and Export Copy) always writes, on top of the project's list: Cocos Creator
// reads sprite atlases only as a .plist.
inline constexpr const char* kAlwaysExported = "plist";

// One sprite as the editor lists it (atlas::resolveSprites() of the shown variant).
struct SpriteInfo {
    QString name;
    QString parent;          // child sprites: the parent's name
    bool child = false;
    bool variantArt = false; // the shown variant has its own art for this image
};

// Everything one background build produced, ready for the GUI thread. Immutable once published,
// so widgets may keep a shared_ptr to it while a newer build runs.
struct BuildSnapshot {
    quint64 generation = 0;
    QString variant;
    atlas::BuildResult result;
    std::vector<SpriteInfo> sprites;     // sorted by name
    QHash<QString, int> spriteIndex;     // name -> index in sprites
    QVector<QImage> pages;               // premultiplied, ready to paint
    QHash<QString, QImage> thumbnails;   // name -> small image (at most 64 px)
    qint64 elapsedMs = 0;

    const SpriteInfo* sprite(const QString& name) const;
    const atlas::Region* region(const QString& name) const;
};
using BuildSnapshotPtr = std::shared_ptr<const BuildSnapshot>;

// A PNG to bring into the project and the sprite name it gets.
struct ImageFile {
    QString name;
    QString path;
};

// What a save wrote, for the log.
struct SaveReport {
    QStringList written;
    std::vector<atlas::Diagnostic> diagnostics;
};

// The open .atlasproj and everything around it: undo stack, selection, current variant, the
// last build and background rebuilds. Every project in the editor is EMBEDDED: its images live
// in memory (atlas::Project::images) and in the packed atlas Save writes next to the project.
// Widgets never change the Project directly; they call edit()/editSprites() (or one of the
// higher level operations), which records an undo step and schedules a rebuild.
class AtlasDocument : public QObject
{
    Q_OBJECT

public:
    explicit AtlasDocument(QObject* parent = nullptr);
    ~AtlasDocument() override;

    const atlas::Project& project() const { return m_project; }
    QString filePath() const;
    QString displayName() const;   // file name, or "Untitled"
    bool isDirty() const;
    QUndoStack* undoStack() const { return m_undo; }
    // A copy for AutoBackup: the project and its packed atlas (where an embedded project keeps its
    // images) in a folder named after `path` (without the extension), every output inside it --
    // never the project's own output folders. Writes in the background; the document does not change.
    bool writeBackup(const QString& path, QString* error) const;

    void newProject();
    // Opens a project; a folder project is converted to an embedded one (and left dirty).
    bool open(const QString& path, QString* error);
    // Takes a project made elsewhere (e.g. imported); it starts out dirty.
    void adopt(const atlas::Project& p);
    // Writes the project, its packed atlas and output formats (plus kAlwaysExported) on a worker
    // thread, waiting with a local event loop. Also plain Save (same path). On failure nothing
    // was written and the document stays dirty.
    bool save(const QString& path, SaveReport* report, QString* error);
    bool isSaving() const { return m_saving; }
    QString storeFile(const QString& variant = QString()) const;   // the packed atlas Save writes

    // Selection: sprite names; the last one is the "current" sprite.
    const QStringList& selection() const { return m_selection; }
    QString currentSprite() const { return m_selection.isEmpty() ? QString() : m_selection.last(); }
    void setSelection(const QStringList& names);

    // Which art the canvas shows: "" = base, otherwise a variant id.
    QString variant() const { return m_variant; }
    void setVariant(const QString& id);
    const atlas::Variant* currentVariant() const;

    BuildSnapshotPtr snapshot() const { return m_snapshot; }
    bool isBuilding() const;
    // The sprite at its original size (transparent where trimmed), from the current build.
    QImage spriteImage(const QString& name) const;
    // The sprite's entry, or a default one when an image has none yet.
    atlas::SpriteDef spriteDef(const QString& name) const;
    bool isImage(const QString& name) const;                         // has pixels of its own
    bool hasVariantArt(const QString& name, const QString& variant) const;
    bool spriteExists(const QString& name) const;
    QString uniqueName(const QString& base) const;
    QStringList descendants(const QString& name) const;   // children, grandchildren, ...
    // Paths stored in the project: relative to the project file once it is saved, absolute
    // before (Save rebases them).
    QString toProjectPath(const QString& absolutePath) const;
    QString resolvePath(const QString& projectPath) const;

    // ---- editing (each call is one undo step) ----
    // mergeKey: consecutive edits with the same key and selection within a short time collapse
    // into one undo step (spin box arrows, typing).
    bool edit(const QString& text, const std::function<void(atlas::Project&)>& change,
              const QString& mergeKey = QString(), const QStringList* selectionAfter = nullptr);
    // Applies `change` to the entries of `names`, creating entries for images as needed and
    // dropping entries that end up holding nothing but defaults.
    bool editSprites(const QStringList& names, const QString& text,
                     const std::function<void(atlas::SpriteDef&)>& change, const QString& mergeKey = QString());
    // Child sprites inside `parent` (rects in the parent's original pixels); selects them.
    bool addChildren(const QString& parent, const std::vector<std::pair<QString, atlas::IRect>>& children,
                     const QString& text);
    // Renames a sprite: its image and variant art, its entry, children's parent, animation frames.
    bool renameSprite(const QString& from, const QString& to, QString* error);
    // Removes sprites with their images, variant art and child sprites.
    void deleteSprites(const QStringList& names);
    QStringList deletionClosure(const QStringList& names) const;   // names + every descendant

    // ---- images ----
    // PNG files (named by their file name) and folders (every PNG below, named by its path in
    // the folder), in order, without duplicate names.
    static QList<ImageFile> collectPngs(const QStringList& paths);
    // Copies the pixels in as one undo step: base art, or a variant's replacement art. Existing
    // images of the same name are replaced. Returns how many went in; `errors` gets the rest.
    int setImages(const QList<ImageFile>& files, const QString& variant, const QString& text, QStringList* errors);
    bool removeVariantArt(const QStringList& names, const QString& variant);
    bool saveSpriteImage(const QString& name, const QString& file, QString* error) const;
    bool extractImages(const QString& dir, bool children, int* count, QString* error) const;

    // ---- references (folders new or changed art is imported from) ----
    bool addReference(const QString& dir);
    bool removeReference(int index);
    bool setVariantReference(const QString& variant, const QString& dir);

    // ---- export copy (writes files elsewhere; runs in the background) ----
    bool isExporting() const;
    // Base (or the shown variant) into `dir`; with allVariants the base into `dir` and each
    // variant into `dir`/<variant id>. Never touches the project or its packed atlas.
    void exportCopy(const QString& dir, bool allVariants);

signals:
    void projectReset();      // another project was opened / created (also emits projectChanged)
    void projectChanged();
    void selectionChanged();
    void variantChanged();
    void filePathChanged();
    void dirtyChanged(bool dirty);
    void buildStarted();
    void buildFinished();
    void saveStarted();
    void saveFinished(bool ok);
    void exportFinished(bool ok, const QString& summary);
    void message(const QString& text);

private:
    friend class ProjectCommand;
    // Called by ProjectCommand on redo/undo.
    void restoreState(const atlas::Project& p, const QStringList& selection);

    void reset(const atlas::Project& p, bool dirty);
    void setForcedDirty(bool on);
    void scheduleRebuild();
    void startBuild();
    void onBuildDone();

    atlas::Project m_project;
    QUndoStack* m_undo;
    QStringList m_selection;
    QString m_variant;
    bool m_forcedDirty = false;      // converted / imported: unsaved although nothing was undone
    quint64 m_changeCount = 0;       // every change of m_project (edits, undo, redo)
    bool m_saving = false;

    QTimer* m_rebuildTimer;
    QFutureWatcher<BuildSnapshotPtr> m_buildWatcher;
    quint64 m_generation = 0;
    bool m_rebuildQueued = false;
    BuildSnapshotPtr m_snapshot;
    mutable QHash<QString, QImage> m_spriteImages;   // per snapshot

    QFutureWatcher<std::pair<bool, QString>> m_exportWatcher;
};

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
inline constexpr const char* g_pszAlwaysExported = "plist";

// One sprite as the editor lists it (atlas::resolveSprites() of the shown variant).
struct SpriteInfo {
    QString m_oName;
    QString m_oParent;          // child sprites: the parent's name
    bool m_bChild = false;
    bool m_bVariantArt = false; // the shown variant has its own art for this image
};

// Everything one background build produced, ready for the GUI thread. Immutable once published,
// so widgets may keep a shared_ptr to it while a newer build runs.
struct BuildSnapshot {
    quint64 m_u64Generation = 0;
    QString m_oVariant;
    atlas::BuildResult m_oResult;
    std::vector<SpriteInfo> m_vSprites;     // sorted by name
    QHash<QString, int> m_oSpriteIndex;     // name -> index in sprites
    QVector<QImage> m_oPages;               // premultiplied, ready to paint
    QHash<QString, QImage> m_oThumbnails;   // name -> small image (at most 64 px)
    qint64 m_i64ElapsedMs = 0;

    const SpriteInfo* f_Sprite(const QString& l_oName) const;
    const atlas::Region* f_Region(const QString& l_oName) const;
};
using BuildSnapshotPtr = std::shared_ptr<const BuildSnapshot>;

// A PNG to bring into the project and the sprite name it gets.
struct ImageFile {
    QString m_oName;
    QString m_oPath;
};

// What a save wrote, for the log.
struct SaveReport {
    QStringList m_oWritten;
    std::vector<atlas::Diagnostic> m_vDiagnostics;
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
    explicit AtlasDocument(QObject* l_pParent = nullptr);
    ~AtlasDocument() override;

    const atlas::Project& f_Project() const { return m_oProject; }
    QString f_FilePath() const;
    QString f_DisplayName() const;   // file name, or "Untitled"
    bool f_IsDirty() const;
    QUndoStack* f_UndoStack() const { return m_pUndo; }
    // A copy for AutoBackup: the project and its packed atlas (where an embedded project keeps its
    // images) in a folder named after `path` (without the extension), every output inside it --
    // never the project's own output folders. Writes in the background; the document does not change.
    bool f_WriteBackup(const QString& l_oPath, QString* l_pError) const;

    void f_NewProject();
    // Opens a project; a folder project is converted to an embedded one (and left dirty).
    bool f_Open(const QString& l_oPath, QString* l_pError);
    // Takes a project made elsewhere (e.g. imported); it starts out dirty.
    void f_Adopt(const atlas::Project& l_oP);
    // Writes the project, its packed atlas and output formats (plus kAlwaysExported) on a worker
    // thread, waiting with a local event loop. Also plain Save (same path). On failure nothing
    // was written and the document stays dirty.
    bool f_Save(const QString& l_oPath, SaveReport* l_pReport, QString* l_pError);
    bool f_IsSaving() const { return m_bSaving; }
    QString f_StoreFile(const QString& l_oVariant = QString()) const;   // the packed atlas Save writes

    // Selection: sprite names; the last one is the "current" sprite.
    const QStringList& f_Selection() const { return m_oSelection; }
    QString f_CurrentSprite() const { return m_oSelection.isEmpty() ? QString() : m_oSelection.last(); }
    void f_SetSelection(const QStringList& l_oNames);

    // Which art the canvas shows: "" = base, otherwise a variant id.
    QString f_Variant() const { return m_oVariant; }
    void f_SetVariant(const QString& l_oId);
    const atlas::Variant* f_CurrentVariant() const;

    BuildSnapshotPtr f_Snapshot() const { return m_spSnapshot; }
    bool f_IsBuilding() const;
    // The sprite at its original size (transparent where trimmed), from the current build.
    QImage f_SpriteImage(const QString& l_oName) const;
    // The sprite's entry, or a default one when an image has none yet.
    atlas::SpriteDef f_SpriteDef(const QString& l_oName) const;
    bool f_IsImage(const QString& l_oName) const;                         // has pixels of its own
    bool f_HasVariantArt(const QString& l_oName, const QString& l_oVariant) const;
    bool f_SpriteExists(const QString& l_oName) const;
    QString f_UniqueName(const QString& l_oBase) const;
    QStringList f_Descendants(const QString& l_oName) const;   // children, grandchildren, ...
    // Paths stored in the project: relative to the project file once it is saved, absolute
    // before (Save rebases them).
    QString f_ToProjectPath(const QString& l_oAbsolutePath) const;
    QString f_ResolvePath(const QString& l_oProjectPath) const;

    // ---- editing (each call is one undo step) ----
    // mergeKey: consecutive edits with the same key and selection within a short time collapse
    // into one undo step (spin box arrows, typing).
    bool f_Edit(const QString& l_oText, const std::function<void(atlas::Project&)>& l_fnChange,
              const QString& l_oMergeKey = QString(), const QStringList* l_pSelectionAfter = nullptr);
    // Applies `change` to the entries of `names`, creating entries for images as needed and
    // dropping entries that end up holding nothing but defaults.
    bool f_EditSprites(const QStringList& l_oNames, const QString& l_oText,
                     const std::function<void(atlas::SpriteDef&)>& l_fnChange, const QString& l_oMergeKey = QString());
    // Child sprites inside `parent` (rects in the parent's original pixels); selects them.
    bool f_AddChildren(const QString& l_oParent, const std::vector<std::pair<QString, atlas::IRect>>& l_vChildren,
                     const QString& l_oText);
    // Renames a sprite: its image and variant art, its entry, children's parent, animation frames.
    bool f_RenameSprite(const QString& l_oFrom, const QString& l_oTo, QString* l_pError);
    // Removes sprites with their images, variant art and child sprites.
    void f_DeleteSprites(const QStringList& l_oNames);
    QStringList f_DeletionClosure(const QStringList& l_oNames) const;   // names + every descendant

    // ---- images ----
    // PNG files (named by their file name) and folders (every PNG below, named by its path in
    // the folder), in order, without duplicate names.
    static QList<ImageFile> f_CollectPngs(const QStringList& l_oPaths);
    // Copies the pixels in as one undo step: base art, or a variant's replacement art. Existing
    // images of the same name are replaced. Returns how many went in; `errors` gets the rest.
    int f_SetImages(const QList<ImageFile>& l_oFiles, const QString& l_oVariant, const QString& l_oText, QStringList* l_pErrors);
    bool f_RemoveVariantArt(const QStringList& l_oNames, const QString& l_oVariant);
    bool f_SaveSpriteImage(const QString& l_oName, const QString& l_oFile, QString* l_pError) const;
    bool f_ExtractImages(const QString& l_oDir, bool l_bChildren, int* l_pCount, QString* l_pError) const;

    // ---- references (folders new or changed art is imported from) ----
    bool f_AddReference(const QString& l_oDir);
    bool f_RemoveReference(int l_iIndex);
    bool f_SetVariantReference(const QString& l_oVariant, const QString& l_oDir);

    // ---- export copy (writes files elsewhere; runs in the background) ----
    bool f_IsExporting() const;
    // Base (or the shown variant) into `dir`; with allVariants the base into `dir` and each
    // variant into `dir`/<variant id>. Never touches the project or its packed atlas.
    void f_ExportCopy(const QString& l_oDir, bool l_bAllVariants);

signals:
    void f_ProjectReset();      // another project was opened / created (also emits projectChanged)
    void f_ProjectChanged();
    void f_SelectionChanged();
    void f_VariantChanged();
    void f_FilePathChanged();
    void f_DirtyChanged(bool l_bDirty);
    void f_BuildStarted();
    void f_BuildFinished();
    void f_SaveStarted();
    void f_SaveFinished(bool l_bOk);
    void f_ExportFinished(bool l_bOk, const QString& l_oSummary);
    void f_Message(const QString& l_oText);

private:
    friend class ProjectCommand;
    // Called by ProjectCommand on redo/undo.
    void f_RestoreState(const atlas::Project& l_oP, const QStringList& l_oSelection);

    void f_Reset(const atlas::Project& l_oP, bool l_bDirty);
    void f_SetForcedDirty(bool l_bOn);
    void f_ScheduleRebuild();
    void f_StartBuild();
    void f_OnBuildDone();

    atlas::Project m_oProject;
    QUndoStack* m_pUndo;
    QStringList m_oSelection;
    QString m_oVariant;
    bool m_bForcedDirty = false;      // converted / imported: unsaved although nothing was undone
    quint64 m_u64ChangeCount = 0;       // every change of m_project (edits, undo, redo)
    bool m_bSaving = false;

    QTimer* m_pRebuildTimer;
    QFutureWatcher<BuildSnapshotPtr> m_oBuildWatcher;
    quint64 m_u64Generation = 0;
    bool m_bRebuildQueued = false;
    BuildSnapshotPtr m_spSnapshot;
    mutable QHash<QString, QImage> m_oSpriteImages;   // per snapshot

    QFutureWatcher<std::pair<bool, QString>> m_oExportWatcher;
};

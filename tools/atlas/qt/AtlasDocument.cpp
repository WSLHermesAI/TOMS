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

std::string f_U8(const QString& l_oS) { return l_oS.toStdString(); }
QString f_Qs(const std::string& l_strS) { return QString::fromStdString(l_strS); }
QString f_Native(const std::string& l_strS) { return QDir::toNativeSeparators(f_Qs(l_strS)); }

constexpr int g_iRebuildDelayMs = 120;
constexpr int g_iThumbnailSize = 64;

// The entry for `name`, created for an image that has none yet.
SpriteDef& f_EntryFor(Project& l_oP, const std::string& l_strName)
{
    if (SpriteDef* l_pD = l_oP.f_FindSprite(l_strName)) return *l_pD;
    SpriteDef l_oD;
    l_oD.m_strName = l_strName;
    l_oP.m_vSprites.push_back(l_oD);
    return l_oP.m_vSprites.back();
}

// A settings-only entry (no file, no parent) that holds nothing is noise in the project file.
bool f_IsEmptySettingsEntry(const SpriteDef& l_oD)
{
    return l_oD.m_strFile.empty() && l_oD.m_strParent.empty() && !l_oD.m_bBake && !l_oD.m_bExclude && !l_oD.m_bHasPivot && !l_oD.m_bHasSplit && l_oD.m_iTrim < 0 &&
           !l_oD.m_bPinned && l_oD.m_vTags.empty();
}

void f_PruneEmptyEntries(Project& l_oP)
{
    l_oP.m_vSprites.erase(std::remove_if(l_oP.m_vSprites.begin(), l_oP.m_vSprites.end(), f_IsEmptySettingsEntry), l_oP.m_vSprites.end());
}

// The page as a QImage: atlas pages are straight RGBA8 (premultiplied only when the project
// says so); painting wants premultiplied ARGB32.
QImage f_ToQImage(const atlas::Image& l_oImg, bool l_bPremultiplied)
{
    if (!l_oImg.f_Valid()) return QImage();
    QImage l_oWrap(l_oImg.m_vPx.data(), l_oImg.m_iW, l_oImg.m_iH, l_oImg.m_iW * 4,
                l_bPremultiplied ? QImage::Format_RGBA8888_Premultiplied : QImage::Format_RGBA8888);
    return l_oWrap.convertToFormat(QImage::Format_ARGB32_Premultiplied);   // deep copy
}

// Runs on a worker thread: builds, lists the sprites and prepares images.
BuildSnapshotPtr f_RunBuild(Project l_oP, std::string l_strVariant, quint64 l_u64Generation)
{
    QElapsedTimer l_oTimer;
    l_oTimer.start();
    auto l_spSnap = std::make_shared<BuildSnapshot>();
    l_spSnap->m_u64Generation = l_u64Generation;
    l_spSnap->m_oVariant = f_Qs(l_strVariant);
    const atlas::ImageSource l_oSrc = atlas::f_FileImageSource();   // embedded projects read no files
    l_spSnap->m_oResult = atlas::f_Build(l_oP, l_oSrc, atlas::BuildOptions{l_strVariant, true});

    const atlas::Variant* l_pV = l_strVariant.empty() ? nullptr : l_oP.f_FindVariant(l_strVariant);
    for (const atlas::ResolvedSprite& l_oRs : atlas::f_ResolveSprites(l_oP, l_oSrc)) {
        SpriteInfo l_oInfo;
        l_oInfo.m_oName = f_Qs(l_oRs.m_oDef.m_strName);
        l_oInfo.m_oParent = f_Qs(l_oRs.m_oDef.m_strParent);
        l_oInfo.m_bChild = l_oRs.m_oDef.f_IsChild();
        l_oInfo.m_bVariantArt = l_pV && l_pV->m_mapImages.count(l_oRs.m_oDef.m_strName) > 0;
        l_spSnap->m_oSpriteIndex.insert(l_oInfo.m_oName, int(l_spSnap->m_vSprites.size()));
        l_spSnap->m_vSprites.push_back(l_oInfo);
    }

    for (const atlas::Page& l_oPage : l_spSnap->m_oResult.m_vPages)
        l_spSnap->m_oPages.push_back(f_ToQImage(l_oPage.m_oImage, l_oP.m_oSettings.m_bPremultiplyAlpha));
    for (const atlas::Region& l_oR : l_spSnap->m_oResult.m_vRegions) {
        if (l_oR.m_iPage < 0 || l_oR.m_iPage >= l_spSnap->m_oPages.size() || l_oR.m_oFrame.f_Empty()) continue;
        QImage l_oT = l_spSnap->m_oPages[l_oR.m_iPage].copy(l_oR.m_oFrame.m_iX, l_oR.m_oFrame.m_iY, l_oR.m_oFrame.m_iW, l_oR.m_oFrame.m_iH);
        if (l_oT.width() > g_iThumbnailSize || l_oT.height() > g_iThumbnailSize)
            l_oT = l_oT.scaled(g_iThumbnailSize, g_iThumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        l_spSnap->m_oThumbnails.insert(f_Qs(l_oR.m_strName), l_oT);
    }
    l_spSnap->m_i64ElapsedMs = l_oTimer.elapsed();
    return l_spSnap;
}

// Export Copy: writes the atlas of the base art and/or variants to folders of the user's choice;
// runs on a worker thread. Never touches the project file or its packed atlas.
std::pair<bool, QString> f_RunExport(Project l_oP, std::vector<std::pair<std::string, std::string>> l_vJobs)   // variant, dir
{
    QStringList l_oLines;
    bool l_bOk = true;
    for (const auto& [l_strV, l_strDir] : l_vJobs) {
        const atlas::Variant* l_pVar = l_strV.empty() ? nullptr : l_oP.f_FindVariant(l_strV);
        const atlas::Output& l_oOut = l_pVar ? l_pVar->m_oOutput : l_oP.m_oOutput;
        const QString l_oLabel = l_strV.empty() ? QObject::tr("base") : QObject::tr("variant %1").arg(f_Qs(l_strV));
        atlas::BuildResult l_oB = atlas::f_Build(l_oP, atlas::BuildOptions{l_strV, true});
        if (!l_oB.f_Ok()) {
            l_oLines << QObject::tr("%1: %2 error(s), nothing written").arg(l_oLabel).arg(l_oB.f_ErrorCount());
            l_bOk = false;
            continue;
        }
        // The Cocos .plist always goes along (Cocos Creator's Sprite Atlas), whatever the project
        // lists; the Properties dock shows that box as always on.
        std::vector<std::string> l_vFormats = l_oOut.m_vFormats;
        if (std::find(l_vFormats.begin(), l_vFormats.end(), g_pszAlwaysExported) == l_vFormats.end()) l_vFormats.push_back(g_pszAlwaysExported);
        std::vector<atlas::ExportFile> l_vFiles;
        std::string l_strErr;
        if (!atlas::f_ExportFiles(l_oB, l_vFormats, l_vFiles, &l_strErr) || !atlas::f_WriteFiles(l_strDir, l_vFiles, &l_strErr)) {
            l_oLines << QObject::tr("%1: %2").arg(l_oLabel, f_Qs(l_strErr));
            l_bOk = false;
            continue;
        }
        QStringList l_oNames;
        for (const atlas::ExportFile& l_oF : l_vFiles) l_oNames << f_Qs(l_oF.m_strName);
        l_oLines << QObject::tr("%1: wrote %2 to %3").arg(l_oLabel, l_oNames.join(QStringLiteral(", ")), f_Native(l_strDir));
    }
    return {l_bOk, l_oLines.join(QLatin1Char('\n'))};
}

struct SaveJob {
    bool m_bOk = false;
    std::string m_strError;
    atlas::SaveResult m_oResult;
    Project m_oProject;   // as saved (paths rebased for Save As)
};

}  // namespace

// ---- BuildSnapshot ------------------------------------------------------------------------------

const SpriteInfo* BuildSnapshot::f_Sprite(const QString& l_oName) const
{
    auto l_oIt = m_oSpriteIndex.find(l_oName);
    return l_oIt == m_oSpriteIndex.end() ? nullptr : &m_vSprites[size_t(*l_oIt)];
}

const atlas::Region* BuildSnapshot::f_Region(const QString& l_oName) const { return m_oResult.f_Find(f_U8(l_oName)); }

// ---- AtlasDocument ------------------------------------------------------------------------------

AtlasDocument::AtlasDocument(QObject* l_pParent)
    : QObject(l_pParent)
    , m_pUndo(new QUndoStack(this))
    , m_pRebuildTimer(new QTimer(this))
{
    m_pRebuildTimer->setSingleShot(true);
    m_pRebuildTimer->setInterval(g_iRebuildDelayMs);
    connect(m_pRebuildTimer, &QTimer::timeout, this, &AtlasDocument::f_StartBuild);
    connect(&m_oBuildWatcher, &QFutureWatcherBase::finished, this, &AtlasDocument::f_OnBuildDone);
    connect(&m_oExportWatcher, &QFutureWatcherBase::finished, this, [this] {
        const auto l_pairR = m_oExportWatcher.result();
        emit f_ExportFinished(l_pairR.first, l_pairR.second);
    });
    connect(m_pUndo, &QUndoStack::cleanChanged, this, [this] { emit f_DirtyChanged(f_IsDirty()); });
    f_NewProject();
}

AtlasDocument::~AtlasDocument()
{
    // The jobs only hold copies, but finishing them here keeps the worker threads from outliving
    // the editor's state at shutdown.
    m_oBuildWatcher.waitForFinished();
    m_oExportWatcher.waitForFinished();
}

QString AtlasDocument::f_FilePath() const { return f_Qs(m_oProject.m_strFilePath); }

QString AtlasDocument::f_DisplayName() const
{
    return m_oProject.m_strFilePath.empty() ? tr("Untitled") : QFileInfo(f_FilePath()).fileName();
}

bool AtlasDocument::f_IsDirty() const { return m_bForcedDirty || !m_pUndo->isClean(); }

void AtlasDocument::f_SetForcedDirty(bool l_bOn)
{
    if (m_bForcedDirty == l_bOn) return;
    m_bForcedDirty = l_bOn;
    emit f_DirtyChanged(f_IsDirty());
}

QString AtlasDocument::f_StoreFile(const QString& l_oVariant) const
{
    const atlas::Variant* l_pV = l_oVariant.isEmpty() ? nullptr : m_oProject.f_FindVariant(f_U8(l_oVariant));
    return f_Qs(atlas::f_StorePath(m_oProject, l_pV));
}

void AtlasDocument::f_Reset(const Project& l_oP, bool l_bDirty)
{
    m_oProject = l_oP;
    m_u64ChangeCount++;
    m_pUndo->clear();
    m_bForcedDirty = l_bDirty;
    m_oSelection.clear();
    m_oVariant.clear();
    m_spSnapshot.reset();
    m_oSpriteImages.clear();
    emit f_ProjectReset();
    emit f_FilePathChanged();
    emit f_VariantChanged();
    emit f_SelectionChanged();
    emit f_ProjectChanged();
    emit f_DirtyChanged(f_IsDirty());
    // The first build of a project should not wait for the debounce.
    m_pRebuildTimer->stop();
    m_u64Generation++;
    f_StartBuild();
}

void AtlasDocument::f_NewProject()
{
    Project l_oP;
    l_oP.m_strName = "atlas";
    l_oP.m_bEmbedded = true;
    l_oP.m_oOutput.m_strDir = ".";
    f_Reset(l_oP, false);
}

void AtlasDocument::f_Adopt(const Project& l_oP)
{
    Project l_oQ = l_oP;
    l_oQ.m_bEmbedded = true;
    f_Reset(l_oQ, true);
}

bool AtlasDocument::f_Open(const QString& l_oPathIn, QString* l_pError)
{
    // Absolute: relative paths in the project are rebased against it on Save As.
    const QString l_oPath = QDir::fromNativeSeparators(QFileInfo(l_oPathIn).absoluteFilePath());
    Project l_oP;
    std::string l_strErr;
    if (!atlas::f_OpenProject(f_U8(l_oPath), l_oP, &l_strErr)) {
        if (l_pError) *l_pError = f_Qs(l_strErr);
        return false;
    }
    if (l_oP.m_bEmbedded) {
        f_Reset(l_oP, false);
        emit f_Message(tr("Opened %1 (%n image(s))", nullptr, int(l_oP.m_mapImages.size())).arg(QDir::toNativeSeparators(l_oPath)));
        return true;
    }
    // A folder project (atlaspack pack, build scripts): take its images in. The folders stay as
    // references to import new or changed art from.
    std::vector<atlas::Diagnostic> l_vDiags;
    const int l_iN = atlas::f_EmbedSources(l_oP, atlas::f_FileImageSource(), &l_vDiags);
    l_oP.m_oOutput.m_strDir = ".";
    f_Reset(l_oP, true);
    emit f_Message(tr("Opened %1 (a folder project: %n image(s) taken in)", nullptr, l_iN).arg(QDir::toNativeSeparators(l_oPath)));
    for (const atlas::Diagnostic& l_oD : l_vDiags) emit f_Message(f_Qs(atlas::f_FormatDiagnostic(l_oD)));
    emit f_Message(tr("Converted to an embedded project — Save writes the packed atlas next to the project; "
                    "the folders are now references."));
    return true;
}

bool AtlasDocument::f_WriteBackup(const QString& l_oPath, QString* l_pError) const
{
    const QFileInfo l_oFi(l_oPath);
    const QDir l_oDir(QDir(l_oFi.absolutePath()).filePath(l_oFi.completeBaseName()));
    if (!QDir().mkpath(l_oDir.absolutePath())) {
        if (l_pError) *l_pError = tr("cannot create %1").arg(QDir::toNativeSeparators(l_oDir.absolutePath()));
        return false;
    }
    Project l_oCopy = m_oProject;
    l_oCopy.m_bEmbedded = true;
    l_oCopy.m_oOutput.m_strDir = ".";   // stays next to the project copy
    for (atlas::Variant& l_oV : l_oCopy.m_vVariants)   // absolute: kept as they are by the save
        l_oV.m_oOutput.m_strDir = f_U8(QDir::fromNativeSeparators(l_oDir.filePath(QStringLiteral("variants/") + f_Qs(l_oV.m_strId))));
    const std::string l_strTarget = f_U8(QDir::fromNativeSeparators(l_oDir.filePath(f_Qs(l_oCopy.m_strName.empty() ? std::string("atlas") : l_oCopy.m_strName) +
                                                                             QStringLiteral(".atlasproj"))));
    (void)QtConcurrent::run([l_oCopy, l_strTarget]() mutable {
        std::string l_strErr;
        if (!atlas::f_SaveProjectAll(l_strTarget, l_oCopy, {g_pszAlwaysExported}, nullptr, &l_strErr))
            std::fprintf(stderr, "[atlas] backup failed: %s\n", l_strErr.c_str());
    });
    return true;
}

bool AtlasDocument::f_Save(const QString& l_oPath, SaveReport* l_pReport, QString* l_pError)
{
    if (m_bSaving) {
        if (l_pError) *l_pError = tr("A save is already running.");
        return false;
    }
    m_bSaving = true;
    emit f_SaveStarted();
    // The worker saves a copy; edits made meanwhile (the event loop keeps running) are noticed
    // through m_changeCount and keep the document dirty.
    const quint64 l_u64ChangeAtStart = m_u64ChangeCount;
    Project l_oCopy = m_oProject;
    l_oCopy.m_bEmbedded = true;
    const std::string l_strTarget = f_U8(QDir::fromNativeSeparators(QFileInfo(l_oPath).absoluteFilePath()));
    QFutureWatcher<SaveJob> l_oWatcher;
    QEventLoop l_oLoop;
    connect(&l_oWatcher, &QFutureWatcherBase::finished, &l_oLoop, &QEventLoop::quit);
    l_oWatcher.setFuture(QtConcurrent::run([l_oCopy, l_strTarget]() mutable {
        SaveJob l_oJob;
        l_oJob.m_bOk = atlas::f_SaveProjectAll(l_strTarget, l_oCopy, {g_pszAlwaysExported}, &l_oJob.m_oResult, &l_oJob.m_strError);
        l_oJob.m_oProject = std::move(l_oCopy);
        return l_oJob;
    }));
    if (!l_oWatcher.isFinished()) l_oLoop.exec(QEventLoop::ExcludeUserInputEvents);
    SaveJob l_oJob = l_oWatcher.result();
    m_bSaving = false;

    if (l_pReport) {
        l_pReport->m_oWritten.clear();
        for (const std::string& l_strF : l_oJob.m_oResult.m_vWritten) l_pReport->m_oWritten << f_Native(l_strF);
        l_pReport->m_vDiagnostics = l_oJob.m_oResult.m_vDiagnostics;
    }
    if (!l_oJob.m_bOk) {
        if (l_pError) *l_pError = f_Qs(l_oJob.m_strError);
        emit f_Message(tr("Save failed (nothing was written): %1").arg(f_Qs(l_oJob.m_strError)));
        emit f_SaveFinished(false);
        return false;
    }
    const bool l_bMoved = l_oJob.m_oProject.m_strFilePath != m_oProject.m_strFilePath;
    if (m_u64ChangeCount == l_u64ChangeAtStart) {
        // Keep the images of the live project (the same pointers) and take the rebased paths.
        m_oProject = std::move(l_oJob.m_oProject);
        m_pUndo->setClean();
        f_SetForcedDirty(false);
    } else {
        // Edited while saving: those edits are not on disk yet.
        if (l_bMoved) atlas::f_RebaseProject(m_oProject, l_oJob.m_oProject.m_strFilePath);
        emit f_Message(tr("The project changed while it was being saved; save again to keep those changes."));
    }
    emit f_FilePathChanged();
    emit f_DirtyChanged(f_IsDirty());
    if (l_bMoved) {
        emit f_ProjectChanged();
        f_ScheduleRebuild();
    }
    emit f_Message(tr("Saved %1 (%n file(s) written)", nullptr, int(l_oJob.m_oResult.m_vWritten.size())).arg(QDir::toNativeSeparators(l_oPath)));
    emit f_SaveFinished(true);
    return true;
}

void AtlasDocument::f_SetSelection(const QStringList& l_oNames)
{
    QStringList l_oClean;
    for (const QString& l_oN : l_oNames)
        if (!l_oN.isEmpty() && !l_oClean.contains(l_oN)) l_oClean << l_oN;
    if (l_oClean == m_oSelection) return;
    m_oSelection = l_oClean;
    emit f_SelectionChanged();
}

void AtlasDocument::f_SetVariant(const QString& l_oId)
{
    if (l_oId == m_oVariant) return;
    m_oVariant = l_oId;
    emit f_VariantChanged();
    f_ScheduleRebuild();
}

const atlas::Variant* AtlasDocument::f_CurrentVariant() const
{
    return m_oVariant.isEmpty() ? nullptr : m_oProject.f_FindVariant(f_U8(m_oVariant));
}

bool AtlasDocument::f_IsBuilding() const { return m_oBuildWatcher.isRunning() || m_pRebuildTimer->isActive(); }

QImage AtlasDocument::f_SpriteImage(const QString& l_oName) const
{
    if (!m_spSnapshot) return QImage();
    auto l_oIt = m_oSpriteImages.find(l_oName);
    if (l_oIt != m_oSpriteImages.end()) return *l_oIt;
    QImage l_oImg;
    const atlas::Region* l_pR = m_spSnapshot->f_Region(l_oName);
    if (l_pR && l_pR->m_iPage >= 0 && l_pR->m_iPage < m_spSnapshot->m_oPages.size()) {
        l_oImg = QImage(std::max(l_pR->m_iOrigW, 1), std::max(l_pR->m_iOrigH, 1), QImage::Format_ARGB32_Premultiplied);
        l_oImg.fill(Qt::transparent);
        QPainter l_oP(&l_oImg);
        l_oP.drawImage(QPoint(l_pR->m_iOffsetX, l_pR->m_iOffsetY), m_spSnapshot->m_oPages[l_pR->m_iPage],
                    QRect(l_pR->m_oFrame.m_iX, l_pR->m_oFrame.m_iY, l_pR->m_oFrame.m_iW, l_pR->m_oFrame.m_iH));
    }
    m_oSpriteImages.insert(l_oName, l_oImg);
    return l_oImg;
}

SpriteDef AtlasDocument::f_SpriteDef(const QString& l_oName) const
{
    if (const SpriteDef* l_pD = m_oProject.f_FindSprite(f_U8(l_oName))) return *l_pD;
    SpriteDef l_oD;
    l_oD.m_strName = f_U8(l_oName);
    return l_oD;
}

bool AtlasDocument::f_IsImage(const QString& l_oName) const { return m_oProject.m_mapImages.count(f_U8(l_oName)) > 0; }

bool AtlasDocument::f_HasVariantArt(const QString& l_oName, const QString& l_oVariant) const
{
    const atlas::Variant* l_pV = l_oVariant.isEmpty() ? nullptr : m_oProject.f_FindVariant(f_U8(l_oVariant));
    return l_pV && l_pV->m_mapImages.count(f_U8(l_oName)) > 0;
}

bool AtlasDocument::f_SpriteExists(const QString& l_oName) const
{
    if (f_IsImage(l_oName) || m_oProject.f_FindSprite(f_U8(l_oName))) return true;
    return m_spSnapshot && m_spSnapshot->f_Sprite(l_oName);
}

QString AtlasDocument::f_UniqueName(const QString& l_oBase) const
{
    if (!f_SpriteExists(l_oBase)) return l_oBase;
    for (int l_iI = 1;; l_iI++) {
        const QString l_oN = QStringLiteral("%1_%2").arg(l_oBase).arg(l_iI);
        if (!f_SpriteExists(l_oN)) return l_oN;
    }
}

QStringList AtlasDocument::f_Descendants(const QString& l_oName) const
{
    QStringList l_oOut;
    QSet<QString> l_oRoots{l_oName};
    bool l_bGrew = true;
    while (l_bGrew) {
        l_bGrew = false;
        for (const SpriteDef& l_oD : m_oProject.m_vSprites) {
            const QString l_oN = f_Qs(l_oD.m_strName);
            if (l_oD.f_IsChild() && l_oRoots.contains(f_Qs(l_oD.m_strParent)) && !l_oRoots.contains(l_oN)) {
                l_oRoots.insert(l_oN);
                l_oOut << l_oN;
                l_bGrew = true;
            }
        }
    }
    return l_oOut;
}

QStringList AtlasDocument::f_DeletionClosure(const QStringList& l_oNames) const
{
    QStringList l_oOut;
    for (const QString& l_oN : l_oNames) {
        if (!l_oOut.contains(l_oN)) l_oOut << l_oN;
        for (const QString& l_oC : f_Descendants(l_oN))
            if (!l_oOut.contains(l_oC)) l_oOut << l_oC;
    }
    return l_oOut;
}

QString AtlasDocument::f_ToProjectPath(const QString& l_oAbsolutePath) const
{
    if (m_oProject.m_strFilePath.empty()) return QDir::fromNativeSeparators(QDir::cleanPath(l_oAbsolutePath));
    return f_Qs(m_oProject.f_Relativize(f_U8(QDir::fromNativeSeparators(l_oAbsolutePath))));
}

QString AtlasDocument::f_ResolvePath(const QString& l_oProjectPath) const { return f_Qs(m_oProject.f_Resolve(f_U8(l_oProjectPath))); }

// ---- editing ------------------------------------------------------------------------------------

bool AtlasDocument::f_Edit(const QString& l_oText, const std::function<void(Project&)>& l_fnChange, const QString& l_oMergeKey,
                         const QStringList* l_pSelectionAfter)
{
    Project l_oP = m_oProject;   // cheap: images are shared
    l_fnChange(l_oP);
    if (f_SameProjectState(l_oP, m_oProject)) {
        if (l_pSelectionAfter) f_SetSelection(*l_pSelectionAfter);
        return false;
    }
    m_pUndo->push(new ProjectCommand(this, l_oText, m_oProject, std::move(l_oP), m_oSelection,
                                    l_pSelectionAfter ? *l_pSelectionAfter : m_oSelection, l_oMergeKey));
    return true;
}

bool AtlasDocument::f_EditSprites(const QStringList& l_oNames, const QString& l_oText, const std::function<void(SpriteDef&)>& l_fnChange,
                                const QString& l_oMergeKey)
{
    if (l_oNames.isEmpty()) return false;
    return f_Edit(l_oText, [&](Project& l_oP) {
        for (const QString& l_oN : l_oNames) l_fnChange(f_EntryFor(l_oP, f_U8(l_oN)));
        f_PruneEmptyEntries(l_oP);
    }, l_oMergeKey.isEmpty() ? QString() : l_oMergeKey + QLatin1Char('|') + l_oNames.join(QLatin1Char('|')));
}

void AtlasDocument::f_RestoreState(const Project& l_oState, const QStringList& l_oSelection)
{
    Project l_oP = l_oState;
    // A step recorded before Save As holds paths relative to the old place.
    if (l_oP.m_strFilePath != m_oProject.m_strFilePath && !m_oProject.m_strFilePath.empty()) atlas::f_RebaseProject(l_oP, m_oProject.m_strFilePath);
    l_oP.m_strFilePath = m_oProject.m_strFilePath;
    m_oProject = std::move(l_oP);
    m_u64ChangeCount++;
    emit f_ProjectChanged();
    f_ScheduleRebuild();
    f_SetSelection(l_oSelection);
}

bool AtlasDocument::f_AddChildren(const QString& l_oParent, const std::vector<std::pair<QString, atlas::IRect>>& l_vChildren,
                                const QString& l_oText)
{
    if (l_vChildren.empty()) return false;
    QStringList l_oNames;
    for (const auto& l_pairC : l_vChildren) l_oNames << l_pairC.first;
    return f_Edit(l_oText, [&](Project& l_oP) {
        for (const auto& l_pairC : l_vChildren) {
            SpriteDef l_oD;
            l_oD.m_strName = f_U8(l_pairC.first);
            l_oD.m_strParent = f_U8(l_oParent);
            l_oD.m_oRect = l_pairC.second;
            l_oP.m_vSprites.push_back(l_oD);
        }
    }, QString(), &l_oNames);
}

bool AtlasDocument::f_RenameSprite(const QString& l_oFrom, const QString& l_oTo, QString* l_pError)
{
    const QString l_oName = l_oTo.trimmed();
    if (l_oName == l_oFrom) return true;
    if (l_oName.isEmpty()) {
        if (l_pError) *l_pError = tr("A sprite needs a name.");
        return false;
    }
    if (f_SpriteExists(l_oName)) {
        if (l_pError) *l_pError = tr("There already is a sprite called '%1'.").arg(l_oName);
        return false;
    }
    if (!f_IsImage(l_oFrom) && !m_oProject.f_FindSprite(f_U8(l_oFrom))) {
        if (l_pError) *l_pError = tr("'%1' is not in the project.").arg(l_oFrom);
        return false;
    }
    QStringList l_oSel = m_oSelection;
    for (QString& l_oS : l_oSel)
        if (l_oS == l_oFrom) l_oS = l_oName;
    return f_Edit(tr("Rename %1").arg(l_oFrom), [&](Project& l_oP) {
        const std::string l_strF = f_U8(l_oFrom), l_strT = f_U8(l_oName);
        auto l_fnMove = [&](std::map<std::string, atlas::ImagePtr>& l_mapM) {
            auto l_itIt = l_mapM.find(l_strF);
            if (l_itIt == l_mapM.end()) return;
            atlas::ImagePtr l_spImg = l_itIt->second;
            l_mapM.erase(l_itIt);
            l_mapM[l_strT] = l_spImg;
        };
        l_fnMove(l_oP.m_mapImages);
        for (atlas::Variant& l_oV : l_oP.m_vVariants) l_fnMove(l_oV.m_mapImages);
        for (SpriteDef& l_oD : l_oP.m_vSprites) {
            if (l_oD.m_strName == l_strF) l_oD.m_strName = l_strT;
            if (l_oD.m_strParent == l_strF) l_oD.m_strParent = l_strT;
        }
        for (atlas::Animation& l_oA : l_oP.m_vAnimations)
            for (atlas::AnimFrame& l_oFr : l_oA.m_vFrames)
                if (l_oFr.m_strSprite == l_strF) l_oFr.m_strSprite = l_strT;
    }, QString(), &l_oSel);
}

void AtlasDocument::f_DeleteSprites(const QStringList& l_oNames)
{
    std::set<std::string> l_setRemoved;
    for (const QString& l_oN : f_DeletionClosure(l_oNames))
        if (f_SpriteExists(l_oN)) l_setRemoved.insert(f_U8(l_oN));
    if (l_setRemoved.empty()) return;
    const QStringList l_oNone;
    f_Edit(l_oNames.size() == 1 ? tr("Delete %1").arg(l_oNames.first()) : tr("Delete %n sprite(s)", nullptr, int(l_oNames.size())),
         [&](Project& l_oP) {
             for (const std::string& l_strN : l_setRemoved) {
                 l_oP.m_mapImages.erase(l_strN);
                 for (atlas::Variant& l_oV : l_oP.m_vVariants) l_oV.m_mapImages.erase(l_strN);
             }
             l_oP.m_vSprites.erase(std::remove_if(l_oP.m_vSprites.begin(), l_oP.m_vSprites.end(),
                                            [&](const SpriteDef& l_oD) { return l_setRemoved.count(l_oD.m_strName) > 0; }),
                             l_oP.m_vSprites.end());
             for (atlas::Animation& l_oA : l_oP.m_vAnimations)
                 l_oA.m_vFrames.erase(std::remove_if(l_oA.m_vFrames.begin(), l_oA.m_vFrames.end(),
                                               [&](const atlas::AnimFrame& l_oF) { return l_setRemoved.count(l_oF.m_strSprite) > 0; }),
                                l_oA.m_vFrames.end());
         }, QString(), &l_oNone);
}

// ---- images -------------------------------------------------------------------------------------

QList<ImageFile> AtlasDocument::f_CollectPngs(const QStringList& l_oPaths)
{
    QList<ImageFile> l_oOut;
    QSet<QString> l_oNames;
    auto l_fnAdd = [&](const QString& l_oName, const QString& l_oFile) {
        if (l_oName.isEmpty() || l_oNames.contains(l_oName)) return;
        l_oNames.insert(l_oName);
        l_oOut.push_back({l_oName, l_oFile});
    };
    const atlas::ImageSource l_oSrc = atlas::f_FileImageSource();
    for (const QString& l_oPath : l_oPaths) {
        const QFileInfo l_oFi(l_oPath);
        if (l_oFi.isDir()) {
            const std::string l_strDir = f_U8(QDir::fromNativeSeparators(l_oFi.absoluteFilePath()));
            for (const std::string& l_strF : l_oSrc.m_fnListPngs(l_strDir, true)) l_fnAdd(f_Qs(atlas::f_ImageNameFor(l_strF, l_strDir)), f_Qs(l_strF));
        } else if (l_oFi.isFile() && l_oFi.suffix().compare(QLatin1String("png"), Qt::CaseInsensitive) == 0) {
            const QString l_oFile = QDir::fromNativeSeparators(l_oFi.absoluteFilePath());
            l_fnAdd(f_Qs(atlas::f_ImageNameFor(f_U8(l_oFile))), l_oFile);
        }
    }
    return l_oOut;
}

int AtlasDocument::f_SetImages(const QList<ImageFile>& l_oFiles, const QString& l_oVariant, const QString& l_oText, QStringList* l_pErrors)
{
    // Decode first: a file that does not load leaves the project alone.
    Project l_oP = m_oProject;
    QStringList l_oAdded;
    for (const ImageFile& l_oF : l_oFiles) {
        atlas::Image l_oImg;
        std::string l_strErr;
        if (!atlas::f_LoadImage(f_U8(l_oF.m_oPath), l_oImg, &l_strErr) || !atlas::f_SetImage(l_oP, f_U8(l_oF.m_oName), l_oImg, f_U8(l_oVariant), &l_strErr)) {
            if (l_pErrors) *l_pErrors << tr("%1: %2").arg(QDir::toNativeSeparators(l_oF.m_oPath), f_Qs(l_strErr));
            continue;
        }
        l_oAdded << l_oF.m_oName;
    }
    if (l_oAdded.isEmpty()) return 0;
    f_Edit(l_oText.isEmpty() ? tr("Add %n image(s)", nullptr, int(l_oAdded.size())) : l_oText, [&](Project& l_oQ) { l_oQ = l_oP; }, QString(), &l_oAdded);
    return int(l_oAdded.size());
}

bool AtlasDocument::f_RemoveVariantArt(const QStringList& l_oNames, const QString& l_oVariant)
{
    if (l_oVariant.isEmpty() || l_oNames.isEmpty()) return false;
    return f_Edit(tr("Remove %1 art").arg(l_oVariant), [&](Project& l_oP) {
        for (atlas::Variant& l_oV : l_oP.m_vVariants)
            if (l_oV.m_strId == f_U8(l_oVariant))
                for (const QString& l_oN : l_oNames) l_oV.m_mapImages.erase(f_U8(l_oN));
    });
}

bool AtlasDocument::f_SaveSpriteImage(const QString& l_oName, const QString& l_oFile, QString* l_pError) const
{
    // An image is saved exactly as held (the variant art when the canvas shows a variant); a
    // child is cut out of the last build.
    atlas::Image l_oImg;
    const atlas::Variant* l_pV = f_CurrentVariant();
    auto l_itIt = l_pV ? l_pV->m_mapImages.find(f_U8(l_oName)) : m_oProject.m_mapImages.end();
    if (l_pV && l_itIt != l_pV->m_mapImages.end() && l_itIt->second) l_oImg = *l_itIt->second;
    else if (auto l_itB = m_oProject.m_mapImages.find(f_U8(l_oName)); l_itB != m_oProject.m_mapImages.end() && l_itB->second) l_oImg = *l_itB->second;
    else if (const atlas::Region* l_pR = m_spSnapshot ? m_spSnapshot->f_Region(l_oName) : nullptr) {
        l_oImg = atlas::f_RenderRegion(m_spSnapshot->m_oResult, *l_pR);
        if (m_oProject.m_oSettings.m_bPremultiplyAlpha) atlas::f_Unpremultiply(l_oImg);
    }
    if (!l_oImg.f_Valid()) {
        if (l_pError) *l_pError = tr("'%1' has no pixels to save.").arg(l_oName);
        return false;
    }
    std::string l_strErr;
    if (!atlas::f_SavePng(f_U8(QDir::fromNativeSeparators(l_oFile)), l_oImg, &l_strErr)) {
        if (l_pError) *l_pError = f_Qs(l_strErr);
        return false;
    }
    return true;
}

bool AtlasDocument::f_ExtractImages(const QString& l_oDir, bool l_bChildren, int* l_pCount, QString* l_pError) const
{
    std::vector<std::string> l_vWritten;
    std::string l_strErr;
    if (!atlas::f_ExtractImages(m_oProject, f_U8(QDir::fromNativeSeparators(l_oDir)), f_U8(m_oVariant), l_bChildren, &l_vWritten, &l_strErr)) {
        if (l_pError) *l_pError = f_Qs(l_strErr);
        return false;
    }
    if (l_pCount) *l_pCount = int(l_vWritten.size());
    return true;
}

// ---- references ---------------------------------------------------------------------------------

bool AtlasDocument::f_AddReference(const QString& l_oDir)
{
    const QString l_oAbs = QDir::cleanPath(QFileInfo(l_oDir).absoluteFilePath());
    for (const atlas::SourceFolder& l_oF : m_oProject.m_vReferences)
        if (QDir::cleanPath(QFileInfo(f_ResolvePath(f_Qs(l_oF.m_strPath))).absoluteFilePath()).compare(l_oAbs, Qt::CaseInsensitive) == 0) {
            emit f_Message(tr("%1 is already a reference folder").arg(QDir::toNativeSeparators(l_oAbs)));
            return false;
        }
    return f_Edit(tr("Add reference folder"), [&](Project& l_oP) {
        atlas::SourceFolder l_oF;
        l_oF.m_strPath = f_U8(f_ToProjectPath(l_oAbs));
        l_oP.m_vReferences.push_back(l_oF);
    });
}

bool AtlasDocument::f_RemoveReference(int l_iIndex)
{
    if (l_iIndex < 0 || l_iIndex >= int(m_oProject.m_vReferences.size())) return false;
    return f_Edit(tr("Remove reference folder"), [&](Project& l_oP) { l_oP.m_vReferences.erase(l_oP.m_vReferences.begin() + l_iIndex); });
}

bool AtlasDocument::f_SetVariantReference(const QString& l_oVariant, const QString& l_oDir)
{
    const std::string l_strPath = l_oDir.isEmpty() ? std::string() : f_U8(f_ToProjectPath(QDir::cleanPath(QFileInfo(l_oDir).absoluteFilePath())));
    return f_Edit(tr("Variant reference folder"), [&](Project& l_oP) {
        for (atlas::Variant& l_oV : l_oP.m_vVariants)
            if (l_oV.m_strId == f_U8(l_oVariant)) l_oV.m_strReference = l_strPath;
    });
}

// ---- builds -------------------------------------------------------------------------------------

void AtlasDocument::f_ScheduleRebuild()
{
    m_u64Generation++;
    m_pRebuildTimer->start();
}

void AtlasDocument::f_StartBuild()
{
    if (m_oBuildWatcher.isRunning()) {
        m_bRebuildQueued = true;
        return;
    }
    emit f_BuildStarted();
    m_oBuildWatcher.setFuture(QtConcurrent::run(f_RunBuild, m_oProject, f_U8(m_oVariant), m_u64Generation));
}

void AtlasDocument::f_OnBuildDone()
{
    BuildSnapshotPtr l_spSnap = m_oBuildWatcher.result();
    if (m_bRebuildQueued) {
        m_bRebuildQueued = false;
        f_StartBuild();
    }
    if (!l_spSnap || l_spSnap->m_u64Generation != m_u64Generation) return;   // the project changed meanwhile
    m_spSnapshot = std::move(l_spSnap);
    m_oSpriteImages.clear();
    emit f_BuildFinished();
}

// ---- export copy --------------------------------------------------------------------------------

bool AtlasDocument::f_IsExporting() const { return m_oExportWatcher.isRunning(); }

void AtlasDocument::f_ExportCopy(const QString& l_oDir, bool l_bAllVariants)
{
    if (f_IsExporting()) return;
    const std::string l_strRoot = f_U8(QDir::fromNativeSeparators(QDir::cleanPath(l_oDir)));
    std::vector<std::pair<std::string, std::string>> l_vJobs;
    if (l_bAllVariants) {
        l_vJobs.push_back({std::string(), l_strRoot});
        for (const atlas::Variant& l_oV : m_oProject.m_vVariants) l_vJobs.push_back({l_oV.m_strId, atlas::f_JoinPath(l_strRoot, l_oV.m_strId)});
    } else {
        l_vJobs.push_back({f_U8(m_oVariant), l_strRoot});
    }
    m_oExportWatcher.setFuture(QtConcurrent::run(f_RunExport, m_oProject, l_vJobs));
}

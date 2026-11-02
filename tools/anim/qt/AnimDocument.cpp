#include "AnimDocument.h"
#include "anim_meshes.h"
#include "anim_recipes.h"
#include <json.hpp>

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

QString f_Qs(const std::string& l_strS) { return QString::fromStdString(l_strS); }
std::string f_U8(const QString& l_oS) { return l_oS.toStdString(); }

bool f_SameState(const AnimState& l_oA, const AnimState& l_oB)
{
    // The data itself, not its JSON: dumping a large file for every edit (and every merge of a drag or
    // a held spin-box arrow) made editing stall.
    return l_oA.m_oAtlasesAbs == l_oB.m_oAtlasesAbs && l_oA.m_oAtlasIds == l_oB.m_oAtlasIds && l_oA.m_iClip == l_oB.m_iClip && l_oA.m_vNode == l_oB.m_vNode &&
           l_oA.m_oFile == l_oB.m_oFile;
}

// The interpolated channels a node animates already (has keys on). Key rows only ever add to
// these: a node that only switches sprites gets no position / rotation / scale / colour keys.
std::vector<Channel> f_AnimatedChannels(const Node& l_oN)
{
    std::vector<Channel> l_vOut;
    for (Channel l_eC : g_arrInterpolated)
        if (f_KeyCount(l_oN, l_eC) > 0) l_vOut.push_back(l_eC);
    return l_vOut;
}

// channel >= 0: that one; < 0: the ones the node animates (position for a node without any).
std::vector<Channel> f_ChannelsFor(const Node& l_oN, int l_iChannel)
{
    if (l_iChannel >= 0) return {Channel(l_iChannel)};
    std::vector<Channel> l_vOut = f_AnimatedChannels(l_oN);
    if (l_vOut.empty()) l_vOut.push_back(Channel::Pos);
    return l_vOut;
}

QString f_UniqueClipName(const AnimFile& l_oF, const QString& l_oBase)
{
    auto l_fnTaken = [&](const QString& l_oN) { return l_oF.f_Find(f_U8(l_oN)) != nullptr; };
    const QString l_oB = l_oBase.isEmpty() ? QStringLiteral("clip") : l_oBase;
    if (!l_fnTaken(l_oB)) return l_oB;
    for (int l_iI = 2;; l_iI++)
        if (!l_fnTaken(QStringLiteral("%1_%2").arg(l_oB).arg(l_iI))) return QStringLiteral("%1_%2").arg(l_oB).arg(l_iI);
}

}  // namespace

QString f_PathToString(const NodePath& l_vP)
{
    QStringList l_oParts;
    for (int l_iI : l_vP) l_oParts << QString::number(l_iI);
    return l_oParts.join(QLatin1Char('/'));
}

NodePath f_PathFromString(const QString& l_oS)
{
    NodePath l_vP;
    if (l_oS.isEmpty()) return l_vP;
    for (const QString& l_oPart : l_oS.split(QLatin1Char('/'))) l_vP.push_back(l_oPart.toInt());
    return l_vP;
}

// ---- AnimCommand --------------------------------------------------------------------------------

AnimCommand::AnimCommand(AnimDocument* l_pDoc, const QString& l_oText, AnimState l_oBefore, AnimState l_oAfter, const QString& l_oMergeKey)
    : QUndoCommand(l_oText)
    , m_pDoc(l_pDoc)
    , m_oBefore(std::move(l_oBefore))
    , m_oAfter(std::move(l_oAfter))
    , m_oMergeKey(l_oMergeKey)
    , m_iId(l_oMergeKey.isEmpty() ? -1 : int(qHash(l_oMergeKey) & INT_MAX))
    , m_i64Time(QDateTime::currentMSecsSinceEpoch())
{
}

void AnimCommand::undo() { m_pDoc->f_RestoreState(m_oBefore); }

void AnimCommand::redo() { m_pDoc->f_RestoreState(m_oAfter); }

bool AnimCommand::mergeWith(const QUndoCommand* l_pOther)
{
    const auto* l_pO = static_cast<const AnimCommand*>(l_pOther);
    const bool l_bDrag = m_oMergeKey.startsWith(QLatin1Char('#'));
    if (l_pO->m_oMergeKey != m_oMergeKey || l_pO->m_oBefore.m_iClip != m_oAfter.m_iClip || l_pO->m_oBefore.m_vNode != m_oAfter.m_vNode) return false;
    if (!l_bDrag && l_pO->m_i64Time - m_i64Time > m_i64MergeWindowMs) return false;
    m_oAfter = l_pO->m_oAfter;
    m_i64Time = l_pO->m_i64Time;
    setObsolete(f_SameState(m_oBefore, m_oAfter));   // dragged back to where it started
    return true;
}

// ---- AnimDocument -------------------------------------------------------------------------------

AnimDocument::AnimDocument(QObject* l_pParent)
    : QObject(l_pParent)
    , m_pUndo(new QUndoStack(this))
{
    connect(m_pUndo, &QUndoStack::cleanChanged, this, [this] { emit f_DirtyChanged(f_IsDirty()); });
    // First of the clipChanged slots: a clip that becomes current starts unmodified.
    connect(this, &AnimDocument::f_ClipChanged, this, &AnimDocument::f_MarkClipBase);
    // The solo is a node of the current clip: another clip, or the node gone, ends it.
    connect(this, &AnimDocument::f_ClipChanged, this, [this] { f_SetSolo(std::nullopt); });
    connect(this, &AnimDocument::f_FileChanged, this, [this] {
        const Clip* l_pC = f_Clip();
        if (m_optSolo && (!l_pC || !f_NodeAt(l_pC->m_oRoot, *m_optSolo))) f_SetSolo(std::nullopt);
    });
    f_NewFile();
}

QString AnimDocument::f_DisplayName() const
{
    return m_oFilePath.isEmpty() ? tr("Untitled.anim") : QFileInfo(m_oFilePath).fileName();
}

bool AnimDocument::f_IsDirty() const { return !m_pUndo->isClean(); }

QString AnimDocument::f_ToJson() const { return f_Qs(toms::anim::f_AnimToJson(m_oState.m_oFile)); }

void AnimDocument::f_NewFile()
{
    AnimState l_oS;
    Clip l_oC;
    l_oC.m_strName = "clip";
    l_oC.m_oRoot.m_strName = "root";
    l_oS.m_oFile.m_vClips.push_back(l_oC);
    l_oS.m_oAtlasesAbs = m_oState.m_oAtlasesAbs;   // keep working with the same sprites (absolute until saved)
    l_oS.m_oAtlasIds = m_oState.m_oAtlasIds;
    f_Reset(l_oS, QString());
}

bool AnimDocument::f_Open(const QString& l_oPath, QString* l_pError)
{
    QFile l_oIn(l_oPath);
    if (!l_oIn.open(QIODevice::ReadOnly)) {
        if (l_pError) *l_pError = l_oIn.errorString();
        return false;
    }
    AnimState l_oS;
    std::string l_strErr;
    if (!toms::anim::f_ParseAnim(l_oIn.readAll().toStdString(), l_oS.m_oFile, &l_strErr)) {
        if (l_pError) *l_pError = f_Qs(l_strErr);
        return false;
    }
    const QString l_oAbs = QFileInfo(l_oPath).absoluteFilePath();
    const QDir l_oDir = QFileInfo(l_oAbs).absoluteDir();
    QStringList l_oAbsolute;
    for (const toms::anim::AtlasRef& l_oA : l_oS.m_oFile.m_vAtlases) {
        const QString l_oP = QDir::cleanPath(l_oDir.absoluteFilePath(f_Qs(l_oA.m_strPath)));   // an absolute path stays as it is
        if (f_IsAbsoluteAtlasPath(l_oA.m_strPath)) l_oAbsolute << l_oP;
        if (l_oS.m_oAtlasesAbs.contains(l_oP)) continue;
        l_oS.m_oAtlasesAbs << l_oP;
        l_oS.m_oAtlasIds << f_Qs(l_oA.m_strId);
    }
    f_Reset(l_oS, l_oAbs);
    if (!l_oAbsolute.isEmpty()) {
        m_oAbsoluteInFile = l_oAbsolute;
        emit f_Message(tr("%1 names %2 atlas(es) with an absolute path: they will be stored relative on save.")
                         .arg(f_DisplayName()).arg(l_oAbsolute.size()));
        emit f_AtlasChanged();   // the Problems dock and the Atlases list show them
    }
    return true;
}

bool AnimDocument::f_Save(const QString& l_oPath, QString* l_pError)
{
    const QString l_oAbs = QFileInfo(l_oPath).absoluteFilePath();
    AnimFile l_oF = m_oState.m_oFile;
    if (!f_RelativeAtlases(l_oAbs, l_oF.m_vAtlases, l_pError)) return false;
    QSaveFile l_oOut(l_oAbs);
    if (!l_oOut.open(QIODevice::WriteOnly)) {
        if (l_pError) *l_pError = l_oOut.errorString();
        return false;
    }
    l_oOut.write(QByteArray::fromStdString(toms::anim::f_AnimToJson(l_oF)));
    if (!l_oOut.commit()) {
        if (l_pError) *l_pError = l_oOut.errorString();
        return false;
    }
    if (!f_SaveMeshes(l_pError)) return false;   // its meshes go into its atlases' mesh files (docs/engine/16_MESH_ANIMATION.md)
    const bool l_bMoved = l_oAbs != m_oFilePath;
    m_oFilePath = l_oAbs;
    m_oState.m_oFile.m_vAtlases = l_oF.m_vAtlases;
    const bool l_bHadAbsolute = !m_oAbsoluteInFile.isEmpty();
    m_oAbsoluteInFile.clear();
    m_pUndo->setClean();
    f_MarkClipBase();   // saved: the clip's changes are kept
    if (l_bMoved) emit f_FilePathChanged();
    if (l_bMoved || l_bHadAbsolute) emit f_AtlasChanged();   // the stored (relative) paths changed
    emit f_DirtyChanged(false);
    emit f_Message(tr("Saved %1").arg(QDir::toNativeSeparators(l_oAbs)));
    return true;
}

bool AnimDocument::f_WriteBackup(const QString& l_oPath, QString* l_pError) const
{
    AnimFile l_oF = m_oState.m_oFile;
    l_oF.m_vAtlases.clear();
    for (int l_iI = 0; l_iI < m_oState.m_oAtlasesAbs.size(); l_iI++) {
        toms::anim::AtlasRef l_oR;
        l_oR.m_strPath = f_U8(QDir::fromNativeSeparators(m_oState.m_oAtlasesAbs[l_iI]));
        l_oR.m_strId = l_iI < m_oState.m_oAtlasIds.size() ? f_U8(m_oState.m_oAtlasIds[l_iI]) : toms::anim::f_DefaultAtlasId(l_oR.m_strPath);
        l_oF.m_vAtlases.push_back(l_oR);
    }
    QSaveFile l_oOut(l_oPath);
    if (!l_oOut.open(QIODevice::WriteOnly) || l_oOut.write(QByteArray::fromStdString(toms::anim::f_AnimToJson(l_oF))) < 0 || !l_oOut.commit()) {
        if (l_pError) *l_pError = l_oOut.errorString();
        return false;
    }
    return true;
}

void AnimDocument::f_Reset(AnimState l_oS, const QString& l_oPath)
{
    m_oFilePath = l_oPath;
    m_oState = std::move(l_oS);
    f_FixSelection(m_oState);
    m_oState.m_oFile.m_vAtlases = f_StoredAtlases(m_oState, m_oFilePath);
    m_pUndo->clear();
    m_pUndo->setClean();
    m_fTime = 0;
    m_oAbsoluteInFile.clear();
    f_LoadAtlases(true);
    f_UpdateMeshes();
    emit f_FileReset();
    emit f_FileChanged();
    emit f_ClipChanged();
    emit f_SelectionChanged();
    emit f_TimeChanged(m_fTime);
    emit f_FilePathChanged();
    emit f_DirtyChanged(false);
}

std::vector<toms::anim::AtlasRef> AnimDocument::f_StoredAtlases(const AnimState& l_oS, const QString& l_oFilePath)
{
    std::vector<toms::anim::AtlasRef> l_vOut;
    const QDir l_oDir = QFileInfo(l_oFilePath).absoluteDir();
    for (int l_iI = 0; l_iI < l_oS.m_oAtlasesAbs.size(); l_iI++) {
        const QString& l_oA = l_oS.m_oAtlasesAbs[l_iI];
        toms::anim::AtlasRef l_oR;
        l_oR.m_strPath = f_U8(QDir::fromNativeSeparators(l_oFilePath.isEmpty() ? l_oA : l_oDir.relativeFilePath(l_oA)));
        l_oR.m_strId = l_iI < l_oS.m_oAtlasIds.size() ? f_U8(l_oS.m_oAtlasIds[l_iI]) : toms::anim::f_DefaultAtlasId(l_oR.m_strPath);
        l_vOut.push_back(l_oR);
    }
    return l_vOut;
}

bool AnimDocument::f_RelativeAtlases(const QString& l_oAnimPath, std::vector<toms::anim::AtlasRef>& l_vOut, QString* l_pError) const
{
    l_vOut = f_StoredAtlases(m_oState, QFileInfo(l_oAnimPath).absoluteFilePath());
    for (size_t l_nI = 0; l_nI < l_vOut.size(); l_nI++)
        if (f_IsAbsoluteAtlasPath(l_vOut[l_nI].m_strPath)) {   // QDir::relativeFilePath gives up across drives / roots
            if (l_pError)
                *l_pError = tr("The atlas %1 cannot be stored relative to %2: they are on different drives.\n\n"
                            "Save the .anim on the same drive as its atlases, or remove / replace that atlas.")
                             .arg(QDir::toNativeSeparators(m_oState.m_oAtlasesAbs[int(l_nI)]), QDir::toNativeSeparators(l_oAnimPath));
            return false;
        }
    return true;
}

void AnimDocument::f_FixSelection(AnimState& l_oS) const
{
    if (l_oS.m_oFile.m_vClips.empty()) {
        l_oS.m_iClip = 0;
        l_oS.m_vNode.clear();
        return;
    }
    l_oS.m_iClip = std::clamp(l_oS.m_iClip, 0, int(l_oS.m_oFile.m_vClips.size()) - 1);
    while (!l_oS.m_vNode.empty() && !f_NodeAt(l_oS.m_oFile.m_vClips[size_t(l_oS.m_iClip)].m_oRoot, l_oS.m_vNode)) l_oS.m_vNode.pop_back();
}

void AnimDocument::f_RestoreState(const AnimState& l_oS)
{
    const bool l_bClipChanged = l_oS.m_iClip != m_oState.m_iClip;
    const bool l_bSelChanged = l_bClipChanged || l_oS.m_vNode != m_oState.m_vNode;
    const bool l_bAtlasChanged = l_oS.m_oAtlasesAbs != m_oState.m_oAtlasesAbs || l_oS.m_oAtlasIds != m_oState.m_oAtlasIds;
    m_oState = l_oS;
    f_FixSelection(m_oState);
    m_oState.m_oFile.m_vAtlases = f_StoredAtlases(m_oState, m_oFilePath);
    if (l_bAtlasChanged) f_LoadAtlases(false);
    f_UpdateMeshes();
    emit f_FileChanged();
    if (l_bClipChanged) emit f_ClipChanged();
    if (l_bSelChanged) emit f_SelectionChanged();
}

// ---- atlases ------------------------------------------------------------------------------------

namespace {

std::shared_ptr<AnimDocument::LoadedAtlas> f_LoadOne(const QString& l_oPath)
{
    auto l_spA = std::make_shared<AnimDocument::LoadedAtlas>();
    l_spA->m_oPath = l_oPath;
    QFile l_oIn(l_oPath);
    std::string l_strErr;
    if (!l_oIn.open(QIODevice::ReadOnly)) {
        l_spA->m_oError = AnimDocument::tr("cannot read %1: %2").arg(QDir::toNativeSeparators(l_oPath), l_oIn.errorString());
        return l_spA;
    }
    if (!toms::f_ParseAtlas(l_oIn.readAll().toStdString(), l_spA->m_oAtlas, &l_strErr)) {
        l_spA->m_oError = AnimDocument::tr("%1: %2").arg(QDir::toNativeSeparators(l_oPath), f_Qs(l_strErr));
        return l_spA;
    }
    QVector<QImage> l_oPages;
    const QDir l_oDir = QFileInfo(l_oPath).absoluteDir();
    bool l_bSizesChanged = false;
    for (toms::AtlasPage& l_oP : l_spA->m_oAtlas.m_vPages) {
        QImage l_oImg(l_oDir.filePath(f_Qs(l_oP.m_strFile)));
        if (l_oImg.isNull()) l_spA->m_oError = AnimDocument::tr("cannot load the atlas page %1").arg(QDir::toNativeSeparators(l_oDir.filePath(f_Qs(l_oP.m_strFile))));
        if (!l_oImg.isNull() && (l_oP.m_iW <= 0 || l_oP.m_iH <= 0)) {
            l_oP.m_iW = l_oImg.width();
            l_oP.m_iH = l_oImg.height();
            l_bSizesChanged = true;
        }
        l_oPages.push_back(l_oImg);
    }
    if (l_bSizesChanged) l_spA->m_oAtlas.f_ComputeUVs();
    for (const QImage& l_oImg : l_oPages) l_spA->m_vRgba.push_back(l_oImg.convertToFormat(QImage::Format_RGBA8888));   // meshes are cut from these
    {   // its mesh file (docs/engine/16_MESH_ANIMATION.md): what other clips put there, "hand" meshes, and its form
        const std::string l_strMeshPath = toms::f_SpriteMeshPath(f_U8(l_oPath));
        const bool l_bThere = QFileInfo::exists(f_Qs(l_strMeshPath));
        bool l_bBinary = false;
        std::string l_strMerr;
        if (!animed::f_ReadMeshFile(l_strMeshPath, l_spA->m_oMeshesOnDisk, &l_bBinary, &l_strMerr)) {
            l_spA->m_oMeshesOnDisk = toms::SpriteMeshFile();
            l_spA->m_oMeshWarning = AnimDocument::tr("Meshes: %1 (cut again)").arg(f_Qs(l_strMerr));
        }
        l_spA->m_bMeshesBinary = l_bThere ? l_bBinary : true;
        l_spA->m_oMeshes = l_spA->m_oMeshesOnDisk;
    }
    l_spA->m_oImages.f_SetPages(l_oPages);
    for (const toms::AtlasRegion& l_oR : l_spA->m_oAtlas.m_vRegions) l_spA->m_oSpriteNames << f_Qs(l_oR.m_strName);
    l_spA->m_oSpriteNames.removeDuplicates();
    l_spA->m_oSpriteNames.sort(Qt::CaseInsensitive);
    l_spA->m_bOk = l_spA->m_oError.isEmpty();
    return l_spA;
}

}  // namespace

// force: reread every atlas; else keep the ones already loaded (undo / redo of a list change).
void AnimDocument::f_LoadAtlases(bool l_bForce)
{
    m_mapBoxes.clear();   // the sprites' visible pixels are measured again (drawClip)
    m_bFitValid = false;
    std::vector<std::shared_ptr<LoadedAtlas>> l_vNext;
    for (const QString& l_oPath : m_oState.m_oAtlasesAbs) {
        std::shared_ptr<LoadedAtlas> l_spA;
        if (!l_bForce)
            for (const auto& l_spOld : m_vAtlases)
                if (l_spOld->m_oPath == l_oPath) l_spA = l_spOld;
        if (!l_spA) {
            l_spA = f_LoadOne(l_oPath);
            if (!l_spA->m_oError.isEmpty()) emit f_Message(tr("Atlas: %1").arg(l_spA->m_oError));
            if (!l_spA->m_oMeshWarning.isEmpty()) emit f_Message(l_spA->m_oMeshWarning);
        }
        l_vNext.push_back(l_spA);
    }
    m_vAtlases = std::move(l_vNext);
    // The game's lookup: every page gets its own texture id, which maps back to (atlas, page).
    m_oAtlasSet = toms::anim::AtlasSet();
    m_vTextures.clear();
    m_vSetToDoc.clear();
    m_oSpriteNames.clear();
    for (size_t l_nI = 0; l_nI < m_vAtlases.size(); l_nI++) {
        const LoadedAtlas& l_oA = *m_vAtlases[l_nI];
        if (!l_oA.m_bOk) continue;
        std::vector<uint16_t> l_vPageTextures;
        for (size_t l_nP = 0; l_nP < l_oA.m_oAtlas.m_vPages.size(); l_nP++) {
            l_vPageTextures.push_back(uint16_t(m_vTextures.size()));
            m_vTextures.push_back({int(l_nI), int(l_nP)});
        }
        m_oAtlasSet.f_Add(l_oA.m_oAtlas, f_U8(f_AtlasId(int(l_nI))), std::move(l_vPageTextures));
        m_vSetToDoc.push_back(int(l_nI));
        m_oSpriteNames << l_oA.m_oSpriteNames;
    }
    m_oSpriteNames.removeDuplicates();
    m_oSpriteNames.sort(Qt::CaseInsensitive);
    // Every copy, by name and then lookup order: "coin (game)", "coin (extra)", "spark (extra)".
    m_oSpriteChoices.clear();
    for (const QString& l_oName : m_oSpriteNames)
        for (size_t l_nI = 0; l_nI < m_vAtlases.size(); l_nI++)
            if (m_vAtlases[l_nI]->m_bOk && m_vAtlases[l_nI]->m_oAtlas.f_Find(f_U8(l_oName))) m_oSpriteChoices << f_SpriteDisplay(f_SpriteRefFor(int(l_nI), l_oName));
    emit f_AtlasChanged();
}

void AnimDocument::f_ReloadAtlases()
{
    f_LoadAtlases(true);
    f_UpdateMeshes();   // cut again from the reloaded pixels
    emit f_FileChanged();   // repaint everything that shows sprites
}

QString AnimDocument::f_StoredAtlasPath(int l_iI) const
{
    return l_iI >= 0 && l_iI < int(m_oState.m_oFile.m_vAtlases.size()) ? f_Qs(m_oState.m_oFile.m_vAtlases[size_t(l_iI)].m_strPath) : QString();
}

bool AnimDocument::f_AtlasPathIsAbsoluteInFile(int l_iI) const
{
    return l_iI >= 0 && l_iI < m_oState.m_oAtlasesAbs.size() && m_oAbsoluteInFile.contains(m_oState.m_oAtlasesAbs[l_iI]);
}

bool AnimDocument::f_AtlasLoaded() const
{
    if (m_vAtlases.empty()) return false;
    for (const auto& l_spA : m_vAtlases)
        if (!l_spA->m_bOk) return false;
    return true;
}

const SpriteImageCache* AnimDocument::f_PageImages(uint16_t l_u16Texture, int* l_pPage) const
{
    if (l_u16Texture >= m_vTextures.size()) return nullptr;
    const auto [l_iAtlas, l_iP] = m_vTextures[l_u16Texture];
    if (l_pPage) *l_pPage = l_iP;
    return &m_vAtlases[size_t(l_iAtlas)]->m_oImages;
}

std::vector<CheckAtlas> AnimDocument::f_CheckAtlases() const
{
    std::vector<CheckAtlas> l_vOut;
    for (int l_iI = 0; l_iI < f_AtlasCount(); l_iI++) {
        const LoadedAtlas& l_oA = f_AtlasAt(l_iI);
        CheckAtlas l_oR;
        l_oR.m_strId = f_U8(f_AtlasId(l_iI));
        l_oR.m_strPath = f_U8(f_StoredAtlasPath(l_iI));
        l_oR.m_pAtlas = l_oA.m_bOk ? &l_oA.m_oAtlas : nullptr;
        l_oR.m_strError = f_U8(l_oA.m_oError);
        l_oR.m_bAbsolute = f_AtlasPathIsAbsoluteInFile(l_iI);
        l_oR.m_pMeshes = l_oA.m_bOk && !l_oA.m_oMeshes.m_mapMeshes.empty() ? &l_oA.m_oMeshes : nullptr;
        l_vOut.push_back(l_oR);
    }
    return l_vOut;
}

const toms::AtlasRegion* AnimDocument::f_FindSprite(const std::string& l_strRef, int* l_pAtlasIndex) const
{
    int l_iEntry = -1;
    const toms::AtlasRegion* l_pR = m_oAtlasSet.f_Find(l_strRef, nullptr, &l_iEntry);
    if (l_pR && l_pAtlasIndex) *l_pAtlasIndex = l_iEntry >= 0 && l_iEntry < int(m_vSetToDoc.size()) ? m_vSetToDoc[size_t(l_iEntry)] : -1;
    return l_pR;
}

std::string AnimDocument::f_SpriteRefFor(int l_iAtlasIndex, const QString& l_oName) const
{
    return toms::anim::f_SpriteRef(f_U8(f_AtlasId(l_iAtlasIndex)), f_U8(l_oName));
}

QString AnimDocument::f_SpriteDisplay(const std::string& l_strRef) const
{
    if (l_strRef.empty()) return tr("(none)");
    const toms::anim::SpriteRef l_oS = toms::anim::f_ParseSpriteRef(l_strRef);
    if (!l_oS.m_strAtlas.empty()) return QStringLiteral("%1 (%2)").arg(f_Qs(l_oS.m_strName), f_Qs(l_oS.m_strAtlas));
    int l_iI = -1;
    if (f_FindSprite(l_strRef, &l_iI) && l_iI >= 0) return QStringLiteral("%1 (auto: %2)").arg(f_Qs(l_oS.m_strName), f_AtlasId(l_iI));
    return f_Qs(l_oS.m_strName);
}

std::string AnimDocument::f_SpriteFromText(const QString& l_oText) const
{
    const QString l_oT = l_oText.trimmed();
    if (l_oT.isEmpty() || l_oT == tr("(none)") || l_oT == QLatin1String("(none)")) return std::string();
    // "name (id)" / "name (auto: id)" -- what the fields show.
    static const QRegularExpression l_oShown(QStringLiteral("^(.*\\S)\\s*\\((?:auto:\\s*)?([^()\\s]+)\\)$"));
    const QRegularExpressionMatch l_oM = l_oShown.match(l_oT);
    if (l_oM.hasMatch()) return toms::anim::f_SpriteRef(f_U8(l_oM.captured(2)), f_U8(l_oM.captured(1)));
    const toms::anim::SpriteRef l_oS = toms::anim::f_ParseSpriteRef(f_U8(l_oT));
    if (!l_oS.m_strAtlas.empty()) return toms::anim::f_SpriteRef(f_U8(f_Qs(l_oS.m_strAtlas).trimmed()), f_U8(f_Qs(l_oS.m_strName).trimmed()));
    int l_iI = -1;   // a bare name: the atlas the lookup picks now
    if (f_FindSprite(l_oS.m_strName, &l_iI) && l_iI >= 0) return f_SpriteRefFor(l_iI, f_Qs(l_oS.m_strName));
    return l_oS.m_strName;
}

QRect AnimDocument::f_RegionRect(int l_iAtlasIndex, const toms::AtlasRegion& l_oR) const
{
    if (l_iAtlasIndex < 0 || l_iAtlasIndex >= f_AtlasCount()) return QRect();
    const QVector<QImage>& l_oPages = f_AtlasAt(l_iAtlasIndex).m_oImages.f_Pages();
    if (l_oR.m_iPage < 0 || l_oR.m_iPage >= l_oPages.size()) return QRect();
    const QSize l_oPs = l_oPages[l_oR.m_iPage].size();
    const int l_iX0 = int(std::lround(l_oR.m_arrUv[0] * l_oPs.width())), l_iY0 = int(std::lround(l_oR.m_arrUv[1] * l_oPs.height()));
    const int l_iX1 = int(std::lround(l_oR.m_arrUv[2] * l_oPs.width())), l_iY1 = int(std::lround(l_oR.m_arrUv[3] * l_oPs.height()));
    return QRect(l_iX0, l_iY0, l_iX1 - l_iX0, l_iY1 - l_iY0);
}

QImage AnimDocument::f_SpriteImage(int l_iAtlasIndex, const QString& l_oName) const
{
    if (l_iAtlasIndex < 0 || l_iAtlasIndex >= f_AtlasCount() || !f_AtlasAt(l_iAtlasIndex).m_bOk) return QImage();
    const toms::AtlasRegion* l_pR = f_AtlasAt(l_iAtlasIndex).m_oAtlas.f_Find(f_U8(l_oName));
    return l_pR ? f_AtlasAt(l_iAtlasIndex).m_oImages.f_Cut(l_pR->m_iPage, f_RegionRect(l_iAtlasIndex, *l_pR)) : QImage();
}

QImage AnimDocument::f_SpriteImage(const QString& l_oRef) const
{
    int l_iI = -1;
    return f_FindSprite(f_U8(l_oRef), &l_iI) ? f_SpriteImage(l_iI, f_Qs(toms::anim::f_ParseSpriteRef(f_U8(l_oRef)).m_strName)) : QImage();
}

bool AnimDocument::f_AtlasIdUsable(const QString& l_oId, int l_iExceptIndex, QString* l_pError) const
{
    if (!f_IsAtlasId(f_U8(l_oId))) {
        if (l_pError) *l_pError = tr("'%1' cannot be an atlas id: it must not be empty or contain ':', '/', '\\', '=' or spaces.").arg(l_oId);
        return false;
    }
    const int l_iOther = f_AtlasIndexOf(l_oId);
    if (l_iOther >= 0 && l_iOther != l_iExceptIndex) {
        if (l_pError) *l_pError = tr("Another atlas (%1) has the id '%2' already: ids must be unique.").arg(f_StoredAtlasPath(l_iOther), l_oId);
        return false;
    }
    return true;
}

QString AnimDocument::f_SuggestAtlasId(const QString& l_oAbsolutePath) const
{
    auto l_fnClean = [](QString l_oS) {
        for (QChar& l_oC : l_oS)
            if (l_oC == QLatin1Char(':') || l_oC == QLatin1Char('/') || l_oC == QLatin1Char('\\') || l_oC == QLatin1Char('=') || l_oC.isSpace())
                l_oC = QLatin1Char('_');
        return l_oS;
    };
    const QFileInfo l_oFi(l_oAbsolutePath);
    const QString l_oBase = l_fnClean(f_Qs(toms::anim::f_DefaultAtlasId(f_U8(l_oFi.fileName()))));
    if (f_AtlasIndexOf(l_oBase) < 0 && f_IsAtlasId(f_U8(l_oBase))) return l_oBase;
    QDir l_oUp = l_oFi.absoluteDir();   // styles/dark16/atlas/game.atlas -> "dark16"
    QString l_oFolder = l_oUp.cdUp() ? l_fnClean(l_oUp.dirName()) : QString();
    QString l_oStart = f_IsAtlasId(f_U8(l_oFolder)) ? l_oFolder : l_oBase.isEmpty() ? QStringLiteral("atlas") : l_oBase;
    if (f_AtlasIndexOf(l_oStart) < 0) return l_oStart;
    for (int l_iN = 2;; l_iN++)
        if (f_AtlasIndexOf(QStringLiteral("%1_%2").arg(l_oStart).arg(l_iN)) < 0) return QStringLiteral("%1_%2").arg(l_oStart).arg(l_iN);
}

bool AnimDocument::f_AddAtlas(const QString& l_oAbsolutePath, const QString& l_oIdIn, QString* l_pError)
{
    const QString l_oAbs = QDir::cleanPath(QFileInfo(l_oAbsolutePath).absoluteFilePath());
    if (m_oState.m_oAtlasesAbs.contains(l_oAbs)) {
        if (l_pError) *l_pError = tr("%1 is already in the list.").arg(QDir::toNativeSeparators(l_oAbs));
        return false;
    }
    const QString l_oId = l_oIdIn.isEmpty() ? f_Qs(toms::anim::f_DefaultAtlasId(f_U8(QFileInfo(l_oAbs).fileName()))) : l_oIdIn.trimmed();
    if (!f_AtlasIdUsable(l_oId, -1, l_pError)) return false;
    QFile l_oProbe(l_oAbs);
    if (!l_oProbe.open(QIODevice::ReadOnly)) {
        if (l_pError) *l_pError = l_oProbe.errorString();
        return false;
    }
    toms::AtlasFile l_oTest;
    std::string l_strErr;
    if (!toms::f_ParseAtlas(l_oProbe.readAll().toStdString(), l_oTest, &l_strErr)) {
        if (l_pError) *l_pError = f_Qs(l_strErr);
        return false;
    }
    if (!m_oFilePath.isEmpty() && f_IsAbsoluteAtlasPath(f_U8(QFileInfo(m_oFilePath).absoluteDir().relativeFilePath(l_oAbs)))) {
        if (l_pError)
            *l_pError = tr("%1 is on another drive than %2: atlas paths are stored relative to the .anim file.")
                         .arg(QDir::toNativeSeparators(l_oAbs), QDir::toNativeSeparators(m_oFilePath));
        return false;
    }
    return f_Edit(tr("Add atlas"), [&](AnimState& l_oS) {
        l_oS.m_oAtlasesAbs << l_oAbs;
        l_oS.m_oAtlasIds << l_oId;
        return true;
    });
}

bool AnimDocument::f_RemoveAtlas(int l_iIndex)
{
    if (l_iIndex < 0 || l_iIndex >= m_oState.m_oAtlasesAbs.size()) return false;
    return f_Edit(tr("Remove atlas"), [&](AnimState& l_oS) {
        l_oS.m_oAtlasesAbs.removeAt(l_iIndex);
        l_oS.m_oAtlasIds.removeAt(l_iIndex);
        return true;
    });
}

bool AnimDocument::f_MoveAtlas(int l_iIndex, int l_iDelta)
{
    const int l_iTo = l_iIndex + l_iDelta;
    if (l_iIndex < 0 || l_iIndex >= m_oState.m_oAtlasesAbs.size() || l_iTo < 0 || l_iTo >= m_oState.m_oAtlasesAbs.size()) return false;
    return f_Edit(tr("Reorder atlases"), [&](AnimState& l_oS) {
        l_oS.m_oAtlasesAbs.move(l_iIndex, l_iTo);
        l_oS.m_oAtlasIds.move(l_iIndex, l_iTo);
        return true;
    });
}

bool AnimDocument::f_RenameAtlasId(int l_iIndex, const QString& l_oIdIn, QString* l_pError)
{
    const QString l_oId = l_oIdIn.trimmed();
    if (l_iIndex < 0 || l_iIndex >= m_oState.m_oAtlasIds.size() || l_oId == m_oState.m_oAtlasIds[l_iIndex]) return false;
    if (!f_AtlasIdUsable(l_oId, l_iIndex, l_pError)) return false;
    const std::string l_strFrom = f_U8(m_oState.m_oAtlasIds[l_iIndex]), l_strTo = f_U8(l_oId);
    int l_iRewritten = 0;
    const bool l_bOk = f_Edit(tr("Rename atlas id"), [&](AnimState& l_oS) {
        l_oS.m_oAtlasIds[l_iIndex] = l_oId;
        for (Clip& l_oC : l_oS.m_oFile.m_vClips)
            l_iRewritten += f_RewriteSpriteRefs(l_oC.m_oRoot, [&](const std::string& l_strR) {
                const toms::anim::SpriteRef l_oP = toms::anim::f_ParseSpriteRef(l_strR);
                return l_oP.m_strAtlas == l_strFrom ? toms::anim::f_SpriteRef(l_strTo, l_oP.m_strName) : l_strR;
            });
        return true;
    });
    if (l_bOk) emit f_Message(tr("Atlas id '%1' is now '%2': %n sprite reference(s) rewritten.", nullptr, l_iRewritten).arg(f_Qs(l_strFrom), l_oId));
    return l_bOk;
}

int AnimDocument::f_AtlasReferenceCount(int l_iIndex) const
{
    if (l_iIndex < 0 || l_iIndex >= f_AtlasCount()) return 0;
    const std::string l_strId = f_U8(f_AtlasId(l_iIndex));
    int l_iN = 0;
    for (const Clip& l_oC : m_oState.m_oFile.m_vClips) {
        std::vector<std::string> l_vRefs;
        f_CollectSpriteRefs(l_oC.m_oRoot, l_vRefs);
        for (const std::string& l_strR : l_vRefs) {
            const toms::anim::SpriteRef l_oP = toms::anim::f_ParseSpriteRef(l_strR);
            if (!l_oP.m_strAtlas.empty()) {
                l_iN += l_oP.m_strAtlas == l_strId;
                continue;
            }
            int l_iOwner = -1, l_iOthers = 0;
            for (int l_iI = 0; l_iI < f_AtlasCount(); l_iI++)
                if (f_AtlasAt(l_iI).m_bOk && f_AtlasAt(l_iI).m_oAtlas.f_Find(l_oP.m_strName)) {
                    if (l_iI == l_iIndex) l_iOwner = l_iI;
                    else l_iOthers++;
                }
            l_iN += l_iOwner == l_iIndex && l_iOthers == 0;
        }
    }
    return l_iN;
}

int AnimDocument::f_BareSpriteReferenceCount() const
{
    int l_iN = 0;
    for (const Clip& l_oC : m_oState.m_oFile.m_vClips) {
        std::vector<std::string> l_vRefs;
        f_CollectSpriteRefs(l_oC.m_oRoot, l_vRefs);
        for (const std::string& l_strR : l_vRefs) l_iN += toms::anim::f_ParseSpriteRef(l_strR).m_strAtlas.empty() && f_FindSprite(l_strR);
    }
    return l_iN;
}

int AnimDocument::f_QualifySpriteReferences()
{
    int l_iChanged = 0;
    f_Edit(tr("Qualify sprite references"), [&](AnimState& l_oS) {
        for (Clip& l_oC : l_oS.m_oFile.m_vClips)
            l_iChanged += f_RewriteSpriteRefs(l_oC.m_oRoot, [&](const std::string& l_strR) {
                if (!toms::anim::f_ParseSpriteRef(l_strR).m_strAtlas.empty()) return l_strR;
                int l_iI = -1;
                return f_FindSprite(l_strR, &l_iI) && l_iI >= 0 ? f_SpriteRefFor(l_iI, f_Qs(l_strR)) : l_strR;
            });
        return l_iChanged > 0;
    });
    emit f_Message(l_iChanged ? tr("Qualified %n sprite reference(s): each now stores its atlas (id:name).", nullptr, l_iChanged)
                         : tr("No bare sprite names to qualify (names no atlas has stay as they are)."));
    return l_iChanged;
}

// ---- current clip / node / time -----------------------------------------------------------------

const Clip* AnimDocument::f_Clip() const
{
    if (m_oState.m_oFile.m_vClips.empty()) return nullptr;
    return &m_oState.m_oFile.m_vClips[size_t(m_oState.m_iClip)];
}

void AnimDocument::f_MarkClipBase()
{
    const Clip* l_pC = f_Clip();
    m_iClipBaseIndex = l_pC ? m_oState.m_iClip : -1;
    m_oClipBase = l_pC ? *l_pC : Clip();
}

bool AnimDocument::f_ClipModified() const
{
    const Clip* l_pC = f_Clip();
    return l_pC && m_iClipBaseIndex == m_oState.m_iClip && *l_pC != m_oClipBase;
}

bool AnimDocument::f_DiscardClipChanges()
{
    if (!f_ClipModified()) return false;
    const Clip l_oBase = m_oClipBase;
    return f_Edit(tr("Discard changes to clip %1").arg(f_Qs(l_oBase.m_strName)), [&](AnimState& l_oS) {
        l_oS.m_oFile.m_vClips[size_t(l_oS.m_iClip)] = l_oBase;
        l_oS.m_vNode.clear();
        return true;
    });
}

void AnimDocument::f_SetClipIndex(int l_iIndex)
{
    if (l_iIndex < 0 || l_iIndex >= int(m_oState.m_oFile.m_vClips.size()) || l_iIndex == m_oState.m_iClip) return;
    m_oState.m_iClip = l_iIndex;
    m_oState.m_vNode.clear();
    emit f_ClipChanged();
    emit f_SelectionChanged();
    if (m_fTime > f_Duration()) f_SetTime(0);
}

const Node* AnimDocument::f_SelectedNode() const
{
    const Clip* l_pC = f_Clip();
    return l_pC ? f_NodeAt(l_pC->m_oRoot, m_oState.m_vNode) : nullptr;
}

void AnimDocument::f_SelectNode(const NodePath& l_vPath)
{
    const Clip* l_pC = f_Clip();
    if (!l_pC || !f_NodeAt(l_pC->m_oRoot, l_vPath) || l_vPath == m_oState.m_vNode) return;
    m_oState.m_vNode = l_vPath;
    emit f_SelectionChanged();
}

bool AnimDocument::f_SelectNodeByName(const QString& l_oName)
{
    const Clip* l_pC = f_Clip();
    if (!l_pC) return false;
    std::function<bool(const Node&, NodePath&)> l_fnFind = [&](const Node& l_oN, NodePath& l_vP) {
        if (f_Qs(l_oN.m_strName) == l_oName) return true;
        for (size_t l_nI = 0; l_nI < l_oN.m_vChildren.size(); l_nI++) {
            l_vP.push_back(int(l_nI));
            if (l_fnFind(l_oN.m_vChildren[l_nI], l_vP)) return true;
            l_vP.pop_back();
        }
        return false;
    };
    NodePath l_vP;
    if (!l_fnFind(l_pC->m_oRoot, l_vP)) return false;
    f_SelectNode(l_vP);
    return true;
}

void AnimDocument::f_SetTime(float l_fT)
{
    if (!std::isfinite(l_fT)) return;
    l_fT = std::clamp(l_fT, 0.0f, 3600.0f);
    if (l_fT == m_fTime) {
        // Asked for the time it already has: still back from a preview that ran on (looping nodes past the end),
        // so a click on the playhead's own key row shows that row.
        if (m_fPreview >= 0) {
            f_SetPreviewTime(-1);
            emit f_TimeChanged(m_fTime);
        }
        return;
    }
    m_fTime = l_fT;
    f_SetPreviewTime(-1);
    emit f_TimeChanged(m_fTime);
}

void AnimDocument::f_SetSolo(const std::optional<NodePath>& l_optPath)
{
    if (l_optPath == m_optSolo) return;
    m_optSolo = l_optPath;
    emit f_SoloChanged();
}

void AnimDocument::f_SetPreviewCharacter(const QString& l_oSprite)
{
    if (l_oSprite == m_oPreviewCharacter) return;
    m_oPreviewCharacter = l_oSprite;
    m_bFitValid = false;
    emit f_FileChanged();   // every view redraws
}

bool AnimDocument::f_SetHeldFile(const QString& l_oPath, QString* l_pError)
{
    toms::anim::AnimFile l_oHeld;
    if (!l_oPath.isEmpty()) {
        QFile l_oF(l_oPath);
        std::string l_strErr;
        if (!l_oF.open(QIODevice::ReadOnly) || !toms::anim::f_ParseAnim(l_oF.readAll().toStdString(), l_oHeld, &l_strErr)) {
            if (l_pError) *l_pError = l_strErr.empty() ? tr("cannot read %1").arg(l_oPath) : f_Qs(l_strErr);
            return false;
        }
    }
    m_oHeldPath = l_oPath;
    m_oHeld = std::move(l_oHeld);
    emit f_FileChanged();   // every view redraws
    return true;
}

const Clip* AnimDocument::f_HeldClip() const
{
    const Clip* l_pC = f_Clip();
    return l_pC && !m_oHeldPath.isEmpty() ? m_oHeld.f_Find(l_pC->m_strName) : nullptr;
}

QStringList AnimDocument::f_CharacterChoices() const
{
    const Clip* l_pC = f_Clip();
    const Node* l_pBody = l_pC ? toms::anim::f_FindNode(l_pC->m_oRoot, "body") : nullptr;
    if (!l_pBody || !toms::anim::f_FindNode(l_pC->m_oRoot, "hand") || l_pBody->m_strSprite.empty()) return {};
    const QString l_oBase = f_Qs(toms::anim::f_ParseSpriteRef(l_pBody->m_strSprite).m_strName);
    QStringList l_oOut;
    for (const QString& l_oName : m_oSpriteNames)
        if (l_oName == l_oBase || l_oName.startsWith(l_oBase + QLatin1Char('_'))) l_oOut << l_oName;
    return l_oOut;
}

const Clip* AnimDocument::f_DrawClip() const
{
    const Clip* l_pC = f_Clip();
    if (!l_pC || m_oPreviewCharacter.isEmpty()) return l_pC;
    if (m_bFitValid && m_oFitFor == m_oPreviewCharacter && m_oFitSource == *l_pC) return &m_oFitted;
    // A sprite's visible pixels, from its atlas page (cached by reference until the atlases reload).
    auto l_fnBoxOf = [this](const std::string& l_strRef, toms::SpriteBox& l_oOut) {
        auto l_itIt = m_mapBoxes.find(l_strRef);
        if (l_itIt == m_mapBoxes.end()) {
            int l_iAi = -1;
            const toms::AtlasRegion* l_pR = f_FindSprite(l_strRef, &l_iAi);
            if (!l_pR || l_iAi < 0 || l_pR->m_bRotated || l_pR->m_bChild || l_pR->m_iPage < 0 || l_pR->m_iPage >= m_vAtlases[size_t(l_iAi)]->m_oImages.f_Pages().size())
                return false;
            const QImage l_oPage = m_vAtlases[size_t(l_iAi)]->m_oImages.f_Pages()[l_pR->m_iPage].convertToFormat(QImage::Format_RGBA8888);
            if (l_pR->m_iX < 0 || l_pR->m_iY < 0 || l_pR->m_iX + l_pR->m_iW > l_oPage.width() || l_pR->m_iY + l_pR->m_iH > l_oPage.height()) return false;
            const uchar* l_pPx = l_oPage.constBits() + size_t(l_pR->m_iY) * l_oPage.bytesPerLine() + size_t(l_pR->m_iX) * 4;
            l_itIt = m_mapBoxes.emplace(l_strRef, toms::f_VisibleBox(l_pPx, l_oPage.bytesPerLine() / 4, l_pR->m_iW, l_pR->m_iH, l_pR->m_iOffX, l_pR->m_iOffY, l_pR->m_iOrigW, l_pR->m_iOrigH)).first;
        }
        l_oOut = l_itIt->second;
        return true;
    };
    // The character's nudge, as the game reads it: data/character_fit.json above the .anim (assets/data/).
    glm::vec2 l_v2Nudge(0.0f);
    for (QDir l_oD = QFileInfo(m_oFilePath).absoluteDir(); !m_oFilePath.isEmpty(); ) {
        QFile l_oF(l_oD.filePath(QStringLiteral("data/character_fit.json")));
        if (l_oF.open(QIODevice::ReadOnly)) {
            const nlohmann::json l_jJ = nlohmann::json::parse(l_oF.readAll().toStdString(), nullptr, false);
            const std::string l_strId = f_U8(m_oPreviewCharacter);
            if (l_jJ.is_object() && l_jJ.contains(l_strId) && l_jJ[l_strId].is_object()) {
                const nlohmann::json l_jH = l_jJ[l_strId].value("hand", nlohmann::json::array());
                if (l_jH.is_array() && l_jH.size() == 2 && l_jH[0].is_number() && l_jH[1].is_number()) l_v2Nudge = {l_jH[0].get<float>(), l_jH[1].get<float>()};
            }
            break;
        }
        if (!l_oD.cdUp()) break;
    }
    m_oFitSource = *l_pC;
    m_oFitted = toms::anim::f_FitCharacter(*l_pC, f_U8(m_oPreviewCharacter), l_fnBoxOf, l_v2Nudge);
    m_oFitFor = m_oPreviewCharacter;
    m_bFitValid = true;
    return &m_oFitted;
}

const Node* AnimDocument::f_DrawNode(const Node* l_pN) const
{
    const Clip* l_pC = f_Clip();
    const Clip* l_pD = f_DrawClip();
    if (!l_pN || !l_pC || l_pC == l_pD) return l_pN;
    NodePath l_vP;
    return f_FindPath(l_pC->m_oRoot, l_pN, l_vP) ? f_NodeAt(l_pD->m_oRoot, l_vP) : nullptr;
}

bool AnimDocument::f_SoloShows(const Node* l_pN) const
{
    const Clip* l_pC = f_Clip();
    if (!m_optSolo || !l_pC) return true;
    NodePath l_vProbe;
    if (!f_FindPath(l_pC->m_oRoot, l_pN, l_vProbe) && f_DrawClip() != l_pC) l_pC = f_DrawClip();   // a node of the fitted copy
    const Node* l_pTop = f_NodeAt(l_pC->m_oRoot, *m_optSolo);
    if (!l_pTop) return true;
    std::function<bool(const Node&)> l_fnInside = [&](const Node& l_oX) {
        if (&l_oX == l_pN) return true;
        for (const Node& l_oK : l_oX.m_vChildren)
            if (l_fnInside(l_oK)) return true;
        return false;
    };
    return l_fnInside(*l_pTop);
}

void AnimDocument::f_SetPreviewTime(float l_fT)
{
    if (!std::isfinite(l_fT) || l_fT < 0) l_fT = -1;
    if (l_fT == m_fPreview) return;
    m_fPreview = l_fT;
    emit f_PreviewTimeChanged();
}

float AnimDocument::f_Duration() const
{
    const Clip* l_pC = f_Clip();
    return l_pC ? l_pC->f_Duration() : 0.0f;
}

void AnimDocument::f_SetAutoKey(bool l_bOn)
{
    if (l_bOn == m_bAutoKey) return;
    m_bAutoKey = l_bOn;
    emit f_AutoKeyChanged(l_bOn);
}

// ---- editing ------------------------------------------------------------------------------------

bool AnimDocument::f_Edit(const QString& l_oText, const std::function<bool(AnimState&)>& l_fnChange, const QString& l_oMergeKey)
{
    emit f_AboutToEdit();
    AnimState l_oAfter = m_oState;
    if (!l_fnChange(l_oAfter)) return false;
    f_FixSelection(l_oAfter);
    l_oAfter.m_oFile.m_vAtlases = f_StoredAtlases(l_oAfter, m_oFilePath);
    if (f_SameState(l_oAfter, m_oState)) return false;
    if (l_oAfter.m_oAtlasesAbs == m_oState.m_oAtlasesAbs && l_oAfter.m_oAtlasIds == m_oState.m_oAtlasIds && l_oAfter.m_oFile == m_oState.m_oFile) {
        // Only the current clip / node moved: not an edit.
        if (l_oAfter.m_iClip != m_oState.m_iClip) f_SetClipIndex(l_oAfter.m_iClip);
        f_SelectNode(l_oAfter.m_vNode);
        return false;
    }
    m_pUndo->push(new AnimCommand(this, l_oText, m_oState, l_oAfter, l_oMergeKey));
    return true;
}

bool AnimDocument::f_EditClip(const QString& l_oText, const std::function<void(Clip&)>& l_fnChange, const QString& l_oMergeKey)
{
    if (!f_Clip()) return false;
    return f_Edit(l_oText, [&](AnimState& l_oS) {
        l_fnChange(l_oS.m_oFile.m_vClips[size_t(l_oS.m_iClip)]);
        return true;
    }, l_oMergeKey);
}

bool AnimDocument::f_EditNodeIf(const NodePath& l_vPath, const QString& l_oText, const std::function<bool(Node&)>& l_fnChange,
                              const QString& l_oMergeKey)
{
    if (!f_Clip()) return false;
    return f_Edit(l_oText, [&](AnimState& l_oS) {
        Node* l_pN = f_NodeAt(l_oS.m_oFile.m_vClips[size_t(l_oS.m_iClip)].m_oRoot, l_vPath);
        return l_pN && l_fnChange(*l_pN);
    }, l_oMergeKey);
}

bool AnimDocument::f_EditNode(const NodePath& l_vPath, const QString& l_oText, const std::function<void(Node&)>& l_fnChange,
                            const QString& l_oMergeKey)
{
    return f_EditNodeIf(l_vPath, l_oText, [&](Node& l_oN) {
        l_fnChange(l_oN);
        return true;
    }, l_oMergeKey);
}

bool AnimDocument::f_EditSelected(const QString& l_oText, const std::function<void(Node&)>& l_fnChange, const QString& l_oMergeKey)
{
    return f_EditNode(m_oState.m_vNode, l_oText, l_fnChange, l_oMergeKey);
}

bool AnimDocument::f_SetChannel(Channel l_eC, const Value& l_oV, const QString& l_oMergeKey)
{
    const Node* l_pSel = f_SelectedNode();
    if (!l_pSel) return false;
    emit f_AboutToEdit();   // playback stops before the key's time is taken
    const float l_fT = m_fTime;
    const bool l_bKeyed = m_bAutoKey || l_eC == Channel::Event || l_eC == Channel::Deform || l_eC == Channel::Trail ||
                       f_HasKeyAt(*l_pSel, l_eC, l_fT);   // no rest deform or trail strength
    const bool l_bOk = f_EditSelected(l_bKeyed ? tr("Key %1").arg(QLatin1String(f_ChannelName(l_eC))) : tr("Set %1").arg(QLatin1String(f_ChannelName(l_eC))),
                                 [&](Node& l_oN) {
                                     if (l_bKeyed) f_SetKey(l_oN, l_eC, l_fT, l_oV);
                                     else f_SetRest(l_oN, l_eC, l_oV);
                                 }, l_oMergeKey);
    if (l_bOk && !l_bKeyed && f_KeyCount(*l_pSel, l_eC) > 0)
        emit f_Message(tr("Auto-key is off: the rest %1 changed, but the channel is animated, so nothing moves here.")
                         .arg(QLatin1String(f_ChannelName(l_eC))));
    return l_bOk;
}

bool AnimDocument::f_AddKeyHere(Channel l_eC)
{
    const Node* l_pSel = f_SelectedNode();
    if (!l_pSel || f_HasKeyAt(*l_pSel, l_eC, m_fTime)) return false;
    Value l_oV = f_ValueAt(*l_pSel, l_eC, m_fTime);
    if (l_eC == Channel::Event) l_oV.m_strS = "event";
    const float l_fT = m_fTime;
    return f_EditSelected(tr("Add %1 key").arg(QLatin1String(f_ChannelName(l_eC))), [&](Node& l_oN) { f_SetKey(l_oN, l_eC, l_fT, l_oV); });
}

bool AnimDocument::f_RemoveKeyHere(Channel l_eC)
{
    const float l_fT = m_fTime;
    return f_EditNodeIf(m_oState.m_vNode, tr("Remove %1 key").arg(QLatin1String(f_ChannelName(l_eC))), [&](Node& l_oN) { return f_RemoveKey(l_oN, l_eC, l_fT); });
}

bool AnimDocument::f_SetEaseHere(Channel l_eC, const Ease& l_oE)
{
    const Node* l_pSel = f_SelectedNode();
    if (!l_pSel) return false;
    Ease l_oCur;
    float l_fKt = 0;
    if (!f_EaseAtOrBefore(*l_pSel, l_eC, m_fTime, l_oCur, &l_fKt)) {
        const std::vector<float> l_vTimes = f_KeyTimes(*l_pSel, l_eC);
        if (l_vTimes.empty()) return false;
        l_fKt = l_vTimes.front();   // before the first key: that key's segment is the next one to play
    }
    return f_EditNodeIf(m_oState.m_vNode, tr("Set ease"), [&](Node& l_oN) { return f_SetEase(l_oN, l_eC, l_fKt, l_oE); });
}

bool AnimDocument::f_AssignSprite(const QString& l_oRef)
{
    Value l_oV;
    l_oV.m_strS = f_U8(l_oRef);
    return f_SetChannel(Channel::Sprite, l_oV);
}

// ---- key list -----------------------------------------------------------------------------------

bool AnimDocument::f_InsertKeys(float l_fT, int l_iChannel)
{
    return f_EditNodeIf(m_oState.m_vNode, tr("Insert key"), [&](Node& l_oN) {
        bool l_bAny = false;
        for (Channel l_eC : f_ChannelsFor(l_oN, l_iChannel)) {
            if (f_HasKeyAt(l_oN, l_eC, l_fT)) continue;
            Value l_oV = f_ValueAt(l_oN, l_eC, l_fT);
            if (l_eC == Channel::Event) l_oV.m_strS = "event";
            f_SetKey(l_oN, l_eC, l_fT, l_oV);
            l_bAny = true;
        }
        return l_bAny;
    });
}

bool AnimDocument::f_DeleteKeys(const std::vector<float>& l_vTimes, int l_iChannel)
{
    return f_EditNodeIf(m_oState.m_vNode, tr("Delete keys"), [&](Node& l_oN) {
        bool l_bAny = false;
        for (float l_fT : l_vTimes) {
            if (l_iChannel >= 0) l_bAny |= f_RemoveKey(l_oN, Channel(l_iChannel), l_fT);
            else
                for (Channel l_eC : g_arrAllChannels) l_bAny |= f_RemoveKey(l_oN, l_eC, l_fT);
        }
        return l_bAny;
    });
}

bool AnimDocument::f_SetCell(float l_fT, Channel l_eC, const Value& l_oV, bool l_bTogether)
{
    return f_EditNodeIf(m_oState.m_vNode, tr("Edit %1 key").arg(QLatin1String(f_ChannelName(l_eC))), [&](Node& l_oN) {
        if (l_bTogether && f_IsInterpolated(l_eC))   // keeps the row whole, on the channels it animates already
            for (Channel l_eO : f_AnimatedChannels(l_oN))
                if (l_eO != l_eC && !f_HasKeyAt(l_oN, l_eO, l_fT)) f_SetKey(l_oN, l_eO, l_fT, f_ValueAt(l_oN, l_eO, l_fT));
        f_SetKey(l_oN, l_eC, l_fT, l_oV);
        return true;
    });
}

bool AnimDocument::f_RemoveCell(float l_fT, Channel l_eC)
{
    return f_EditNodeIf(m_oState.m_vNode, tr("Remove %1 key").arg(QLatin1String(f_ChannelName(l_eC))), [&](Node& l_oN) { return f_RemoveKey(l_oN, l_eC, l_fT); });
}

bool AnimDocument::f_MoveRows(const std::vector<float>& l_vFrom, const std::vector<float>& l_vTo, QString* l_pConflict, const QString& l_oText)
{
    std::string l_strWhy;
    const bool l_bOk = f_EditNodeIf(m_oState.m_vNode, l_oText.isEmpty() ? tr("Move keys") : l_oText,
                               [&](Node& l_oN) { return f_MoveKeyTimes(l_oN, l_vFrom, l_vTo, &l_strWhy); });
    if (!l_bOk && l_pConflict) *l_pConflict = f_Qs(l_strWhy);
    return l_bOk;
}

bool AnimDocument::f_MoveChannelKey(Channel l_eC, float l_fFrom, float l_fTo, QString* l_pConflict)
{
    const Node* l_pSel = f_SelectedNode();
    if (!l_pSel) return false;
    if (l_fTo < 0) {
        if (l_pConflict) *l_pConflict = tr("time must not be negative");
        return false;
    }
    if (!f_SameTime(l_fFrom, l_fTo) && f_HasKeyAt(*l_pSel, l_eC, l_fTo)) {
        if (l_pConflict) *l_pConflict = tr("%1 already has a key at %2").arg(QLatin1String(f_ChannelName(l_eC))).arg(l_fTo);
        return false;
    }
    return f_EditNodeIf(m_oState.m_vNode, tr("Move %1 key").arg(QLatin1String(f_ChannelName(l_eC))), [&](Node& l_oN) {
        Value l_oV;
        if (!f_KeyAt(l_oN, l_eC, l_fFrom, l_oV)) return false;
        Ease l_oE;
        const bool l_bEased = f_EaseAt(l_oN, l_eC, l_fFrom, l_oE);
        f_RemoveKey(l_oN, l_eC, l_fFrom);
        f_SetKey(l_oN, l_eC, l_fTo, l_oV);
        if (l_bEased) f_SetEase(l_oN, l_eC, l_fTo, l_oE);
        return true;
    });
}

bool AnimDocument::f_Rescale(float l_fNewDuration, bool l_bWholeClip, QString* l_pError)
{
    const Clip* l_pC = f_Clip();
    const Node* l_pSel = f_SelectedNode();
    if (!l_pC || !l_pSel || !(l_fNewDuration > 0)) {
        if (l_pError) *l_pError = tr("the new duration must be above 0");
        return false;
    }
    float l_fCur = 0;
    if (l_bWholeClip) l_fCur = l_pC->f_Duration();
    else {
        const std::vector<float> l_vT = f_UnionKeyTimes(*l_pSel);
        l_fCur = l_vT.empty() ? 0 : l_vT.back();
    }
    if (l_fCur <= 0) {
        if (l_pError) *l_pError = tr("there are no key times to scale");
        return false;
    }
    const float l_fFactor = l_fNewDuration / l_fCur;
    if (l_bWholeClip)
        return f_EditClip(tr("Rescale clip"), [&](Clip& l_oClip) {
            f_ScaleKeyTimes(l_oClip.m_oRoot, l_fFactor, true);
            if (l_oClip.m_fLength > 0) l_oClip.m_fLength *= l_fFactor;
        });
    return f_EditSelected(tr("Rescale keys"), [&](Node& l_oN) { f_ScaleKeyTimes(l_oN, l_fFactor, false); });
}

bool AnimDocument::f_Ramp(const std::vector<float>& l_vTimesIn, Channel l_eC, const Value& l_oFirst, const Value& l_oLast, bool l_bAlphaOnly)
{
    if (l_vTimesIn.size() < 2 || !f_IsInterpolated(l_eC)) return false;
    std::vector<float> l_vTimes = l_vTimesIn;
    std::sort(l_vTimes.begin(), l_vTimes.end());
    const float l_fT0 = l_vTimes.front(), l_fT1 = l_vTimes.back();
    return f_EditNodeIf(m_oState.m_vNode, l_bAlphaOnly ? tr("Ramp alpha") : tr("Ramp %1").arg(QLatin1String(f_ChannelName(l_eC))), [&](Node& l_oN) {
        // Sample before writing: the colour a row keeps must not depend on the keys just written.
        std::vector<Value> l_vCurrent;
        for (float l_fT : l_vTimes) l_vCurrent.push_back(f_ValueAt(l_oN, l_eC, l_fT));
        for (size_t l_nI = 0; l_nI < l_vTimes.size(); l_nI++) {
            const float l_fF = l_fT1 > l_fT0 ? (l_vTimes[l_nI] - l_fT0) / (l_fT1 - l_fT0) : float(l_nI) / float(l_vTimes.size() - 1);
            Value l_oV = l_vCurrent[l_nI];
            if (l_bAlphaOnly) l_oV.m_v4V.w = l_oFirst.m_v4V.x + (l_oLast.m_v4V.x - l_oFirst.m_v4V.x) * l_fF;
            else l_oV.m_v4V = l_oFirst.m_v4V + (l_oLast.m_v4V - l_oFirst.m_v4V) * l_fF;
            f_SetKey(l_oN, l_eC, l_vTimes[l_nI], l_oV);
        }
        return true;
    });
}

bool AnimDocument::f_Fade(const std::vector<float>& l_vTimes, bool l_bIn)
{
    Value l_oA, l_oB;
    l_oA.m_v4V.x = l_bIn ? 0.0f : 1.0f;
    l_oB.m_v4V.x = l_bIn ? 1.0f : 0.0f;
    return f_Ramp(l_vTimes, Channel::Color, l_oA, l_oB, true);
}

bool AnimDocument::f_SpriteSequence(const std::vector<float>& l_vTimesIn, const QStringList& l_oSprites)
{
    if (l_vTimesIn.empty() || l_oSprites.isEmpty()) return false;
    std::vector<float> l_vTimes = l_vTimesIn;
    std::sort(l_vTimes.begin(), l_vTimes.end());
    return f_EditNodeIf(m_oState.m_vNode, tr("Sprite sequence"), [&](Node& l_oN) {
        for (size_t l_nI = 0; l_nI < l_vTimes.size(); l_nI++) {
            Value l_oV;
            l_oV.m_strS = f_U8(l_oSprites[int(l_nI % size_t(l_oSprites.size()))]);
            f_SetKey(l_oN, Channel::Sprite, l_vTimes[l_nI], l_oV);
        }
        return true;
    });
}

bool AnimDocument::f_InsertSpriteSequence(float l_fStart, float l_fGap, const QStringList& l_oSprites, bool l_bExtendClip)
{
    if (l_oSprites.isEmpty() || l_fGap <= 0.0f || !f_Clip() || !f_SelectedNode()) return false;
    l_fStart = std::max(0.0f, l_fStart);
    const float l_fLast = l_fStart + l_fGap * float(l_oSprites.size() - 1);
    return f_Edit(tr("Insert %n sprite key(s)", nullptr, l_oSprites.size()), [&](AnimState& l_oS) {
        Clip& l_oC = l_oS.m_oFile.m_vClips[size_t(l_oS.m_iClip)];
        Node* l_pN = f_NodeAt(l_oC.m_oRoot, l_oS.m_vNode);
        if (!l_pN) return false;
        for (int l_iI = 0; l_iI < l_oSprites.size(); l_iI++) {
            Value l_oV;
            l_oV.m_strS = f_U8(l_oSprites[l_iI]);
            f_SetKey(*l_pN, Channel::Sprite, l_fStart + l_fGap * float(l_iI), l_oV);
        }
        if (l_bExtendClip && l_oC.m_fLength > 0 && l_oC.m_fLength < l_fLast) l_oC.m_fLength = l_fLast;
        return true;
    });
}

bool AnimDocument::f_SetEaseRows(const std::vector<float>& l_vTimes, const Ease& l_oE)
{
    return f_EditNodeIf(m_oState.m_vNode, tr("Set ease"), [&](Node& l_oN) {
        bool l_bAny = false;
        for (float l_fT : l_vTimes)
            for (Channel l_eC : g_arrInterpolated) l_bAny |= f_SetEase(l_oN, l_eC, l_fT, l_oE);
        return l_bAny;
    });
}

// ---- structure ----------------------------------------------------------------------------------

bool AnimDocument::f_AddNode(const NodePath& l_vParent, Node l_oNode, const QString& l_oText)
{
    return f_Edit(l_oText, [&](AnimState& l_oS) {
        if (l_oS.m_oFile.m_vClips.empty()) return false;
        Node& l_oRoot = l_oS.m_oFile.m_vClips[size_t(l_oS.m_iClip)].m_oRoot;
        Node* l_pP = f_NodeAt(l_oRoot, l_vParent);
        if (!l_pP) return false;
        l_oNode.m_strName = f_UniqueNodeName(l_oRoot, l_oNode.m_strName);
        l_pP->m_vChildren.push_back(std::move(l_oNode));
        l_oS.m_vNode = l_vParent;
        l_oS.m_vNode.push_back(int(l_pP->m_vChildren.size()) - 1);
        return true;
    });
}

bool AnimDocument::f_AddSpriteNode(const NodePath& l_vParent, const QString& l_oSprite, const glm::vec2& l_v2Pos)
{
    Node l_oN;
    l_oN.m_strName = f_U8(f_Qs(toms::anim::f_ParseSpriteRef(f_U8(l_oSprite)).m_strName).section(QLatin1Char('/'), -1));
    l_oN.m_strSprite = f_U8(l_oSprite);
    l_oN.m_v3Pos = glm::vec3(l_v2Pos, 0.0f);
    return f_AddNode(l_vParent, l_oN, tr("Add sprite node"));
}

bool AnimDocument::f_DeleteNode(const NodePath& l_vPath)
{
    if (l_vPath.empty()) {
        emit f_Message(tr("The root node cannot be deleted (it is the clip)."));
        return false;
    }
    return f_Edit(tr("Delete node"), [&](AnimState& l_oS) {
        if (l_oS.m_oFile.m_vClips.empty()) return false;
        NodePath l_vParent(l_vPath.begin(), l_vPath.end() - 1);
        Node* l_pP = f_NodeAt(l_oS.m_oFile.m_vClips[size_t(l_oS.m_iClip)].m_oRoot, l_vParent);
        if (!l_pP || l_vPath.back() >= int(l_pP->m_vChildren.size())) return false;
        l_pP->m_vChildren.erase(l_pP->m_vChildren.begin() + l_vPath.back());
        l_oS.m_vNode = l_vParent;
        return true;
    });
}

bool AnimDocument::f_DuplicateNode(const NodePath& l_vPath)
{
    if (l_vPath.empty()) {
        emit f_Message(tr("The root node cannot be duplicated; duplicate the clip instead."));
        return false;
    }
    return f_Edit(tr("Duplicate node"), [&](AnimState& l_oS) {
        if (l_oS.m_oFile.m_vClips.empty()) return false;
        Node& l_oRoot = l_oS.m_oFile.m_vClips[size_t(l_oS.m_iClip)].m_oRoot;
        NodePath l_vParent(l_vPath.begin(), l_vPath.end() - 1);
        Node* l_pP = f_NodeAt(l_oRoot, l_vParent);
        if (!l_pP || l_vPath.back() >= int(l_pP->m_vChildren.size())) return false;
        Node l_oCopy = l_pP->m_vChildren[size_t(l_vPath.back())];
        animed::f_RenameForPaste(l_oRoot, l_oCopy);
        l_pP->m_vChildren.insert(l_pP->m_vChildren.begin() + l_vPath.back() + 1, std::move(l_oCopy));
        l_oS.m_vNode = l_vParent;
        l_oS.m_vNode.push_back(l_vPath.back() + 1);
        return true;
    });
}

bool AnimDocument::f_ParentBonesUnder(const NodePath& l_vPath)
{
    QString l_oWhy;
    const bool l_bDone = f_Edit(tr("Move bones under the sprite"), [&](AnimState& l_oS) {
        if (l_oS.m_oFile.m_vClips.empty()) return false;
        toms::anim::Clip& l_oClip = l_oS.m_oFile.m_vClips[size_t(l_oS.m_iClip)];
        NodePath l_vCur = l_vPath;   // the skinned node's path, as bones before it move out of its parent
        Node* l_pNode = f_NodeAt(l_oClip.m_oRoot, l_vCur);
        if (!l_pNode || !l_pNode->f_Skinned()) return false;
        bool l_bMoved = false;
        // One bone subtree at a time (paths change as nodes move); a bone under another outside bone moves with it.
        for (int l_iGuard = 0; l_iGuard < 64; l_iGuard++) {
            l_pNode = f_NodeAt(l_oClip.m_oRoot, l_vCur);
            const std::vector<std::string> l_vOutside = animed::f_BonesOutside(l_oClip.m_oRoot, *l_pNode);
            if (l_vOutside.empty()) break;
            // The outermost of them: one whose parent is not itself an outside bone.
            NodePath l_vBonePath;
            for (const std::string& l_strB : l_vOutside) {
                NodePath l_vP;
                std::function<bool(const Node&, NodePath&)> l_fnFind = [&](const Node& l_oN, NodePath& l_vAt) {
                    if (l_oN.m_strName == l_strB) return true;
                    for (size_t l_nI = 0; l_nI < l_oN.m_vChildren.size(); l_nI++) {
                        l_vAt.push_back(int(l_nI));
                        if (l_fnFind(l_oN.m_vChildren[l_nI], l_vAt)) return true;
                        l_vAt.pop_back();
                    }
                    return false;
                };
                if (!l_fnFind(l_oClip.m_oRoot, l_vP)) continue;
                const Node* l_pParent = l_vP.empty() ? nullptr : f_NodeAt(l_oClip.m_oRoot, NodePath(l_vP.begin(), l_vP.end() - 1));
                if (l_pParent && std::find(l_vOutside.begin(), l_vOutside.end(), l_pParent->m_strName) != l_vOutside.end()) continue;
                l_vBonePath = l_vP;
                break;
            }
            if (l_vBonePath.empty()) { l_oWhy = tr("the bones hold the sprite node: move the sprite out of them first"); return false; }
            if (l_vBonePath.size() <= l_vCur.size() && std::equal(l_vBonePath.begin(), l_vBonePath.end(), l_vCur.begin())) {
                l_oWhy = tr("bone '%1' holds the sprite node: move the sprite out of it first").arg(f_Qs(f_NodeAt(l_oClip.m_oRoot, l_vBonePath)->m_strName));
                return false;
            }
            // The offset between the bone's old parent and the node, at rest: it must be a move only.
            std::vector<std::pair<const Node*, glm::mat4>> l_vRest;
            toms::anim::f_RestWorlds(l_oClip, l_vRest);
            auto l_fnRestOf = [&](const Node* l_pN) {
                for (const auto& l_pairR : l_vRest) if (l_pairR.first == l_pN) return l_pairR.second;
                return glm::mat4(1.0f);
            };
            const NodePath l_vParentPath(l_vBonePath.begin(), l_vBonePath.end() - 1);
            const glm::mat4 l_mat4T = glm::inverse(l_fnRestOf(f_NodeAt(l_oClip.m_oRoot, l_vCur))) * l_fnRestOf(f_NodeAt(l_oClip.m_oRoot, l_vParentPath));
            for (int l_iC = 0; l_iC < 3; l_iC++)
                for (int l_iR = 0; l_iR < 3; l_iR++)
                    if (std::fabs(l_mat4T[l_iC][l_iR] - (l_iC == l_iR ? 1.0f : 0.0f)) > 1e-4f) {
                        l_oWhy = tr("the sprite node is rotated or scaled against the parent of bone '%1': move it by hand")
                                  .arg(f_Qs(f_NodeAt(l_oClip.m_oRoot, l_vBonePath)->m_strName));
                        return false;
                    }
            const glm::vec3 l_v3Shift(l_mat4T[3]);
            Node* l_pOldParent = f_NodeAt(l_oClip.m_oRoot, l_vParentPath);
            Node l_oBone = std::move(l_pOldParent->m_vChildren[size_t(l_vBonePath.back())]);
            l_pOldParent->m_vChildren.erase(l_pOldParent->m_vChildren.begin() + l_vBonePath.back());
            l_oBone.m_v3Pos += l_v3Shift;
            for (auto& l_oK : l_oBone.m_vPosKeys) l_oK.m_oV += l_v3Shift;
            // The node's path may have moved up if the bone came before it under the same parent.
            if (l_vCur.size() >= l_vBonePath.size() && std::equal(l_vParentPath.begin(), l_vParentPath.end(), l_vCur.begin()) &&
                l_vCur[l_vParentPath.size()] > l_vBonePath.back())
                l_vCur[l_vParentPath.size()]--;
            f_NodeAt(l_oClip.m_oRoot, l_vCur)->m_vChildren.push_back(std::move(l_oBone));
            l_bMoved = true;
        }
        l_oS.m_vNode = l_vCur;   // the skinned node stays selected
        return l_bMoved;
    });
    if (!l_oWhy.isEmpty()) emit f_Message(tr("Move bones under the sprite: %1").arg(l_oWhy));
    return l_bDone;
}

bool AnimDocument::f_SetLatticeSize(int l_iCols, int l_iRows)
{
    l_iCols = std::clamp(l_iCols, 2, toms::anim::g_iMaxLattice);
    l_iRows = std::clamp(l_iRows, 2, toms::anim::g_iMaxLattice);
    const Node* l_pSel = f_SelectedNode();
    if (!l_pSel || (l_pSel->f_LatticeCols() == l_iCols && l_pSel->f_LatticeRows() == l_iRows && l_pSel->m_arrLattice[0] >= 2)) return false;
    return f_EditSelected(tr("Lattice"), [l_iCols, l_iRows](Node& l_oN) {
        const int l_iOc = l_oN.f_LatticeCols(), l_iOrows = l_oN.f_LatticeRows();
        for (auto& l_oK : l_oN.m_vDeformKeys) {
            toms::anim::Deform l_mapMoved;
            for (const auto& [l_iI, l_v2Off] : l_oK.m_oV) {
                const int l_iC = l_iI % l_iOc, l_iR = l_iI / l_iOc;
                const int l_iNc = (int)std::lround(float(l_iC) / (l_iOc - 1) * (l_iCols - 1)), l_iNr = (int)std::lround(float(l_iR) / (l_iOrows - 1) * (l_iRows - 1));
                l_mapMoved[l_iNr * l_iCols + l_iNc] = l_v2Off;
            }
            l_oK.m_oV = std::move(l_mapMoved);
        }
        l_oN.m_arrLattice[0] = l_iCols;
        l_oN.m_arrLattice[1] = l_iRows;
    });
}

void AnimDocument::f_UpdateMeshes()
{
    // Each atlas: its mesh file as on disk, with every mesh this file's clips need added -- cut from its pixels (cached
    // by key), or the file's own when it is marked "hand".
    for (const auto& l_spA : m_vAtlases) {
        l_spA->m_oMeshes = l_spA->m_oMeshesOnDisk;
        l_spA->m_setNeededKeys.clear();
    }
    for (const toms::anim::MeshNeed& l_oNeed : toms::anim::f_MeshesNeeded(m_oState.m_oFile, m_oAtlasSet)) {
        const int l_iDoc = l_oNeed.m_iEntry >= 0 && l_oNeed.m_iEntry < int(m_vSetToDoc.size()) ? m_vSetToDoc[size_t(l_oNeed.m_iEntry)] : -1;
        if (l_iDoc < 0) continue;
        LoadedAtlas& l_oA = *m_vAtlases[size_t(l_iDoc)];
        l_oA.m_setNeededKeys.insert(l_oNeed.m_strKey);
        if (const toms::SpriteMesh* l_pHand = l_oA.m_oMeshesOnDisk.f_Find(l_oNeed.m_strKey); l_pHand && l_pHand->m_strSource == "hand") continue;
        auto l_itCached = l_oA.m_mapCut.find(l_oNeed.m_strKey);
        if (l_itCached == l_oA.m_mapCut.end()) {
            toms::SpriteMesh l_oM;
            const toms::AtlasRegion* l_pR = l_oNeed.m_pRegion;
            if (l_pR->m_iPage >= 0 && l_pR->m_iPage < int(l_oA.m_vRgba.size())) {
                const QImage& l_oPage = l_oA.m_vRgba[size_t(l_pR->m_iPage)];
                if (l_pR->m_iX >= 0 && l_pR->m_iY >= 0 && l_pR->m_iX + l_pR->m_iW <= l_oPage.width() && l_pR->m_iY + l_pR->m_iH <= l_oPage.height())
                    l_oM = toms::anim::f_CutMesh(l_oNeed, l_oPage.constBits() + size_t(l_pR->m_iY) * l_oPage.bytesPerLine() + size_t(l_pR->m_iX) * 4,
                                            l_oPage.bytesPerLine() / 4);
            }
            l_itCached = l_oA.m_mapCut.emplace(l_oNeed.m_strKey, std::move(l_oM)).first;
        }
        if (!l_itCached->second.m_vTriangles.empty()) l_oA.m_oMeshes.m_mapMeshes[l_oNeed.m_strKey] = l_itCached->second;
    }
    for (size_t l_nE = 0; l_nE < m_oAtlasSet.m_vEntries.size() && l_nE < m_vSetToDoc.size(); l_nE++) {
        const LoadedAtlas& l_oA = *m_vAtlases[size_t(m_vSetToDoc[l_nE])];
        m_oAtlasSet.m_vEntries[l_nE].m_pMeshes = l_oA.m_oMeshes.m_mapMeshes.empty() ? nullptr : &l_oA.m_oMeshes;
    }
}

bool AnimDocument::f_SaveMeshes(QString* l_pError)
{
    // Each atlas's mesh file gets this file's meshes; the others in it (other clips') stay. It is read again first, so
    // what another clip saved meanwhile is kept.
    for (const auto& l_spAp : m_vAtlases) {
        LoadedAtlas& l_oA = *l_spAp;
        if (!l_oA.m_bOk || l_oA.m_setNeededKeys.empty()) continue;
        const std::string l_strMeshPath = toms::f_SpriteMeshPath(f_U8(l_oA.m_oPath));
        toms::SpriteMeshFile l_oNow;
        std::string l_strErr;
        if (!animed::f_ReadMeshFile(l_strMeshPath, l_oNow, nullptr, &l_strErr)) l_oNow = toms::SpriteMeshFile();   // broken: made again
        toms::SpriteMeshFile l_oMerged = l_oNow;
        for (const std::string& l_strKey : l_oA.m_setNeededKeys)
            if (const toms::SpriteMesh* l_pM = l_oA.m_oMeshes.f_Find(l_strKey)) l_oMerged.m_mapMeshes[l_strKey] = *l_pM;
        if (l_oMerged.m_mapMeshes == l_oNow.m_mapMeshes && l_strErr.empty()) {
            l_oA.m_oMeshesOnDisk = std::move(l_oNow);
            continue;
        }
        if (!animed::f_WriteMeshFile(l_strMeshPath, l_oMerged, l_oA.m_bMeshesBinary, &l_strErr)) {
            if (l_pError) *l_pError = tr("The .anim was saved, but the mesh file %1 was not: %2").arg(QDir::toNativeSeparators(f_Qs(l_strMeshPath)), f_Qs(l_strErr));
            return false;
        }
        l_oA.m_oMeshesOnDisk = std::move(l_oMerged);
    }
    f_UpdateMeshes();
    return true;
}

bool AnimDocument::f_ApplyMotionRecipe(const std::string& l_strId, float l_fStart, float l_fPeriod, float l_fStrength)
{
    const Node* l_pSel = f_SelectedNode();
    const animed::MotionRecipe* l_pRecipe = animed::f_FindMotionRecipe(l_strId);
    if (!l_pSel || !l_pRecipe) return false;
    animed::RecipeParams l_oP;
    l_oP.m_fStart = l_fStart;
    l_oP.m_fPeriod = l_fPeriod;
    l_oP.m_fStrength = l_fStrength;
    if (const toms::AtlasRegion* l_pR = f_FindSprite(toms::anim::f_SpriteAt(*l_pSel, l_fStart))) {
        l_oP.m_fSpriteW = float(l_pR->m_iOrigW);
        l_oP.m_fSpriteH = float(l_pR->m_iOrigH);
    }
    const NodePath l_vPath = m_oState.m_vNode;
    std::string l_strErr;
    const bool l_bOk = f_Edit(tr("Recipe: %1").arg(f_Qs(l_pRecipe->m_strTitle)), [&](AnimState& l_oS) {
        if (l_oS.m_oFile.m_vClips.empty()) return false;
        return animed::f_ApplyMotionRecipe(l_oS.m_oFile.m_vClips[size_t(l_oS.m_iClip)], l_vPath, l_strId, l_oP, &l_strErr);
    });
    if (!l_bOk && !l_strErr.empty()) emit f_Message(tr("Motion recipe: %1").arg(f_Qs(l_strErr)));
    return l_bOk;
}

bool AnimDocument::f_MeshGridOf(const Node& l_oN, int& l_iCols, int& l_iRows) const
{
    const toms::AtlasRegion* l_pR = f_FindSprite(toms::anim::f_SpriteAt(l_oN, m_fTime));
    if (!l_pR) return false;
    if (l_oN.f_HasMeshGrid()) {
        l_iCols = l_oN.m_arrMeshGrid[0];
        l_iRows = l_oN.m_arrMeshGrid[1];
    } else {   // 8-pixel cells
        l_iCols = std::max(1, (l_pR->m_iW + toms::anim::g_iMeshCell - 1) / toms::anim::g_iMeshCell);
        l_iRows = std::max(1, (l_pR->m_iH + toms::anim::g_iMeshCell - 1) / toms::anim::g_iMeshCell);
    }
    return true;
}

bool AnimDocument::f_SetMeshGrid(int l_iCols, int l_iRows)
{
    l_iCols = std::clamp(l_iCols, 0, toms::anim::g_iMaxMeshGrid);
    l_iRows = std::clamp(l_iRows, 0, toms::anim::g_iMaxMeshGrid);
    if (l_iCols == 0 || l_iRows == 0) l_iCols = l_iRows = 0;
    return f_EditSelected(tr("Mesh grid"), [&](Node& l_oN) {
        l_oN.m_arrMeshGrid[0] = l_iCols;
        l_oN.m_arrMeshGrid[1] = l_iRows;
    });
}

QString AnimDocument::f_CopyNodeText(const NodePath& l_vPath) const
{
    const toms::anim::Clip* l_pC = f_Clip();
    const Node* l_pN = l_pC ? animed::f_NodeAt(l_pC->m_oRoot, l_vPath) : nullptr;
    return l_pN ? f_Qs(animed::f_NodeClipboardText(*l_pN)) : QString();
}

bool AnimDocument::f_PasteNode(const NodePath& l_vParent, int l_iIndex, const QString& l_oText)
{
    Node l_oNode;
    std::string l_strErr;
    if (!animed::f_NodeFromClipboardText(f_U8(l_oText), l_oNode, &l_strErr)) {
        emit f_Message(tr("Paste: %1").arg(f_Qs(l_strErr)));
        return false;
    }
    return f_Edit(tr("Paste node"), [&](AnimState& l_oS) {
        if (l_oS.m_oFile.m_vClips.empty()) return false;
        Node& l_oRoot = l_oS.m_oFile.m_vClips[size_t(l_oS.m_iClip)].m_oRoot;
        Node* l_pP = f_NodeAt(l_oRoot, l_vParent);
        if (!l_pP) return false;
        animed::f_RenameForPaste(l_oRoot, l_oNode);
        const int l_iAt = l_iIndex < 0 || l_iIndex > int(l_pP->m_vChildren.size()) ? int(l_pP->m_vChildren.size()) : l_iIndex;
        l_pP->m_vChildren.insert(l_pP->m_vChildren.begin() + l_iAt, std::move(l_oNode));
        l_oS.m_vNode = l_vParent;
        l_oS.m_vNode.push_back(l_iAt);
        return true;
    });
}

bool AnimDocument::f_RenameNode(const NodePath& l_vPath, const QString& l_oName)
{
    const QString l_oN = l_oName.trimmed();
    if (l_oN.isEmpty()) return false;
    return f_EditNode(l_vPath, tr("Rename node"), [&](Node& l_oNode) { l_oNode.m_strName = f_U8(l_oN); });
}

bool AnimDocument::f_MoveNode(const NodePath& l_vFrom, const NodePath& l_vToParent, int l_iIndex)
{
    if (l_vFrom.empty()) return false;
    if (l_vToParent.size() >= l_vFrom.size() && std::equal(l_vFrom.begin(), l_vFrom.end(), l_vToParent.begin())) {
        emit f_Message(tr("A node cannot be moved into itself."));
        return false;
    }
    return f_Edit(tr("Move node"), [&](AnimState& l_oS) {
        if (l_oS.m_oFile.m_vClips.empty()) return false;
        Node& l_oRoot = l_oS.m_oFile.m_vClips[size_t(l_oS.m_iClip)].m_oRoot;
        const NodePath l_vFromParent(l_vFrom.begin(), l_vFrom.end() - 1);
        Node* l_pFp = f_NodeAt(l_oRoot, l_vFromParent);
        if (!l_pFp || l_vFrom.back() >= int(l_pFp->m_vChildren.size()) || !f_NodeAt(l_oRoot, l_vToParent)) return false;
        Node l_oMoving = std::move(l_pFp->m_vChildren[size_t(l_vFrom.back())]);
        l_pFp->m_vChildren.erase(l_pFp->m_vChildren.begin() + l_vFrom.back());
        NodePath l_vTp = l_vToParent;
        const size_t l_nD = l_vFromParent.size();
        if (l_vTp.size() > l_nD && std::equal(l_vFromParent.begin(), l_vFromParent.end(), l_vTp.begin()) && l_vTp[l_nD] > l_vFrom.back()) l_vTp[l_nD]--;
        if (l_vTp == l_vFromParent && l_iIndex > l_vFrom.back()) l_iIndex--;
        Node* l_pNp = f_NodeAt(l_oRoot, l_vTp);
        l_iIndex = std::clamp(l_iIndex, 0, int(l_pNp->m_vChildren.size()));
        l_pNp->m_vChildren.insert(l_pNp->m_vChildren.begin() + l_iIndex, std::move(l_oMoving));
        l_oS.m_vNode = l_vTp;
        l_oS.m_vNode.push_back(l_iIndex);
        return true;
    });
}

bool AnimDocument::f_AddClip(const QString& l_oName)
{
    return f_Edit(tr("Add clip"), [&](AnimState& l_oS) {
        Clip l_oC;
        l_oC.m_strName = f_U8(f_UniqueClipName(l_oS.m_oFile, l_oName));
        l_oC.m_oRoot.m_strName = "root";
        l_oS.m_oFile.m_vClips.push_back(l_oC);
        l_oS.m_iClip = int(l_oS.m_oFile.m_vClips.size()) - 1;
        l_oS.m_vNode.clear();
        return true;
    });
}

bool AnimDocument::f_DuplicateClip(int l_iIndex)
{
    return f_Edit(tr("Duplicate clip"), [&](AnimState& l_oS) {
        if (l_iIndex < 0 || l_iIndex >= int(l_oS.m_oFile.m_vClips.size())) return false;
        Clip l_oC = l_oS.m_oFile.m_vClips[size_t(l_iIndex)];
        l_oC.m_strName = f_U8(f_UniqueClipName(l_oS.m_oFile, f_Qs(l_oC.m_strName)));
        l_oS.m_oFile.m_vClips.insert(l_oS.m_oFile.m_vClips.begin() + l_iIndex + 1, l_oC);
        l_oS.m_iClip = l_iIndex + 1;
        l_oS.m_vNode.clear();
        return true;
    });
}

bool AnimDocument::f_RenameClip(int l_iIndex, const QString& l_oName)
{
    const QString l_oN = l_oName.trimmed();
    if (l_oN.isEmpty() || l_iIndex < 0 || l_iIndex >= int(m_oState.m_oFile.m_vClips.size())) return false;
    const Clip* l_pOther = m_oState.m_oFile.f_Find(f_U8(l_oN));
    if (l_pOther && l_pOther != &m_oState.m_oFile.m_vClips[size_t(l_iIndex)]) {
        emit f_Message(tr("There is already a clip called '%1'.").arg(l_oN));
        return false;
    }
    return f_Edit(tr("Rename clip"), [&](AnimState& l_oS) {
        l_oS.m_oFile.m_vClips[size_t(l_iIndex)].m_strName = f_U8(l_oN);
        return true;
    });
}

bool AnimDocument::f_DeleteClip(int l_iIndex)
{
    if (m_oState.m_oFile.m_vClips.size() <= 1) {
        emit f_Message(tr("A file keeps at least one clip."));
        return false;
    }
    return f_Edit(tr("Delete clip"), [&](AnimState& l_oS) {
        if (l_iIndex < 0 || l_iIndex >= int(l_oS.m_oFile.m_vClips.size())) return false;
        l_oS.m_oFile.m_vClips.erase(l_oS.m_oFile.m_vClips.begin() + l_iIndex);
        if (l_iIndex < l_oS.m_iClip) l_oS.m_iClip--;   // another clip: the current one stays current
        else if (l_iIndex == l_oS.m_iClip) {
            l_oS.m_iClip = std::min(l_iIndex, int(l_oS.m_oFile.m_vClips.size()) - 1);
            l_oS.m_vNode.clear();
        }
        return true;
    });
}

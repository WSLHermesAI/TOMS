#include "ParticleDocument.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUndoCommand>
#include <QUndoStack>

using toms::fx::Effect;
using toms::fx::Emitter;

namespace {

QString f_Qs(const std::string& l_strS) { return QString::fromStdString(l_strS); }
std::string f_U8(const QString& l_oS) { return l_oS.toStdString(); }

bool f_SameState(const ParticleState& l_oA, const ParticleState& l_oB)
{
    return l_oA.m_iEffect == l_oB.m_iEffect && l_oA.m_iEmitter == l_oB.m_iEmitter && l_oA.m_oAtlasesAbs == l_oB.m_oAtlasesAbs && l_oA.m_oAtlasIds == l_oB.m_oAtlasIds &&
           toms::fx::f_ParticlesToJson(l_oA.m_oFile) == toms::fx::f_ParticlesToJson(l_oB.m_oFile);
}

std::shared_ptr<ParticleDocument::LoadedAtlas> f_LoadOne(const QString& l_oPath)
{
    auto l_spA = std::make_shared<ParticleDocument::LoadedAtlas>();
    l_spA->m_oPath = l_oPath;
    QFile l_oIn(l_oPath);
    if (!l_oIn.open(QIODevice::ReadOnly)) {
        l_spA->m_oError = ParticleDocument::tr("cannot read %1: %2").arg(QDir::toNativeSeparators(l_oPath), l_oIn.errorString());
        return l_spA;
    }
    std::string l_strErr;
    if (!toms::f_ParseAtlas(l_oIn.readAll().toStdString(), l_spA->m_oAtlas, &l_strErr)) {
        l_spA->m_oError = QStringLiteral("%1: %2").arg(QDir::toNativeSeparators(l_oPath), f_Qs(l_strErr));
        return l_spA;
    }
    QVector<QImage> l_oPages;
    const QDir l_oDir = QFileInfo(l_oPath).absoluteDir();
    bool l_bSizes = false;
    for (toms::AtlasPage& l_oP : l_spA->m_oAtlas.m_vPages) {
        QImage l_oImg(l_oDir.filePath(f_Qs(l_oP.m_strFile)));
        if (l_oImg.isNull()) l_spA->m_oError = ParticleDocument::tr("cannot load the atlas page %1").arg(QDir::toNativeSeparators(l_oDir.filePath(f_Qs(l_oP.m_strFile))));
        if (!l_oImg.isNull() && (l_oP.m_iW <= 0 || l_oP.m_iH <= 0)) { l_oP.m_iW = l_oImg.width(); l_oP.m_iH = l_oImg.height(); l_bSizes = true; }
        l_oPages.push_back(l_oImg);
    }
    if (l_bSizes) l_spA->m_oAtlas.f_ComputeUVs();
    l_spA->m_oImages.f_SetPages(l_oPages);
    for (const toms::AtlasRegion& l_oR : l_spA->m_oAtlas.m_vRegions) l_spA->m_oSpriteNames << f_Qs(l_oR.m_strName);
    l_spA->m_oSpriteNames.removeDuplicates();
    l_spA->m_oSpriteNames.sort(Qt::CaseInsensitive);
    l_spA->m_bOk = l_spA->m_oError.isEmpty();
    return l_spA;
}

}  // namespace

// One undo step: the state before and after. Consecutive edits with the same merge key merge.
class ParticleCommand : public QUndoCommand
{
public:
    ParticleCommand(ParticleDocument* l_pDoc, const QString& l_oText, ParticleState l_oBefore, ParticleState l_oAfter, const QString& l_oMergeKey)
        : QUndoCommand(l_oText), m_pDoc(l_pDoc), m_oBefore(std::move(l_oBefore)), m_oAfter(std::move(l_oAfter)), m_oKey(l_oMergeKey)
    {
    }
    void undo() override { m_pDoc->f_SetState(m_oBefore); }
    void redo() override
    {
        if (m_bFirst) { m_bFirst = false; return; }   // the edit already applied it
        m_pDoc->f_SetState(m_oAfter);
    }
    int id() const override { return m_oKey.isEmpty() ? -1 : 1; }
    bool mergeWith(const QUndoCommand* l_pOther) override
    {
        const auto* l_pO = static_cast<const ParticleCommand*>(l_pOther);
        if (l_pO->m_oKey != m_oKey) return false;
        m_oAfter = l_pO->m_oAfter;
        return true;
    }

private:
    ParticleDocument* m_pDoc;
    ParticleState m_oBefore, m_oAfter;
    QString m_oKey;
    bool m_bFirst = true;
};

ParticleDocument::ParticleDocument(QObject* l_pParent)
    : QObject(l_pParent)
    , m_pUndo(new QUndoStack(this))
{
    connect(m_pUndo, &QUndoStack::cleanChanged, this, [this] { emit f_FileChanged(); });
}

QString ParticleDocument::f_DefaultFxAtlas()
{
#ifdef TOMS_FX_ATLAS
    const QString l_oP = QString::fromUtf8(TOMS_FX_ATLAS);
    if (QFileInfo::exists(l_oP)) return QFileInfo(l_oP).absoluteFilePath();
#endif
    // the release in Editor/bin (another PC: the build path above is not there): the repo's own assets
    const QString l_oRel = QCoreApplication::applicationDirPath() + QStringLiteral("/../../assets/media/atlas/fx.atlas");
    if (QFileInfo::exists(l_oRel)) return QFileInfo(l_oRel).absoluteFilePath();
    return QString();
}

QString ParticleDocument::f_DisplayName() const
{
    return m_oFilePath.isEmpty() ? tr("untitled.particle") : QFileInfo(m_oFilePath).fileName();
}

bool ParticleDocument::f_IsDirty() const { return !m_pUndo->isClean(); }

QString ParticleDocument::f_ToJson() const { return f_Qs(toms::fx::f_ParticlesToJson(m_oState.m_oFile)); }

void ParticleDocument::f_SyncStoredAtlases(ParticleState& l_oS) const
{
    l_oS.m_oFile.m_vAtlases.clear();
    const QDir l_oDir = m_oFilePath.isEmpty() ? QDir() : QFileInfo(m_oFilePath).absoluteDir();
    for (int l_iI = 0; l_iI < l_oS.m_oAtlasesAbs.size(); l_iI++) {
        toms::anim::AtlasRef l_oR;
        l_oR.m_strPath = f_U8(m_oFilePath.isEmpty() ? l_oS.m_oAtlasesAbs[l_iI] : l_oDir.relativeFilePath(l_oS.m_oAtlasesAbs[l_iI]));
        l_oR.m_strId = f_U8(l_oS.m_oAtlasIds.value(l_iI));
        l_oS.m_oFile.m_vAtlases.push_back(l_oR);
    }
}

void ParticleDocument::f_NewFile()
{
    ParticleState l_oS;
    const QString l_oFx = f_DefaultFxAtlas();
    if (!l_oFx.isEmpty()) {
        l_oS.m_oAtlasesAbs << l_oFx;
        l_oS.m_oAtlasIds << QStringLiteral("fx");
    }
    Effect l_oE;
    l_oE.m_strName = "effect";
    Emitter l_oM;
    l_oM.m_strName = "emitter";
    l_oM.m_strSprite = l_oFx.isEmpty() ? std::string() : "fx:dot";
    l_oM.m_oBlend = toms::fx::Blend::f_Add();
    l_oM.m_oLife = toms::fx::Range(0.6f, 1.0f);
    l_oM.m_oSpeed = toms::fx::Range(40, 80);
    l_oM.m_fSpread = 20;
    l_oM.m_oSize = toms::fx::Range(6, 10);
    toms::fx::CurveKey<glm::vec4> l_oA, l_oB;
    l_oA.m_fT = 0; l_oA.m_oV = glm::vec4(1);
    l_oB.m_fT = 1; l_oB.m_oV = glm::vec4(1, 1, 1, 0);
    l_oM.m_oColorOverLife.m_vKeys = {l_oA, l_oB};
    l_oE.m_vEmitters.push_back(l_oM);
    l_oS.m_oFile.m_vEffects.push_back(l_oE);
    l_oS.m_iEmitter = 0;
    m_oFilePath.clear();
    f_SyncStoredAtlases(l_oS);
    m_oState = l_oS;
    m_pUndo->clear();
    f_LoadAtlases();
    emit f_FileChanged();
    emit f_SelectionChanged();
}

bool ParticleDocument::f_Open(const QString& l_oPath, QString* l_pError)
{
    QFile l_oIn(l_oPath);
    if (!l_oIn.open(QIODevice::ReadOnly)) {
        if (l_pError) *l_pError = l_oIn.errorString();
        return false;
    }
    ParticleState l_oS;
    std::string l_strErr;
    if (!toms::fx::f_ParseParticles(l_oIn.readAll().toStdString(), l_oS.m_oFile, &l_strErr)) {
        if (l_pError) *l_pError = f_Qs(l_strErr);
        return false;
    }
    const QDir l_oDir = QFileInfo(l_oPath).absoluteDir();
    for (const toms::anim::AtlasRef& l_oR : l_oS.m_oFile.m_vAtlases) {
        l_oS.m_oAtlasesAbs << QDir::cleanPath(l_oDir.absoluteFilePath(f_Qs(l_oR.m_strPath)));
        l_oS.m_oAtlasIds << f_Qs(l_oR.m_strId);
    }
    l_oS.m_iEffect = 0;
    l_oS.m_iEmitter = l_oS.m_oFile.m_vEffects.empty() || l_oS.m_oFile.m_vEffects[0].m_vEmitters.empty() ? -1 : 0;
    m_oFilePath = QFileInfo(l_oPath).absoluteFilePath();
    m_oState = l_oS;
    m_pUndo->clear();
    f_LoadAtlases();
    emit f_FileChanged();
    emit f_SelectionChanged();
    return true;
}

bool ParticleDocument::f_WriteBackup(const QString& l_oPath, QString* l_pError) const
{
    toms::fx::ParticleFile l_oF = m_oState.m_oFile;
    l_oF.m_vAtlases.clear();
    for (int l_iI = 0; l_iI < m_oState.m_oAtlasesAbs.size(); l_iI++) {
        toms::anim::AtlasRef l_oR;
        l_oR.m_strPath = f_U8(QDir::fromNativeSeparators(m_oState.m_oAtlasesAbs[l_iI]));
        l_oR.m_strId = f_U8(m_oState.m_oAtlasIds.value(l_iI));
        l_oF.m_vAtlases.push_back(l_oR);
    }
    QSaveFile l_oOut(l_oPath);
    if (!l_oOut.open(QIODevice::WriteOnly) || l_oOut.write(toms::fx::f_ParticlesToJson(l_oF).c_str()) < 0 || !l_oOut.commit()) {
        if (l_pError) *l_pError = l_oOut.errorString();
        return false;
    }
    return true;
}

bool ParticleDocument::f_SaveTo(const QString& l_oPath, QString* l_pError)
{
    const QDir l_oDir = QFileInfo(l_oPath).absoluteDir();
    for (const QString& l_oA : m_oState.m_oAtlasesAbs)
        if (QDir::isAbsolutePath(l_oDir.relativeFilePath(l_oA))) {
            if (l_pError)
                *l_pError = tr("The atlas %1 cannot be stored relative to %2: they are on different drives.")
                             .arg(QDir::toNativeSeparators(l_oA), QDir::toNativeSeparators(l_oPath));
            return false;
        }
    const QString l_oOld = m_oFilePath;
    m_oFilePath = QFileInfo(l_oPath).absoluteFilePath();
    ParticleState l_oS = m_oState;
    f_SyncStoredAtlases(l_oS);
    QSaveFile l_oOut(l_oPath);
    if (!l_oOut.open(QIODevice::WriteOnly) || l_oOut.write(toms::fx::f_ParticlesToJson(l_oS.m_oFile).c_str()) < 0 || !l_oOut.commit()) {
        if (l_pError) *l_pError = l_oOut.errorString();
        m_oFilePath = l_oOld;
        return false;
    }
    m_oState = l_oS;
    m_pUndo->setClean();
    emit f_FileChanged();
    return true;
}

const Effect* ParticleDocument::f_Effect() const
{
    const auto& l_vEs = m_oState.m_oFile.m_vEffects;
    return m_oState.m_iEffect >= 0 && m_oState.m_iEffect < int(l_vEs.size()) ? &l_vEs[size_t(m_oState.m_iEffect)] : nullptr;
}

const Emitter* ParticleDocument::f_Emitter() const
{
    const Effect* l_pE = f_Effect();
    return l_pE && m_oState.m_iEmitter >= 0 && m_oState.m_iEmitter < int(l_pE->m_vEmitters.size()) ? &l_pE->m_vEmitters[size_t(m_oState.m_iEmitter)] : nullptr;
}

void ParticleDocument::f_Select(int l_iEffect, int l_iEmitter)
{
    const auto& l_vEs = m_oState.m_oFile.m_vEffects;
    l_iEffect = l_vEs.empty() ? 0 : std::clamp(l_iEffect, 0, int(l_vEs.size()) - 1);
    if (!l_vEs.empty()) l_iEmitter = std::clamp(l_iEmitter, -1, int(l_vEs[size_t(l_iEffect)].m_vEmitters.size()) - 1);
    else l_iEmitter = -1;
    if (l_iEffect == m_oState.m_iEffect && l_iEmitter == m_oState.m_iEmitter) return;
    m_oState.m_iEffect = l_iEffect;
    m_oState.m_iEmitter = l_iEmitter;
    emit f_SelectionChanged();
}

void ParticleDocument::f_SetState(const ParticleState& l_oS)
{
    const bool l_bAtlases = l_oS.m_oAtlasesAbs != m_oState.m_oAtlasesAbs || l_oS.m_oAtlasIds != m_oState.m_oAtlasIds;
    const bool l_bSel = l_oS.m_iEffect != m_oState.m_iEffect || l_oS.m_iEmitter != m_oState.m_iEmitter;
    m_oState = l_oS;
    if (l_bAtlases) f_LoadAtlases();
    emit f_FileChanged();
    if (l_bSel) emit f_SelectionChanged();
}

bool ParticleDocument::f_Edit(const QString& l_oText, const std::function<bool(ParticleState&)>& l_fnChange, const QString& l_oMergeKey)
{
    ParticleState l_oAfter = m_oState;
    if (!l_fnChange(l_oAfter)) return false;
    f_SyncStoredAtlases(l_oAfter);
    if (f_SameState(l_oAfter, m_oState)) return false;
    const ParticleState l_oBefore = m_oState;
    f_SetState(l_oAfter);
    m_pUndo->push(new ParticleCommand(this, l_oText, l_oBefore, l_oAfter, l_oMergeKey));
    return true;
}

bool ParticleDocument::f_EditEffect(const QString& l_oText, const std::function<void(Effect&)>& l_fnChange, const QString& l_oMergeKey)
{
    if (!f_Effect()) return false;
    return f_Edit(l_oText, [&](ParticleState& l_oS) {
        l_fnChange(l_oS.m_oFile.m_vEffects[size_t(l_oS.m_iEffect)]);
        return true;
    }, l_oMergeKey);
}

bool ParticleDocument::f_EditEmitter(const QString& l_oText, const std::function<void(Emitter&)>& l_fnChange, const QString& l_oMergeKey)
{
    if (!f_Emitter()) return false;
    return f_Edit(l_oText, [&](ParticleState& l_oS) {
        l_fnChange(l_oS.m_oFile.m_vEffects[size_t(l_oS.m_iEffect)].m_vEmitters[size_t(l_oS.m_iEmitter)]);
        return true;
    }, l_oMergeKey);
}

QString ParticleDocument::f_UniqueEffectName(const QString& l_oBase) const
{
    auto l_fnTaken = [&](const QString& l_oN) { return m_oState.m_oFile.f_Find(f_U8(l_oN)) != nullptr; };
    if (!l_fnTaken(l_oBase)) return l_oBase;
    for (int l_iI = 2;; l_iI++)
        if (!l_fnTaken(QStringLiteral("%1 %2").arg(l_oBase).arg(l_iI))) return QStringLiteral("%1 %2").arg(l_oBase).arg(l_iI);
}

QString ParticleDocument::f_UniqueEmitterName(const QString& l_oBase) const
{
    const Effect* l_pE = f_Effect();
    auto l_fnTaken = [&](const QString& l_oN) {
        if (!l_pE) return false;
        for (const Emitter& l_oM : l_pE->m_vEmitters)
            if (f_Qs(l_oM.m_strName) == l_oN) return true;
        return false;
    };
    if (!l_fnTaken(l_oBase)) return l_oBase;
    for (int l_iI = 2;; l_iI++)
        if (!l_fnTaken(QStringLiteral("%1 %2").arg(l_oBase).arg(l_iI))) return QStringLiteral("%1 %2").arg(l_oBase).arg(l_iI);
}

bool ParticleDocument::f_AddEffect(Effect l_oE)
{
    if (l_oE.m_strName.empty() || m_oState.m_oFile.f_Find(l_oE.m_strName)) l_oE.m_strName = f_U8(f_UniqueEffectName(l_oE.m_strName.empty() ? QStringLiteral("effect") : f_Qs(l_oE.m_strName)));
    return f_Edit(tr("Add effect"), [&](ParticleState& l_oS) {
        const int l_iAt = l_oS.m_oFile.m_vEffects.empty() ? 0 : l_oS.m_iEffect + 1;
        l_oS.m_oFile.m_vEffects.insert(l_oS.m_oFile.m_vEffects.begin() + l_iAt, l_oE);
        l_oS.m_iEffect = l_iAt;
        l_oS.m_iEmitter = l_oE.m_vEmitters.empty() ? -1 : 0;
        return true;
    });
}

bool ParticleDocument::f_RemoveEffect(int l_iIndex)
{
    if (l_iIndex < 0 || l_iIndex >= int(m_oState.m_oFile.m_vEffects.size())) return false;
    return f_Edit(tr("Delete effect"), [&](ParticleState& l_oS) {
        l_oS.m_oFile.m_vEffects.erase(l_oS.m_oFile.m_vEffects.begin() + l_iIndex);
        l_oS.m_iEffect = std::clamp(l_iIndex, 0, std::max(0, int(l_oS.m_oFile.m_vEffects.size()) - 1));
        l_oS.m_iEmitter = l_oS.m_oFile.m_vEffects.empty() || l_oS.m_oFile.m_vEffects[size_t(l_oS.m_iEffect)].m_vEmitters.empty() ? -1 : 0;
        return true;
    });
}

bool ParticleDocument::f_AddEmitter(Emitter l_oM)
{
    if (!f_Effect()) return false;
    l_oM.m_strName = f_U8(f_UniqueEmitterName(l_oM.m_strName.empty() ? QStringLiteral("emitter") : f_Qs(l_oM.m_strName)));
    return f_Edit(tr("Add emitter"), [&](ParticleState& l_oS) {
        auto& l_vList = l_oS.m_oFile.m_vEffects[size_t(l_oS.m_iEffect)].m_vEmitters;
        const int l_iAt = l_oS.m_iEmitter < 0 ? int(l_vList.size()) : l_oS.m_iEmitter + 1;
        l_vList.insert(l_vList.begin() + l_iAt, l_oM);
        l_oS.m_iEmitter = l_iAt;
        return true;
    });
}

bool ParticleDocument::f_RemoveEmitter(int l_iIndex)
{
    const Effect* l_pE = f_Effect();
    if (!l_pE || l_iIndex < 0 || l_iIndex >= int(l_pE->m_vEmitters.size())) return false;
    return f_Edit(tr("Delete emitter"), [&](ParticleState& l_oS) {
        auto& l_vList = l_oS.m_oFile.m_vEffects[size_t(l_oS.m_iEffect)].m_vEmitters;
        l_vList.erase(l_vList.begin() + l_iIndex);
        l_oS.m_iEmitter = l_vList.empty() ? -1 : std::min(l_iIndex, int(l_vList.size()) - 1);
        return true;
    });
}

bool ParticleDocument::f_DuplicateEmitter(int l_iIndex)
{
    const Effect* l_pE = f_Effect();
    if (!l_pE || l_iIndex < 0 || l_iIndex >= int(l_pE->m_vEmitters.size())) return false;
    Emitter l_oCopy = l_pE->m_vEmitters[size_t(l_iIndex)];
    l_oCopy.m_strName = f_U8(f_UniqueEmitterName(f_Qs(l_oCopy.m_strName)));
    return f_Edit(tr("Duplicate emitter"), [&](ParticleState& l_oS) {
        auto& l_vList = l_oS.m_oFile.m_vEffects[size_t(l_oS.m_iEffect)].m_vEmitters;
        l_vList.insert(l_vList.begin() + l_iIndex + 1, l_oCopy);
        l_oS.m_iEmitter = l_iIndex + 1;
        return true;
    });
}

bool ParticleDocument::f_MoveEmitter(int l_iIndex, int l_iDelta)
{
    const Effect* l_pE = f_Effect();
    const int l_iTo = l_iIndex + l_iDelta;
    if (!l_pE || l_iIndex < 0 || l_iIndex >= int(l_pE->m_vEmitters.size()) || l_iTo < 0 || l_iTo >= int(l_pE->m_vEmitters.size())) return false;
    return f_Edit(tr("Move emitter"), [&](ParticleState& l_oS) {
        auto& l_vList = l_oS.m_oFile.m_vEffects[size_t(l_oS.m_iEffect)].m_vEmitters;
        std::swap(l_vList[size_t(l_iIndex)], l_vList[size_t(l_iTo)]);
        l_oS.m_iEmitter = l_iTo;
        return true;
    });
}

// ---- atlases --------------------------------------------------------------------------------------

void ParticleDocument::f_LoadAtlases()
{
    std::vector<std::shared_ptr<LoadedAtlas>> l_vNext;
    for (const QString& l_oPath : m_oState.m_oAtlasesAbs) {
        std::shared_ptr<LoadedAtlas> l_spA;
        for (const auto& l_spOld : m_vAtlases)
            if (l_spOld->m_oPath == l_oPath) l_spA = l_spOld;
        if (!l_spA) {
            l_spA = f_LoadOne(l_oPath);
            if (!l_spA->m_oError.isEmpty()) emit f_Message(tr("Atlas: %1").arg(l_spA->m_oError));
        }
        l_vNext.push_back(l_spA);
    }
    m_vAtlases = std::move(l_vNext);
    m_oAtlasSet = toms::anim::AtlasSet();
    m_vTextures.clear();
    for (size_t l_nI = 0; l_nI < m_vAtlases.size(); l_nI++) {
        const LoadedAtlas& l_oA = *m_vAtlases[l_nI];
        if (!l_oA.m_bOk) continue;
        std::vector<uint16_t> l_vPages;
        for (size_t l_nP = 0; l_nP < l_oA.m_oAtlas.m_vPages.size(); l_nP++) {
            l_vPages.push_back(uint16_t(m_vTextures.size()));
            m_vTextures.push_back({int(l_nI), int(l_nP)});
        }
        m_oAtlasSet.f_Add(l_oA.m_oAtlas, f_U8(f_AtlasId(int(l_nI))), std::move(l_vPages));
    }
    emit f_AtlasChanged();
}

bool ParticleDocument::f_AddAtlas(const QString& l_oPathIn, QString* l_pError)
{
    const QString l_oPath = QFileInfo(l_oPathIn).absoluteFilePath();
    if (m_oState.m_oAtlasesAbs.contains(l_oPath, Qt::CaseInsensitive)) {
        if (l_pError) *l_pError = tr("%1 is in the list already").arg(QDir::toNativeSeparators(l_oPath));
        return false;
    }
    if (!m_oFilePath.isEmpty() && QDir::isAbsolutePath(QFileInfo(m_oFilePath).absoluteDir().relativeFilePath(l_oPath))) {
        if (l_pError) *l_pError = tr("%1 is on another drive than the .particle file: it cannot be stored relative to it")
                                .arg(QDir::toNativeSeparators(l_oPath));
        return false;
    }
    QString l_oId = f_Qs(toms::anim::f_DefaultAtlasId(f_U8(QFileInfo(l_oPath).fileName())));
    const QString l_oBase = l_oId;
    for (int l_iN = 2; m_oState.m_oAtlasIds.contains(l_oId); l_iN++) l_oId = QStringLiteral("%1%2").arg(l_oBase).arg(l_iN);
    return f_Edit(tr("Add atlas"), [&](ParticleState& l_oS) {
        l_oS.m_oAtlasesAbs << l_oPath;
        l_oS.m_oAtlasIds << l_oId;
        return true;
    });
}

bool ParticleDocument::f_RemoveAtlas(int l_iIndex)
{
    if (l_iIndex < 0 || l_iIndex >= m_oState.m_oAtlasesAbs.size()) return false;
    return f_Edit(tr("Remove atlas"), [&](ParticleState& l_oS) {
        l_oS.m_oAtlasesAbs.removeAt(l_iIndex);
        l_oS.m_oAtlasIds.removeAt(l_iIndex);
        return true;
    });
}

const SpriteImageCache* ParticleDocument::f_PageImages(uint16_t l_u16Texture, int* l_pPage) const
{
    if (l_u16Texture >= m_vTextures.size()) return nullptr;
    const auto [l_iAtlas, l_iP] = m_vTextures[l_u16Texture];
    if (l_pPage) *l_pPage = l_iP;
    return &m_vAtlases[size_t(l_iAtlas)]->m_oImages;
}

QStringList ParticleDocument::f_SpriteRefs() const
{
    QStringList l_oOut;
    for (int l_iI = 0; l_iI < f_AtlasCount(); l_iI++)
        if (f_AtlasAt(l_iI).m_bOk)
            for (const QString& l_oN : f_AtlasAt(l_iI).m_oSpriteNames) l_oOut << f_AtlasId(l_iI) + QLatin1Char(':') + l_oN;
    return l_oOut;
}

QImage ParticleDocument::f_SpriteImage(const QString& l_oRef) const
{
    uint16_t l_u16Tex = 0;
    const toms::AtlasRegion* l_pR = m_oAtlasSet.f_Find(f_U8(l_oRef), &l_u16Tex);
    int l_iPage = 0;
    const SpriteImageCache* l_pImg = l_pR ? f_PageImages(l_u16Tex, &l_iPage) : nullptr;
    return l_pImg ? l_pImg->f_Cut(l_iPage, QRect(l_pR->m_iX, l_pR->m_iY, l_pR->m_iW, l_pR->m_iH)) : QImage();
}

std::vector<toms::fx::Problem> ParticleDocument::f_Problems() const
{
    std::vector<toms::fx::Problem> l_vOut = toms::fx::f_CheckParticles(m_oState.m_oFile, &m_oAtlasSet);
    for (int l_iI = 0; l_iI < f_AtlasCount(); l_iI++)
        if (!f_AtlasAt(l_iI).m_bOk) l_vOut.insert(l_vOut.begin(), {f_U8(f_AtlasId(l_iI)), f_U8(f_AtlasAt(l_iI).m_oError), false});
    return l_vOut;
}

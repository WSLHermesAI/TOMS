#pragma once

#include "SpriteImageCache.h"
#include "atlas_file.h"
#include "particle_fx.h"

#include <QObject>
#include <QStringList>

#include <functional>
#include <memory>
#include <vector>

class QUndoStack;

// Everything one undo step restores: the file, the current effect / emitter, and the atlases
// (absolute, in lookup order, with their ids; the file's "atlases" are derived from them and the
// file path -- relative once the file has a place, absolute while it is untitled).
struct ParticleState {
    toms::fx::ParticleFile m_oFile;
    QStringList m_oAtlasesAbs;
    QStringList m_oAtlasIds;
    int m_iEffect = 0;
    int m_iEmitter = -1;   // -1: the effect itself is selected
};

// The open .particle file: undo stack, selection, and the atlases its sprites come from (parsed
// .atlas + page images, each with an id). Widgets never change the file directly: they call edit()
// or one of the operations below, each one undo step. Edits with the same merge key that follow
// each other (a spin box held down, a handle dragged) become one step.
class ParticleDocument : public QObject
{
    Q_OBJECT

public:
    explicit ParticleDocument(QObject* l_pParent = nullptr);

    const toms::fx::ParticleFile& f_File() const { return m_oState.m_oFile; }
    const ParticleState& f_State() const { return m_oState; }
    QString f_FilePath() const { return m_oFilePath; }
    QString f_DisplayName() const;
    bool f_IsDirty() const;
    QUndoStack* f_UndoStack() const { return m_pUndo; }
    // A copy for AutoBackup: atlas paths absolute; the document does not change.
    bool f_WriteBackup(const QString& l_oPath, QString* l_pError) const;
    QString f_ToJson() const;

    // A new untitled file: one effect with one emitter drawing fx:dot from the fx atlas (when found).
    void f_NewFile();
    bool f_Open(const QString& l_oPath, QString* l_pError);
    // Writes the file; the atlas paths are stored relative to `path`. Fails (nothing written) when
    // an atlas cannot be reached relatively (another drive).
    bool f_SaveTo(const QString& l_oPath, QString* l_pError);

    // ---- selection ----
    int f_EffectIndex() const { return m_oState.m_iEffect; }
    int f_EmitterIndex() const { return m_oState.m_iEmitter; }
    const toms::fx::Effect* f_Effect() const;
    const toms::fx::Emitter* f_Emitter() const;
    void f_Select(int l_iEffect, int l_iEmitter);

    // ---- edits (one undo step each; false = nothing changed) ----
    bool f_Edit(const QString& l_oText, const std::function<bool(ParticleState&)>& l_fnChange, const QString& l_oMergeKey = QString());
    bool f_EditEffect(const QString& l_oText, const std::function<void(toms::fx::Effect&)>& l_fnChange, const QString& l_oMergeKey = QString());
    bool f_EditEmitter(const QString& l_oText, const std::function<void(toms::fx::Emitter&)>& l_fnChange, const QString& l_oMergeKey = QString());
    bool f_AddEffect(toms::fx::Effect l_oE);              // after the current one; selected
    bool f_RemoveEffect(int l_iIndex);
    bool f_AddEmitter(toms::fx::Emitter l_oE);            // to the current effect; selected
    bool f_RemoveEmitter(int l_iIndex);
    bool f_DuplicateEmitter(int l_iIndex);
    bool f_MoveEmitter(int l_iIndex, int l_iDelta);
    // Emitter / effect names that are free ("spark", "spark 2", ...).
    QString f_UniqueEffectName(const QString& l_oBase) const;
    QString f_UniqueEmitterName(const QString& l_oBase) const;

    // ---- atlases ----
    struct LoadedAtlas {
        QString m_oPath;              // absolute
        toms::AtlasFile m_oAtlas;
        bool m_bOk = false;
        QString m_oError;
        SpriteImageCache m_oImages;
        QStringList m_oSpriteNames;   // sorted
    };
    int f_AtlasCount() const { return int(m_vAtlases.size()); }
    const LoadedAtlas& f_AtlasAt(int l_iI) const { return *m_vAtlases[size_t(l_iI)]; }
    QString f_AtlasId(int l_iI) const { return m_oState.m_oAtlasIds.value(l_iI); }
    int f_AtlasIndexOf(const QString& l_oId) const { return int(m_oState.m_oAtlasIds.indexOf(l_oId)); }
    bool f_AddAtlas(const QString& l_oPath, QString* l_pError);   // id = file name (or a free variant); one undo step
    bool f_RemoveAtlas(int l_iIndex);
    const toms::anim::AtlasSet& f_AtlasSet() const { return m_oAtlasSet; }
    // The page image a quad's texture id stands for (the editor gives every page its own id).
    const SpriteImageCache* f_PageImages(uint16_t l_u16Texture, int* l_pPage) const;
    QStringList f_SpriteRefs() const;                       // every "id:name", by atlas then name
    QImage f_SpriteImage(const QString& l_oRef) const;         // the sprite's packed pixels (null: unknown)

    std::vector<toms::fx::Problem> f_Problems() const;

    // The fx atlas a new file starts with (the TOMS one, when the build knows where it is).
    static QString f_DefaultFxAtlas();

signals:
    void f_FileChanged();        // any edit, undo, redo, open, new
    void f_SelectionChanged();
    void f_AtlasChanged();       // the atlas list or their contents
    void f_Message(const QString& l_oText);

private:
    friend class ParticleCommand;
    void f_SetState(const ParticleState& l_oS);   // undo / redo
    void f_LoadAtlases();
    void f_SyncStoredAtlases(ParticleState& l_oS) const;

    ParticleState m_oState;
    QString m_oFilePath;
    QUndoStack* m_pUndo;
    std::vector<std::shared_ptr<LoadedAtlas>> m_vAtlases;
    toms::anim::AtlasSet m_oAtlasSet;
    std::vector<std::pair<int, int>> m_vTextures;   // texture id -> (atlas, page)
};

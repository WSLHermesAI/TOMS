#pragma once

#include "SpriteImageCache.h"
#include "anim_clip.h"

#include <map>
#include <set>
#include <optional>
#include "anim_check.h"
#include "anim_keys.h"
#include "anim_player.h"
#include "atlas_file.h"

#include <QObject>
#include <QStringList>
#include <QUndoCommand>

#include <functional>
#include <memory>
#include <vector>

class QUndoStack;

using NodePath = std::vector<int>;   // child indices from the clip's root (empty = the root)
QString f_PathToString(const NodePath& l_vP);   // "0/2/1" ("" = root)
NodePath f_PathFromString(const QString& l_oS);

// Everything one undo step restores: the file, which clip and node were current, and the atlases
// (absolute, in lookup order, with their ids; the file's relative "atlases" are derived from them
// and the current file path -- an untitled file holds them absolute, in memory only).
struct AnimState {
    toms::anim::AnimFile m_oFile;
    QStringList m_oAtlasesAbs;
    QStringList m_oAtlasIds;      // = atlasesAbs: the ids "id:name" sprite references use
    int m_iClip = 0;
    NodePath m_vNode;
};

// The open .anim file and everything around it: undo stack, current clip / node, playhead,
// auto-key, and the atlases the sprites come from (parsed .atlas + page images each, with an id).
// A sprite reference "id:name" resolves in the atlas with that id, a bare "name" in the first atlas
// that has it -- the game's AtlasSet. Every sprite the editor writes names its atlas. Widgets never
// change the file directly: they call edit() or one of the operations below, each one undo step.
// Phase 2 (one plugin-based studio app) keeps this as the anim plugin's document.
class AnimDocument : public QObject
{
    Q_OBJECT

public:
    explicit AnimDocument(QObject* l_pParent = nullptr);

    const toms::anim::AnimFile& f_File() const { return m_oState.m_oFile; }
    const AnimState& f_State() const { return m_oState; }
    QString f_FilePath() const { return m_oFilePath; }
    QString f_DisplayName() const;
    bool f_IsDirty() const;
    QUndoStack* f_UndoStack() const { return m_pUndo; }
    QString f_ToJson() const;

    void f_NewFile();
    bool f_Open(const QString& l_oPath, QString* l_pError);
    // Writes the file (Save, or Save As when `path` differs: the atlas paths are rebased). Every
    // atlas is stored relative to `path`; when one cannot be (another drive), nothing is written
    // and `error` names it.
    bool f_Save(const QString& l_oPath, QString* l_pError);
    // A copy for AutoBackup: atlas paths absolute (it opens from the backups folder); the document
    // (path, saved state) does not change.
    bool f_WriteBackup(const QString& l_oPath, QString* l_pError) const;
    // The atlas paths relative to an .anim at `animPath`; false (and `error`) when one can only
    // be absolute.
    bool f_RelativeAtlases(const QString& l_oAnimPath, std::vector<toms::anim::AtlasRef>& l_vOut, QString* l_pError) const;

    // ---- atlases ----
    struct LoadedAtlas {
        QString m_oPath;              // absolute
        toms::AtlasFile m_oAtlas;
        std::vector<QImage> m_vRgba;   // its pages as RGBA8888, what meshes are cut from (docs/engine/16)
        // Its mesh file (<atlas>.mesh.json beside it, shared by every clip using the atlas): as on disk, and with the
        // meshes this file's clips need added (what is drawn, and merged into the file on save).
        toms::SpriteMeshFile m_oMeshesOnDisk, m_oMeshes;
        bool m_bMeshesBinary = true;                     // the mesh file's form: kept; a new one binary
        QString m_oMeshWarning;                          // its mesh file could not be read (cut again)
        std::set<std::string> m_setNeededKeys;             // the keys this file's clips need now
        std::map<std::string, toms::SpriteMesh> m_mapCut;  // meshes cut from its pixels, by key
        bool m_bOk = false;
        QString m_oError;
        SpriteImageCache m_oImages;
        QStringList m_oSpriteNames;   // sorted
    };
    QStringList f_AtlasPaths() const { return m_oState.m_oAtlasesAbs; }   // absolute, lookup order
    QStringList f_AtlasIds() const { return m_oState.m_oAtlasIds; }       // the same order
    QString f_AtlasId(int l_iI) const { return m_oState.m_oAtlasIds.value(l_iI); }
    int f_AtlasIndexOf(const QString& l_oId) const { return int(m_oState.m_oAtlasIds.indexOf(l_oId)); }   // -1: none
    int f_AtlasCount() const { return int(m_vAtlases.size()); }
    const LoadedAtlas& f_AtlasAt(int l_iI) const { return *m_vAtlases[size_t(l_iI)]; }
    QString f_StoredAtlasPath(int l_iI) const;      // as the file stores it (relative when titled)
    bool f_AtlasPathIsAbsoluteInFile(int l_iI) const;   // the opened file named it with an absolute path
    bool f_AtlasLoaded() const;                  // at least one, and every one loaded
    // The game's lookup over the loaded atlases; each page's texture id maps back with pageImages().
    const toms::anim::AtlasSet& f_AtlasSet() const { return m_oAtlasSet; }
    const SpriteImageCache* f_PageImages(uint16_t l_u16Texture, int* l_pPage) const;
    std::vector<animed::CheckAtlas> f_CheckAtlases() const;   // for checkAnim
    // The region a sprite reference resolves to (AtlasSet::find over the loaded atlases, as the
    // game does) and which atlas that is.
    const toms::AtlasRegion* f_FindSprite(const std::string& l_strRef, int* l_pAtlasIndex = nullptr) const;
    QStringList f_SpriteNames() const { return m_oSpriteNames; }   // every atlas, unique, sorted
    QRect f_RegionRect(int l_iAtlasIndex, const toms::AtlasRegion& l_oR) const;   // its pixels on its page
    QImage f_SpriteImage(const QString& l_oRef) const;                          // as it resolves
    QImage f_SpriteImage(int l_iAtlasIndex, const QString& l_oName) const;         // from that atlas

    // ---- sprite references as the widgets show and take them ----
    // "id:name" for that atlas's sprite: what picking it anywhere stores.
    std::string f_SpriteRefFor(int l_iAtlasIndex, const QString& l_oName) const;
    // "name (id)"; a bare name: "name (auto: id)" with the atlas the lookup picks now, or just
    // "name" when none has it; "" -> "(none)".
    QString f_SpriteDisplay(const std::string& l_strRef) const;
    // Text typed or picked in a sprite field -> the reference to store: "name (id)", "id:name", or
    // a bare name (qualified with the atlas the lookup picks, when one has it); "(none)" -> "".
    std::string f_SpriteFromText(const QString& l_oText) const;
    // spriteDisplay() of every sprite of every loaded atlas (one entry per copy), by name.
    QStringList f_SpriteChoices() const { return m_oSpriteChoices; }

    // Each one undo step. addAtlas refuses a file that does not parse, one already in the list,
    // an id that is not usable or taken (empty id = the file name without ".atlas"), and -- for a
    // saved .anim -- one that cannot be stored relative to it.
    bool f_AddAtlas(const QString& l_oAbsolutePath, const QString& l_oId, QString* l_pError);
    bool f_RemoveAtlas(int l_iIndex);
    bool f_MoveAtlas(int l_iIndex, int l_iDelta);
    // Renames an atlas id and every "oldId:..." reference in every clip, in one step. Refuses an
    // id that atlasIdUsable() refuses.
    bool f_RenameAtlasId(int l_iIndex, const QString& l_oId, QString* l_pError);
    // Not empty, no ':' (or '/', '\', '=', spaces), and no other atlas (than exceptIndex) has it.
    bool f_AtlasIdUsable(const QString& l_oId, int l_iExceptIndex, QString* l_pError) const;
    // The id for a new atlas: its file name without ".atlas"; when another atlas has that, the
    // folder above its folder (styles/dark16/atlas/game.atlas -> "dark16"), else with a number.
    QString f_SuggestAtlasId(const QString& l_oAbsolutePath) const;
    // References that stop resolving when the atlas is removed: "id:..." ones naming it and bare
    // ones no other loaded atlas has.
    int f_AtlasReferenceCount(int l_iIndex) const;
    // Bare references in every clip -> "id:name" with the atlas the lookup picks now (one step).
    // Returns how many changed; bare names no atlas has stay as they are.
    int f_QualifySpriteReferences();
    int f_BareSpriteReferenceCount() const;   // that qualifySpriteReferences() would change
    void f_ReloadAtlases();

    // ---- current clip / node / time ----
    int f_ClipIndex() const { return m_oState.m_iClip; }
    const toms::anim::Clip* f_Clip() const;
    // ---- a character clip shown as another character (docs/engine/05_ANIMATION.md "Characters"; View > Preview as Character).
    // Only the view changes: edits still go to clip(). The fitted copy is kept until the clip or the character changes,
    // so poses made from it stay valid.
    void f_SetPreviewCharacter(const QString& l_oSprite);   // "" = as made
    QString f_PreviewCharacter() const { return m_oPreviewCharacter; }
    // The sprites the clip's `body` can be shown as: its own sprite and the atlas's sprites named like it
    // ("player" -> "player", "player_mage", ...). Empty: the clip has no `body` / `hand` nodes.
    QStringList f_CharacterChoices() const;
    const toms::anim::Clip* f_DrawClip() const;           // clip(), or it fitted to previewCharacter()
    // ---- a held thing (View > Held Clip; docs/engine/05_ANIMATION.md "Characters", anim_player.h CharacterRig): another
    // .anim file (a weapon) whose clip named like clip() is drawn at the `hand` node, at the same time. The view only;
    // its sprites are looked up in this file's atlases (by id, else by name).
    bool f_SetHeldFile(const QString& l_oPath, QString* l_pError = nullptr);   // "" = none
    QString f_HeldFile() const { return m_oHeldPath; }
    const toms::anim::AnimFile& f_HeldFileData() const { return m_oHeld; }
    const toms::anim::Clip* f_HeldClip() const;           // nullptr: no held file, or it has no clip of this name
    // The node of drawClip() at the same place as `n` of clip() (the same node when not previewing).
    const toms::anim::Node* f_DrawNode(const toms::anim::Node* l_pN) const;
    void f_SetClipIndex(int l_iIndex);
    // The current clip differs from how it was when it became current (or when the file was last
    // saved, if later).
    bool f_ClipModified() const;
    // Puts the current clip back to that state (one undo step). False when it is not modified.
    bool f_DiscardClipChanges();
    const NodePath& f_SelectedPath() const { return m_oState.m_vNode; }
    const toms::anim::Node* f_SelectedNode() const;
    void f_SelectNode(const NodePath& l_vPath);
    bool f_SelectNodeByName(const QString& l_oName);   // first match, depth first
    float f_Time() const { return m_fTime; }
    void f_SetTime(float l_fT);   // also ends a preview time
    // The time the viewport draws: time(), or -- while playback runs on after the clip's end for its
    // looping nodes -- a later time. Editing still happens at time().
    float f_PreviewTime() const { return m_fPreview >= 0 ? m_fPreview : m_fTime; }
    // Solo (preview only, not saved): the viewport draws just this node and its children (in their
    // place: the parents still move them, their own sprites are not drawn). Empty = everything.
    const std::optional<NodePath>& f_Solo() const { return m_optSolo; }
    void f_SetSolo(const std::optional<NodePath>& l_optPath);
    // The nodes the viewport draws under the solo (null when there is no solo: all of them).
    bool f_SoloShows(const toms::anim::Node* l_pN) const;   // n: a node of clip() or of drawClip()
    void f_SetPreviewTime(float l_fT);   // < 0 = back to time()
    float f_Duration() const;
    bool f_AutoKey() const { return m_bAutoKey; }
    void f_SetAutoKey(bool l_bOn);
    bool f_KeyTogether() const { return m_bKeyTogether; }   // key list: "Key all channels together"
    void f_SetKeyTogether(bool l_bOn) { m_bKeyTogether = l_bOn; }

    // ---- editing (each call is one undo step; false = nothing changed) ----
    // `change` edits a copy of the whole state (file, current clip and node) and returns false to
    // cancel. mergeKey: consecutive edits with the same key fold into one step (spin box arrows,
    // typing within a short time; a key starting with '#' -- one viewport drag -- always folds).
    bool f_Edit(const QString& l_oText, const std::function<bool(AnimState&)>& l_fnChange, const QString& l_oMergeKey = QString());
    bool f_EditClip(const QString& l_oText, const std::function<void(toms::anim::Clip&)>& l_fnChange, const QString& l_oMergeKey = QString());
    bool f_EditNode(const NodePath& l_vPath, const QString& l_oText, const std::function<void(toms::anim::Node&)>& l_fnChange,
                  const QString& l_oMergeKey = QString());
    bool f_EditSelected(const QString& l_oText, const std::function<void(toms::anim::Node&)>& l_fnChange, const QString& l_oMergeKey = QString());

    // A channel of the selected node at the playhead: auto-key on writes a key at the playhead;
    // off, it updates a key that is exactly at the playhead, else the rest value.
    bool f_SetChannel(animed::Channel l_eC, const animed::Value& l_oV, const QString& l_oMergeKey = QString());
    bool f_AddKeyHere(animed::Channel l_eC);      // a key at the playhead with the value shown now
    bool f_RemoveKeyHere(animed::Channel l_eC);
    bool f_SetEaseHere(animed::Channel l_eC, const toms::anim::Ease& l_oE);   // key at/before the playhead
    // A sprite picked in the Sprites dock ("id:name"): rest sprite, or a sprite key at the playhead (auto-key).
    bool f_AssignSprite(const QString& l_oRef);

    // ---- key list (rows = key times of the selected node) ----
    // Keys at time t: `channel` < 0 = the interpolated channels the node animates already (position
    // when it animates none; never a new track), else that one.
    bool f_InsertKeys(float l_fT, int l_iChannel);
    bool f_DeleteKeys(const std::vector<float>& l_vTimes, int l_iChannel);   // channel < 0 = all channels
    bool f_SetCell(float l_fT, animed::Channel l_eC, const animed::Value& l_oV, bool l_bTogether);
    bool f_RemoveCell(float l_fT, animed::Channel l_eC);
    // Moves whole rows; on a clash nothing changes and `conflict` says where.
    bool f_MoveRows(const std::vector<float>& l_vFrom, const std::vector<float>& l_vTo, QString* l_pConflict, const QString& l_oText = QString());
    bool f_MoveChannelKey(animed::Channel l_eC, float l_fFrom, float l_fTo, QString* l_pConflict);
    bool f_Rescale(float l_fNewDuration, bool l_bWholeClip, QString* l_pError);
    // channel Color with alphaOnly: only alpha ramps, the colour keeps what it shows.
    bool f_Ramp(const std::vector<float>& l_vTimes, animed::Channel l_eC, const animed::Value& l_oFirst, const animed::Value& l_oLast, bool l_bAlphaOnly);
    bool f_Fade(const std::vector<float>& l_vTimes, bool l_bIn);
    bool f_SpriteSequence(const std::vector<float>& l_vTimes, const QStringList& l_oSprites);
    // One sprite key per reference on the selected node, at start, start + gap, ... (keys already
    // at those times are replaced). extendClip: a fixed clip length grows to the last key.
    bool f_InsertSpriteSequence(float l_fStart, float l_fGap, const QStringList& l_oSprites, bool l_bExtendClip);
    bool f_SetEaseRows(const std::vector<float>& l_vTimes, const toms::anim::Ease& l_oE);

    // ---- structure ----
    bool f_AddNode(const NodePath& l_vParent, toms::anim::Node l_oNode, const QString& l_oText);   // selects it
    bool f_AddSpriteNode(const NodePath& l_vParent, const QString& l_oSprite, const glm::vec2& l_v2Pos);
    bool f_DeleteNode(const NodePath& l_vPath);
    bool f_DuplicateNode(const NodePath& l_vPath);   // the copy's names made unique, its skins pointed at its own bones
    // Copy / paste (Edit menu): the node at `path` and its subtree as clipboard text (animed::nodeClipboardText);
    // empty when there is no such node. pasteNode puts a copied node under `parent` at `index` (-1 = last), renamed
    // to fit the clip (animed::renameForPaste), and selects it; false (and a message) when the text holds none.
    QString f_CopyNodeText(const NodePath& l_vPath) const;
    // The skinned node at `path` gets the bones of its skin that lie outside it (animed::bonesOutside) as its
    // children, nothing moving on screen: each such bone's subtree goes under the node with its rest position and
    // position keys shifted by the offset between its old parent and the node. Then moving the node moves its mesh and
    // bones together. One undo step; false (and a message) when the node and the old parent differ by more than a
    // move (rotated or scaled against each other), or a bone holds the node.
    bool f_ParentBonesUnder(const NodePath& l_vPath);
    // The selected node's mesh grid, cells across and down (its meshGrid; 0, 0 = 8-pixel cells). One undo step; the
    // mesh is cut again at once, and saved with the file (docs/engine/16_MESH_ANIMATION.md).
    bool f_SetMeshGrid(int l_iCols, int l_iRows);
    // A motion recipe (anim_recipes.h: breathe, sway, squash, jelly on the lattice; bone_sway, nod, swing on a skin's
    // bones) written on the selected node from `start`: one undo step. `period` 0 = the recipe's own. False and a
    // message when it does not fit the node.
    bool f_ApplyMotionRecipe(const std::string& l_strId, float l_fStart, float l_fPeriod, float l_fStrength);
    // The selected node's lattice size (cols x rows, 2 .. 16). Its deform keys are kept: each moved point goes to the
    // nearest point of the new grid. One undo step.
    bool f_SetLatticeSize(int l_iCols, int l_iRows);
    // The grid a node's sprite (the one at the playhead) is cut on: its meshGrid, or the cells 8-pixel cells make.
    // False when no atlas has the sprite.
    bool f_MeshGridOf(const toms::anim::Node& l_oN, int& l_iCols, int& l_iRows) const;
    // Atlas i's meshes: its mesh file with every mesh this file's clips need (each skinned or deformed node's sprite on
    // its grid, cut from the atlas's pixels as the clips change). Saving adds those to the atlas's mesh file.
    const toms::SpriteMeshFile& f_AtlasMeshes(int l_iI) const { return m_vAtlases[size_t(l_iI)]->m_oMeshes; }
    bool f_PasteNode(const NodePath& l_vParent, int l_iIndex, const QString& l_oText);
    bool f_RenameNode(const NodePath& l_vPath, const QString& l_oName);
    bool f_MoveNode(const NodePath& l_vFrom, const NodePath& l_vToParent, int l_iIndex);   // reparent / reorder
    bool f_AddClip(const QString& l_oName);
    bool f_DuplicateClip(int l_iIndex);
    bool f_RenameClip(int l_iIndex, const QString& l_oName);
    bool f_DeleteClip(int l_iIndex);

signals:
    void f_FileReset();          // another file was opened / created (also fileChanged, clipChanged)
    void f_FileChanged();        // any edit, undo or redo
    // An edit is about to change the file (before it reads the playhead): the editor pauses playback, so a drag or
    // a typed value with Auto-Key on keys the frame on screen once, not every frame the playhead passes.
    void f_AboutToEdit();
    void f_ClipChanged();
    void f_SelectionChanged();
    void f_TimeChanged(float l_fT);
    void f_PreviewTimeChanged();
    void f_SoloChanged();
    void f_AtlasChanged();
    void f_DirtyChanged(bool l_bDirty);
    void f_FilePathChanged();
    void f_AutoKeyChanged(bool l_bOn);
    void f_Message(const QString& l_oText);

private:
    friend class AnimCommand;
    void f_RestoreState(const AnimState& l_oS);   // undo / redo
    void f_Reset(AnimState l_oS, const QString& l_oPath);
    void f_LoadAtlases(bool l_bForce);
    static std::vector<toms::anim::AtlasRef> f_StoredAtlases(const AnimState& l_oS, const QString& l_oFilePath);
    void f_FixSelection(AnimState& l_oS) const;
    bool f_EditNodeIf(const NodePath& l_vPath, const QString& l_oText, const std::function<bool(toms::anim::Node&)>& l_fnChange,
                    const QString& l_oMergeKey = QString());

    AnimState m_oState;
    QString m_oFilePath;
    QUndoStack* m_pUndo;
    float m_fTime = 0;
    toms::anim::Clip m_oClipBase;     // see clipModified()
    int m_iClipBaseIndex = -1;
    void f_MarkClipBase();
    float m_fPreview = -1;   // see previewTime()
    std::optional<NodePath> m_optSolo;
    QString m_oPreviewCharacter;                       // setPreviewCharacter
    QString m_oHeldPath;                               // setHeldFile
    toms::anim::AnimFile m_oHeld;
    mutable toms::anim::Clip m_oFitSource, m_oFitted;   // drawClip's cache: the clip it was made from, and the copy
    mutable QString m_oFitFor;
    mutable bool m_bFitValid = false;
    mutable std::map<std::string, toms::SpriteBox> m_mapBoxes;   // sprites' visible pixels, by reference (fitting)
    bool m_bAutoKey = true;
    bool m_bKeyTogether = false;

    std::vector<std::shared_ptr<LoadedAtlas>> m_vAtlases;   // = m_state.atlasesAbs
    toms::anim::AtlasSet m_oAtlasSet;
    void f_UpdateMeshes();                    // each atlas's meshes for the current file
    bool f_SaveMeshes(QString* l_pError);        // adds this file's meshes to its atlases' mesh files
    std::vector<std::pair<int, int>> m_vTextures;           // texture id -> (atlas, page)
    std::vector<int> m_vSetToDoc;                           // m_atlasSet entry -> atlas index
    QStringList m_oSpriteNames;
    QStringList m_oSpriteChoices;
    QStringList m_oAbsoluteInFile;   // atlases the opened file named absolutely (until saved)
};

// The one undo command: the whole state before and after an edit (an .anim is small; snapshots
// are simpler to get right than one inverse per kind of edit).
class AnimCommand : public QUndoCommand
{
public:
    AnimCommand(AnimDocument* l_pDoc, const QString& l_oText, AnimState l_oBefore, AnimState l_oAfter, const QString& l_oMergeKey);
    void undo() override;
    void redo() override;
    int id() const override { return m_iId; }
    bool mergeWith(const QUndoCommand* l_pOther) override;

private:
    static constexpr qint64 m_i64MergeWindowMs = 1500;
    AnimDocument* m_pDoc;
    AnimState m_oBefore, m_oAfter;
    QString m_oMergeKey;
    int m_iId = -1;
    qint64 m_i64Time = 0;
};

#pragma once

#include "SpriteImageCache.h"
#include "anim_clip.h"
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
QString pathToString(const NodePath& p);   // "0/2/1" ("" = root)
NodePath pathFromString(const QString& s);

// Everything one undo step restores: the file, which clip and node were current, and the atlases
// (absolute, in lookup order, with their ids; the file's relative "atlases" are derived from them
// and the current file path -- an untitled file holds them absolute, in memory only).
struct AnimState {
    toms::anim::AnimFile file;
    QStringList atlasesAbs;
    QStringList atlasIds;      // = atlasesAbs: the ids "id:name" sprite references use
    int clip = 0;
    NodePath node;
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
    explicit AnimDocument(QObject* parent = nullptr);

    const toms::anim::AnimFile& file() const { return m_state.file; }
    const AnimState& state() const { return m_state; }
    QString filePath() const { return m_filePath; }
    QString displayName() const;
    bool isDirty() const;
    QUndoStack* undoStack() const { return m_undo; }
    QString toJson() const;

    void newFile();
    bool open(const QString& path, QString* error);
    // Writes the file (Save, or Save As when `path` differs: the atlas paths are rebased). Every
    // atlas is stored relative to `path`; when one cannot be (another drive), nothing is written
    // and `error` names it.
    bool save(const QString& path, QString* error);
    // The atlas paths relative to an .anim at `animPath`; false (and `error`) when one can only
    // be absolute.
    bool relativeAtlases(const QString& animPath, std::vector<toms::anim::AtlasRef>& out, QString* error) const;

    // ---- atlases ----
    struct LoadedAtlas {
        QString path;              // absolute
        toms::AtlasFile atlas;
        bool ok = false;
        QString error;
        SpriteImageCache images;
        QStringList spriteNames;   // sorted
    };
    QStringList atlasPaths() const { return m_state.atlasesAbs; }   // absolute, lookup order
    QStringList atlasIds() const { return m_state.atlasIds; }       // the same order
    QString atlasId(int i) const { return m_state.atlasIds.value(i); }
    int atlasIndexOf(const QString& id) const { return int(m_state.atlasIds.indexOf(id)); }   // -1: none
    int atlasCount() const { return int(m_atlases.size()); }
    const LoadedAtlas& atlasAt(int i) const { return *m_atlases[size_t(i)]; }
    QString storedAtlasPath(int i) const;      // as the file stores it (relative when titled)
    bool atlasPathIsAbsoluteInFile(int i) const;   // the opened file named it with an absolute path
    bool atlasLoaded() const;                  // at least one, and every one loaded
    // The game's lookup over the loaded atlases; each page's texture id maps back with pageImages().
    const toms::anim::AtlasSet& atlasSet() const { return m_atlasSet; }
    const SpriteImageCache* pageImages(uint16_t texture, int* page) const;
    std::vector<animed::CheckAtlas> checkAtlases() const;   // for checkAnim
    // The region a sprite reference resolves to (AtlasSet::find over the loaded atlases, as the
    // game does) and which atlas that is.
    const toms::AtlasRegion* findSprite(const std::string& ref, int* atlasIndex = nullptr) const;
    QStringList spriteNames() const { return m_spriteNames; }   // every atlas, unique, sorted
    QRect regionRect(int atlasIndex, const toms::AtlasRegion& r) const;   // its pixels on its page
    QImage spriteImage(const QString& ref) const;                          // as it resolves
    QImage spriteImage(int atlasIndex, const QString& name) const;         // from that atlas

    // ---- sprite references as the widgets show and take them ----
    // "id:name" for that atlas's sprite: what picking it anywhere stores.
    std::string spriteRefFor(int atlasIndex, const QString& name) const;
    // "name (id)"; a bare name: "name (auto: id)" with the atlas the lookup picks now, or just
    // "name" when none has it; "" -> "(none)".
    QString spriteDisplay(const std::string& ref) const;
    // Text typed or picked in a sprite field -> the reference to store: "name (id)", "id:name", or
    // a bare name (qualified with the atlas the lookup picks, when one has it); "(none)" -> "".
    std::string spriteFromText(const QString& text) const;
    // spriteDisplay() of every sprite of every loaded atlas (one entry per copy), by name.
    QStringList spriteChoices() const { return m_spriteChoices; }

    // Each one undo step. addAtlas refuses a file that does not parse, one already in the list,
    // an id that is not usable or taken (empty id = the file name without ".atlas"), and -- for a
    // saved .anim -- one that cannot be stored relative to it.
    bool addAtlas(const QString& absolutePath, const QString& id, QString* error);
    bool removeAtlas(int index);
    bool moveAtlas(int index, int delta);
    // Renames an atlas id and every "oldId:..." reference in every clip, in one step. Refuses an
    // id that atlasIdUsable() refuses.
    bool renameAtlasId(int index, const QString& id, QString* error);
    // Not empty, no ':' (or '/', '\', '=', spaces), and no other atlas (than exceptIndex) has it.
    bool atlasIdUsable(const QString& id, int exceptIndex, QString* error) const;
    // The id for a new atlas: its file name without ".atlas"; when another atlas has that, the
    // folder above its folder (styles/dark16/atlas/game.atlas -> "dark16"), else with a number.
    QString suggestAtlasId(const QString& absolutePath) const;
    // References that stop resolving when the atlas is removed: "id:..." ones naming it and bare
    // ones no other loaded atlas has.
    int atlasReferenceCount(int index) const;
    // Bare references in every clip -> "id:name" with the atlas the lookup picks now (one step).
    // Returns how many changed; bare names no atlas has stay as they are.
    int qualifySpriteReferences();
    int bareSpriteReferenceCount() const;   // that qualifySpriteReferences() would change
    void reloadAtlases();

    // ---- current clip / node / time ----
    int clipIndex() const { return m_state.clip; }
    const toms::anim::Clip* clip() const;
    void setClipIndex(int index);
    const NodePath& selectedPath() const { return m_state.node; }
    const toms::anim::Node* selectedNode() const;
    void selectNode(const NodePath& path);
    bool selectNodeByName(const QString& name);   // first match, depth first
    float time() const { return m_time; }
    void setTime(float t);
    float duration() const;
    bool autoKey() const { return m_autoKey; }
    void setAutoKey(bool on);
    bool keyTogether() const { return m_keyTogether; }   // key list: "Key all channels together"
    void setKeyTogether(bool on) { m_keyTogether = on; }

    // ---- editing (each call is one undo step; false = nothing changed) ----
    // `change` edits a copy of the whole state (file, current clip and node) and returns false to
    // cancel. mergeKey: consecutive edits with the same key fold into one step (spin box arrows,
    // typing within a short time; a key starting with '#' -- one viewport drag -- always folds).
    bool edit(const QString& text, const std::function<bool(AnimState&)>& change, const QString& mergeKey = QString());
    bool editClip(const QString& text, const std::function<void(toms::anim::Clip&)>& change, const QString& mergeKey = QString());
    bool editNode(const NodePath& path, const QString& text, const std::function<void(toms::anim::Node&)>& change,
                  const QString& mergeKey = QString());
    bool editSelected(const QString& text, const std::function<void(toms::anim::Node&)>& change, const QString& mergeKey = QString());

    // A channel of the selected node at the playhead: auto-key on writes a key at the playhead;
    // off, it updates a key that is exactly at the playhead, else the rest value.
    bool setChannel(animed::Channel c, const animed::Value& v, const QString& mergeKey = QString());
    bool addKeyHere(animed::Channel c);      // a key at the playhead with the value shown now
    bool removeKeyHere(animed::Channel c);
    bool setEaseHere(animed::Channel c, const toms::anim::Ease& e);   // key at/before the playhead
    // A sprite picked in the Sprites dock ("id:name"): rest sprite, or a sprite key at the playhead (auto-key).
    bool assignSprite(const QString& ref);

    // ---- key list (rows = key times of the selected node) ----
    // Keys at time t: `channel` < 0 = the interpolated channels the node animates already (position
    // when it animates none; never a new track), else that one.
    bool insertKeys(float t, int channel);
    bool deleteKeys(const std::vector<float>& times, int channel);   // channel < 0 = all channels
    bool setCell(float t, animed::Channel c, const animed::Value& v, bool together);
    bool removeCell(float t, animed::Channel c);
    // Moves whole rows; on a clash nothing changes and `conflict` says where.
    bool moveRows(const std::vector<float>& from, const std::vector<float>& to, QString* conflict, const QString& text = QString());
    bool moveChannelKey(animed::Channel c, float from, float to, QString* conflict);
    bool rescale(float newDuration, bool wholeClip, QString* error);
    // channel Color with alphaOnly: only alpha ramps, the colour keeps what it shows.
    bool ramp(const std::vector<float>& times, animed::Channel c, const animed::Value& first, const animed::Value& last, bool alphaOnly);
    bool fade(const std::vector<float>& times, bool in);
    bool spriteSequence(const std::vector<float>& times, const QStringList& sprites);
    // One sprite key per reference on the selected node, at start, start + gap, ... (keys already
    // at those times are replaced). extendClip: a fixed clip length grows to the last key.
    bool insertSpriteSequence(float start, float gap, const QStringList& sprites, bool extendClip);
    bool setEaseRows(const std::vector<float>& times, const toms::anim::Ease& e);

    // ---- structure ----
    bool addNode(const NodePath& parent, toms::anim::Node node, const QString& text);   // selects it
    bool addSpriteNode(const NodePath& parent, const QString& sprite, const glm::vec2& pos);
    bool deleteNode(const NodePath& path);
    bool duplicateNode(const NodePath& path);
    bool renameNode(const NodePath& path, const QString& name);
    bool moveNode(const NodePath& from, const NodePath& toParent, int index);   // reparent / reorder
    bool addClip(const QString& name);
    bool duplicateClip(int index);
    bool renameClip(int index, const QString& name);
    bool deleteClip(int index);

signals:
    void fileReset();          // another file was opened / created (also fileChanged, clipChanged)
    void fileChanged();        // any edit, undo or redo
    void clipChanged();
    void selectionChanged();
    void timeChanged(float t);
    void atlasChanged();
    void dirtyChanged(bool dirty);
    void filePathChanged();
    void autoKeyChanged(bool on);
    void message(const QString& text);

private:
    friend class AnimCommand;
    void restoreState(const AnimState& s);   // undo / redo
    void reset(AnimState s, const QString& path);
    void loadAtlases(bool force);
    static std::vector<toms::anim::AtlasRef> storedAtlases(const AnimState& s, const QString& filePath);
    void fixSelection(AnimState& s) const;
    bool editNodeIf(const NodePath& path, const QString& text, const std::function<bool(toms::anim::Node&)>& change,
                    const QString& mergeKey = QString());

    AnimState m_state;
    QString m_filePath;
    QUndoStack* m_undo;
    float m_time = 0;
    bool m_autoKey = true;
    bool m_keyTogether = false;

    std::vector<std::shared_ptr<LoadedAtlas>> m_atlases;   // = m_state.atlasesAbs
    toms::anim::AtlasSet m_atlasSet;
    std::vector<std::pair<int, int>> m_textures;           // texture id -> (atlas, page)
    std::vector<int> m_setToDoc;                           // m_atlasSet entry -> atlas index
    QStringList m_spriteNames;
    QStringList m_spriteChoices;
    QStringList m_absoluteInFile;   // atlases the opened file named absolutely (until saved)
};

// The one undo command: the whole state before and after an edit (an .anim is small; snapshots
// are simpler to get right than one inverse per kind of edit).
class AnimCommand : public QUndoCommand
{
public:
    AnimCommand(AnimDocument* doc, const QString& text, AnimState before, AnimState after, const QString& mergeKey);
    void undo() override;
    void redo() override;
    int id() const override { return m_id; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    static constexpr qint64 kMergeWindowMs = 1500;
    AnimDocument* m_doc;
    AnimState m_before, m_after;
    QString m_mergeKey;
    int m_id = -1;
    qint64 m_time = 0;
};

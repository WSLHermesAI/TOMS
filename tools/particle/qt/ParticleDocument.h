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
    toms::fx::ParticleFile file;
    QStringList atlasesAbs;
    QStringList atlasIds;
    int effect = 0;
    int emitter = -1;   // -1: the effect itself is selected
};

// The open .particle file: undo stack, selection, and the atlases its sprites come from (parsed
// .atlas + page images, each with an id). Widgets never change the file directly: they call edit()
// or one of the operations below, each one undo step. Edits with the same merge key that follow
// each other (a spin box held down, a handle dragged) become one step.
class ParticleDocument : public QObject
{
    Q_OBJECT

public:
    explicit ParticleDocument(QObject* parent = nullptr);

    const toms::fx::ParticleFile& file() const { return m_state.file; }
    const ParticleState& state() const { return m_state; }
    QString filePath() const { return m_filePath; }
    QString displayName() const;
    bool isDirty() const;
    QUndoStack* undoStack() const { return m_undo; }
    QString toJson() const;

    // A new untitled file: one effect with one emitter drawing fx:dot from the fx atlas (when found).
    void newFile();
    bool open(const QString& path, QString* error);
    // Writes the file; the atlas paths are stored relative to `path`. Fails (nothing written) when
    // an atlas cannot be reached relatively (another drive).
    bool saveTo(const QString& path, QString* error);

    // ---- selection ----
    int effectIndex() const { return m_state.effect; }
    int emitterIndex() const { return m_state.emitter; }
    const toms::fx::Effect* effect() const;
    const toms::fx::Emitter* emitter() const;
    void select(int effect, int emitter);

    // ---- edits (one undo step each; false = nothing changed) ----
    bool edit(const QString& text, const std::function<bool(ParticleState&)>& change, const QString& mergeKey = QString());
    bool editEffect(const QString& text, const std::function<void(toms::fx::Effect&)>& change, const QString& mergeKey = QString());
    bool editEmitter(const QString& text, const std::function<void(toms::fx::Emitter&)>& change, const QString& mergeKey = QString());
    bool addEffect(toms::fx::Effect e);              // after the current one; selected
    bool removeEffect(int index);
    bool addEmitter(toms::fx::Emitter e);            // to the current effect; selected
    bool removeEmitter(int index);
    bool duplicateEmitter(int index);
    bool moveEmitter(int index, int delta);
    // Emitter / effect names that are free ("spark", "spark 2", ...).
    QString uniqueEffectName(const QString& base) const;
    QString uniqueEmitterName(const QString& base) const;

    // ---- atlases ----
    struct LoadedAtlas {
        QString path;              // absolute
        toms::AtlasFile atlas;
        bool ok = false;
        QString error;
        SpriteImageCache images;
        QStringList spriteNames;   // sorted
    };
    int atlasCount() const { return int(m_atlases.size()); }
    const LoadedAtlas& atlasAt(int i) const { return *m_atlases[size_t(i)]; }
    QString atlasId(int i) const { return m_state.atlasIds.value(i); }
    int atlasIndexOf(const QString& id) const { return int(m_state.atlasIds.indexOf(id)); }
    bool addAtlas(const QString& path, QString* error);   // id = file name (or a free variant); one undo step
    bool removeAtlas(int index);
    const toms::anim::AtlasSet& atlasSet() const { return m_atlasSet; }
    // The page image a quad's texture id stands for (the editor gives every page its own id).
    const SpriteImageCache* pageImages(uint16_t texture, int* page) const;
    QStringList spriteRefs() const;                       // every "id:name", by atlas then name
    QImage spriteImage(const QString& ref) const;         // the sprite's packed pixels (null: unknown)

    std::vector<toms::fx::Problem> problems() const;

    // The fx atlas a new file starts with (the TOMS one, when the build knows where it is).
    static QString defaultFxAtlas();

signals:
    void fileChanged();        // any edit, undo, redo, open, new
    void selectionChanged();
    void atlasChanged();       // the atlas list or their contents
    void message(const QString& text);

private:
    friend class ParticleCommand;
    void setState(const ParticleState& s);   // undo / redo
    void loadAtlases();
    void syncStoredAtlases(ParticleState& s) const;

    ParticleState m_state;
    QString m_filePath;
    QUndoStack* m_undo;
    std::vector<std::shared_ptr<LoadedAtlas>> m_atlases;
    toms::anim::AtlasSet m_atlasSet;
    std::vector<std::pair<int, int>> m_textures;   // texture id -> (atlas, page)
};

#pragma once

#include "AnimClipsDock.h"
#include "AnimDocument.h"

#include <QDockWidget>

class AnimAtlasesPanel;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QToolButton;

// Right dock: the current clip's settings, the file's atlases, the selected node's rest settings, and the values at
// the playhead with a key indicator per channel (filled = key here, hollow = animated but no key
// here, dotted = no track; click to remove / add a key), plus the ease of the key at or before
// the playhead per interpolated channel.
class AnimPropertiesDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit AnimPropertiesDock(AnimDocument* doc, QWidget* parent = nullptr);

    // The key indicator button of a channel (selftest / tests).
    QToolButton* keyButton(animed::Channel c) const { return m_key[int(c)]; }
    QDoubleSpinBox* posXSpin() const { return m_pos[0]; }
    QComboBox* spriteCombo() const { return m_sprite; }   // the rest sprite picker
    AnimAtlasesPanel* atlasesPanel() const { return m_atlases; }

private:
    void refresh();
    void refreshPlayhead();
    void commitChannel(animed::Channel c);
    QToolButton* makeKeyButton(animed::Channel c, QWidget* parent);

    QString skinInfo(const toms::anim::Node& n) const;   // the Skin row's note
public:
    QString skinInfoText() const;   // the note as shown ("" when hidden; the selftest)
    bool bonesOutsideShown() const;   // the "bones outside" warning is up (the selftest)
    QSpinBox* meshGridBox() const { return m_meshCols; }   // cells across (and m_meshRows down)
private:
    AnimDocument* m_doc;
    bool m_updating = false;
    ClipFields m_clip;
    AnimAtlasesPanel* m_atlases;
    QGroupBox *m_nodeBox, *m_playBox, *m_easeBox;
    QLineEdit* m_name;
    QComboBox* m_sprite;
    QString m_spriteShown;   // what refresh() put in m_sprite
    QCheckBox* m_atlasPivot;
    QDoubleSpinBox *m_pivotX, *m_pivotY;
    QSpinBox* m_order;
    QComboBox* m_blend;
    QCheckBox *m_inheritColor, *m_restVisible;
    QComboBox* m_playback;   // once + stay / once + hide / loop
    QLineEdit* m_skin;       // skin bones, comma separated (docs/34_MESH_ANIMATION.md)
    QLabel* m_skinInfo;      // its mesh, bones and weights, or why it has none
    QWidget* m_bonesOutside;          // a warning + "Move Bones Under This Sprite" (bones the node does not carry)
    QLabel* m_bonesOutsideText;
    QSpinBox *m_meshCols, *m_meshRows;   // the node's mesh grid, cells across and down (re-cuts its mesh)
    QWidget* m_meshGridRow;
    QSpinBox *m_latticeCols, *m_latticeRows;   // the deform lattice's points across and down (docs/34 step 4)
    QLabel* m_playLabel;
    QDoubleSpinBox *m_pos[3], *m_rot[3], *m_scale[3], *m_color[4];   // x, y, z
    QLabel* m_spriteHere;
    QCheckBox* m_visibleHere;
    QToolButton* m_key[animed::kChannelCount] = {};
    QComboBox* m_ease[4] = {};    // pos, rot, scale, color
    QLabel* m_easeAt[4] = {};
};

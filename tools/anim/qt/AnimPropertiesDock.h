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
    QDoubleSpinBox* posXSpin() const { return m_posX; }
    QComboBox* spriteCombo() const { return m_sprite; }   // the rest sprite picker
    AnimAtlasesPanel* atlasesPanel() const { return m_atlases; }

private:
    void refresh();
    void refreshPlayhead();
    void commitChannel(animed::Channel c);
    QToolButton* makeKeyButton(animed::Channel c, QWidget* parent);

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
    QLabel* m_playLabel;
    QDoubleSpinBox *m_posX, *m_posY, *m_rot, *m_scaleX, *m_scaleY, *m_color[4];
    QLabel* m_spriteHere;
    QCheckBox* m_visibleHere;
    QToolButton* m_key[animed::kChannelCount] = {};
    QComboBox* m_ease[4] = {};    // pos, rot, scale, color
    QLabel* m_easeAt[4] = {};
};

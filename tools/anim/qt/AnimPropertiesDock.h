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
    explicit AnimPropertiesDock(AnimDocument* l_pDoc, QWidget* l_pParent = nullptr);

    // The key indicator button of a channel (selftest / tests).
    QToolButton* f_KeyButton(animed::Channel l_eC) const { return m_arrKey[int(l_eC)]; }
    QDoubleSpinBox* f_PosXSpin() const { return m_arrPos[0]; }
    QComboBox* f_SpriteCombo() const { return m_pSprite; }   // the rest sprite picker
    AnimAtlasesPanel* f_AtlasesPanel() const { return m_pAtlases; }

private:
    void f_Refresh();
    void f_RefreshPlayhead();
    void f_CommitChannel(animed::Channel l_eC);
    QToolButton* f_MakeKeyButton(animed::Channel l_eC, QWidget* l_pParent);

    QString f_SkinInfo(const toms::anim::Node& l_oN) const;   // the Skin row's note
public:
    QString f_SkinInfoText() const;   // the note as shown ("" when hidden; the selftest)
    bool f_BonesOutsideShown() const;   // the "bones outside" warning is up (the selftest)
    QSpinBox* f_MeshGridBox() const { return m_pMeshCols; }   // cells across (and m_meshRows down)
private:
    AnimDocument* m_pDoc;
    bool m_bUpdating = false;
    ClipFields m_oClip;
    AnimAtlasesPanel* m_pAtlases;
    QGroupBox *m_pNodeBox, *m_pPlayBox, *m_pEaseBox;
    QLineEdit* m_pName;
    QComboBox* m_pSprite;
    QString m_oSpriteShown;   // what refresh() put in m_sprite
    QCheckBox* m_pAtlasPivot;
    QDoubleSpinBox *m_pPivotX, *m_pPivotY;
    QSpinBox* m_pOrder;
    QComboBox* m_pBlend;
    QCheckBox *m_pInheritColor, *m_pRestVisible;
    QComboBox* m_pPlayback;   // once + stay / once + hide / loop
    QLineEdit* m_pSkin;       // skin bones, comma separated (docs/engine/16_MESH_ANIMATION.md)
    QLabel* m_pSkinInfo;      // its mesh, bones and weights, or why it has none
    QWidget* m_pBonesOutside;          // a warning + "Move Bones Under This Sprite" (bones the node does not carry)
    QLabel* m_pBonesOutsideText;
    QSpinBox *m_pMeshCols, *m_pMeshRows;   // the node's mesh grid, cells across and down (re-cuts its mesh)
    QWidget* m_pMeshGridRow;
    QSpinBox *m_pLatticeCols, *m_pLatticeRows;   // the deform lattice's points across and down (docs/engine/16 step 4)
    // Effect and trail (docs/engine/18_NODE_EFFECTS.md): the shader its subtree is drawn through, its params at the
    // playhead (keyed like the other channels), its colour; the afterimages' count, gap, colour, blending, strength.
    QGroupBox* m_pFxBox;
    QComboBox* m_pFxShader;
    QDoubleSpinBox *m_arrFxParam[4], *m_arrFxColor[4];
    QLabel* m_arrFxParamNames[4];
    QSpinBox* m_pTrailCount;
    QDoubleSpinBox *m_pTrailGap, *m_arrTrailColor[4], *m_pTrailStrength;
    QCheckBox* m_pTrailAdd;
    void f_RefreshEffect(const toms::anim::Node* l_pN);
    QLabel* m_pPlayLabel;
    QDoubleSpinBox *m_arrPos[3], *m_arrRot[3], *m_arrScale[3], *m_arrColor[4];   // x, y, z
    QLabel* m_pSpriteHere;
    QCheckBox* m_pVisibleHere;
    QToolButton* m_arrKey[animed::g_iChannelCount] = {};
    QComboBox* m_arrEase[4] = {};    // pos, rot, scale, color
    QLabel* m_arrEaseAt[4] = {};
};

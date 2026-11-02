#include "AnimPropertiesDock.h"

#include "AnimAtlasesPanel.h"
#include "BezierDialog.h"
#include "Icons.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

using namespace animed;
using toms::anim::Node;

namespace {

QString f_Qs(const std::string& l_strS) { return QString::fromStdString(l_strS); }

QDoubleSpinBox* f_Dspin(double l_dLo, double l_dHi, int l_iDecimals, double l_dStep, QWidget* l_pParent)
{
    auto* l_pS = new QDoubleSpinBox(l_pParent);
    l_pS->setRange(l_dLo, l_dHi);
    l_pS->setDecimals(l_iDecimals);
    l_pS->setSingleStep(l_dStep);
    l_pS->setKeyboardTracking(false);
    l_pS->setMinimumWidth(52);
    l_pS->setAccelerated(true);
    return l_pS;
}

QWidget* f_Row(QWidget* l_pParent, const QList<QPair<QString, QWidget*>>& l_oFields, QWidget* l_pTrailing = nullptr)
{
    auto* l_pW = new QWidget(l_pParent);
    auto* l_pL = new QHBoxLayout(l_pW);
    l_pL->setContentsMargins(0, 0, 0, 0);
    l_pL->setSpacing(3);
    for (const auto& l_pairF : l_oFields) {
        if (!l_pairF.first.isEmpty()) {
            auto* l_pLab = new QLabel(l_pairF.first, l_pW);
            l_pLab->setForegroundRole(QPalette::PlaceholderText);
            l_pL->addWidget(l_pLab);
        }
        l_pL->addWidget(l_pairF.second, 1);
    }
    if (l_pTrailing) l_pL->addWidget(l_pTrailing);
    return l_pW;
}

}  // namespace

AnimPropertiesDock::AnimPropertiesDock(AnimDocument* l_pDoc, QWidget* l_pParent)
    : QDockWidget(tr("Properties"), l_pParent)
    , m_pDoc(l_pDoc)
{
    setObjectName(QStringLiteral("AnimPropertiesDock"));
    auto* l_pScroll = new QScrollArea(this);
    l_pScroll->setWidgetResizable(true);
    l_pScroll->setFrameShape(QFrame::NoFrame);
    auto* l_pBody = new QWidget(l_pScroll);
    auto* l_pLayout = new QVBoxLayout(l_pBody);
    l_pLayout->setContentsMargins(6, 6, 6, 6);
    l_pLayout->setSpacing(8);

    // ---- clip ----
    auto* l_pClipBox = new QGroupBox(tr("Clip"), l_pBody);
    m_oClip = ClipFields::f_Create(l_pDoc, l_pClipBox, &m_bUpdating);
    auto* l_pCf = new QFormLayout(l_pClipBox);
    l_pCf->addRow(tr("Length"), m_oClip.m_pLength);
    l_pCf->addRow(tr("Plays"), m_oClip.m_pPlayCount);
    l_pCf->addRow(QString(), m_oClip.m_pStay);
    l_pLayout->addWidget(l_pClipBox);

    // ---- atlases (the file's, in lookup order) ----
    auto* l_pAtlasBox = new QGroupBox(tr("Atlases"), l_pBody);
    m_pAtlases = new AnimAtlasesPanel(l_pDoc, l_pAtlasBox);
    auto* l_pAl = new QVBoxLayout(l_pAtlasBox);
    l_pAl->addWidget(m_pAtlases);
    l_pLayout->addWidget(l_pAtlasBox);

    // ---- node (rest) ----
    m_pNodeBox = new QGroupBox(tr("Node"), l_pBody);
    auto* l_pNf = new QFormLayout(m_pNodeBox);
    m_pName = new QLineEdit(m_pNodeBox);
    m_pSprite = new QComboBox(m_pNodeBox);
    m_pSprite->setEditable(true);
    m_pSprite->setInsertPolicy(QComboBox::NoInsert);
    m_pSprite->setMaxVisibleItems(24);
    m_pSprite->setToolTip(tr("The rest sprite, as name (atlas id): picking one stores its atlas (\"id:name\"). "
                            "(none) = a group that only moves its children. Type to filter."));
    m_pAtlasPivot = new QCheckBox(tr("Atlas pivot"), m_pNodeBox);
    m_pAtlasPivot->setToolTip(tr("Use the sprite's pivot from the atlas (x_pivot, else the centre)"));
    m_pPivotX = f_Dspin(-4, 5, 3, 0.05, m_pNodeBox);
    m_pPivotY = f_Dspin(-4, 5, 3, 0.05, m_pNodeBox);
    m_pOrder = new QSpinBox(m_pNodeBox);
    m_pOrder->setRange(-999, 999);
    m_pOrder->setKeyboardTracking(false);
    m_pOrder->setToolTip(tr("Among siblings; < 0 draws before (under) the parent's own sprite"));
    m_pBlend = new QComboBox(m_pNodeBox);
    m_pBlend->addItems({tr("normal"), tr("add")});
    m_pInheritColor = new QCheckBox(tr("Inherit colour"), m_pNodeBox);
    m_pRestVisible = new QCheckBox(tr("Visible (rest)"), m_pNodeBox);
    m_pPlayback = new QComboBox(m_pNodeBox);
    m_pPlayback->addItems({tr("Once, stay at the last key"), tr("Once, then hide"), tr("Loop")});
    m_pPlayback->setToolTip(tr("How this node plays its keys (with its children's: first..last key of the subtree).\n"
                              "Once, stay: holds its last pose (drawn, nothing updates).\n"
                              "Once, then hide: hidden with its children after its last key.\n"
                              "Loop: plays first..last key again, forever, also after the clip has ended."));
    m_pSkin = new QLineEdit(m_pNodeBox);
    m_pSkin->setPlaceholderText(tr("(not skinned)"));
    m_pSkin->setToolTip(tr("Bones that bend this sprite's mesh: node names of this clip, comma separated. Empty = not skinned (drawn as a quad). The weights are made from the rest pose; its mesh is cut from the sprite and saved with the .anim (<name>.mesh.json). docs/engine/16_MESH_ANIMATION.md"));
    l_pNf->addRow(tr("Name"), m_pName);
    l_pNf->addRow(tr("Sprite"), m_pSprite);
    l_pNf->addRow(tr("Playback"), m_pPlayback);
    l_pNf->addRow(tr("Pivot"), m_pAtlasPivot);
    l_pNf->addRow(QString(), f_Row(m_pNodeBox, {{QStringLiteral("x"), m_pPivotX}, {QStringLiteral("y"), m_pPivotY}}));
    l_pNf->addRow(tr("Order"), m_pOrder);
    l_pNf->addRow(tr("Blend"), m_pBlend);
    l_pNf->addRow(QString(), m_pInheritColor);
    l_pNf->addRow(QString(), m_pRestVisible);
    l_pNf->addRow(tr("Skin"), m_pSkin);
    m_pSkinInfo = new QLabel(m_pNodeBox);
    m_pSkinInfo->setWordWrap(true);
    m_pSkinInfo->setForegroundRole(QPalette::PlaceholderText);
    l_pNf->addRow(QString(), m_pSkinInfo);
    {   // Bones the node does not carry: moving it slides the mesh off them. One click puts them under it.
        m_pBonesOutside = new QWidget(m_pNodeBox);
        auto* l_pV = new QVBoxLayout(m_pBonesOutside);
        l_pV->setContentsMargins(0, 0, 0, 0);
        m_pBonesOutsideText = new QLabel(m_pBonesOutside);
        m_pBonesOutsideText->setWordWrap(true);
        QPalette l_oWarn = m_pBonesOutsideText->palette();
        l_oWarn.setColor(QPalette::WindowText, QColor(0xe0, 0xa0, 0x30));
        m_pBonesOutsideText->setPalette(l_oWarn);
        auto* l_pFix = new QPushButton(tr("Move Bones Under This Sprite"), m_pBonesOutside);
        l_pFix->setToolTip(tr("The bones become children of this node, nothing moving on screen: then moving the sprite "
                           "(Object mode) moves its mesh and bones together. One undo step"));
        l_pV->addWidget(m_pBonesOutsideText);
        l_pV->addWidget(l_pFix);
        connect(l_pFix, &QPushButton::clicked, this, [this] { m_pDoc->f_ParentBonesUnder(m_pDoc->f_SelectedPath()); });
        l_pNf->addRow(QString(), m_pBonesOutside);
    }
    m_pMeshCols = new QSpinBox(m_pNodeBox);
    m_pMeshRows = new QSpinBox(m_pNodeBox);
    for (QSpinBox* l_pB : {m_pMeshCols, m_pMeshRows}) {
        l_pB->setRange(1, toms::anim::g_iMaxMeshGrid);
        l_pB->setKeyboardTracking(false);
        l_pB->setToolTip(tr("This node's mesh: its sprite cut from its alpha on cells across x down (more cells bend more "
                         "smoothly). Kept in the node; the mesh is saved with the .anim (<name>.mesh.json)"));
        connect(l_pB, &QSpinBox::valueChanged, this, [this] {
            if (!m_bUpdating) m_pDoc->f_SetMeshGrid(m_pMeshCols->value(), m_pMeshRows->value());
        });
    }
    m_pMeshGridRow = f_Row(m_pNodeBox, {{QStringLiteral("x"), m_pMeshCols}, {QStringLiteral("y"), m_pMeshRows}});
    l_pNf->addRow(tr("Mesh grid"), m_pMeshGridRow);
    m_pLatticeCols = new QSpinBox(m_pNodeBox);
    m_pLatticeRows = new QSpinBox(m_pNodeBox);
    for (QSpinBox* l_pB : {m_pLatticeCols, m_pLatticeRows}) {
        l_pB->setRange(2, toms::anim::g_iMaxLattice);
        l_pB->setKeyboardTracking(false);
        l_pB->setToolTip(tr("The deform lattice: points across and down over the sprite (default 4 x 4). View > Lattice Mode "
                         "(L) shows it; drag a point to key the deform at the playhead"));
        connect(l_pB, &QSpinBox::valueChanged, this, [this] {
            if (!m_bUpdating) m_pDoc->f_SetLatticeSize(m_pLatticeCols->value(), m_pLatticeRows->value());
        });
    }
    l_pNf->addRow(tr("Lattice"), f_Row(m_pNodeBox, {{QStringLiteral("x"), m_pLatticeCols}, {QStringLiteral("y"), m_pLatticeRows}}));
    l_pLayout->addWidget(m_pNodeBox);

    // ---- effect and trail (docs/engine/18_NODE_EFFECTS.md) ----
    m_pFxBox = new QGroupBox(tr("Effect"), l_pBody);
    auto* l_pFf = new QFormLayout(m_pFxBox);
    m_pFxShader = new QComboBox(m_pFxBox);
    m_pFxShader->addItems({tr("(none)"), tr("Group"), tr("Motion blur"), tr("Glow (HDR)"), tr("Flash"), tr("Outline")});
    m_pFxShader->setToolTip(tr("This node and its whole subtree are drawn into a picture of their own, then drawn through "
                              "this shader. Nodes outside the subtree are not touched; effects nest. "
                              "docs/engine/18_NODE_EFFECTS.md"));
    connect(m_pFxShader, &QComboBox::currentIndexChanged, this, [this](int l_iI) {
        if (!m_bUpdating) m_pDoc->f_EditSelected(tr("Effect"), [l_iI](Node& l_oN) { l_oN.m_oFx.m_eShader = toms::anim::FxShader(l_iI); });
    });
    l_pFf->addRow(tr("Shader"), m_pFxShader);
    QWidget* l_pParams = new QWidget(m_pFxBox);
    auto* l_pPg = new QGridLayout(l_pParams);
    l_pPg->setContentsMargins(0, 0, 0, 0);
    for (int l_iI = 0; l_iI < 4; l_iI++) {
        m_arrFxParamNames[l_iI] = new QLabel(l_pParams);
        m_arrFxParam[l_iI] = f_Dspin(-9999, 9999, 3, 0.1, l_pParams);
        l_pPg->addWidget(m_arrFxParamNames[l_iI], l_iI / 2, (l_iI % 2) * 2);
        l_pPg->addWidget(m_arrFxParam[l_iI], l_iI / 2, (l_iI % 2) * 2 + 1);
        connect(m_arrFxParam[l_iI], &QDoubleSpinBox::valueChanged, this, [this] {
            if (m_bUpdating || !m_pDoc->f_SelectedNode()) return;
            Value l_oV;
            l_oV.m_v4V = {float(m_arrFxParam[0]->value()), float(m_arrFxParam[1]->value()), float(m_arrFxParam[2]->value()), float(m_arrFxParam[3]->value())};
            m_pDoc->f_SetChannel(Channel::Fx, l_oV, QStringLiteral("prop.fx.") + f_PathToString(m_pDoc->f_SelectedPath()));
        });
    }
    l_pFf->addRow(tr("Params"), f_Row(m_pFxBox, {{QString(), l_pParams}}, f_MakeKeyButton(Channel::Fx, m_pFxBox)));
    for (int l_iI = 0; l_iI < 4; l_iI++) {
        m_arrFxColor[l_iI] = f_Dspin(0, 8, 3, 0.05, m_pFxBox);
        connect(m_arrFxColor[l_iI], &QDoubleSpinBox::valueChanged, this, [this] {
            if (m_bUpdating) return;
            const glm::vec4 l_v4C(float(m_arrFxColor[0]->value()), float(m_arrFxColor[1]->value()), float(m_arrFxColor[2]->value()), float(m_arrFxColor[3]->value()));
            m_pDoc->f_EditSelected(tr("Effect colour"), [l_v4C](Node& l_oN) { l_oN.m_oFx.m_v4Color = l_v4C; },
                                QStringLiteral("prop.fxcolor") + f_PathToString(m_pDoc->f_SelectedPath()));
        });
    }
    l_pFf->addRow(tr("Colour"), f_Row(m_pFxBox, {{QStringLiteral("r"), m_arrFxColor[0]}, {QStringLiteral("g"), m_arrFxColor[1]},
                                           {QStringLiteral("b"), m_arrFxColor[2]}, {QStringLiteral("a"), m_arrFxColor[3]}}));
    m_pTrailCount = new QSpinBox(m_pFxBox);
    m_pTrailCount->setRange(0, toms::anim::g_iMaxTrail);
    m_pTrailCount->setKeyboardTracking(false);
    m_pTrailCount->setToolTip(tr("Afterimages: copies of this subtree as it was a little earlier, fading, drawn behind it. 0 = none"));
    m_pTrailGap = f_Dspin(0.005, 1, 3, 0.005, m_pFxBox);
    m_pTrailGap->setToolTip(tr("Seconds between two afterimages"));
    m_pTrailAdd = new QCheckBox(tr("Add"), m_pFxBox);
    m_pTrailAdd->setToolTip(tr("Additive afterimages (light) instead of normal blending"));
    auto l_fnTrailEdit = [this] {
        if (m_bUpdating) return;
        toms::anim::NodeTrail l_oT;
        l_oT.m_iCount = m_pTrailCount->value();
        l_oT.m_fGap = float(m_pTrailGap->value());
        l_oT.m_v4Color = {float(m_arrTrailColor[0]->value()), float(m_arrTrailColor[1]->value()), float(m_arrTrailColor[2]->value()), float(m_arrTrailColor[3]->value())};
        l_oT.m_bAdditive = m_pTrailAdd->isChecked();
        m_pDoc->f_EditSelected(tr("Trail"), [l_oT](Node& l_oN) { l_oN.m_oTrail = l_oT; }, QStringLiteral("prop.trail") + f_PathToString(m_pDoc->f_SelectedPath()));
    };
    for (int l_iI = 0; l_iI < 4; l_iI++) {
        m_arrTrailColor[l_iI] = f_Dspin(0, 1, 3, 0.05, m_pFxBox);
        connect(m_arrTrailColor[l_iI], &QDoubleSpinBox::valueChanged, this, l_fnTrailEdit);
    }
    connect(m_pTrailCount, &QSpinBox::valueChanged, this, l_fnTrailEdit);
    connect(m_pTrailGap, &QDoubleSpinBox::valueChanged, this, l_fnTrailEdit);
    connect(m_pTrailAdd, &QCheckBox::toggled, this, l_fnTrailEdit);
    l_pFf->addRow(tr("Trail"), f_Row(m_pFxBox, {{tr("count"), m_pTrailCount}, {tr("gap"), m_pTrailGap}}, m_pTrailAdd));
    l_pFf->addRow(QString(), f_Row(m_pFxBox, {{QStringLiteral("r"), m_arrTrailColor[0]}, {QStringLiteral("g"), m_arrTrailColor[1]},
                                         {QStringLiteral("b"), m_arrTrailColor[2]}, {QStringLiteral("a"), m_arrTrailColor[3]}}));
    m_pTrailStrength = f_Dspin(0, 1, 3, 0.05, m_pFxBox);
    m_pTrailStrength->setToolTip(tr("The afterimages' strength at the playhead (0..1; keyed like the other channels)"));
    connect(m_pTrailStrength, &QDoubleSpinBox::valueChanged, this, [this] {
        if (m_bUpdating || !m_pDoc->f_SelectedNode()) return;
        Value l_oV;
        l_oV.m_v4V = {float(m_pTrailStrength->value()), 0, 0, 0};
        m_pDoc->f_SetChannel(Channel::Trail, l_oV, QStringLiteral("prop.trailkey.") + f_PathToString(m_pDoc->f_SelectedPath()));
    });
    l_pFf->addRow(tr("Strength"), f_Row(m_pFxBox, {{QString(), m_pTrailStrength}}, f_MakeKeyButton(Channel::Trail, m_pFxBox)));
    l_pLayout->addWidget(m_pFxBox);

    // ---- at the playhead ----
    m_pPlayBox = new QGroupBox(tr("At playhead"), l_pBody);
    auto* l_pPf = new QFormLayout(m_pPlayBox);
    m_pPlayLabel = new QLabel(m_pPlayBox);
    m_pPlayLabel->setForegroundRole(QPalette::PlaceholderText);
    for (int l_iI = 0; l_iI < 3; l_iI++) {
        m_arrPos[l_iI] = f_Dspin(-99999, 99999, 2, 1, m_pPlayBox);
        m_arrRot[l_iI] = f_Dspin(-99999, 99999, 2, 5, m_pPlayBox);
        m_arrScale[l_iI] = f_Dspin(-999, 999, 3, 0.05, m_pPlayBox);
    }
    m_arrPos[2]->setToolTip(tr("z: depth (into the screen). The 2D view does not move with it; a 3D camera would"));
    m_arrRot[0]->setToolTip(tr("x: tilt about the horizontal axis (the 2D view foreshortens the sprite)"));
    m_arrRot[1]->setToolTip(tr("y: tilt about the vertical axis (the 2D view foreshortens the sprite)"));
    m_arrRot[2]->setToolTip(tr("z: turn in the screen plane, clockwise (the viewport's rotate handle)"));
    m_arrScale[2]->setToolTip(tr("z: depth scale (shows with x / y tilts and a 3D camera)"));
    for (int l_iI = 0; l_iI < 4; l_iI++) m_arrColor[l_iI] = f_Dspin(0, 1, 3, 0.05, m_pPlayBox);
    m_pSpriteHere = new QLabel(m_pPlayBox);
    m_pSpriteHere->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_pVisibleHere = new QCheckBox(m_pPlayBox);
    l_pPf->addRow(m_pPlayLabel);
    auto l_fnXyz = [](QDoubleSpinBox* const l_arrS[3]) -> QList<QPair<QString, QWidget*>> {
        return {{QStringLiteral("x"), l_arrS[0]}, {QStringLiteral("y"), l_arrS[1]}, {QStringLiteral("z"), l_arrS[2]}};
    };
    l_pPf->addRow(tr("Pos"), f_Row(m_pPlayBox, l_fnXyz(m_arrPos), f_MakeKeyButton(Channel::Pos, m_pPlayBox)));
    l_pPf->addRow(tr("Rot"), f_Row(m_pPlayBox, l_fnXyz(m_arrRot), f_MakeKeyButton(Channel::Rot, m_pPlayBox)));
    l_pPf->addRow(tr("Scale"), f_Row(m_pPlayBox, l_fnXyz(m_arrScale), f_MakeKeyButton(Channel::Scale, m_pPlayBox)));
    l_pPf->addRow(tr("Colour"), f_Row(m_pPlayBox, {{QString(), m_arrColor[0]}, {QString(), m_arrColor[1]}, {QString(), m_arrColor[2]}},
                                 f_MakeKeyButton(Channel::Color, m_pPlayBox)));
    l_pPf->addRow(tr("Alpha"), f_Row(m_pPlayBox, {{QString(), m_arrColor[3]}}));
    l_pPf->addRow(tr("Sprite"), f_Row(m_pPlayBox, {{QString(), m_pSpriteHere}}, f_MakeKeyButton(Channel::Sprite, m_pPlayBox)));
    l_pPf->addRow(tr("Visible"), f_Row(m_pPlayBox, {{QString(), m_pVisibleHere}}, f_MakeKeyButton(Channel::Visible, m_pPlayBox)));
    m_arrColor[0]->setToolTip(tr("red"));
    m_arrColor[1]->setToolTip(tr("green"));
    m_arrColor[2]->setToolTip(tr("blue"));
    l_pLayout->addWidget(m_pPlayBox);

    // ---- ease of the key at / before the playhead ----
    m_pEaseBox = new QGroupBox(tr("Ease (key at / before the playhead)"), l_pBody);
    auto* l_pEg = new QGridLayout(m_pEaseBox);
    l_pEg->setColumnStretch(1, 1);
    for (int l_iI = 0; l_iI < 4; l_iI++) {
        const Channel l_eC = g_arrInterpolated[l_iI];
        auto* l_pLab = new QLabel(QLatin1String(f_ChannelName(l_eC)), m_pEaseBox);
        m_arrEase[l_iI] = new QComboBox(m_pEaseBox);
        EaseUi::f_Fill(m_arrEase[l_iI]);
        m_arrEaseAt[l_iI] = new QLabel(m_pEaseBox);
        m_arrEaseAt[l_iI]->setForegroundRole(QPalette::PlaceholderText);
        m_arrEaseAt[l_iI]->setMinimumWidth(52);
        l_pEg->addWidget(l_pLab, l_iI, 0);
        l_pEg->addWidget(m_arrEase[l_iI], l_iI, 1);
        l_pEg->addWidget(m_arrEaseAt[l_iI], l_iI, 2);
        connect(m_arrEase[l_iI], &QComboBox::activated, this, [this, l_iI, l_eC](int l_iIndex) {
            const Node* l_pN = m_pDoc->f_SelectedNode();
            if (!l_pN) return;
            toms::anim::Ease l_oCur, l_oChosen;
            if (!f_EaseAtOrBefore(*l_pN, l_eC, m_pDoc->f_Time(), l_oCur) && f_KeyCount(*l_pN, l_eC) > 0) f_EaseAt(*l_pN, l_eC, f_KeyTimes(*l_pN, l_eC).front(), l_oCur);
            if (EaseUi::f_Picked(m_arrEase[l_iI], l_iIndex, l_oCur, this, l_oChosen)) m_pDoc->f_SetEaseHere(l_eC, l_oChosen);
            f_RefreshPlayhead();
        });
    }
    l_pLayout->addWidget(m_pEaseBox);
    l_pLayout->addStretch(1);
    l_pScroll->setWidget(l_pBody);
    setWidget(l_pScroll);

    // ---- wiring ----
    connect(m_pName, &QLineEdit::editingFinished, this, [this] {
        if (!m_bUpdating && m_pDoc->f_SelectedNode() && m_pName->text() != f_Qs(m_pDoc->f_SelectedNode()->m_strName))
            m_pDoc->f_RenameNode(m_pDoc->f_SelectedPath(), m_pName->text());
    });
    auto l_fnCommitSprite = [this] {
        if (m_bUpdating || !m_pDoc->f_SelectedNode()) return;
        const QString l_oText = m_pSprite->currentText().trimmed();
        if (l_oText == m_oSpriteShown) return;   // untouched (a bare legacy name stays as it is)
        const std::string l_strS = m_pDoc->f_SpriteFromText(l_oText);
        if (l_strS == m_pDoc->f_SelectedNode()->m_strSprite) return;
        m_pDoc->f_EditSelected(tr("Set sprite"), [l_strS](Node& l_oN) { l_oN.m_strSprite = l_strS; });
    };
    connect(m_pSprite, &QComboBox::activated, this, l_fnCommitSprite);
    connect(m_pSprite->lineEdit(), &QLineEdit::editingFinished, this, l_fnCommitSprite);
    connect(m_pAtlasPivot, &QCheckBox::toggled, this, [this](bool l_bAtlas) {
        if (m_bUpdating) return;
        glm::vec2 l_v2Start(0.5f, 0.5f);
        if (const Node* l_pN = m_pDoc->f_SelectedNode())
            if (const toms::AtlasRegion* l_pR = m_pDoc->f_FindSprite(f_SpriteAt(*l_pN, m_pDoc->f_Time()))) l_v2Start = {l_pR->m_arrPivot[0], l_pR->m_arrPivot[1]};
        m_pDoc->f_EditSelected(tr("Pivot"), [l_bAtlas, l_v2Start](Node& l_oN) {
            l_oN.m_bHasPivot = !l_bAtlas;
            if (!l_bAtlas) l_oN.m_v2Pivot = l_v2Start;
        });
    });
    auto l_fnCommitPivot = [this] {
        if (m_bUpdating) return;
        const glm::vec2 l_v2P(float(m_pPivotX->value()), float(m_pPivotY->value()));
        m_pDoc->f_EditSelected(tr("Pivot"), [l_v2P](Node& l_oN) {
            l_oN.m_bHasPivot = true;
            l_oN.m_v2Pivot = l_v2P;
        }, QStringLiteral("prop.pivot") + f_PathToString(m_pDoc->f_SelectedPath()));
    };
    connect(m_pPivotX, &QDoubleSpinBox::valueChanged, this, l_fnCommitPivot);
    connect(m_pPivotY, &QDoubleSpinBox::valueChanged, this, l_fnCommitPivot);
    connect(m_pOrder, &QSpinBox::valueChanged, this, [this](int l_iV) {
        if (!m_bUpdating) m_pDoc->f_EditSelected(tr("Order"), [l_iV](Node& l_oN) { l_oN.m_iOrder = l_iV; }, QStringLiteral("prop.order") + f_PathToString(m_pDoc->f_SelectedPath()));
    });
    connect(m_pBlend, &QComboBox::activated, this, [this](int l_iI) {
        if (!m_bUpdating) m_pDoc->f_EditSelected(tr("Blend"), [l_iI](Node& l_oN) { l_oN.m_eBlend = l_iI == 1 ? toms::anim::Blend::Add : toms::anim::Blend::Normal; });
    });
    connect(m_pSkin, &QLineEdit::editingFinished, this, [this] {
        if (m_bUpdating) return;
        std::vector<std::string> l_vBones;
        for (const QString& l_oB : m_pSkin->text().split(QLatin1Char(','), Qt::SkipEmptyParts))
            if (!l_oB.trimmed().isEmpty()) l_vBones.push_back(l_oB.trimmed().toStdString());
        m_pDoc->f_EditSelected(tr("Skin"), [l_vBones](Node& l_oN) {
            if (l_oN.m_vSkinBones != l_vBones) l_oN.m_vSkinWeights.clear();   // other bones: the weights are made again
            l_oN.m_vSkinBones = l_vBones;
        });
    });
    connect(m_pInheritColor, &QCheckBox::toggled, this, [this](bool l_bOn) {
        if (!m_bUpdating) m_pDoc->f_EditSelected(tr("Inherit colour"), [l_bOn](Node& l_oN) { l_oN.m_bInheritColor = l_bOn; });
    });
    connect(m_pRestVisible, &QCheckBox::toggled, this, [this](bool l_bOn) {
        if (!m_bUpdating) m_pDoc->f_EditSelected(tr("Visible"), [l_bOn](Node& l_oN) { l_oN.m_bVisible = l_bOn; });
    });
    connect(m_pPlayback, &QComboBox::activated, this, [this](int l_iI) {
        if (!m_bUpdating) m_pDoc->f_EditSelected(tr("Playback"), [l_iI](Node& l_oN) {
            l_oN.m_bLoop = l_iI == 2;
            l_oN.m_bStayAtLastFrame = l_iI != 1;
        });
    });
    for (int l_iI = 0; l_iI < 3; l_iI++) {
        connect(m_arrPos[l_iI], &QDoubleSpinBox::valueChanged, this, [this] { f_CommitChannel(Channel::Pos); });
        connect(m_arrRot[l_iI], &QDoubleSpinBox::valueChanged, this, [this] { f_CommitChannel(Channel::Rot); });
        connect(m_arrScale[l_iI], &QDoubleSpinBox::valueChanged, this, [this] { f_CommitChannel(Channel::Scale); });
    }
    for (QDoubleSpinBox* l_pS : m_arrColor) connect(l_pS, &QDoubleSpinBox::valueChanged, this, [this] { f_CommitChannel(Channel::Color); });
    connect(m_pVisibleHere, &QCheckBox::toggled, this, [this] { f_CommitChannel(Channel::Visible); });

    connect(l_pDoc, &AnimDocument::f_FileChanged, this, &AnimPropertiesDock::f_Refresh);
    connect(l_pDoc, &AnimDocument::f_SelectionChanged, this, &AnimPropertiesDock::f_Refresh);
    connect(l_pDoc, &AnimDocument::f_ClipChanged, this, &AnimPropertiesDock::f_Refresh);
    connect(l_pDoc, &AnimDocument::f_TimeChanged, this, &AnimPropertiesDock::f_RefreshPlayhead);
    connect(l_pDoc, &AnimDocument::f_AutoKeyChanged, this, &AnimPropertiesDock::f_RefreshPlayhead);
    connect(l_pDoc, &AnimDocument::f_AtlasChanged, this, [this] {
        const QSignalBlocker l_oB(m_pSprite);
        m_pSprite->clear();
        m_pSprite->addItem(tr("(none)"));
        m_pSprite->addItems(m_pDoc->f_SpriteChoices());
        auto* l_pC = new QCompleter(m_pDoc->f_SpriteChoices(), m_pSprite);
        l_pC->setFilterMode(Qt::MatchContains);
        l_pC->setCaseSensitivity(Qt::CaseInsensitive);
        m_pSprite->setCompleter(l_pC);
        f_Refresh();
    });
    f_Refresh();
}

QToolButton* AnimPropertiesDock::f_MakeKeyButton(Channel l_eC, QWidget* l_pParent)
{
    auto* l_pB = new QToolButton(l_pParent);
    l_pB->setAutoRaise(true);
    l_pB->setIconSize(QSize(16, 16));
    m_arrKey[int(l_eC)] = l_pB;
    connect(l_pB, &QToolButton::clicked, this, [this, l_eC] {
        const Node* l_pN = m_pDoc->f_SelectedNode();
        if (!l_pN) return;
        if (f_HasKeyAt(*l_pN, l_eC, m_pDoc->f_Time())) m_pDoc->f_RemoveKeyHere(l_eC);
        else m_pDoc->f_AddKeyHere(l_eC);
    });
    return l_pB;
}

void AnimPropertiesDock::f_CommitChannel(Channel l_eC)
{
    if (m_bUpdating || !m_pDoc->f_SelectedNode()) return;
    Value l_oV;
    switch (l_eC) {
    case Channel::Pos: l_oV.m_v4V = {float(m_arrPos[0]->value()), float(m_arrPos[1]->value()), float(m_arrPos[2]->value()), 0}; break;
    case Channel::Rot: l_oV.m_v4V = {float(m_arrRot[0]->value()), float(m_arrRot[1]->value()), float(m_arrRot[2]->value()), 0}; break;
    case Channel::Scale: l_oV.m_v4V = {float(m_arrScale[0]->value()), float(m_arrScale[1]->value()), float(m_arrScale[2]->value()), 0}; break;
    case Channel::Color:
        l_oV.m_v4V = {float(m_arrColor[0]->value()), float(m_arrColor[1]->value()), float(m_arrColor[2]->value()), float(m_arrColor[3]->value())};
        break;
    case Channel::Visible: l_oV.m_bB = m_pVisibleHere->isChecked(); break;
    default: return;
    }
    m_pDoc->f_SetChannel(l_eC, l_oV, QStringLiteral("prop.%1.%2").arg(QLatin1String(f_ChannelName(l_eC)), f_PathToString(m_pDoc->f_SelectedPath())));
}

// The effect's params and the trail's strength at the playhead; the params' names follow the shader.
void AnimPropertiesDock::f_RefreshEffect(const Node* l_pN)
{
    m_pFxBox->setEnabled(l_pN != nullptr);
    const int l_iShader = l_pN ? int(l_pN->m_oFx.m_eShader) : 0;
    static const char* l_arrNames[6][4] = {{"", "", "", ""},
                                       {"", "", "", ""},
                                       {QT_TR_NOOP("dx"), QT_TR_NOOP("dy"), QT_TR_NOOP("auto"), ""},
                                       {QT_TR_NOOP("intensity"), QT_TR_NOOP("radius"), QT_TR_NOOP("threshold"), QT_TR_NOOP("bloom")},
                                       {QT_TR_NOOP("amount"), "", "", ""},
                                       {QT_TR_NOOP("width"), QT_TR_NOOP("intensity"), "", ""}};
    for (int l_iI = 0; l_iI < 4; l_iI++) {
        const bool l_bUsed = l_arrNames[l_iShader][l_iI][0] != 0;
        m_arrFxParamNames[l_iI]->setText(l_bUsed ? tr(l_arrNames[l_iShader][l_iI]) : QString());
        m_arrFxParamNames[l_iI]->setVisible(l_bUsed);
        m_arrFxParam[l_iI]->setVisible(l_bUsed);
    }
    if (!l_pN) return;
    const glm::vec4 l_v4P = toms::anim::f_FxParamsAt(*l_pN, m_pDoc->f_Time());
    for (int l_iI = 0; l_iI < 4; l_iI++) {
        const QSignalBlocker l_oB(m_arrFxParam[l_iI]);
        m_arrFxParam[l_iI]->setValue(l_v4P[l_iI]);
    }
    const QSignalBlocker l_oB(m_pTrailStrength);
    m_pTrailStrength->setValue(toms::anim::f_TrailAt(*l_pN, m_pDoc->f_Time()));
}

bool AnimPropertiesDock::f_BonesOutsideShown() const { return m_pBonesOutside->isVisibleTo(m_pNodeBox); }

QString AnimPropertiesDock::f_SkinInfoText() const { return m_pSkinInfo->isVisibleTo(m_pNodeBox) ? m_pSkinInfo->text() : QString(); }

// What the selected node's skin is: its mesh, bones and weights (as the game bends it at the playhead), or why it
// draws as a plain quad.
QString AnimPropertiesDock::f_SkinInfo(const Node& l_oN) const
{
    const toms::anim::Clip* l_pClip = m_pDoc->f_Clip();
    if (!l_pClip) return QString();
    std::vector<toms::anim::NodePose> l_vPoses;
    toms::anim::f_Evaluate(*l_pClip, m_pDoc->f_Time(), l_vPoses);
    size_t l_nI = 0;
    while (l_nI < l_vPoses.size() && l_vPoses[l_nI].m_pNode != &l_oN) l_nI++;
    toms::anim::SkinView l_oSv;
    switch (toms::anim::f_SkinView(*l_pClip, l_vPoses, l_nI, m_pDoc->f_AtlasSet(), toms::anim::f_Placement(0, 0), &l_oSv)) {
    case toms::anim::SkinState::NotSkinned: return QString();
    case toms::anim::SkinState::NoSprite: return tr("No sprite to bend.");
    case toms::anim::SkinState::Rotated: return tr("The sprite is rotated in its atlas: it has no mesh and draws as a quad.");
    case toms::anim::SkinState::NoMesh:
        return tr("No mesh for this sprite (a child sprite, or no visible pixels): it draws as a quad.");
    case toms::anim::SkinState::Ok: break;
    }
    QString l_oText = tr("Mesh: %1 points, %2 triangles. %3 bone(s), weights %4. View > Show Skin shows them.")
                       .arg(l_oSv.m_vVertices.size()).arg(l_oSv.m_vTriangles.size() / 3).arg(l_oSv.m_vBones.size())
                       .arg(l_oSv.m_bAutoWeights ? tr("automatic") : tr("painted"));
    QStringList l_oMissing;
    for (size_t l_nB = 0; l_nB < l_oSv.m_vBones.size(); l_nB++)
        if (!l_oSv.m_vBoneFound[l_nB]) l_oMissing << QString::fromStdString(l_oSv.m_vBones[l_nB]);
    if (!l_oMissing.isEmpty()) l_oText += QLatin1Char(' ') + tr("Not a node of this clip: %1.").arg(l_oMissing.join(QStringLiteral(", ")));
    return l_oText;
}

void AnimPropertiesDock::f_Refresh()
{
    m_bUpdating = true;
    m_oClip.f_Refresh(m_pDoc);
    const Node* l_pN = m_pDoc->f_SelectedNode();
    m_pNodeBox->setEnabled(l_pN != nullptr);
    if (l_pN) {
        m_pNodeBox->setTitle(tr("Node: %1").arg(f_Qs(l_pN->m_strName)));
        m_pName->setText(f_Qs(l_pN->m_strName));
        const QSignalBlocker l_oB(m_pSprite);
        m_oSpriteShown = m_pDoc->f_SpriteDisplay(l_pN->m_strSprite);
        const int l_iIdx = m_pSprite->findText(m_oSpriteShown);
        if (l_iIdx >= 0) m_pSprite->setCurrentIndex(l_iIdx);
        m_pSprite->setEditText(m_oSpriteShown);
        m_pAtlasPivot->setChecked(!l_pN->m_bHasPivot);
        glm::vec2 l_v2Pv = l_pN->m_v2Pivot;
        if (!l_pN->m_bHasPivot)
            if (const toms::AtlasRegion* l_pR = m_pDoc->f_FindSprite(f_SpriteAt(*l_pN, m_pDoc->f_Time()))) l_v2Pv = {l_pR->m_arrPivot[0], l_pR->m_arrPivot[1]};
        m_pPivotX->setValue(l_v2Pv.x);
        m_pPivotY->setValue(l_v2Pv.y);
        m_pPivotX->setEnabled(l_pN->m_bHasPivot);
        m_pPivotY->setEnabled(l_pN->m_bHasPivot);
        m_pOrder->setValue(l_pN->m_iOrder);
        m_pBlend->setCurrentIndex(l_pN->m_eBlend == toms::anim::Blend::Add ? 1 : 0);
        m_pInheritColor->setChecked(l_pN->m_bInheritColor);
        m_pRestVisible->setChecked(l_pN->m_bVisible);
        m_pPlayback->setCurrentIndex(l_pN->m_bLoop ? 2 : l_pN->m_bStayAtLastFrame ? 0 : 1);
        QStringList l_oBones;
        for (const std::string& l_strB : l_pN->m_vSkinBones) l_oBones << f_Qs(l_strB);
        m_pSkin->setText(l_oBones.join(QStringLiteral(", ")));
        m_pSkinInfo->setVisible(l_pN->f_Skinned());
        if (l_pN->f_Skinned()) m_pSkinInfo->setText(f_SkinInfo(*l_pN));
        QStringList l_oOutside;
        if (l_pN->f_Skinned() && m_pDoc->f_Clip())
            for (const std::string& l_strB : animed::f_BonesOutside(m_pDoc->f_Clip()->m_oRoot, *l_pN)) l_oOutside << f_Qs(l_strB);
        m_pBonesOutside->setVisible(!l_oOutside.isEmpty());
        m_pBonesOutsideText->setText(tr("Bones outside this sprite: %1. Moving the sprite in Object mode slides its mesh off "
                                       "them; to move the whole object, put them under it.").arg(l_oOutside.join(QStringLiteral(", "))));
        int l_iCols = 0, l_iRows = 0;
        const bool l_bGrid = l_pN->f_UsesMesh() && m_pDoc->f_MeshGridOf(*l_pN, l_iCols, l_iRows);
        m_pMeshGridRow->setVisible(l_bGrid);
        if (auto* l_pForm = qobject_cast<QFormLayout*>(m_pNodeBox->layout()))
            if (QWidget* l_pLabel = l_pForm->labelForField(m_pMeshGridRow)) l_pLabel->setVisible(l_bGrid);
        if (l_bGrid) {
            m_pMeshCols->setValue(l_iCols);
            m_pMeshRows->setValue(l_iRows);
        }
        m_pLatticeCols->setValue(l_pN->f_LatticeCols());
        m_pLatticeRows->setValue(l_pN->f_LatticeRows());
        m_pFxShader->setCurrentIndex(int(l_pN->m_oFx.m_eShader));
        for (int l_iI = 0; l_iI < 4; l_iI++) m_arrFxColor[l_iI]->setValue(l_pN->m_oFx.m_v4Color[l_iI]);
        m_pTrailCount->setValue(l_pN->m_oTrail.m_iCount);
        m_pTrailGap->setValue(l_pN->m_oTrail.m_fGap);
        for (int l_iI = 0; l_iI < 4; l_iI++) m_arrTrailColor[l_iI]->setValue(l_pN->m_oTrail.m_v4Color[l_iI]);
        m_pTrailAdd->setChecked(l_pN->m_oTrail.m_bAdditive);
    } else {
        m_pNodeBox->setTitle(tr("Node"));
    }
    m_bUpdating = false;
    f_RefreshPlayhead();
}

void AnimPropertiesDock::f_RefreshPlayhead()
{
    m_bUpdating = true;
    const Node* l_pN = m_pDoc->f_SelectedNode();
    const float l_fT = m_pDoc->f_Time();
    m_pPlayBox->setEnabled(l_pN != nullptr);
    m_pEaseBox->setEnabled(l_pN != nullptr);
    m_pPlayLabel->setText(tr("t = %1 s   auto-key %2").arg(l_fT, 0, 'f', 3).arg(m_pDoc->f_AutoKey() ? tr("ON (edits write keys)") : tr("off (edits change rest values)")));
    f_RefreshEffect(l_pN);
    if (l_pN) {
        const glm::vec3 l_v3P = toms::anim::f_PositionAt(*l_pN, l_fT), l_v3R = toms::anim::f_RotationAt(*l_pN, l_fT), l_v3S = toms::anim::f_ScaleAt(*l_pN, l_fT);
        const glm::vec4 l_v4C = toms::anim::f_ColorAt(*l_pN, l_fT);
        for (int l_iI = 0; l_iI < 3; l_iI++) {
            const QSignalBlocker l_oBp(m_arrPos[l_iI]), l_oBr(m_arrRot[l_iI]), l_oBs(m_arrScale[l_iI]);
            m_arrPos[l_iI]->setValue(l_v3P[l_iI]);
            m_arrRot[l_iI]->setValue(l_v3R[l_iI]);
            m_arrScale[l_iI]->setValue(l_v3S[l_iI]);
        }
        for (int l_iI = 0; l_iI < 4; l_iI++) {
            const QSignalBlocker l_oB(m_arrColor[l_iI]);
            m_arrColor[l_iI]->setValue(l_v4C[l_iI]);
        }
        const std::string l_strSp = toms::anim::f_SpriteAt(*l_pN, l_fT);
        m_pSpriteHere->setText(m_pDoc->f_SpriteDisplay(l_strSp));
        m_pSpriteHere->setToolTip(l_strSp.empty() ? QString() : tr("Stored as \"%1\"").arg(f_Qs(l_strSp)));
        {
            const QSignalBlocker l_oB(m_pVisibleHere);
            m_pVisibleHere->setChecked(toms::anim::f_VisibleAt(*l_pN, l_fT));
        }
        for (Channel l_eCh : g_arrAllChannels) {
            QToolButton* l_pB = m_arrKey[int(l_eCh)];
            if (!l_pB) continue;
            const bool l_bHere = f_HasKeyAt(*l_pN, l_eCh, l_fT), l_bTrack = f_KeyCount(*l_pN, l_eCh) > 0;
            l_pB->setIcon(Icons::f_Icon(l_bHere ? Icons::Id::KeyOn : l_bTrack ? Icons::Id::KeyOff : Icons::Id::KeyNone));
            l_pB->setToolTip(l_bHere ? tr("Key at the playhead: click to remove it")
                               : l_bTrack ? tr("Animated, no key here: click to key the current value")
                                       : tr("No keys: click to start a track with the current value"));
        }
        for (int l_iI = 0; l_iI < 4; l_iI++) {
            const Channel l_eCh = g_arrInterpolated[l_iI];
            toms::anim::Ease l_oE;
            float l_fKt = 0;
            bool l_bHave = f_EaseAtOrBefore(*l_pN, l_eCh, l_fT, l_oE, &l_fKt);
            if (!l_bHave && f_KeyCount(*l_pN, l_eCh) > 0) {
                l_fKt = f_KeyTimes(*l_pN, l_eCh).front();
                l_bHave = f_EaseAt(*l_pN, l_eCh, l_fKt, l_oE);
            }
            EaseUi::f_Show(m_arrEase[l_iI], l_bHave ? &l_oE : nullptr);
            m_arrEaseAt[l_iI]->setText(l_bHave ? tr("@ %1").arg(l_fKt, 0, 'f', 3) : tr("no keys"));
        }
    }
    m_bUpdating = false;
}

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

QString qs(const std::string& s) { return QString::fromStdString(s); }

QDoubleSpinBox* dspin(double lo, double hi, int decimals, double step, QWidget* parent)
{
    auto* s = new QDoubleSpinBox(parent);
    s->setRange(lo, hi);
    s->setDecimals(decimals);
    s->setSingleStep(step);
    s->setKeyboardTracking(false);
    s->setMinimumWidth(52);
    s->setAccelerated(true);
    return s;
}

QWidget* row(QWidget* parent, const QList<QPair<QString, QWidget*>>& fields, QWidget* trailing = nullptr)
{
    auto* w = new QWidget(parent);
    auto* l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(3);
    for (const auto& f : fields) {
        if (!f.first.isEmpty()) {
            auto* lab = new QLabel(f.first, w);
            lab->setForegroundRole(QPalette::PlaceholderText);
            l->addWidget(lab);
        }
        l->addWidget(f.second, 1);
    }
    if (trailing) l->addWidget(trailing);
    return w;
}

}  // namespace

AnimPropertiesDock::AnimPropertiesDock(AnimDocument* doc, QWidget* parent)
    : QDockWidget(tr("Properties"), parent)
    , m_doc(doc)
{
    setObjectName(QStringLiteral("AnimPropertiesDock"));
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* body = new QWidget(scroll);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(8);

    // ---- clip ----
    auto* clipBox = new QGroupBox(tr("Clip"), body);
    m_clip = ClipFields::create(doc, clipBox, &m_updating);
    auto* cf = new QFormLayout(clipBox);
    cf->addRow(tr("Length"), m_clip.length);
    cf->addRow(tr("Plays"), m_clip.playCount);
    cf->addRow(QString(), m_clip.stay);
    layout->addWidget(clipBox);

    // ---- atlases (the file's, in lookup order) ----
    auto* atlasBox = new QGroupBox(tr("Atlases"), body);
    m_atlases = new AnimAtlasesPanel(doc, atlasBox);
    auto* al = new QVBoxLayout(atlasBox);
    al->addWidget(m_atlases);
    layout->addWidget(atlasBox);

    // ---- node (rest) ----
    m_nodeBox = new QGroupBox(tr("Node"), body);
    auto* nf = new QFormLayout(m_nodeBox);
    m_name = new QLineEdit(m_nodeBox);
    m_sprite = new QComboBox(m_nodeBox);
    m_sprite->setEditable(true);
    m_sprite->setInsertPolicy(QComboBox::NoInsert);
    m_sprite->setMaxVisibleItems(24);
    m_sprite->setToolTip(tr("The rest sprite, as name (atlas id): picking one stores its atlas (\"id:name\"). "
                            "(none) = a group that only moves its children. Type to filter."));
    m_atlasPivot = new QCheckBox(tr("Atlas pivot"), m_nodeBox);
    m_atlasPivot->setToolTip(tr("Use the sprite's pivot from the atlas (x_pivot, else the centre)"));
    m_pivotX = dspin(-4, 5, 3, 0.05, m_nodeBox);
    m_pivotY = dspin(-4, 5, 3, 0.05, m_nodeBox);
    m_order = new QSpinBox(m_nodeBox);
    m_order->setRange(-999, 999);
    m_order->setKeyboardTracking(false);
    m_order->setToolTip(tr("Among siblings; < 0 draws before (under) the parent's own sprite"));
    m_blend = new QComboBox(m_nodeBox);
    m_blend->addItems({tr("normal"), tr("add")});
    m_inheritColor = new QCheckBox(tr("Inherit colour"), m_nodeBox);
    m_restVisible = new QCheckBox(tr("Visible (rest)"), m_nodeBox);
    m_playback = new QComboBox(m_nodeBox);
    m_playback->addItems({tr("Once, stay at the last key"), tr("Once, then hide"), tr("Loop")});
    m_playback->setToolTip(tr("How this node plays its keys (with its children's: first..last key of the subtree).\n"
                              "Once, stay: holds its last pose (drawn, nothing updates).\n"
                              "Once, then hide: hidden with its children after its last key.\n"
                              "Loop: plays first..last key again, forever, also after the clip has ended."));
    m_skin = new QLineEdit(m_nodeBox);
    m_skin->setPlaceholderText(tr("(not skinned)"));
    m_skin->setToolTip(tr("Bones that bend this sprite's mesh: node names of this clip, comma separated. Empty = not skinned (drawn as a quad). The weights are made from the rest pose; its mesh is cut from the sprite and saved with the .anim (<name>.mesh.json). docs/engine/16_MESH_ANIMATION.md"));
    nf->addRow(tr("Name"), m_name);
    nf->addRow(tr("Sprite"), m_sprite);
    nf->addRow(tr("Playback"), m_playback);
    nf->addRow(tr("Pivot"), m_atlasPivot);
    nf->addRow(QString(), row(m_nodeBox, {{QStringLiteral("x"), m_pivotX}, {QStringLiteral("y"), m_pivotY}}));
    nf->addRow(tr("Order"), m_order);
    nf->addRow(tr("Blend"), m_blend);
    nf->addRow(QString(), m_inheritColor);
    nf->addRow(QString(), m_restVisible);
    nf->addRow(tr("Skin"), m_skin);
    m_skinInfo = new QLabel(m_nodeBox);
    m_skinInfo->setWordWrap(true);
    m_skinInfo->setForegroundRole(QPalette::PlaceholderText);
    nf->addRow(QString(), m_skinInfo);
    {   // Bones the node does not carry: moving it slides the mesh off them. One click puts them under it.
        m_bonesOutside = new QWidget(m_nodeBox);
        auto* v = new QVBoxLayout(m_bonesOutside);
        v->setContentsMargins(0, 0, 0, 0);
        m_bonesOutsideText = new QLabel(m_bonesOutside);
        m_bonesOutsideText->setWordWrap(true);
        QPalette warn = m_bonesOutsideText->palette();
        warn.setColor(QPalette::WindowText, QColor(0xe0, 0xa0, 0x30));
        m_bonesOutsideText->setPalette(warn);
        auto* fix = new QPushButton(tr("Move Bones Under This Sprite"), m_bonesOutside);
        fix->setToolTip(tr("The bones become children of this node, nothing moving on screen: then moving the sprite "
                           "(Object mode) moves its mesh and bones together. One undo step"));
        v->addWidget(m_bonesOutsideText);
        v->addWidget(fix);
        connect(fix, &QPushButton::clicked, this, [this] { m_doc->parentBonesUnder(m_doc->selectedPath()); });
        nf->addRow(QString(), m_bonesOutside);
    }
    m_meshCols = new QSpinBox(m_nodeBox);
    m_meshRows = new QSpinBox(m_nodeBox);
    for (QSpinBox* b : {m_meshCols, m_meshRows}) {
        b->setRange(1, toms::anim::kMaxMeshGrid);
        b->setKeyboardTracking(false);
        b->setToolTip(tr("This node's mesh: its sprite cut from its alpha on cells across x down (more cells bend more "
                         "smoothly). Kept in the node; the mesh is saved with the .anim (<name>.mesh.json)"));
        connect(b, &QSpinBox::valueChanged, this, [this] {
            if (!m_updating) m_doc->setMeshGrid(m_meshCols->value(), m_meshRows->value());
        });
    }
    m_meshGridRow = row(m_nodeBox, {{QStringLiteral("x"), m_meshCols}, {QStringLiteral("y"), m_meshRows}});
    nf->addRow(tr("Mesh grid"), m_meshGridRow);
    m_latticeCols = new QSpinBox(m_nodeBox);
    m_latticeRows = new QSpinBox(m_nodeBox);
    for (QSpinBox* b : {m_latticeCols, m_latticeRows}) {
        b->setRange(2, toms::anim::kMaxLattice);
        b->setKeyboardTracking(false);
        b->setToolTip(tr("The deform lattice: points across and down over the sprite (default 4 x 4). View > Lattice Mode "
                         "(L) shows it; drag a point to key the deform at the playhead"));
        connect(b, &QSpinBox::valueChanged, this, [this] {
            if (!m_updating) m_doc->setLatticeSize(m_latticeCols->value(), m_latticeRows->value());
        });
    }
    nf->addRow(tr("Lattice"), row(m_nodeBox, {{QStringLiteral("x"), m_latticeCols}, {QStringLiteral("y"), m_latticeRows}}));
    layout->addWidget(m_nodeBox);

    // ---- at the playhead ----
    m_playBox = new QGroupBox(tr("At playhead"), body);
    auto* pf = new QFormLayout(m_playBox);
    m_playLabel = new QLabel(m_playBox);
    m_playLabel->setForegroundRole(QPalette::PlaceholderText);
    for (int i = 0; i < 3; i++) {
        m_pos[i] = dspin(-99999, 99999, 2, 1, m_playBox);
        m_rot[i] = dspin(-99999, 99999, 2, 5, m_playBox);
        m_scale[i] = dspin(-999, 999, 3, 0.05, m_playBox);
    }
    m_pos[2]->setToolTip(tr("z: depth (into the screen). The 2D view does not move with it; a 3D camera would"));
    m_rot[0]->setToolTip(tr("x: tilt about the horizontal axis (the 2D view foreshortens the sprite)"));
    m_rot[1]->setToolTip(tr("y: tilt about the vertical axis (the 2D view foreshortens the sprite)"));
    m_rot[2]->setToolTip(tr("z: turn in the screen plane, clockwise (the viewport's rotate handle)"));
    m_scale[2]->setToolTip(tr("z: depth scale (shows with x / y tilts and a 3D camera)"));
    for (int i = 0; i < 4; i++) m_color[i] = dspin(0, 1, 3, 0.05, m_playBox);
    m_spriteHere = new QLabel(m_playBox);
    m_spriteHere->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_visibleHere = new QCheckBox(m_playBox);
    pf->addRow(m_playLabel);
    auto xyz = [](QDoubleSpinBox* const s[3]) -> QList<QPair<QString, QWidget*>> {
        return {{QStringLiteral("x"), s[0]}, {QStringLiteral("y"), s[1]}, {QStringLiteral("z"), s[2]}};
    };
    pf->addRow(tr("Pos"), row(m_playBox, xyz(m_pos), makeKeyButton(Channel::Pos, m_playBox)));
    pf->addRow(tr("Rot"), row(m_playBox, xyz(m_rot), makeKeyButton(Channel::Rot, m_playBox)));
    pf->addRow(tr("Scale"), row(m_playBox, xyz(m_scale), makeKeyButton(Channel::Scale, m_playBox)));
    pf->addRow(tr("Colour"), row(m_playBox, {{QString(), m_color[0]}, {QString(), m_color[1]}, {QString(), m_color[2]}},
                                 makeKeyButton(Channel::Color, m_playBox)));
    pf->addRow(tr("Alpha"), row(m_playBox, {{QString(), m_color[3]}}));
    pf->addRow(tr("Sprite"), row(m_playBox, {{QString(), m_spriteHere}}, makeKeyButton(Channel::Sprite, m_playBox)));
    pf->addRow(tr("Visible"), row(m_playBox, {{QString(), m_visibleHere}}, makeKeyButton(Channel::Visible, m_playBox)));
    m_color[0]->setToolTip(tr("red"));
    m_color[1]->setToolTip(tr("green"));
    m_color[2]->setToolTip(tr("blue"));
    layout->addWidget(m_playBox);

    // ---- ease of the key at / before the playhead ----
    m_easeBox = new QGroupBox(tr("Ease (key at / before the playhead)"), body);
    auto* eg = new QGridLayout(m_easeBox);
    eg->setColumnStretch(1, 1);
    for (int i = 0; i < 4; i++) {
        const Channel c = kInterpolated[i];
        auto* lab = new QLabel(QLatin1String(channelName(c)), m_easeBox);
        m_ease[i] = new QComboBox(m_easeBox);
        EaseUi::fill(m_ease[i]);
        m_easeAt[i] = new QLabel(m_easeBox);
        m_easeAt[i]->setForegroundRole(QPalette::PlaceholderText);
        m_easeAt[i]->setMinimumWidth(52);
        eg->addWidget(lab, i, 0);
        eg->addWidget(m_ease[i], i, 1);
        eg->addWidget(m_easeAt[i], i, 2);
        connect(m_ease[i], &QComboBox::activated, this, [this, i, c](int index) {
            const Node* n = m_doc->selectedNode();
            if (!n) return;
            toms::anim::Ease cur, chosen;
            if (!easeAtOrBefore(*n, c, m_doc->time(), cur) && keyCount(*n, c) > 0) easeAt(*n, c, keyTimes(*n, c).front(), cur);
            if (EaseUi::picked(m_ease[i], index, cur, this, chosen)) m_doc->setEaseHere(c, chosen);
            refreshPlayhead();
        });
    }
    layout->addWidget(m_easeBox);
    layout->addStretch(1);
    scroll->setWidget(body);
    setWidget(scroll);

    // ---- wiring ----
    connect(m_name, &QLineEdit::editingFinished, this, [this] {
        if (!m_updating && m_doc->selectedNode() && m_name->text() != qs(m_doc->selectedNode()->name))
            m_doc->renameNode(m_doc->selectedPath(), m_name->text());
    });
    auto commitSprite = [this] {
        if (m_updating || !m_doc->selectedNode()) return;
        const QString text = m_sprite->currentText().trimmed();
        if (text == m_spriteShown) return;   // untouched (a bare legacy name stays as it is)
        const std::string s = m_doc->spriteFromText(text);
        if (s == m_doc->selectedNode()->sprite) return;
        m_doc->editSelected(tr("Set sprite"), [s](Node& n) { n.sprite = s; });
    };
    connect(m_sprite, &QComboBox::activated, this, commitSprite);
    connect(m_sprite->lineEdit(), &QLineEdit::editingFinished, this, commitSprite);
    connect(m_atlasPivot, &QCheckBox::toggled, this, [this](bool atlas) {
        if (m_updating) return;
        glm::vec2 start(0.5f, 0.5f);
        if (const Node* n = m_doc->selectedNode())
            if (const toms::AtlasRegion* r = m_doc->findSprite(spriteAt(*n, m_doc->time()))) start = {r->pivot[0], r->pivot[1]};
        m_doc->editSelected(tr("Pivot"), [atlas, start](Node& n) {
            n.hasPivot = !atlas;
            if (!atlas) n.pivot = start;
        });
    });
    auto commitPivot = [this] {
        if (m_updating) return;
        const glm::vec2 p(float(m_pivotX->value()), float(m_pivotY->value()));
        m_doc->editSelected(tr("Pivot"), [p](Node& n) {
            n.hasPivot = true;
            n.pivot = p;
        }, QStringLiteral("prop.pivot") + pathToString(m_doc->selectedPath()));
    };
    connect(m_pivotX, &QDoubleSpinBox::valueChanged, this, commitPivot);
    connect(m_pivotY, &QDoubleSpinBox::valueChanged, this, commitPivot);
    connect(m_order, &QSpinBox::valueChanged, this, [this](int v) {
        if (!m_updating) m_doc->editSelected(tr("Order"), [v](Node& n) { n.order = v; }, QStringLiteral("prop.order") + pathToString(m_doc->selectedPath()));
    });
    connect(m_blend, &QComboBox::activated, this, [this](int i) {
        if (!m_updating) m_doc->editSelected(tr("Blend"), [i](Node& n) { n.blend = i == 1 ? toms::anim::Blend::Add : toms::anim::Blend::Normal; });
    });
    connect(m_skin, &QLineEdit::editingFinished, this, [this] {
        if (m_updating) return;
        std::vector<std::string> bones;
        for (const QString& b : m_skin->text().split(QLatin1Char(','), Qt::SkipEmptyParts))
            if (!b.trimmed().isEmpty()) bones.push_back(b.trimmed().toStdString());
        m_doc->editSelected(tr("Skin"), [bones](Node& n) {
            if (n.skinBones != bones) n.skinWeights.clear();   // other bones: the weights are made again
            n.skinBones = bones;
        });
    });
    connect(m_inheritColor, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_updating) m_doc->editSelected(tr("Inherit colour"), [on](Node& n) { n.inheritColor = on; });
    });
    connect(m_restVisible, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_updating) m_doc->editSelected(tr("Visible"), [on](Node& n) { n.visible = on; });
    });
    connect(m_playback, &QComboBox::activated, this, [this](int i) {
        if (!m_updating) m_doc->editSelected(tr("Playback"), [i](Node& n) {
            n.loop = i == 2;
            n.stayAtLastFrame = i != 1;
        });
    });
    for (int i = 0; i < 3; i++) {
        connect(m_pos[i], &QDoubleSpinBox::valueChanged, this, [this] { commitChannel(Channel::Pos); });
        connect(m_rot[i], &QDoubleSpinBox::valueChanged, this, [this] { commitChannel(Channel::Rot); });
        connect(m_scale[i], &QDoubleSpinBox::valueChanged, this, [this] { commitChannel(Channel::Scale); });
    }
    for (QDoubleSpinBox* s : m_color) connect(s, &QDoubleSpinBox::valueChanged, this, [this] { commitChannel(Channel::Color); });
    connect(m_visibleHere, &QCheckBox::toggled, this, [this] { commitChannel(Channel::Visible); });

    connect(doc, &AnimDocument::fileChanged, this, &AnimPropertiesDock::refresh);
    connect(doc, &AnimDocument::selectionChanged, this, &AnimPropertiesDock::refresh);
    connect(doc, &AnimDocument::clipChanged, this, &AnimPropertiesDock::refresh);
    connect(doc, &AnimDocument::timeChanged, this, &AnimPropertiesDock::refreshPlayhead);
    connect(doc, &AnimDocument::autoKeyChanged, this, &AnimPropertiesDock::refreshPlayhead);
    connect(doc, &AnimDocument::atlasChanged, this, [this] {
        const QSignalBlocker b(m_sprite);
        m_sprite->clear();
        m_sprite->addItem(tr("(none)"));
        m_sprite->addItems(m_doc->spriteChoices());
        auto* c = new QCompleter(m_doc->spriteChoices(), m_sprite);
        c->setFilterMode(Qt::MatchContains);
        c->setCaseSensitivity(Qt::CaseInsensitive);
        m_sprite->setCompleter(c);
        refresh();
    });
    refresh();
}

QToolButton* AnimPropertiesDock::makeKeyButton(Channel c, QWidget* parent)
{
    auto* b = new QToolButton(parent);
    b->setAutoRaise(true);
    b->setIconSize(QSize(16, 16));
    m_key[int(c)] = b;
    connect(b, &QToolButton::clicked, this, [this, c] {
        const Node* n = m_doc->selectedNode();
        if (!n) return;
        if (hasKeyAt(*n, c, m_doc->time())) m_doc->removeKeyHere(c);
        else m_doc->addKeyHere(c);
    });
    return b;
}

void AnimPropertiesDock::commitChannel(Channel c)
{
    if (m_updating || !m_doc->selectedNode()) return;
    Value v;
    switch (c) {
    case Channel::Pos: v.v = {float(m_pos[0]->value()), float(m_pos[1]->value()), float(m_pos[2]->value()), 0}; break;
    case Channel::Rot: v.v = {float(m_rot[0]->value()), float(m_rot[1]->value()), float(m_rot[2]->value()), 0}; break;
    case Channel::Scale: v.v = {float(m_scale[0]->value()), float(m_scale[1]->value()), float(m_scale[2]->value()), 0}; break;
    case Channel::Color:
        v.v = {float(m_color[0]->value()), float(m_color[1]->value()), float(m_color[2]->value()), float(m_color[3]->value())};
        break;
    case Channel::Visible: v.b = m_visibleHere->isChecked(); break;
    default: return;
    }
    m_doc->setChannel(c, v, QStringLiteral("prop.%1.%2").arg(QLatin1String(channelName(c)), pathToString(m_doc->selectedPath())));
}

bool AnimPropertiesDock::bonesOutsideShown() const { return m_bonesOutside->isVisibleTo(m_nodeBox); }

QString AnimPropertiesDock::skinInfoText() const { return m_skinInfo->isVisibleTo(m_nodeBox) ? m_skinInfo->text() : QString(); }

// What the selected node's skin is: its mesh, bones and weights (as the game bends it at the playhead), or why it
// draws as a plain quad.
QString AnimPropertiesDock::skinInfo(const Node& n) const
{
    const toms::anim::Clip* clip = m_doc->clip();
    if (!clip) return QString();
    std::vector<toms::anim::NodePose> poses;
    toms::anim::evaluate(*clip, m_doc->time(), poses);
    size_t i = 0;
    while (i < poses.size() && poses[i].node != &n) i++;
    toms::anim::SkinView sv;
    switch (toms::anim::skinView(*clip, poses, i, m_doc->atlasSet(), toms::anim::placement(0, 0), &sv)) {
    case toms::anim::SkinState::NotSkinned: return QString();
    case toms::anim::SkinState::NoSprite: return tr("No sprite to bend.");
    case toms::anim::SkinState::Rotated: return tr("The sprite is rotated in its atlas: it has no mesh and draws as a quad.");
    case toms::anim::SkinState::NoMesh:
        return tr("No mesh for this sprite (a child sprite, or no visible pixels): it draws as a quad.");
    case toms::anim::SkinState::Ok: break;
    }
    QString text = tr("Mesh: %1 points, %2 triangles. %3 bone(s), weights %4. View > Show Skin shows them.")
                       .arg(sv.vertices.size()).arg(sv.triangles.size() / 3).arg(sv.bones.size())
                       .arg(sv.autoWeights ? tr("automatic") : tr("painted"));
    QStringList missing;
    for (size_t b = 0; b < sv.bones.size(); b++)
        if (!sv.boneFound[b]) missing << QString::fromStdString(sv.bones[b]);
    if (!missing.isEmpty()) text += QLatin1Char(' ') + tr("Not a node of this clip: %1.").arg(missing.join(QStringLiteral(", ")));
    return text;
}

void AnimPropertiesDock::refresh()
{
    m_updating = true;
    m_clip.refresh(m_doc);
    const Node* n = m_doc->selectedNode();
    m_nodeBox->setEnabled(n != nullptr);
    if (n) {
        m_nodeBox->setTitle(tr("Node: %1").arg(qs(n->name)));
        m_name->setText(qs(n->name));
        const QSignalBlocker b(m_sprite);
        m_spriteShown = m_doc->spriteDisplay(n->sprite);
        const int idx = m_sprite->findText(m_spriteShown);
        if (idx >= 0) m_sprite->setCurrentIndex(idx);
        m_sprite->setEditText(m_spriteShown);
        m_atlasPivot->setChecked(!n->hasPivot);
        glm::vec2 pv = n->pivot;
        if (!n->hasPivot)
            if (const toms::AtlasRegion* r = m_doc->findSprite(spriteAt(*n, m_doc->time()))) pv = {r->pivot[0], r->pivot[1]};
        m_pivotX->setValue(pv.x);
        m_pivotY->setValue(pv.y);
        m_pivotX->setEnabled(n->hasPivot);
        m_pivotY->setEnabled(n->hasPivot);
        m_order->setValue(n->order);
        m_blend->setCurrentIndex(n->blend == toms::anim::Blend::Add ? 1 : 0);
        m_inheritColor->setChecked(n->inheritColor);
        m_restVisible->setChecked(n->visible);
        m_playback->setCurrentIndex(n->loop ? 2 : n->stayAtLastFrame ? 0 : 1);
        QStringList bones;
        for (const std::string& b : n->skinBones) bones << qs(b);
        m_skin->setText(bones.join(QStringLiteral(", ")));
        m_skinInfo->setVisible(n->skinned());
        if (n->skinned()) m_skinInfo->setText(skinInfo(*n));
        QStringList outside;
        if (n->skinned() && m_doc->clip())
            for (const std::string& b : animed::bonesOutside(m_doc->clip()->root, *n)) outside << qs(b);
        m_bonesOutside->setVisible(!outside.isEmpty());
        m_bonesOutsideText->setText(tr("Bones outside this sprite: %1. Moving the sprite in Object mode slides its mesh off "
                                       "them; to move the whole object, put them under it.").arg(outside.join(QStringLiteral(", "))));
        int cols = 0, rows = 0;
        const bool grid = n->usesMesh() && m_doc->meshGridOf(*n, cols, rows);
        m_meshGridRow->setVisible(grid);
        if (auto* form = qobject_cast<QFormLayout*>(m_nodeBox->layout()))
            if (QWidget* label = form->labelForField(m_meshGridRow)) label->setVisible(grid);
        if (grid) {
            m_meshCols->setValue(cols);
            m_meshRows->setValue(rows);
        }
        m_latticeCols->setValue(n->latticeCols());
        m_latticeRows->setValue(n->latticeRows());
    } else {
        m_nodeBox->setTitle(tr("Node"));
    }
    m_updating = false;
    refreshPlayhead();
}

void AnimPropertiesDock::refreshPlayhead()
{
    m_updating = true;
    const Node* n = m_doc->selectedNode();
    const float t = m_doc->time();
    m_playBox->setEnabled(n != nullptr);
    m_easeBox->setEnabled(n != nullptr);
    m_playLabel->setText(tr("t = %1 s   auto-key %2").arg(t, 0, 'f', 3).arg(m_doc->autoKey() ? tr("ON (edits write keys)") : tr("off (edits change rest values)")));
    if (n) {
        const glm::vec3 p = toms::anim::positionAt(*n, t), r = toms::anim::rotationAt(*n, t), s = toms::anim::scaleAt(*n, t);
        const glm::vec4 c = toms::anim::colorAt(*n, t);
        for (int i = 0; i < 3; i++) {
            const QSignalBlocker bp(m_pos[i]), br(m_rot[i]), bs(m_scale[i]);
            m_pos[i]->setValue(p[i]);
            m_rot[i]->setValue(r[i]);
            m_scale[i]->setValue(s[i]);
        }
        for (int i = 0; i < 4; i++) {
            const QSignalBlocker b(m_color[i]);
            m_color[i]->setValue(c[i]);
        }
        const std::string sp = toms::anim::spriteAt(*n, t);
        m_spriteHere->setText(m_doc->spriteDisplay(sp));
        m_spriteHere->setToolTip(sp.empty() ? QString() : tr("Stored as \"%1\"").arg(qs(sp)));
        {
            const QSignalBlocker b(m_visibleHere);
            m_visibleHere->setChecked(toms::anim::visibleAt(*n, t));
        }
        for (Channel ch : kAllChannels) {
            QToolButton* b = m_key[int(ch)];
            if (!b) continue;
            const bool here = hasKeyAt(*n, ch, t), track = keyCount(*n, ch) > 0;
            b->setIcon(Icons::icon(here ? Icons::Id::KeyOn : track ? Icons::Id::KeyOff : Icons::Id::KeyNone));
            b->setToolTip(here ? tr("Key at the playhead: click to remove it")
                               : track ? tr("Animated, no key here: click to key the current value")
                                       : tr("No keys: click to start a track with the current value"));
        }
        for (int i = 0; i < 4; i++) {
            const Channel ch = kInterpolated[i];
            toms::anim::Ease e;
            float kt = 0;
            bool have = easeAtOrBefore(*n, ch, t, e, &kt);
            if (!have && keyCount(*n, ch) > 0) {
                kt = keyTimes(*n, ch).front();
                have = easeAt(*n, ch, kt, e);
            }
            EaseUi::show(m_ease[i], have ? &e : nullptr);
            m_easeAt[i]->setText(have ? tr("@ %1").arg(kt, 0, 'f', 3) : tr("no keys"));
        }
    }
    m_updating = false;
}

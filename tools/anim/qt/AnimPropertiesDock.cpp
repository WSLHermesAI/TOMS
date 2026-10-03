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
    nf->addRow(tr("Name"), m_name);
    nf->addRow(tr("Sprite"), m_sprite);
    nf->addRow(tr("Pivot"), m_atlasPivot);
    nf->addRow(QString(), row(m_nodeBox, {{QStringLiteral("x"), m_pivotX}, {QStringLiteral("y"), m_pivotY}}));
    nf->addRow(tr("Order"), m_order);
    nf->addRow(tr("Blend"), m_blend);
    nf->addRow(QString(), m_inheritColor);
    nf->addRow(QString(), m_restVisible);
    layout->addWidget(m_nodeBox);

    // ---- at the playhead ----
    m_playBox = new QGroupBox(tr("At playhead"), body);
    auto* pf = new QFormLayout(m_playBox);
    m_playLabel = new QLabel(m_playBox);
    m_playLabel->setForegroundRole(QPalette::PlaceholderText);
    m_posX = dspin(-99999, 99999, 2, 1, m_playBox);
    m_posY = dspin(-99999, 99999, 2, 1, m_playBox);
    m_rot = dspin(-99999, 99999, 2, 5, m_playBox);
    m_scaleX = dspin(-999, 999, 3, 0.05, m_playBox);
    m_scaleY = dspin(-999, 999, 3, 0.05, m_playBox);
    for (int i = 0; i < 4; i++) m_color[i] = dspin(0, 1, 3, 0.05, m_playBox);
    m_spriteHere = new QLabel(m_playBox);
    m_spriteHere->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_visibleHere = new QCheckBox(m_playBox);
    pf->addRow(m_playLabel);
    pf->addRow(tr("Pos"), row(m_playBox, {{QStringLiteral("x"), m_posX}, {QStringLiteral("y"), m_posY}}, makeKeyButton(Channel::Pos, m_playBox)));
    pf->addRow(tr("Rot"), row(m_playBox, {{QString(), m_rot}}, makeKeyButton(Channel::Rot, m_playBox)));
    pf->addRow(tr("Scale"), row(m_playBox, {{QStringLiteral("x"), m_scaleX}, {QStringLiteral("y"), m_scaleY}}, makeKeyButton(Channel::Scale, m_playBox)));
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
    connect(m_inheritColor, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_updating) m_doc->editSelected(tr("Inherit colour"), [on](Node& n) { n.inheritColor = on; });
    });
    connect(m_restVisible, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_updating) m_doc->editSelected(tr("Visible"), [on](Node& n) { n.visible = on; });
    });
    for (QDoubleSpinBox* s : {m_posX, m_posY}) connect(s, &QDoubleSpinBox::valueChanged, this, [this] { commitChannel(Channel::Pos); });
    connect(m_rot, &QDoubleSpinBox::valueChanged, this, [this] { commitChannel(Channel::Rot); });
    for (QDoubleSpinBox* s : {m_scaleX, m_scaleY}) connect(s, &QDoubleSpinBox::valueChanged, this, [this] { commitChannel(Channel::Scale); });
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
    case Channel::Pos: v.v = {float(m_posX->value()), float(m_posY->value()), 0, 0}; break;
    case Channel::Rot: v.v.x = float(m_rot->value()); break;
    case Channel::Scale: v.v = {float(m_scaleX->value()), float(m_scaleY->value()), 0, 0}; break;
    case Channel::Color:
        v.v = {float(m_color[0]->value()), float(m_color[1]->value()), float(m_color[2]->value()), float(m_color[3]->value())};
        break;
    case Channel::Visible: v.b = m_visibleHere->isChecked(); break;
    default: return;
    }
    m_doc->setChannel(c, v, QStringLiteral("prop.%1.%2").arg(QLatin1String(channelName(c)), pathToString(m_doc->selectedPath())));
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
        const glm::vec2 p = toms::anim::positionAt(*n, t), s = toms::anim::scaleAt(*n, t);
        const glm::vec4 c = toms::anim::colorAt(*n, t);
        for (QDoubleSpinBox* w : {m_posX, m_posY, m_rot, m_scaleX, m_scaleY, m_color[0], m_color[1], m_color[2], m_color[3]})
            w->blockSignals(true);
        m_posX->setValue(p.x);
        m_posY->setValue(p.y);
        m_rot->setValue(toms::anim::rotationAt(*n, t));
        m_scaleX->setValue(s.x);
        m_scaleY->setValue(s.y);
        for (int i = 0; i < 4; i++) m_color[i]->setValue(c[i]);
        for (QDoubleSpinBox* w : {m_posX, m_posY, m_rot, m_scaleX, m_scaleY, m_color[0], m_color[1], m_color[2], m_color[3]})
            w->blockSignals(false);
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

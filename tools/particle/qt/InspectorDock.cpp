#include "InspectorDock.h"

#include "ParticleDocument.h"
#include "ParticleWidgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDropEvent>
#include <QFormLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QScrollArea>
#include <QSpinBox>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

using toms::fx::Blend;
using toms::fx::Burst;
using toms::fx::Effect;
using toms::fx::Emitter;
using toms::fx::Range;
using toms::fx::Shape;

namespace {

const char* kSpriteMime = "application/x-toms-sprite";

QString qs(const std::string& s) { return QString::fromStdString(s); }

QDoubleSpinBox* dspin(double lo, double hi, int decimals, double step, const QString& suffix = QString())
{
    auto* s = new QDoubleSpinBox;
    s->setRange(lo, hi);
    s->setDecimals(decimals);
    s->setSingleStep(step);
    s->setSuffix(suffix);
    s->setKeyboardTracking(false);
    return s;
}

// The frames list takes sprites dropped from the Sprites dock and reorders by dragging; Delete removes.
class FramesFilter : public QObject
{
public:
    FramesFilter(QListWidget* list, std::function<void()> changed) : QObject(list), m_list(list), m_changed(std::move(changed))
    {
        list->viewport()->installEventFilter(this);
        list->installEventFilter(this);
    }
    bool eventFilter(QObject* o, QEvent* e) override
    {
        if (o == m_list && e->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Delete) {
            qDeleteAll(m_list->selectedItems());
            m_changed();
            return true;
        }
        if (o != m_list->viewport()) return false;
        if (e->type() == QEvent::DragEnter || e->type() == QEvent::DragMove) {
            auto* d = static_cast<QDragMoveEvent*>(e);
            if (!d->mimeData()->hasFormat(QLatin1String(kSpriteMime))) return false;
            d->setDropAction(Qt::CopyAction);
            d->accept();
            return true;
        }
        if (e->type() == QEvent::Drop) {
            auto* d = static_cast<QDropEvent*>(e);
            if (!d->mimeData()->hasFormat(QLatin1String(kSpriteMime))) return false;
            const QStringList refs = QString::fromUtf8(d->mimeData()->data(QLatin1String(kSpriteMime))).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            QListWidgetItem* at = m_list->itemAt(d->position().toPoint());
            int row = at ? m_list->row(at) : m_list->count();
            for (const QString& r : refs) m_list->insertItem(row++, r);
            d->setDropAction(Qt::CopyAction);
            d->accept();
            m_changed();
            return true;
        }
        return false;
    }

private:
    QListWidget* m_list;
    std::function<void()> m_changed;
};

}  // namespace

QWidget* InspectorDock::section(QVBoxLayout* into, const QString& title, QFormLayout** form)
{
    auto* box = new QWidget;
    auto* v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);
    auto* head = new QToolButton;
    head->setText(title);
    head->setCheckable(true);
    head->setChecked(true);
    head->setArrowType(Qt::DownArrow);
    head->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    head->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    head->setStyleSheet(QStringLiteral("QToolButton { font-weight: bold; text-align: left; border: none; padding: 4px; }"));
    auto* body = new QWidget;
    *form = new QFormLayout(body);
    (*form)->setContentsMargins(14, 0, 4, 6);
    (*form)->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    connect(head, &QToolButton::toggled, this, [head, body](bool open) {
        body->setVisible(open);
        head->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });
    v->addWidget(head);
    v->addWidget(body);
    into->addWidget(box);
    return box;
}

QString InspectorDock::mergeKey(const QString& field)
{
    if (field != m_lastField || !m_fieldClock.isValid() || m_fieldClock.elapsed() > 1200) m_serial++;
    m_lastField = field;
    m_fieldClock.restart();
    return field + QString::number(m_serial);
}

InspectorDock::InspectorDock(ParticleDocument* doc, QWidget* parent)
    : QDockWidget(tr("Inspector"), parent)
    , m_doc(doc)
{
    setObjectName(QStringLiteral("ParticleInspectorDock"));
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* body = new QWidget;
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(2);
    scroll->setWidget(body);
    setWidget(scroll);

    // Shorthands: an edit of the selected emitter / effect with a merge key per field.
    auto em = [this](const QString& field, const QString& text, std::function<void(Emitter&)> f) {
        if (!m_updating) m_doc->editEmitter(text, f, mergeKey(field));
    };
    auto ef = [this](const QString& field, const QString& text, std::function<void(Effect&)> f) {
        if (!m_updating) m_doc->editEffect(text, f, mergeKey(field));
    };
    QFormLayout* f = nullptr;

    // ---- Effect
    m_effectSection = section(v, tr("Effect"), &f);
    m_effectName = new QLineEdit;
    m_duration = dspin(0, 600, 2, 0.1, tr(" s"));
    m_duration->setSpecialValueText(tr("endless"));
    m_duration->setToolTip(tr("0 = endless (until stopped). A one-shot stops making particles at its end."));
    m_loop = new QCheckBox(tr("start over at the end"));
    m_prewarm = dspin(0, 60, 2, 0.5, tr(" s"));
    m_prewarm->setToolTip(tr("Simulated before the first frame: an already-burning fire"));
    m_seed = new QSpinBox;
    m_seed->setRange(0, 1000000000);
    m_seed->setSpecialValueText(tr("new each play"));
    f->addRow(tr("Name"), m_effectName);
    f->addRow(tr("Duration"), m_duration);
    f->addRow(tr("Loop"), m_loop);
    f->addRow(tr("Prewarm"), m_prewarm);
    f->addRow(tr("Seed"), m_seed);
    connect(m_effectName, &QLineEdit::editingFinished, this, [this, ef] {
        const QString n = m_effectName->text().trimmed();
        if (n.isEmpty() || (m_doc->effect() && qs(m_doc->effect()->name) == n)) return;
        ef(QStringLiteral("name"), tr("Rename effect"), [n](Effect& e) { e.name = n.toStdString(); });
    });
    connect(m_duration, &QDoubleSpinBox::valueChanged, this, [ef](double x) { ef(QStringLiteral("duration"), tr("Duration"), [x](Effect& e) { e.duration = float(x); }); });
    connect(m_loop, &QCheckBox::toggled, this, [ef](bool on) { ef(QStringLiteral("loop"), tr("Loop"), [on](Effect& e) { e.loop = on; }); });
    connect(m_prewarm, &QDoubleSpinBox::valueChanged, this, [ef](double x) { ef(QStringLiteral("prewarm"), tr("Prewarm"), [x](Effect& e) { e.prewarm = float(x); }); });
    connect(m_seed, &QSpinBox::valueChanged, this, [ef](int x) { ef(QStringLiteral("seed"), tr("Seed"), [x](Effect& e) { e.seed = uint32_t(x); }); });

    m_none = new QLabel(tr("Select an emitter in the Effects dock to edit it."));
    m_none->setWordWrap(true);
    m_none->setMargin(8);
    v->addWidget(m_none);

    m_emitterSections = new QWidget;
    auto* ev = new QVBoxLayout(m_emitterSections);
    ev->setContentsMargins(0, 0, 0, 0);
    ev->setSpacing(2);
    v->addWidget(m_emitterSections);
    v->addStretch(1);

    // ---- Emitter
    section(ev, tr("Emitter"), &f);
    m_emitterName = new QLineEdit;
    m_offset = new Vec2Edit(-10000, 10000, 1);
    m_start = dspin(0, 600, 2, 0.1, tr(" s"));
    m_stop = dspin(0, 600, 2, 0.1, tr(" s"));
    m_stop->setSpecialValueText(tr("until the end"));
    m_space = new QComboBox;
    m_space->addItems({tr("world: particles stay where born"), tr("local: particles move with the effect")});
    m_max = new QSpinBox;
    m_max->setRange(1, 100000);
    f->addRow(tr("Name"), m_emitterName);
    f->addRow(tr("Offset"), m_offset);
    f->addRow(tr("Start"), m_start);
    f->addRow(tr("Stop"), m_stop);
    f->addRow(tr("Space"), m_space);
    f->addRow(tr("Max particles"), m_max);
    connect(m_emitterName, &QLineEdit::editingFinished, this, [this, em] {
        const QString n = m_emitterName->text().trimmed();
        if (n.isEmpty() || (m_doc->emitter() && qs(m_doc->emitter()->name) == n)) return;
        em(QStringLiteral("ename"), tr("Rename emitter"), [n](Emitter& m) { m.name = n.toStdString(); });
    });
    connect(m_offset, &Vec2Edit::changed, this, [em](const glm::vec2& o) { em(QStringLiteral("offset"), tr("Offset"), [o](Emitter& m) { m.offset = o; }); });
    connect(m_start, &QDoubleSpinBox::valueChanged, this, [em](double x) { em(QStringLiteral("start"), tr("Start"), [x](Emitter& m) { m.start = float(x); }); });
    connect(m_stop, &QDoubleSpinBox::valueChanged, this, [em](double x) { em(QStringLiteral("stop"), tr("Stop"), [x](Emitter& m) { m.stop = float(x); }); });
    connect(m_space, &QComboBox::currentIndexChanged, this, [em](int i) { em(QStringLiteral("space"), tr("Space"), [i](Emitter& m) { m.localSpace = i == 1; }); });
    connect(m_max, &QSpinBox::valueChanged, this, [em](int x) { em(QStringLiteral("max"), tr("Max particles"), [x](Emitter& m) { m.maxParticles = x; }); });

    // ---- Emission
    section(ev, tr("Emission"), &f);
    m_rate = dspin(0, 10000, 2, 1, tr(" /s"));
    f->addRow(tr("Rate"), m_rate);
    m_bursts = new QTableWidget(0, 5);
    m_bursts->setHorizontalHeaderLabels({tr("t (s)"), tr("min"), tr("max"), tr("repeat"), tr("every (s)")});
    m_bursts->verticalHeader()->setVisible(false);
    m_bursts->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_bursts->setMaximumHeight(110);
    m_bursts->setToolTip(tr("A burst fires 'min..max' particles at emitter time t, then 'repeat' more times every 'every' seconds (-1 = while active)"));
    auto* burstButtons = new QWidget;
    {
        auto* h = new QHBoxLayout(burstButtons);
        h->setContentsMargins(0, 0, 0, 0);
        auto* add = new QToolButton;
        add->setText(tr("+ Burst"));
        auto* del = new QToolButton;
        del->setText(tr("− Burst"));
        h->addWidget(add);
        h->addWidget(del);
        h->addStretch(1);
        connect(add, &QToolButton::clicked, this, [this] {
            m_doc->editEmitter(tr("Add burst"), [](Emitter& m) {
                Burst b;
                b.t = m.bursts.empty() ? 0.0f : m.bursts.back().t + 0.5f;
                m.bursts.push_back(b);
            });
        });
        connect(del, &QToolButton::clicked, this, [this] {
            const int row = m_bursts->currentRow() >= 0 ? m_bursts->currentRow() : m_bursts->rowCount() - 1;
            if (row < 0) return;
            m_doc->editEmitter(tr("Delete burst"), [row](Emitter& m) {
                if (row < int(m.bursts.size())) m.bursts.erase(m.bursts.begin() + row);
            });
        });
    }
    f->addRow(tr("Bursts"), m_bursts);
    f->addRow(QString(), burstButtons);
    connect(m_rate, &QDoubleSpinBox::valueChanged, this, [em](double x) { em(QStringLiteral("rate"), tr("Rate"), [x](Emitter& m) { m.rate = float(x); }); });
    // Queued: the refresh that follows replaces the table's items, not while one is signalling.
    connect(m_bursts, &QTableWidget::itemChanged, this, [this] {
        if (!m_updating) QMetaObject::invokeMethod(this, &InspectorDock::commitBursts, Qt::QueuedConnection);
    });

    // ---- Shape
    section(ev, tr("Shape"), &m_shapeForm);
    f = m_shapeForm;
    m_shapeType = new QComboBox;
    for (int t = 0; t <= Shape::Ring; t++) m_shapeType->addItem(QString::fromLatin1(toms::fx::shapeName(Shape::Type(t))));
    m_length = dspin(0, 10000, 1, 1, tr(" px"));
    m_angle = dspin(-360, 360, 1, 5, tr("°"));
    m_width = dspin(0, 10000, 1, 1, tr(" px"));
    m_height = dspin(0, 10000, 1, 1, tr(" px"));
    m_radius = dspin(0, 10000, 1, 1, tr(" px"));
    m_thickness = dspin(0, 10000, 1, 1, tr(" px"));
    m_arcFrom = dspin(-360, 360, 0, 5, tr("°"));
    m_arcTo = dspin(-360, 720, 0, 5, tr("°"));
    m_edge = new QCheckBox(tr("only on the outline"));
    m_outward = new QCheckBox(tr("fly away from the centre"));
    m_outward->setToolTip(tr("Instead of Direction: each particle leaves away from the shape's centre (a point: every direction)"));
    f->addRow(tr("Type"), m_shapeType);
    f->addRow(tr("Length"), m_length);
    f->addRow(tr("Angle"), m_angle);
    f->addRow(tr("Width"), m_width);
    f->addRow(tr("Height"), m_height);
    f->addRow(tr("Radius"), m_radius);
    f->addRow(tr("Thickness"), m_thickness);
    f->addRow(tr("Arc from"), m_arcFrom);
    f->addRow(tr("Arc to"), m_arcTo);
    f->addRow(tr("Edge"), m_edge);
    f->addRow(tr("Outward"), m_outward);
    connect(m_shapeType, &QComboBox::currentIndexChanged, this, [this, em](int i) {
        updateShapeRows();
        em(QStringLiteral("shape"), tr("Shape"), [i](Emitter& m) {
            m.shape.type = Shape::Type(i);
            // A sensible size so the new shape is visible.
            if (i == Shape::Line && m.shape.length <= 0) m.shape.length = 40;
            if (i == Shape::Box && m.shape.width <= 0) { m.shape.width = 40; m.shape.height = 20; }
            if ((i == Shape::Circle || i == Shape::Ring) && m.shape.radius <= 0) m.shape.radius = 20;
            if (i == Shape::Ring && m.shape.thickness <= 0) m.shape.thickness = 4;
        });
    });
    auto shapeField = [&](QDoubleSpinBox* s, const char* key, std::function<void(Shape&, float)> set) {
        connect(s, &QDoubleSpinBox::valueChanged, this, [em, key, set](double x) {
            em(QString::fromLatin1(key), tr("Shape"), [set, x](Emitter& m) { set(m.shape, float(x)); });
        });
    };
    shapeField(m_length, "slength", [](Shape& s, float x) { s.length = x; });
    shapeField(m_angle, "sangle", [](Shape& s, float x) { s.angle = x; });
    shapeField(m_width, "swidth", [](Shape& s, float x) { s.width = x; });
    shapeField(m_height, "sheight", [](Shape& s, float x) { s.height = x; });
    shapeField(m_radius, "sradius", [](Shape& s, float x) { s.radius = x; });
    shapeField(m_thickness, "sthick", [](Shape& s, float x) { s.thickness = x; });
    shapeField(m_arcFrom, "sarc0", [](Shape& s, float x) { s.arcFrom = x; });
    shapeField(m_arcTo, "sarc1", [](Shape& s, float x) { s.arcTo = x; });
    connect(m_edge, &QCheckBox::toggled, this, [em](bool on) { em(QStringLiteral("sedge"), tr("Shape edge"), [on](Emitter& m) { m.shape.edge = on; }); });
    connect(m_outward, &QCheckBox::toggled, this, [em](bool on) { em(QStringLiteral("sout"), tr("Outward"), [on](Emitter& m) { m.shape.outward = on; }); });

    // ---- Start values
    section(ev, tr("Start values"), &f);
    m_life = new RangeEdit(0.01, 600, 2, 0.1, tr(" s"));
    m_direction = dspin(-360, 360, 0, 5, tr("°"));
    m_direction->setToolTip(tr("-90 = up, 0 = right, 90 = down"));
    m_spread = dspin(0, 180, 0, 5, tr("°"));
    m_spread->setPrefix(QStringLiteral("± "));
    m_speed = new RangeEdit(0, 100000, 1, 5, tr(" px/s"));
    m_size = new RangeEdit(0, 10000, 1, 1, tr(" px"));
    m_rotation = new RangeEdit(-3600, 3600, 0, 5, tr("°"));
    m_spin = new RangeEdit(-36000, 36000, 0, 10, tr("°/s"));
    auto* colors = new QWidget;
    {
        auto* h = new QHBoxLayout(colors);
        h->setContentsMargins(0, 0, 0, 0);
        m_color = new ColorButton;
        m_color2 = new ColorButton;
        m_randomColor = new QCheckBox(tr("random between"));
        h->addWidget(m_color);
        h->addWidget(m_randomColor);
        h->addWidget(m_color2);
        h->addStretch(1);
    }
    m_sprite = new QComboBox;
    m_sprite->setEditable(true);
    m_sprite->setInsertPolicy(QComboBox::NoInsert);
    m_sprite->setToolTip(tr("The sprite as \"atlas:name\" (or double-click one in the Sprites dock). A flipbook replaces it."));
    f->addRow(tr("Life"), m_life);
    f->addRow(tr("Direction"), m_direction);
    f->addRow(tr("Spread"), m_spread);
    f->addRow(tr("Speed"), m_speed);
    f->addRow(tr("Size"), m_size);
    f->addRow(tr("Rotation"), m_rotation);
    f->addRow(tr("Spin"), m_spin);
    f->addRow(tr("Colour"), colors);
    f->addRow(tr("Sprite"), m_sprite);
    auto range = [&](RangeEdit* r, const char* key, const QString& text, std::function<void(Emitter&, const Range&)> set) {
        connect(r, &RangeEdit::changed, this, [em, key, text, set](const Range& x) {
            em(QString::fromLatin1(key), text, [set, x](Emitter& m) { set(m, x); });
        });
    };
    range(m_life, "life", tr("Life"), [](Emitter& m, const Range& r) { m.life = r; });
    range(m_speed, "speed", tr("Speed"), [](Emitter& m, const Range& r) { m.speed = r; });
    range(m_size, "size", tr("Size"), [](Emitter& m, const Range& r) { m.size = r; });
    range(m_rotation, "rotation", tr("Rotation"), [](Emitter& m, const Range& r) { m.rotation = r; });
    range(m_spin, "spin", tr("Spin"), [](Emitter& m, const Range& r) { m.spin = r; });
    connect(m_direction, &QDoubleSpinBox::valueChanged, this, [em](double x) { em(QStringLiteral("dir"), tr("Direction"), [x](Emitter& m) { m.direction = float(x); }); });
    connect(m_spread, &QDoubleSpinBox::valueChanged, this, [em](double x) { em(QStringLiteral("spread"), tr("Spread"), [x](Emitter& m) { m.spread = float(x); }); });
    connect(m_color, &ColorButton::changed, this, [this, em](const glm::vec4& c) {
        const bool rnd = m_randomColor->isChecked();
        em(QStringLiteral("color"), tr("Colour"), [c, rnd](Emitter& m) {
            m.color = c;
            if (!rnd) m.color2 = c;
        });
    });
    connect(m_color2, &ColorButton::changed, this, [em](const glm::vec4& c) { em(QStringLiteral("color2"), tr("Colour"), [c](Emitter& m) { m.color2 = c; }); });
    connect(m_randomColor, &QCheckBox::toggled, this, [this, em](bool on) {
        m_color2->setEnabled(on);
        em(QStringLiteral("rcolor"), tr("Random colour"), [on](Emitter& m) {
            if (!on) m.color2 = m.color;
            else if (m.color2 == m.color) m.color2 = glm::vec4(1.0f);
        });
    });
    auto commitSprite = [this, em] {
        const QString s = m_sprite->currentText().trimmed();
        if (m_doc->emitter() && qs(m_doc->emitter()->sprite) == s) return;
        em(QStringLiteral("sprite"), tr("Sprite"), [s](Emitter& m) { m.sprite = s.toStdString(); });
    };
    connect(m_sprite, &QComboBox::activated, this, commitSprite);
    connect(m_sprite->lineEdit(), &QLineEdit::editingFinished, this, commitSprite);

    // ---- Over life
    section(ev, tr("Over life"), &f);
    m_colorCurve = new GradientEdit;
    m_sizeCurve = new CurveEdit;
    m_speedCurve = new CurveEdit;
    m_spinCurve = new CurveEdit;
    f->addRow(tr("Colour"), m_colorCurve);
    f->addRow(tr("Size ×"), m_sizeCurve);
    f->addRow(tr("Speed ×"), m_speedCurve);
    f->addRow(tr("Spin ×"), m_spinCurve);
    connect(m_colorCurve, &GradientEdit::changed, this, [this](const toms::fx::Curve<glm::vec4>& c) {
        if (!m_updating) m_doc->editEmitter(tr("Colour over life"), [c](Emitter& m) { m.colorOverLife = c; },
                                            QStringLiteral("ccurve%1").arg(m_colorCurve->gesture()));
    });
    auto curve = [&](CurveEdit* w, const char* key, const QString& text, std::function<void(Emitter&, const toms::fx::Curve<float>&)> set) {
        connect(w, &CurveEdit::changed, this, [this, w, key, text, set](const toms::fx::Curve<float>& c) {
            if (!m_updating) m_doc->editEmitter(text, [set, c](Emitter& m) { set(m, c); }, QStringLiteral("%1%2").arg(QLatin1String(key)).arg(w->gesture()));
        });
    };
    curve(m_sizeCurve, "zcurve", tr("Size over life"), [](Emitter& m, const toms::fx::Curve<float>& c) { m.sizeOverLife = c; });
    curve(m_speedCurve, "vcurve", tr("Speed over life"), [](Emitter& m, const toms::fx::Curve<float>& c) { m.speedOverLife = c; });
    curve(m_spinCurve, "scurve", tr("Spin over life"), [](Emitter& m, const toms::fx::Curve<float>& c) { m.spinOverLife = c; });

    // ---- Forces
    section(ev, tr("Forces"), &f);
    m_gravity = new Vec2Edit(-100000, 100000, 1);
    m_gravity->setToolTip(tr("px/s²; y down: [0, 300] falls, [0, -30] rises"));
    m_drag = dspin(0, 100, 2, 0.1, tr(" /s"));
    m_drag->setToolTip(tr("Slows particles down: velocity × e^(-drag·t)"));
    m_radial = new RangeEdit(-100000, 100000, 1, 5, tr(" px/s²"));
    m_radial->setToolTip(tr("Away from the emitter (negative: towards it)"));
    m_tangential = new RangeEdit(-100000, 100000, 1, 5, tr(" px/s²"));
    m_tangential->setToolTip(tr("Around the emitter, clockwise (negative: anticlockwise): orbits, swirls"));
    f->addRow(tr("Gravity"), m_gravity);
    f->addRow(tr("Drag"), m_drag);
    f->addRow(tr("Radial"), m_radial);
    f->addRow(tr("Tangential"), m_tangential);
    connect(m_gravity, &Vec2Edit::changed, this, [em](const glm::vec2& g) { em(QStringLiteral("gravity"), tr("Gravity"), [g](Emitter& m) { m.gravity = g; }); });
    connect(m_drag, &QDoubleSpinBox::valueChanged, this, [em](double x) { em(QStringLiteral("drag"), tr("Drag"), [x](Emitter& m) { m.drag = float(x); }); });
    range(m_radial, "radial", tr("Radial"), [](Emitter& m, const Range& r) { m.radial = r; });
    range(m_tangential, "tangential", tr("Tangential"), [](Emitter& m, const Range& r) { m.tangential = r; });

    // ---- Flipbook
    section(ev, tr("Flipbook"), &f);
    m_frames = new QListWidget;
    m_frames->setDragDropMode(QAbstractItemView::InternalMove);
    m_frames->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_frames->setMaximumHeight(110);
    m_frames->setToolTip(tr("Drop sprites here (several at once: in their order); drag to reorder; Delete removes. "
                            "Frames replace the Sprite."));
    new FramesFilter(m_frames, [this] { QMetaObject::invokeMethod(this, &InspectorDock::commitFrames, Qt::QueuedConnection); });
    connect(m_frames->model(), &QAbstractItemModel::rowsMoved, this, [this] {
        if (!m_updating) QMetaObject::invokeMethod(this, &InspectorDock::commitFrames, Qt::QueuedConnection);
    });
    m_frameMode = new QComboBox;
    m_frameMode->addItems({tr("over the life (all frames once)"), tr("loop at fps")});
    m_fps = dspin(0.1, 240, 1, 1, tr(" fps"));
    m_randomFrame = new QCheckBox(tr("each particle starts on a random frame"));
    f->addRow(tr("Frames"), m_frames);
    f->addRow(tr("Mode"), m_frameMode);
    f->addRow(tr("Speed"), m_fps);
    f->addRow(tr("Start"), m_randomFrame);
    connect(m_frameMode, &QComboBox::currentIndexChanged, this, [this, em](int i) {
        m_fps->setEnabled(i == 1);
        em(QStringLiteral("fmode"), tr("Flipbook mode"), [i](Emitter& m) { m.framesByFps = i == 1; });
    });
    connect(m_fps, &QDoubleSpinBox::valueChanged, this, [em](double x) { em(QStringLiteral("fps"), tr("Flipbook fps"), [x](Emitter& m) { m.fps = float(x); }); });
    connect(m_randomFrame, &QCheckBox::toggled, this, [em](bool on) { em(QStringLiteral("frand"), tr("Random frame"), [on](Emitter& m) { m.randomFrame = on; }); });

    // ---- Render
    section(ev, tr("Render"), &f);
    m_blend = new QComboBox;
    m_blend->addItems({QStringLiteral("normal"), QStringLiteral("add"), QStringLiteral("multiply"), QStringLiteral("screen"), tr("custom…")});
    m_blend->setToolTip(tr("normal: paints over · add: adds light (fire, sparks, magic) · multiply: darkens (shadows) · "
                           "screen: soft light. multiply / screen / custom draw as normal in the game until phase 2b."));
    m_src = new QComboBox;
    m_dst = new QComboBox;
    for (int i = 0; i < Blend::FactorCount; i++) {
        m_src->addItem(QString::fromLatin1(Blend::factorName(Blend::Factor(i))));
        m_dst->addItem(QString::fromLatin1(Blend::factorName(Blend::Factor(i))));
    }
    auto* custom = new QWidget;
    {
        auto* h = new QHBoxLayout(custom);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(new QLabel(tr("src")));
        h->addWidget(m_src, 1);
        h->addWidget(new QLabel(tr("dst")));
        h->addWidget(m_dst, 1);
    }
    m_order = new QSpinBox;
    m_order->setRange(-1000, 1000);
    m_order->setToolTip(tr("Among this effect's emitters: higher draws in front"));
    m_align = new QCheckBox(tr("turn to the direction of motion"));
    m_oldestOnTop = new QCheckBox(tr("older particles over newer ones"));
    f->addRow(tr("Blend"), m_blend);
    f->addRow(tr("Factors"), custom);
    f->addRow(tr("Order"), m_order);
    f->addRow(tr("Align"), m_align);
    f->addRow(tr("Stacking"), m_oldestOnTop);
    m_simulation = new QComboBox;
    m_simulation->addItems({tr("auto: GPU above the game's particle threshold"), tr("CPU"), tr("GPU (when the device can)")});
    m_simulation->setToolTip(tr("Where the game simulates this emitter.\n"
                                "auto: on the GPU when Max particles is above the game setting particleGpuThreshold "
                                "(desktop 5000, phones 3000).\n"
                                "GPU: on the GPU whenever the device has compute shaders (not in the browser or on GLES phones: "
                                "there it falls back to the CPU).\n"
                                "CPU: always on the CPU.\n"
                                "The editor's preview always simulates on the CPU; both give the same particles."));
    f->addRow(tr("Simulation"), m_simulation);
    connect(m_simulation, &QComboBox::currentIndexChanged, this, [em](int i) {
        using Sim = toms::fx::Emitter::Simulation;
        const Sim s = i == 2 ? Sim::Gpu : i == 1 ? Sim::Cpu : Sim::Auto;
        em(QStringLiteral("simulation"), tr("Simulation"), [s](Emitter& m) { m.simulation = s; });
    });
    connect(m_blend, &QComboBox::activated, this, [this, em](int i) {
        static const Blend presets[] = {Blend::normal(), Blend::add(), Blend::multiply(), Blend::screen()};
        if (i < 4) em(QStringLiteral("blend"), tr("Blend"), [i](Emitter& m) { m.blend = presets[i]; });
        refresh();   // shows / hides the factors
    });
    auto factor = [this, em](QComboBox* c, bool src) {
        connect(c, &QComboBox::activated, this, [em, src](int i) {
            em(QStringLiteral("factor"), tr("Blend factor"), [i, src](Emitter& m) { (src ? m.blend.src : m.blend.dst) = Blend::Factor(i); });
        });
    };
    factor(m_src, true);
    factor(m_dst, false);
    connect(m_order, &QSpinBox::valueChanged, this, [em](int x) { em(QStringLiteral("order"), tr("Order"), [x](Emitter& m) { m.order = x; }); });
    connect(m_align, &QCheckBox::toggled, this, [em](bool on) { em(QStringLiteral("align"), tr("Align to motion"), [on](Emitter& m) { m.alignToVelocity = on; }); });
    connect(m_oldestOnTop, &QCheckBox::toggled, this, [em](bool on) { em(QStringLiteral("oldest"), tr("Stacking"), [on](Emitter& m) { m.oldestOnTop = on; }); });

    connect(doc, &ParticleDocument::fileChanged, this, &InspectorDock::refresh);
    connect(doc, &ParticleDocument::selectionChanged, this, &InspectorDock::refresh);
    connect(doc, &ParticleDocument::atlasChanged, this, &InspectorDock::refreshSprites);
    refreshSprites();
    refresh();
}

void InspectorDock::refreshSprites()
{
    const QString cur = m_sprite->currentText();
    const QSignalBlocker b(m_sprite);
    m_sprite->clear();
    for (const QString& r : m_doc->spriteRefs()) {
        const QImage img = m_doc->spriteImage(r);
        m_sprite->addItem(img.isNull() ? QIcon() : QIcon(QPixmap::fromImage(img.scaled(20, 20, Qt::KeepAspectRatio, Qt::SmoothTransformation))), r);
    }
    m_sprite->setEditText(cur);
}

void InspectorDock::updateShapeRows()
{
    const int t = m_shapeType->currentIndex();
    auto show = [this](QWidget* w, bool on) { m_shapeForm->setRowVisible(w, on); };
    show(m_length, t == Shape::Line);
    show(m_angle, t == Shape::Line);
    show(m_width, t == Shape::Box);
    show(m_height, t == Shape::Box);
    show(m_radius, t == Shape::Circle || t == Shape::Ring);
    show(m_thickness, t == Shape::Ring);
    show(m_arcFrom, t == Shape::Circle || t == Shape::Ring);
    show(m_arcTo, t == Shape::Circle || t == Shape::Ring);
    show(m_edge, t == Shape::Box || t == Shape::Circle);
}

void InspectorDock::refresh()
{
    m_updating = true;
    const Effect* e = m_doc->effect();
    const Emitter* m = m_doc->emitter();
    m_effectSection->setEnabled(e != nullptr);
    m_emitterSections->setVisible(m != nullptr);
    m_none->setVisible(m == nullptr);
    setWindowTitle(m ? tr("Inspector: %1").arg(qs(m->name)) : e ? tr("Inspector: %1").arg(qs(e->name)) : tr("Inspector"));
    if (e) {
        if (!m_effectName->hasFocus()) m_effectName->setText(qs(e->name));
        m_duration->setValue(e->duration);
        m_loop->setChecked(e->loop);
        m_loop->setEnabled(e->duration > 0);
        m_prewarm->setValue(e->prewarm);
        m_seed->setValue(int(e->seed));
    }
    if (m) {
        if (!m_emitterName->hasFocus()) m_emitterName->setText(qs(m->name));
        m_offset->setValue(m->offset);
        m_start->setValue(m->start);
        m_stop->setValue(m->stop);
        m_space->setCurrentIndex(m->localSpace ? 1 : 0);
        m_max->setValue(m->maxParticles);
        m_rate->setValue(m->rate);
        m_bursts->setRowCount(int(m->bursts.size()));
        for (int i = 0; i < int(m->bursts.size()); i++) {
            const Burst& b = m->bursts[size_t(i)];
            const double vals[] = {b.t, b.count.min, b.count.max, double(b.repeat), b.interval};
            for (int c = 0; c < 5; c++) m_bursts->setItem(i, c, new QTableWidgetItem(QString::number(vals[c], 'g', 6)));
        }
        m_shapeType->setCurrentIndex(int(m->shape.type));
        m_length->setValue(m->shape.length);
        m_angle->setValue(m->shape.angle);
        m_width->setValue(m->shape.width);
        m_height->setValue(m->shape.height);
        m_radius->setValue(m->shape.radius);
        m_thickness->setValue(m->shape.thickness);
        m_arcFrom->setValue(m->shape.arcFrom);
        m_arcTo->setValue(m->shape.arcTo);
        m_edge->setChecked(m->shape.edge);
        m_outward->setChecked(m->shape.outward);
        updateShapeRows();
        m_life->setValue(m->life);
        m_direction->setValue(m->direction);
        m_direction->setEnabled(!m->shape.outward);
        m_spread->setValue(m->spread);
        m_speed->setValue(m->speed);
        m_size->setValue(m->size);
        m_rotation->setValue(m->rotation);
        m_spin->setValue(m->spin);
        m_color->setColor(m->color);
        m_color2->setColor(m->color2);
        m_randomColor->setChecked(m->color != m->color2);
        m_color2->setEnabled(m->color != m->color2);
        if (!m_sprite->lineEdit()->hasFocus()) m_sprite->setEditText(qs(m->sprite));
        m_sprite->setEnabled(m->frames.empty());
        m_colorCurve->setCurve(m->colorOverLife);
        m_sizeCurve->setCurve(m->sizeOverLife);
        m_speedCurve->setCurve(m->speedOverLife);
        m_spinCurve->setCurve(m->spinOverLife);
        m_gravity->setValue(m->gravity);
        m_drag->setValue(m->drag);
        m_radial->setValue(m->radial);
        m_tangential->setValue(m->tangential);
        m_frames->clear();
        for (const std::string& fr : m->frames) m_frames->addItem(qs(fr));
        m_frameMode->setCurrentIndex(m->framesByFps ? 1 : 0);
        m_fps->setValue(m->fps);
        m_fps->setEnabled(m->framesByFps);
        m_randomFrame->setChecked(m->randomFrame);
        const QString preset = qs(m->blend.presetName());
        const bool customBlend = preset.isEmpty() || m_blend->currentIndex() == 4;
        m_blend->setCurrentIndex(preset.isEmpty() ? 4 : (customBlend && m_blend->currentIndex() == 4 ? 4 : m_blend->findText(preset)));
        m_src->setCurrentIndex(int(m->blend.src));
        m_dst->setCurrentIndex(int(m->blend.dst));
        for (QWidget* w : {static_cast<QWidget*>(m_src), static_cast<QWidget*>(m_dst)}) w->setEnabled(m_blend->currentIndex() == 4);
        m_order->setValue(m->order);
        m_align->setChecked(m->alignToVelocity);
        m_oldestOnTop->setChecked(m->oldestOnTop);
        m_simulation->setCurrentIndex(m->simulation == Emitter::Simulation::Gpu ? 2 : m->simulation == Emitter::Simulation::Cpu ? 1 : 0);
    }
    m_updating = false;
}

void InspectorDock::commitBursts()
{
    std::vector<Burst> bursts;
    for (int r = 0; r < m_bursts->rowCount(); r++) {
        auto val = [&](int c, double def) {
            const QTableWidgetItem* it = m_bursts->item(r, c);
            bool ok = false;
            const double v = it ? it->text().toDouble(&ok) : def;
            return ok ? v : def;
        };
        Burst b;
        b.t = float(std::max(0.0, val(0, 0)));
        const float lo = float(std::max(0.0, val(1, 1))), hi = float(std::max(0.0, val(2, lo)));
        b.count = Range(std::min(lo, hi), std::max(lo, hi));
        b.repeat = int(val(3, 0));
        b.interval = float(std::max(0.0, val(4, 0.5)));
        bursts.push_back(b);
    }
    m_doc->editEmitter(tr("Bursts"), [&](Emitter& m) { m.bursts = bursts; }, mergeKey(QStringLiteral("bursts")));
}

void InspectorDock::commitFrames()
{
    std::vector<std::string> frames;
    for (int i = 0; i < m_frames->count(); i++) frames.push_back(m_frames->item(i)->text().toStdString());
    m_doc->editEmitter(tr("Flipbook frames"), [&](Emitter& m) { m.frames = frames; });
}

#include "BezierDialog.h"

#include "Theme.h"
#include "anim_keys.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {
// y may overshoot (back-like curves), so the box shows -1..2.
constexpr double kYMin = -1.0, kYMax = 2.0;

struct Preset {
    const char* name;
    float b[4];
};
const Preset kPresets[] = {
    {"ease", {0.25f, 0.1f, 0.25f, 1.0f}},       {"ease-in", {0.42f, 0.0f, 1.0f, 1.0f}},
    {"ease-out", {0.0f, 0.0f, 0.58f, 1.0f}},    {"ease-in-out", {0.42f, 0.0f, 0.58f, 1.0f}},
    {"overshoot", {0.34f, 1.56f, 0.64f, 1.0f}}, {"anticipate", {0.36f, -0.4f, 0.6f, 1.0f}},
};
}  // namespace

BezierCurveEdit::BezierCurveEdit(QWidget* parent)
    : QWidget(parent)
    , m_timer(new QTimer(this))
{
    setMinimumSize(220, 220);
    setMouseTracking(true);
    connect(m_timer, &QTimer::timeout, this, [this] {
        m_phase = std::fmod(m_phase + 0.016, 1.6);   // 1 s run + 0.6 s pause
        update();
    });
    m_timer->start(16);
}

void BezierCurveEdit::setBezier(const float b[4])
{
    std::copy(b, b + 4, m_b);
    update();
}

QRectF BezierCurveEdit::box() const
{
    const double s = std::min(width(), height()) - 24;
    return QRectF((width() - s) / 2, (height() - s) / 2, s, s);
}

QPointF BezierCurveEdit::toWidget(double x, double y) const
{
    const QRectF b = box();
    return QPointF(b.left() + x * b.width(), b.bottom() - (y - kYMin) / (kYMax - kYMin) * b.height());
}

QPointF BezierCurveEdit::fromWidget(const QPointF& w) const
{
    const QRectF b = box();
    return QPointF((w.x() - b.left()) / b.width(), kYMin + (b.bottom() - w.y()) / b.height() * (kYMax - kYMin));
}

void BezierCurveEdit::paintEvent(QPaintEvent*)
{
    const Theme::Colors& tc = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), palette().color(QPalette::Base));
    // The unit square (0..1 in x and y) and the overshoot area around it.
    p.setPen(QPen(tc.grid, 1));
    for (int i = 0; i <= 4; i++) p.drawLine(toWidget(i / 4.0, kYMin), toWidget(i / 4.0, kYMax));
    for (double y = kYMin; y <= kYMax + 1e-9; y += 0.5) p.drawLine(toWidget(0, y), toWidget(1, y));
    p.setPen(QPen(tc.gridAxis, 1));
    p.drawRect(QRectF(toWidget(0, 1), toWidget(1, 0)));
    const QPointF p0 = toWidget(0, 0), p3 = toWidget(1, 1), h1 = toWidget(m_b[0], m_b[1]), h2 = toWidget(m_b[2], m_b[3]);
    p.setPen(QPen(tc.pageBorder, 1.5));
    p.drawLine(p0, h1);
    p.drawLine(p3, h2);
    // The curve as the runtime evaluates it.
    toms::anim::Ease e;
    e.kind = toms::anim::Ease::kBezier;
    std::copy(m_b, m_b + 4, e.bezier);
    QPainterPath path(p0);
    for (int i = 1; i <= 100; i++) path.lineTo(toWidget(i / 100.0, e.apply(float(i / 100.0))));
    p.setPen(QPen(tc.selection, 2.5));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    for (int i = 0; i < 2; i++) {
        p.setPen(QPen(Qt::white, 1.5));
        p.setBrush(i == 0 ? tc.gizmoX : tc.gizmoY);
        p.drawEllipse(i == 0 ? h1 : h2, 6, 6);
    }
    // Timing preview: a dot moving with the eased value along the top edge.
    const double t = std::min(1.0, m_phase);
    const double v = e.apply(float(t));
    p.setPen(Qt::NoPen);
    p.setBrush(tc.key);
    p.drawEllipse(QPointF(box().left() + v * box().width(), box().top() - 6), 5, 5);
    p.setBrush(tc.key.darker(150));
    p.drawEllipse(toWidget(t, v), 3.5, 3.5);
}

void BezierCurveEdit::mousePressEvent(QMouseEvent* e)
{
    const QPointF h[2] = {toWidget(m_b[0], m_b[1]), toWidget(m_b[2], m_b[3])};
    m_drag = -1;
    for (int i = 0; i < 2; i++)
        if (std::hypot(e->position().x() - h[i].x(), e->position().y() - h[i].y()) < 10) m_drag = i;
}

void BezierCurveEdit::mouseMoveEvent(QMouseEvent* e)
{
    if (m_drag < 0) return;
    const QPointF c = fromWidget(e->position());
    m_b[m_drag * 2] = float(std::clamp(c.x(), 0.0, 1.0));
    m_b[m_drag * 2 + 1] = float(std::clamp(c.y(), kYMin, kYMax));
    update();
    emit changed();
}

void BezierCurveEdit::mouseReleaseEvent(QMouseEvent*) { m_drag = -1; }

BezierDialog::BezierDialog(const toms::anim::Ease& start, QWidget* parent)
    : QDialog(parent)
    , m_curve(new BezierCurveEdit(this))
{
    setWindowTitle(tr("Bezier Ease"));
    if (start.kind == toms::anim::Ease::kBezier) m_curve->setBezier(start.bezier);
    m_presets = new QComboBox(this);
    m_presets->addItem(tr("Presets…"));
    for (const Preset& pr : kPresets) m_presets->addItem(QString::fromLatin1(pr.name));
    connect(m_presets, &QComboBox::activated, this, [this](int i) {
        if (i <= 0) return;
        m_curve->setBezier(kPresets[i - 1].b);
        syncSpins();
        m_presets->setCurrentIndex(0);
    });
    auto* spins = new QHBoxLayout();
    const char* labels[4] = {"x1", "y1", "x2", "y2"};
    for (int i = 0; i < 4; i++) {
        m_spin[i] = new QDoubleSpinBox(this);
        m_spin[i]->setDecimals(3);
        m_spin[i]->setSingleStep(0.05);
        m_spin[i]->setRange(i % 2 == 0 ? 0.0 : kYMin, i % 2 == 0 ? 1.0 : kYMax);
        auto* lab = new QLabel(QString::fromLatin1(labels[i]), this);
        lab->setForegroundRole(QPalette::PlaceholderText);
        spins->addWidget(lab);
        spins->addWidget(m_spin[i], 1);
        connect(m_spin[i], &QDoubleSpinBox::valueChanged, this, [this] {
            if (m_updating) return;
            float b[4];
            for (int k = 0; k < 4; k++) b[k] = float(m_spin[k]->value());
            m_curve->setBezier(b);
        });
    }
    connect(m_curve, &BezierCurveEdit::changed, this, &BezierDialog::syncSpins);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* l = new QVBoxLayout(this);
    l->addWidget(m_presets);
    l->addWidget(m_curve, 1);
    l->addLayout(spins);
    auto* hint = new QLabel(tr("Like CSS cubic-bezier(x1, y1, x2, y2). Drag the handles; y may overshoot."), this);
    hint->setForegroundRole(QPalette::PlaceholderText);
    hint->setWordWrap(true);
    l->addWidget(hint);
    l->addWidget(buttons);
    syncSpins();
}

void BezierDialog::syncSpins()
{
    m_updating = true;
    for (int i = 0; i < 4; i++) m_spin[i]->setValue(m_curve->bezier()[i]);
    m_updating = false;
}

toms::anim::Ease BezierDialog::ease() const
{
    toms::anim::Ease e;
    e.kind = toms::anim::Ease::kBezier;
    std::copy(m_curve->bezier(), m_curve->bezier() + 4, e.bezier);
    return e;
}

namespace EaseUi {

void fill(QComboBox* combo)
{
    combo->clear();
    for (const std::string& n : toms::anim::easeNames()) combo->addItem(QString::fromStdString(n));
    combo->addItem(QObject::tr("Bezier…"));
    combo->setMaxVisibleItems(20);
}

void show(QComboBox* combo, const toms::anim::Ease* e)
{
    const QSignalBlocker block(combo);
    const int bezierItem = combo->count() - 1;
    combo->setEnabled(e != nullptr);
    if (!e) {
        combo->setCurrentIndex(1);   // linear, greyed out
        combo->setItemText(bezierItem, QObject::tr("Bezier…"));
        return;
    }
    if (e->kind == toms::anim::Ease::kBezier) {
        combo->setItemText(bezierItem, QString::fromStdString(animed::formatEase(*e)));
        combo->setCurrentIndex(bezierItem);
    } else {
        combo->setItemText(bezierItem, QObject::tr("Bezier…"));
        combo->setCurrentIndex(std::clamp(e->kind, 0, bezierItem - 1));
    }
}

bool picked(QComboBox* combo, int index, const toms::anim::Ease& current, QWidget* parent, toms::anim::Ease& out)
{
    if (index < 0) return false;
    if (index == combo->count() - 1) {
        BezierDialog dlg(current, parent);
        if (dlg.exec() != QDialog::Accepted) {
            show(combo, &current);
            return false;
        }
        out = dlg.ease();
        return true;
    }
    out = toms::anim::Ease();
    out.kind = index;
    return true;
}

}  // namespace EaseUi

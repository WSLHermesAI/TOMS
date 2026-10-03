#include "ParticleWidgets.h"

#include "Theme.h"

#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

using toms::fx::Curve;
using toms::fx::CurveKey;
using toms::fx::Ease;
using toms::fx::Range;

namespace {

QDoubleSpinBox* spin(double lo, double hi, int decimals, double step, const QString& suffix, QWidget* parent)
{
    auto* s = new QDoubleSpinBox(parent);
    s->setRange(lo, hi);
    s->setDecimals(decimals);
    s->setSingleStep(step);
    s->setSuffix(suffix);
    s->setKeyboardTracking(false);
    s->setMinimumWidth(70);
    return s;
}

QPixmap checker(int cell)
{
    QPixmap pm(cell * 2, cell * 2);
    pm.fill(QColor(200, 200, 200));
    QPainter p(&pm);
    p.fillRect(0, 0, cell, cell, QColor(150, 150, 150));
    p.fillRect(cell, cell, cell, cell, QColor(150, 150, 150));
    return pm;
}

}  // namespace

// ---- RangeEdit ---------------------------------------------------------------------------------

RangeEdit::RangeEdit(double lo, double hi, int decimals, double step, const QString& suffix, QWidget* parent)
    : QWidget(parent)
    , m_min(spin(lo, hi, decimals, step, suffix, this))
    , m_max(spin(lo, hi, decimals, step, suffix, this))
    , m_link(new QToolButton(this))
{
    m_link->setCheckable(true);
    m_link->setText(QStringLiteral("="));
    m_link->setToolTip(tr("Linked: one value (no randomness). Unlinked: each particle picks a value between min and max."));
    auto* l = new QHBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(3);
    l->addWidget(m_min, 1);
    l->addWidget(m_link);
    l->addWidget(m_max, 1);
    // Linked: max follows min. Unlinked: a min above the max pushes the max up, and the other way.
    connect(m_min, &QDoubleSpinBox::valueChanged, this, [this] {
        if (!m_setting && (m_link->isChecked() || m_min->value() > m_max->value())) {
            const QSignalBlocker b(m_max);
            m_max->setValue(m_min->value());
        }
        emitChanged();
    });
    connect(m_max, &QDoubleSpinBox::valueChanged, this, [this] {
        if (!m_setting && m_max->value() < m_min->value()) {
            const QSignalBlocker b(m_min);
            m_min->setValue(m_max->value());
        }
        emitChanged();
    });
    connect(m_link, &QToolButton::toggled, this, [this](bool on) {
        m_max->setEnabled(!on);
        if (on && !m_setting) {
            const QSignalBlocker b(m_max);
            m_max->setValue(m_min->value());
            emitChanged();
        }
    });
}

void RangeEdit::setValue(const Range& r)
{
    m_setting = true;
    const QSignalBlocker a(m_min), b(m_max), c(m_link);
    m_min->setValue(r.min);
    m_max->setValue(r.max);
    m_link->setChecked(r.fixed());
    m_max->setEnabled(!r.fixed());
    m_setting = false;
}

Range RangeEdit::value() const
{
    const float lo = float(m_min->value()), hi = float(m_link->isChecked() ? m_min->value() : m_max->value());
    return Range(std::min(lo, hi), std::max(lo, hi));
}

void RangeEdit::emitChanged()
{
    if (!m_setting) emit changed(value());
}

// ---- Vec2Edit ----------------------------------------------------------------------------------

Vec2Edit::Vec2Edit(double lo, double hi, int decimals, QWidget* parent)
    : QWidget(parent)
    , m_x(spin(lo, hi, decimals, 1, QString(), this))
    , m_y(spin(lo, hi, decimals, 1, QString(), this))
{
    m_x->setPrefix(QStringLiteral("x "));
    m_y->setPrefix(QStringLiteral("y "));
    auto* l = new QHBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(3);
    l->addWidget(m_x, 1);
    l->addWidget(m_y, 1);
    for (QDoubleSpinBox* s : {m_x, m_y})
        connect(s, &QDoubleSpinBox::valueChanged, this, [this] {
            if (!m_setting) emit changed(value());
        });
}

void Vec2Edit::setValue(const glm::vec2& v)
{
    m_setting = true;
    m_x->setValue(v.x);
    m_y->setValue(v.y);
    m_setting = false;
}

glm::vec2 Vec2Edit::value() const { return {float(m_x->value()), float(m_y->value())}; }

// ---- ColorButton -------------------------------------------------------------------------------

ColorButton::ColorButton(QWidget* parent)
    : QToolButton(parent)
{
    setMinimumSize(44, 22);
    setToolTip(tr("Click to pick the colour and its alpha"));
    connect(this, &QToolButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(toQColor(m_color), this, tr("Colour"), QColorDialog::ShowAlphaChannel);
        if (!c.isValid()) return;
        setColor(fromQColor(c));
        emit changed(m_color);
    });
}

QColor ColorButton::toQColor(const glm::vec4& c)
{
    auto b = [](float v) { return std::clamp(int(std::lround(v * 255)), 0, 255); };
    return QColor(b(c.r), b(c.g), b(c.b), b(c.a));
}

glm::vec4 ColorButton::fromQColor(const QColor& c)
{
    auto r = [](int v) { return std::round(v / 255.0f * 1000.0f) / 1000.0f; };
    return {r(c.red()), r(c.green()), r(c.blue()), r(c.alpha())};
}

void ColorButton::setColor(const glm::vec4& c)
{
    m_color = c;
    update();
}

void ColorButton::paintEvent(QPaintEvent* e)
{
    QToolButton::paintEvent(e);
    QPainter p(this);
    const QRect r = rect().adjusted(4, 4, -4, -4);
    p.fillRect(r, QBrush(checker(4)));
    p.fillRect(r, toQColor(m_color));
    p.setPen(palette().color(QPalette::Mid));
    p.drawRect(r.adjusted(0, 0, -1, -1));
}

// ---- EaseCombo ---------------------------------------------------------------------------------

EaseCombo::EaseCombo(QWidget* parent)
    : QComboBox(parent)
{
    for (const std::string& n : toms::anim::easeNames()) addItem(QString::fromStdString(n));
    setToolTip(tr("The curve from this key to the next"));
    connect(this, &QComboBox::currentIndexChanged, this, [this] {
        if (!m_setting) emit easeChanged(ease());
    });
}

void EaseCombo::setEase(const Ease& e)
{
    m_setting = true;
    const int bez = findText(QStringLiteral("bezier"));
    if (e.kind == Ease::kBezier) {
        m_bezier = e;
        if (bez < 0) addItem(QStringLiteral("bezier"));
        setCurrentIndex(findText(QStringLiteral("bezier")));
    } else {
        if (bez >= 0) removeItem(bez);
        setCurrentIndex(findText(QString::fromStdString(toms::anim::easeName(e))));
    }
    m_setting = false;
}

Ease EaseCombo::ease() const
{
    if (currentText() == QLatin1String("bezier")) return m_bezier;
    Ease e;
    toms::anim::parseEase(currentText().toStdString(), e);
    return e;
}

// ---- CurveEdit ---------------------------------------------------------------------------------

CurveEdit::CurveEdit(QWidget* parent)
    : QWidget(parent)
    , m_value(spin(-100, 100, 2, 0.1, QString(), this))
    , m_ease(new EaseCombo(this))
    , m_clear(new QToolButton(this))
{
    setMinimumHeight(130);
    setToolTip(tr("Drag a key to move it, double-click to add one, right-click to delete. x = the particle's life, 0..1."));
    m_clear->setText(tr("Clear"));
    m_value->setPrefix(QStringLiteral("× "));
    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(m_value);
    row->addWidget(m_ease, 1);
    row->addWidget(m_clear);
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->addStretch(1);
    l->addLayout(row);
    connect(m_value, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_setting || m_sel < 0) return;
        m_curve.keys[size_t(m_sel)].v = float(v);
        commit();
    });
    connect(m_ease, &EaseCombo::easeChanged, this, [this](const Ease& e) {
        if (m_sel < 0) return;
        m_curve.keys[size_t(m_sel)].ease = e;
        commit();
    });
    connect(m_clear, &QToolButton::clicked, this, [this] {
        m_curve.keys.clear();
        m_sel = -1;
        m_gesture++;
        commit();
    });
    syncRow();
}

void CurveEdit::setCurve(const Curve<float>& c)
{
    m_curve = c;
    if (m_sel >= int(m_curve.keys.size())) m_sel = int(m_curve.keys.size()) - 1;
    if (!m_dragging) range(m_lo, m_hi);
    syncRow();
    update();
}

void CurveEdit::syncRow()
{
    m_setting = true;
    const bool has = m_sel >= 0 && m_sel < int(m_curve.keys.size());
    m_value->setEnabled(has);
    m_ease->setEnabled(has);
    m_clear->setEnabled(!m_curve.keys.empty());
    if (has) {
        m_value->setValue(m_curve.keys[size_t(m_sel)].v);
        m_ease->setEase(m_curve.keys[size_t(m_sel)].ease);
    }
    m_setting = false;
}

QRectF CurveEdit::graph() const { return QRectF(6, 4, width() - 12, height() - 40); }

void CurveEdit::range(float& lo, float& hi) const
{
    lo = 0;
    hi = 2;
    for (const auto& k : m_curve.keys) {
        lo = std::min(lo, k.v);
        hi = std::max(hi, k.v * 1.15f);
    }
}

QPointF CurveEdit::toPx(float t, float v) const
{
    const QRectF g = graph();
    return QPointF(g.left() + t * g.width(), g.bottom() - (v - m_lo) / std::max(1e-6f, m_hi - m_lo) * g.height());
}

void CurveEdit::fromPx(const QPointF& p, float& t, float& v) const
{
    const QRectF g = graph();
    t = float(std::clamp((p.x() - g.left()) / g.width(), 0.0, 1.0));
    v = float(m_lo + (g.bottom() - p.y()) / g.height() * (m_hi - m_lo));
    t = std::round(t * 100) / 100;
    v = std::round(v * 100) / 100;
}

int CurveEdit::keyAt(const QPointF& p) const
{
    for (int i = 0; i < int(m_curve.keys.size()); i++)
        if (QLineF(toPx(m_curve.keys[size_t(i)].t, m_curve.keys[size_t(i)].v), p).length() <= 7) return i;
    return -1;
}

void CurveEdit::sortKeepSelection()
{
    const float selT = m_sel >= 0 ? m_curve.keys[size_t(m_sel)].t : -1;
    const float selV = m_sel >= 0 ? m_curve.keys[size_t(m_sel)].v : 0;
    std::stable_sort(m_curve.keys.begin(), m_curve.keys.end(), [](const CurveKey<float>& a, const CurveKey<float>& b) { return a.t < b.t; });
    if (m_sel >= 0)
        for (int i = 0; i < int(m_curve.keys.size()); i++)
            if (m_curve.keys[size_t(i)].t == selT && m_curve.keys[size_t(i)].v == selV) m_sel = i;
}

void CurveEdit::commit()
{
    syncRow();
    update();
    emit changed(m_curve);
}

void CurveEdit::addKey(float t, float v)
{
    CurveKey<float> k;
    k.t = std::clamp(t, 0.0f, 1.0f);
    k.v = v;
    m_curve.keys.push_back(k);
    m_sel = int(m_curve.keys.size()) - 1;
    sortKeepSelection();
    m_gesture++;
    commit();
}

void CurveEdit::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF g = graph();
    p.fillRect(g, palette().color(QPalette::Base));
    p.setPen(QPen(palette().color(QPalette::Mid), 1, Qt::DotLine));
    for (float v : {0.0f, 1.0f, 2.0f})
        if (v >= m_lo && v <= m_hi) p.drawLine(toPx(0, v), toPx(1, v));
    p.setPen(palette().color(QPalette::PlaceholderText));
    p.drawText(QRectF(g.left() + 3, toPx(0, 1).y() - 14, 40, 14), Qt::AlignLeft, QStringLiteral("×1"));
    if (m_curve.empty()) {
        p.setPen(QPen(palette().color(QPalette::PlaceholderText), 1, Qt::DashLine));
        p.drawLine(toPx(0, 1), toPx(1, 1));
        p.drawText(g, Qt::AlignCenter, tr("no curve (×1) — double-click to add a key"));
        return;
    }
    QPainterPath path;
    for (int i = 0; i <= 80; i++) {
        const float t = i / 80.0f;
        const QPointF pt = toPx(t, m_curve.at(t, 1.0f));
        i ? path.lineTo(pt) : path.moveTo(pt);
    }
    p.setPen(QPen(Theme::colors().key, 2));
    p.drawPath(path);
    for (int i = 0; i < int(m_curve.keys.size()); i++) {
        const auto& k = m_curve.keys[size_t(i)];
        p.setPen(QPen(Qt::black, 1));
        p.setBrush(i == m_sel ? Theme::colors().selection : palette().color(QPalette::Text));
        p.drawEllipse(toPx(k.t, k.v), 4.5, 4.5);
    }
}

void CurveEdit::mousePressEvent(QMouseEvent* e)
{
    const int k = keyAt(e->position());
    if (e->button() == Qt::RightButton) {
        if (k >= 0) {
            m_curve.keys.erase(m_curve.keys.begin() + k);
            m_sel = -1;
            m_gesture++;
            commit();
        }
        return;
    }
    m_sel = k;
    m_dragging = k >= 0;
    m_gesture++;
    syncRow();
    update();
}

void CurveEdit::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_dragging || m_sel < 0) return;
    float t, v;
    fromPx(e->position(), t, v);
    m_curve.keys[size_t(m_sel)].t = t;
    m_curve.keys[size_t(m_sel)].v = v;
    sortKeepSelection();
    commit();
}

void CurveEdit::mouseReleaseEvent(QMouseEvent*)
{
    if (m_dragging) {
        m_dragging = false;
        range(m_lo, m_hi);
        update();
    }
}

void CurveEdit::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (keyAt(e->position()) >= 0 || !graph().contains(e->position())) return;
    float t, v;
    fromPx(e->position(), t, v);
    addKey(t, v);
}

// ---- GradientEdit ------------------------------------------------------------------------------

GradientEdit::GradientEdit(QWidget* parent)
    : QWidget(parent)
    , m_color(new ColorButton(this))
    , m_ease(new EaseCombo(this))
    , m_clear(new QToolButton(this))
{
    setMinimumHeight(78);
    setToolTip(tr("Colour × alpha over the particle's life. Drag a stop to move it, double-click it for its colour, "
                  "double-click the bar to add one, right-click to delete."));
    m_clear->setText(tr("Clear"));
    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(m_color);
    row->addWidget(m_ease, 1);
    row->addWidget(m_clear);
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->addStretch(1);
    l->addLayout(row);
    connect(m_color, &ColorButton::changed, this, [this](const glm::vec4& c) {
        if (m_sel < 0) return;
        m_curve.keys[size_t(m_sel)].v = c;
        m_gesture++;
        commit();
    });
    connect(m_ease, &EaseCombo::easeChanged, this, [this](const Ease& e) {
        if (m_sel < 0) return;
        m_curve.keys[size_t(m_sel)].ease = e;
        m_gesture++;
        commit();
    });
    connect(m_clear, &QToolButton::clicked, this, [this] {
        m_curve.keys.clear();
        m_sel = -1;
        m_gesture++;
        commit();
    });
    syncRow();
}

void GradientEdit::setCurve(const Curve<glm::vec4>& c)
{
    m_curve = c;
    if (m_sel >= int(m_curve.keys.size())) m_sel = int(m_curve.keys.size()) - 1;
    syncRow();
    update();
}

void GradientEdit::syncRow()
{
    m_setting = true;
    const bool has = m_sel >= 0 && m_sel < int(m_curve.keys.size());
    m_color->setEnabled(has);
    m_ease->setEnabled(has);
    m_clear->setEnabled(!m_curve.keys.empty());
    if (has) {
        m_color->setColor(m_curve.keys[size_t(m_sel)].v);
        m_ease->setEase(m_curve.keys[size_t(m_sel)].ease);
    }
    m_setting = false;
}

QRectF GradientEdit::bar() const { return QRectF(8, 4, width() - 16, 24); }

float GradientEdit::tAt(double x) const
{
    const QRectF b = bar();
    return std::round(float(std::clamp((x - b.left()) / b.width(), 0.0, 1.0)) * 100) / 100;
}

int GradientEdit::stopAt(const QPointF& p) const
{
    const QRectF b = bar();
    for (int i = 0; i < int(m_curve.keys.size()); i++) {
        const double x = b.left() + m_curve.keys[size_t(i)].t * b.width();
        if (std::fabs(p.x() - x) <= 6 && p.y() >= b.top() - 2 && p.y() <= b.bottom() + 14) return i;
    }
    return -1;
}

void GradientEdit::sortKeepSelection()
{
    const float selT = m_sel >= 0 ? m_curve.keys[size_t(m_sel)].t : -1;
    std::stable_sort(m_curve.keys.begin(), m_curve.keys.end(), [](const auto& a, const auto& b) { return a.t < b.t; });
    if (m_sel >= 0)
        for (int i = 0; i < int(m_curve.keys.size()); i++)
            if (m_curve.keys[size_t(i)].t == selT) m_sel = i;
}

void GradientEdit::commit()
{
    syncRow();
    update();
    emit changed(m_curve);
}

void GradientEdit::addStop(float t)
{
    CurveKey<glm::vec4> k;
    k.t = std::clamp(t, 0.0f, 1.0f);
    k.v = m_curve.at(k.t, glm::vec4(1.0f));
    m_curve.keys.push_back(k);
    m_sel = int(m_curve.keys.size()) - 1;
    sortKeepSelection();
    m_gesture++;
    commit();
}

void GradientEdit::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QRectF b = bar();
    p.fillRect(b, QBrush(checker(5)));
    const int n = int(b.width());
    for (int x = 0; x < n; x++) {
        const float t = n > 1 ? x / float(n - 1) : 0;
        p.fillRect(QRectF(b.left() + x, b.top(), 1.5, b.height()), ColorButton::toQColor(m_curve.at(t, glm::vec4(1.0f))));
    }
    p.setPen(palette().color(QPalette::Mid));
    p.setBrush(Qt::NoBrush);
    p.drawRect(b);
    if (m_curve.empty()) {
        p.setPen(Qt::black);
        p.drawText(b, Qt::AlignCenter, tr("white (no change) — double-click to add a stop"));
    }
    p.setRenderHint(QPainter::Antialiasing);
    for (int i = 0; i < int(m_curve.keys.size()); i++) {
        const auto& k = m_curve.keys[size_t(i)];
        const double x = b.left() + k.t * b.width();
        QPolygonF tri;
        tri << QPointF(x, b.bottom() + 1) << QPointF(x - 6, b.bottom() + 12) << QPointF(x + 6, b.bottom() + 12);
        p.setPen(QPen(i == m_sel ? Theme::colors().selection : palette().color(QPalette::Text), i == m_sel ? 2 : 1));
        QColor c = ColorButton::toQColor(k.v);
        c.setAlpha(255);
        p.setBrush(c);
        p.drawPolygon(tri);
    }
}

void GradientEdit::mousePressEvent(QMouseEvent* e)
{
    const int s = stopAt(e->position());
    if (e->button() == Qt::RightButton) {
        if (s >= 0) {
            m_curve.keys.erase(m_curve.keys.begin() + s);
            m_sel = -1;
            m_gesture++;
            commit();
        }
        return;
    }
    m_sel = s;
    m_dragging = s >= 0;
    m_gesture++;
    syncRow();
    update();
}

void GradientEdit::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_dragging || m_sel < 0) return;
    m_curve.keys[size_t(m_sel)].t = tAt(e->position().x());
    sortKeepSelection();
    commit();
}

void GradientEdit::mouseReleaseEvent(QMouseEvent*) { m_dragging = false; }

void GradientEdit::mouseDoubleClickEvent(QMouseEvent* e)
{
    const int s = stopAt(e->position());
    if (s >= 0) {
        m_sel = s;
        syncRow();
        m_color->click();
        return;
    }
    if (bar().adjusted(0, 0, 0, 14).contains(e->position())) addStop(tAt(e->position().x()));
}

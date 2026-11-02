#include "TimelineDock.h"

#include "ParticleDocument.h"
#include "ParticlePlayback.h"
#include "Theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>

#include <cmath>

using toms::fx::Effect;
using toms::fx::Emitter;

namespace {
constexpr double g_dNames = 110;   // the lane names column
constexpr double g_dRuler = 22;
constexpr double g_dLane = 22;
}

TimelineView::TimelineView(ParticleDocument* l_pDoc, ParticlePlayback* l_pPlayback, QWidget* l_pParent)
    : QWidget(l_pParent)
    , m_pDoc(l_pDoc)
    , m_pPlay(l_pPlayback)
{
    setMouseTracking(true);
    setMinimumHeight(int(g_dRuler + g_dLane * 2));
    connect(l_pPlayback, &ParticlePlayback::f_Ticked, this, qOverload<>(&QWidget::update));
    connect(l_pDoc, &ParticleDocument::f_FileChanged, this, [this] {
        updateGeometry();
        update();
    });
    connect(l_pDoc, &ParticleDocument::f_SelectionChanged, this, [this] {
        updateGeometry();
        update();
    });
}

QSize TimelineView::sizeHint() const
{
    const Effect* l_pE = m_pDoc->f_Effect();
    return QSize(500, int(g_dRuler + g_dLane * (l_pE ? std::max<size_t>(1, l_pE->m_vEmitters.size()) : 1) + 6));
}

float TimelineView::f_Span() const
{
    const Effect* l_pE = m_pDoc->f_Effect();
    if (!l_pE) return 4;
    if (l_pE->m_fDuration > 0) return l_pE->m_fDuration;
    float l_fS = 4;
    for (const Emitter& l_oM : l_pE->m_vEmitters) {
        l_fS = std::max({l_fS, l_oM.m_fStop + 1, l_oM.m_fStart + 2});
        for (const auto& l_oB : l_oM.m_vBursts) l_fS = std::max(l_fS, l_oM.m_fStart + l_oB.m_fT + 1);
    }
    return std::ceil(l_fS);
}

double TimelineView::f_XOf(float l_fT) const { return g_dNames + l_fT / f_Span() * (width() - g_dNames - 10); }

float TimelineView::f_TimeAt(double l_dX) const
{
    const float l_fT = float((l_dX - g_dNames) / (width() - g_dNames - 10) * f_Span());
    return std::clamp(std::round(l_fT * 100) / 100, 0.0f, f_Span());
}

QRectF TimelineView::f_LaneRect(int l_iI) const { return QRectF(0, g_dRuler + l_iI * g_dLane, width(), g_dLane); }

QRectF TimelineView::f_BarRect(int l_iI) const
{
    const Effect* l_pE = m_pDoc->f_Effect();
    if (!l_pE || l_iI < 0 || l_iI >= int(l_pE->m_vEmitters.size())) return QRectF();
    const Emitter& l_oM = l_pE->m_vEmitters[size_t(l_iI)];
    const float l_fEnd = l_oM.m_fStop > 0 ? l_oM.m_fStop : f_Span();
    const QRectF l_oLane = f_LaneRect(l_iI);
    return QRectF(QPointF(f_XOf(l_oM.m_fStart), l_oLane.top() + 4), QPointF(f_XOf(l_fEnd), l_oLane.bottom() - 4));
}

void TimelineView::paintEvent(QPaintEvent*)
{
    QPainter l_oP(this);
    const Theme::Colors& l_oTc = Theme::f_Colors();
    l_oP.fillRect(rect(), palette().color(QPalette::Base));
    const Effect* l_pE = m_pDoc->f_Effect();
    // Ruler.
    l_oP.fillRect(QRectF(0, 0, width(), g_dRuler), palette().color(QPalette::AlternateBase));
    const float l_fS = f_Span();
    const float l_fStep = l_fS <= 2 ? 0.25f : l_fS <= 6 ? 0.5f : l_fS <= 20 ? 1.0f : 5.0f;
    l_oP.setPen(palette().color(QPalette::Text));
    for (float l_fT = 0; l_fT <= l_fS + 1e-4f; l_fT += l_fStep) {
        const double l_dX = f_XOf(l_fT);
        const bool l_bMajor = std::fabs(std::fmod(l_fT + 1e-4f, l_fStep * 2)) < 1e-3f;
        l_oP.drawLine(QPointF(l_dX, g_dRuler - (l_bMajor ? 9 : 5)), QPointF(l_dX, g_dRuler));
        if (l_bMajor) l_oP.drawText(QRectF(l_dX + 2, 1, 50, 12), Qt::AlignLeft | Qt::AlignTop, QString::number(l_fT, 'g', 3));
    }
    if (!l_pE) return;
    if (l_pE->m_fDuration <= 0) {
        l_oP.setPen(palette().color(QPalette::PlaceholderText));
        l_oP.drawText(QRectF(4, 2, g_dNames - 6, g_dRuler - 4), Qt::AlignLeft | Qt::AlignVCenter, tr("endless"));
    }
    // Lanes.
    for (int l_iI = 0; l_iI < int(l_pE->m_vEmitters.size()); l_iI++) {
        const Emitter& l_oM = l_pE->m_vEmitters[size_t(l_iI)];
        const QRectF l_oLane = f_LaneRect(l_iI);
        if (l_iI == m_pDoc->f_EmitterIndex()) l_oP.fillRect(l_oLane, QColor(l_oTc.m_oSelection.red(), l_oTc.m_oSelection.green(), l_oTc.m_oSelection.blue(), 45));
        l_oP.setPen(palette().color(QPalette::Text));
        l_oP.drawText(l_oLane.adjusted(6, 0, -(width() - g_dNames), 0), Qt::AlignVCenter | Qt::AlignLeft,
                   fontMetrics().elidedText(QString::fromStdString(l_oM.m_strName), Qt::ElideRight, int(g_dNames - 10)));
        const QRectF l_oBar = f_BarRect(l_iI);
        if (l_oM.m_fRate > 0) {
            QColor l_oC = l_oM.m_oBlend == toms::fx::Blend::f_Add() ? QColor(230, 150, 60) : QColor(110, 150, 220);
            l_oP.setPen(Qt::NoPen);
            l_oP.setBrush(l_oC);
            l_oP.drawRoundedRect(l_oBar, 3, 3);
            if (l_oM.m_fStop <= 0) {   // open-ended: until the effect ends
                l_oP.setPen(QPen(Qt::white, 1));
                l_oP.drawText(l_oBar.adjusted(0, 0, -4, 0), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("→"));
            }
        } else {
            l_oP.setPen(QPen(palette().color(QPalette::Mid), 1, Qt::DashLine));
            l_oP.setBrush(Qt::NoBrush);
            l_oP.drawRoundedRect(l_oBar, 3, 3);
        }
        // Bursts.
        l_oP.setPen(QPen(Qt::black, 1));
        l_oP.setBrush(l_oTc.m_oKey);
        const float l_fEnd = std::min(f_Span(), l_oM.m_fStop > 0 ? l_oM.m_fStop : f_Span());
        for (const toms::fx::Burst& l_oB : l_oM.m_vBursts) {
            const int l_iTimes = l_oB.m_iRepeat < 0 || l_oB.m_fInterval <= 0 ? (l_oB.m_fInterval > 0 ? 1000 : 1) : l_oB.m_iRepeat + 1;
            for (int l_iK = 0; l_iK < l_iTimes; l_iK++) {
                const float l_fT = l_oM.m_fStart + l_oB.m_fT + (l_oB.m_fInterval > 0 ? l_iK * l_oB.m_fInterval : 0);
                if (l_fT > l_fEnd + 1e-4f) break;
                const QPointF l_oC(f_XOf(l_fT), l_oLane.center().y());
                QPolygonF l_oD;
                l_oD << l_oC + QPointF(0, -6) << l_oC + QPointF(5, 0) << l_oC + QPointF(0, 6) << l_oC + QPointF(-5, 0);
                l_oP.drawPolygon(l_oD);
            }
        }
    }
    // Playhead.
    const double l_dX = f_XOf(std::min(m_pPlay->f_Time(), l_fS));
    l_oP.setPen(QPen(QColor(230, 60, 60), 2));
    l_oP.drawLine(QPointF(l_dX, 0), QPointF(l_dX, height()));
}

void TimelineView::mousePressEvent(QMouseEvent* l_pE)
{
    if (l_pE->button() != Qt::LeftButton) return;
    m_iGesture++;
    const QPointF l_oPos = l_pE->position();
    if (l_oPos.y() < g_dRuler) {
        m_eDrag = Drag::Seek;
        m_pPlay->f_Seek(f_TimeAt(l_oPos.x()));
        return;
    }
    const Effect* l_pEf = m_pDoc->f_Effect();
    const int l_iLane = int((l_oPos.y() - g_dRuler) / g_dLane);
    if (!l_pEf || l_iLane < 0 || l_iLane >= int(l_pEf->m_vEmitters.size())) return;
    m_pDoc->f_Select(m_pDoc->f_EffectIndex(), l_iLane);
    const QRectF l_oBar = f_BarRect(l_iLane);
    const Emitter& l_oM = l_pEf->m_vEmitters[size_t(l_iLane)];
    m_iLane = l_iLane;
    m_fPressT = f_TimeAt(l_oPos.x());
    m_fStartStart = l_oM.m_fStart;
    m_fStartStop = l_oM.m_fStop;
    if (std::fabs(l_oPos.x() - l_oBar.left()) <= 5) m_eDrag = Drag::Start;
    else if (std::fabs(l_oPos.x() - l_oBar.right()) <= 5) m_eDrag = Drag::Stop;
    else if (l_oBar.contains(l_oPos)) m_eDrag = Drag::Move;
    else m_eDrag = Drag::None;
}

void TimelineView::mouseMoveEvent(QMouseEvent* l_pE)
{
    const QPointF l_oPos = l_pE->position();
    if (m_eDrag == Drag::None) {
        const Effect* l_pEf = m_pDoc->f_Effect();
        const int l_iLane = int((l_oPos.y() - g_dRuler) / g_dLane);
        Qt::CursorShape l_eCur = Qt::ArrowCursor;
        if (l_pEf && l_oPos.y() >= g_dRuler && l_iLane >= 0 && l_iLane < int(l_pEf->m_vEmitters.size())) {
            const QRectF l_oBar = f_BarRect(l_iLane);
            if (std::fabs(l_oPos.x() - l_oBar.left()) <= 5 || std::fabs(l_oPos.x() - l_oBar.right()) <= 5) l_eCur = Qt::SizeHorCursor;
            else if (l_oBar.contains(l_oPos)) l_eCur = Qt::OpenHandCursor;
        }
        setCursor(l_eCur);
        return;
    }
    const float l_fT = f_TimeAt(l_oPos.x());
    const QString l_oKey = QStringLiteral("timeline%1").arg(m_iGesture);
    switch (m_eDrag) {
    case Drag::Seek: m_pPlay->f_Seek(l_fT); break;
    case Drag::Start:
        m_pDoc->f_EditEmitter(tr("Emitter start"), [&](Emitter& l_oM) {
            l_oM.m_fStart = l_oM.m_fStop > 0 ? std::min(l_fT, l_oM.m_fStop - 0.01f) : l_fT;
        }, l_oKey);
        break;
    case Drag::Stop:
        m_pDoc->f_EditEmitter(tr("Emitter stop"), [&](Emitter& l_oM) {
            l_oM.m_fStop = l_fT >= f_Span() - 1e-3f ? 0.0f : std::max(l_fT, l_oM.m_fStart + 0.01f);   // to the very end = open-ended
        }, l_oKey);
        break;
    case Drag::Move: {
        const float l_fD = l_fT - m_fPressT;
        m_pDoc->f_EditEmitter(tr("Move emitter in time"), [&](Emitter& l_oM) {
            const float l_fShift = std::max(l_fD, -m_fStartStart);
            l_oM.m_fStart = m_fStartStart + l_fShift;
            if (m_fStartStop > 0) l_oM.m_fStop = m_fStartStop + l_fShift;
        }, l_oKey);
        break;
    }
    case Drag::None: break;
    }
}

void TimelineView::mouseReleaseEvent(QMouseEvent*) { m_eDrag = Drag::None; }

TimelineDock::TimelineDock(ParticleDocument* l_pDoc, ParticlePlayback* l_pPlayback, QWidget* l_pParent)
    : QDockWidget(tr("Timeline"), l_pParent)
    , m_pView(new TimelineView(l_pDoc, l_pPlayback, this))
{
    setObjectName(QStringLiteral("ParticleTimelineDock"));
    auto* l_pScroll = new QScrollArea(this);
    l_pScroll->setWidgetResizable(true);
    l_pScroll->setFrameShape(QFrame::NoFrame);
    l_pScroll->setWidget(m_pView);
    setWidget(l_pScroll);
}

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

const char* g_pszSpriteMime = "application/x-toms-sprite";

QString f_Qs(const std::string& l_strS) { return QString::fromStdString(l_strS); }

QDoubleSpinBox* f_Dspin(double l_dLo, double l_dHi, int l_iDecimals, double l_dStep, const QString& l_oSuffix = QString())
{
    auto* l_pS = new QDoubleSpinBox;
    l_pS->setRange(l_dLo, l_dHi);
    l_pS->setDecimals(l_iDecimals);
    l_pS->setSingleStep(l_dStep);
    l_pS->setSuffix(l_oSuffix);
    l_pS->setKeyboardTracking(false);
    return l_pS;
}

// The frames list takes sprites dropped from the Sprites dock and reorders by dragging; Delete removes.
class FramesFilter : public QObject
{
public:
    FramesFilter(QListWidget* l_pList, std::function<void()> l_fnChanged) : QObject(l_pList), m_pList(l_pList), m_fnChanged(std::move(l_fnChanged))
    {
        l_pList->viewport()->installEventFilter(this);
        l_pList->installEventFilter(this);
    }
    bool eventFilter(QObject* l_pO, QEvent* l_pE) override
    {
        if (l_pO == m_pList && l_pE->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(l_pE)->key() == Qt::Key_Delete) {
            qDeleteAll(m_pList->selectedItems());
            m_fnChanged();
            return true;
        }
        if (l_pO != m_pList->viewport()) return false;
        if (l_pE->type() == QEvent::DragEnter || l_pE->type() == QEvent::DragMove) {
            auto* l_pD = static_cast<QDragMoveEvent*>(l_pE);
            if (!l_pD->mimeData()->hasFormat(QLatin1String(g_pszSpriteMime))) return false;
            l_pD->setDropAction(Qt::CopyAction);
            l_pD->accept();
            return true;
        }
        if (l_pE->type() == QEvent::Drop) {
            auto* l_pD = static_cast<QDropEvent*>(l_pE);
            if (!l_pD->mimeData()->hasFormat(QLatin1String(g_pszSpriteMime))) return false;
            const QStringList l_oRefs = QString::fromUtf8(l_pD->mimeData()->data(QLatin1String(g_pszSpriteMime))).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            QListWidgetItem* l_pAt = m_pList->itemAt(l_pD->position().toPoint());
            int l_iRow = l_pAt ? m_pList->row(l_pAt) : m_pList->count();
            for (const QString& l_oR : l_oRefs) m_pList->insertItem(l_iRow++, l_oR);
            l_pD->setDropAction(Qt::CopyAction);
            l_pD->accept();
            m_fnChanged();
            return true;
        }
        return false;
    }

private:
    QListWidget* m_pList;
    std::function<void()> m_fnChanged;
};

}  // namespace

QWidget* InspectorDock::f_Section(QVBoxLayout* l_pInto, const QString& l_oTitle, QFormLayout** l_pForm)
{
    auto* l_pBox = new QWidget;
    auto* l_pV = new QVBoxLayout(l_pBox);
    l_pV->setContentsMargins(0, 0, 0, 0);
    l_pV->setSpacing(2);
    auto* l_pHead = new QToolButton;
    l_pHead->setText(l_oTitle);
    l_pHead->setCheckable(true);
    l_pHead->setChecked(true);
    l_pHead->setArrowType(Qt::DownArrow);
    l_pHead->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    l_pHead->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    l_pHead->setStyleSheet(QStringLiteral("QToolButton { font-weight: bold; text-align: left; border: none; padding: 4px; }"));
    auto* l_pBody = new QWidget;
    *l_pForm = new QFormLayout(l_pBody);
    (*l_pForm)->setContentsMargins(14, 0, 4, 6);
    (*l_pForm)->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    connect(l_pHead, &QToolButton::toggled, this, [l_pHead, l_pBody](bool l_bOpen) {
        l_pBody->setVisible(l_bOpen);
        l_pHead->setArrowType(l_bOpen ? Qt::DownArrow : Qt::RightArrow);
    });
    l_pV->addWidget(l_pHead);
    l_pV->addWidget(l_pBody);
    l_pInto->addWidget(l_pBox);
    return l_pBox;
}

QString InspectorDock::f_MergeKey(const QString& l_oField)
{
    if (l_oField != m_oLastField || !m_oFieldClock.isValid() || m_oFieldClock.elapsed() > 1200) m_iSerial++;
    m_oLastField = l_oField;
    m_oFieldClock.restart();
    return l_oField + QString::number(m_iSerial);
}

InspectorDock::InspectorDock(ParticleDocument* l_pDoc, QWidget* l_pParent)
    : QDockWidget(tr("Inspector"), l_pParent)
    , m_pDoc(l_pDoc)
{
    setObjectName(QStringLiteral("ParticleInspectorDock"));
    auto* l_pScroll = new QScrollArea(this);
    l_pScroll->setWidgetResizable(true);
    l_pScroll->setFrameShape(QFrame::NoFrame);
    auto* l_pBody = new QWidget;
    auto* l_pV = new QVBoxLayout(l_pBody);
    l_pV->setContentsMargins(4, 4, 4, 4);
    l_pV->setSpacing(2);
    l_pScroll->setWidget(l_pBody);
    setWidget(l_pScroll);

    // Shorthands: an edit of the selected emitter / effect with a merge key per field.
    auto l_fnEm = [this](const QString& l_oField, const QString& l_oText, std::function<void(Emitter&)> l_fnF) {
        if (!m_bUpdating) m_pDoc->f_EditEmitter(l_oText, l_fnF, f_MergeKey(l_oField));
    };
    auto l_fnEf = [this](const QString& l_oField, const QString& l_oText, std::function<void(Effect&)> l_fnF) {
        if (!m_bUpdating) m_pDoc->f_EditEffect(l_oText, l_fnF, f_MergeKey(l_oField));
    };
    QFormLayout* l_pF = nullptr;

    // ---- Effect
    m_pEffectSection = f_Section(l_pV, tr("Effect"), &l_pF);
    m_pEffectName = new QLineEdit;
    m_pDuration = f_Dspin(0, 600, 2, 0.1, tr(" s"));
    m_pDuration->setSpecialValueText(tr("endless"));
    m_pDuration->setToolTip(tr("0 = endless (until stopped). A one-shot stops making particles at its end."));
    m_pLoop = new QCheckBox(tr("start over at the end"));
    m_pPrewarm = f_Dspin(0, 60, 2, 0.5, tr(" s"));
    m_pPrewarm->setToolTip(tr("Simulated before the first frame: an already-burning fire"));
    m_pSeed = new QSpinBox;
    m_pSeed->setRange(0, 1000000000);
    m_pSeed->setSpecialValueText(tr("new each play"));
    l_pF->addRow(tr("Name"), m_pEffectName);
    l_pF->addRow(tr("Duration"), m_pDuration);
    l_pF->addRow(tr("Loop"), m_pLoop);
    l_pF->addRow(tr("Prewarm"), m_pPrewarm);
    l_pF->addRow(tr("Seed"), m_pSeed);
    connect(m_pEffectName, &QLineEdit::editingFinished, this, [this, l_fnEf] {
        const QString l_oN = m_pEffectName->text().trimmed();
        if (l_oN.isEmpty() || (m_pDoc->f_Effect() && f_Qs(m_pDoc->f_Effect()->m_strName) == l_oN)) return;
        l_fnEf(QStringLiteral("name"), tr("Rename effect"), [l_oN](Effect& l_oE) { l_oE.m_strName = l_oN.toStdString(); });
    });
    connect(m_pDuration, &QDoubleSpinBox::valueChanged, this, [l_fnEf](double l_dX) { l_fnEf(QStringLiteral("duration"), tr("Duration"), [l_dX](Effect& l_oE) { l_oE.m_fDuration = float(l_dX); }); });
    connect(m_pLoop, &QCheckBox::toggled, this, [l_fnEf](bool l_bOn) { l_fnEf(QStringLiteral("loop"), tr("Loop"), [l_bOn](Effect& l_oE) { l_oE.m_bLoop = l_bOn; }); });
    connect(m_pPrewarm, &QDoubleSpinBox::valueChanged, this, [l_fnEf](double l_dX) { l_fnEf(QStringLiteral("prewarm"), tr("Prewarm"), [l_dX](Effect& l_oE) { l_oE.m_fPrewarm = float(l_dX); }); });
    connect(m_pSeed, &QSpinBox::valueChanged, this, [l_fnEf](int l_iX) { l_fnEf(QStringLiteral("seed"), tr("Seed"), [l_iX](Effect& l_oE) { l_oE.m_uSeed = uint32_t(l_iX); }); });

    m_pNone = new QLabel(tr("Select an emitter in the Effects dock to edit it."));
    m_pNone->setWordWrap(true);
    m_pNone->setMargin(8);
    l_pV->addWidget(m_pNone);

    m_pEmitterSections = new QWidget;
    auto* l_pEv = new QVBoxLayout(m_pEmitterSections);
    l_pEv->setContentsMargins(0, 0, 0, 0);
    l_pEv->setSpacing(2);
    l_pV->addWidget(m_pEmitterSections);
    l_pV->addStretch(1);

    // ---- Emitter
    f_Section(l_pEv, tr("Emitter"), &l_pF);
    m_pEmitterName = new QLineEdit;
    m_pOffset = new Vec2Edit(-10000, 10000, 1);
    m_pStart = f_Dspin(0, 600, 2, 0.1, tr(" s"));
    m_pStop = f_Dspin(0, 600, 2, 0.1, tr(" s"));
    m_pStop->setSpecialValueText(tr("until the end"));
    m_pSpace = new QComboBox;
    m_pSpace->addItems({tr("world: particles stay where born"), tr("local: particles move with the effect")});
    m_pMax = new QSpinBox;
    m_pMax->setRange(1, 100000);
    l_pF->addRow(tr("Name"), m_pEmitterName);
    l_pF->addRow(tr("Offset"), m_pOffset);
    l_pF->addRow(tr("Start"), m_pStart);
    l_pF->addRow(tr("Stop"), m_pStop);
    l_pF->addRow(tr("Space"), m_pSpace);
    l_pF->addRow(tr("Max particles"), m_pMax);
    connect(m_pEmitterName, &QLineEdit::editingFinished, this, [this, l_fnEm] {
        const QString l_oN = m_pEmitterName->text().trimmed();
        if (l_oN.isEmpty() || (m_pDoc->f_Emitter() && f_Qs(m_pDoc->f_Emitter()->m_strName) == l_oN)) return;
        l_fnEm(QStringLiteral("ename"), tr("Rename emitter"), [l_oN](Emitter& l_oM) { l_oM.m_strName = l_oN.toStdString(); });
    });
    connect(m_pOffset, &Vec2Edit::f_Changed, this, [l_fnEm](const glm::vec2& l_v2O) { l_fnEm(QStringLiteral("offset"), tr("Offset"), [l_v2O](Emitter& l_oM) { l_oM.m_v2Offset = l_v2O; }); });
    connect(m_pStart, &QDoubleSpinBox::valueChanged, this, [l_fnEm](double l_dX) { l_fnEm(QStringLiteral("start"), tr("Start"), [l_dX](Emitter& l_oM) { l_oM.m_fStart = float(l_dX); }); });
    connect(m_pStop, &QDoubleSpinBox::valueChanged, this, [l_fnEm](double l_dX) { l_fnEm(QStringLiteral("stop"), tr("Stop"), [l_dX](Emitter& l_oM) { l_oM.m_fStop = float(l_dX); }); });
    connect(m_pSpace, &QComboBox::currentIndexChanged, this, [l_fnEm](int l_iI) { l_fnEm(QStringLiteral("space"), tr("Space"), [l_iI](Emitter& l_oM) { l_oM.m_bLocalSpace = l_iI == 1; }); });
    connect(m_pMax, &QSpinBox::valueChanged, this, [l_fnEm](int l_iX) { l_fnEm(QStringLiteral("max"), tr("Max particles"), [l_iX](Emitter& l_oM) { l_oM.m_iMaxParticles = l_iX; }); });

    // ---- Emission
    f_Section(l_pEv, tr("Emission"), &l_pF);
    m_pRate = f_Dspin(0, 10000, 2, 1, tr(" /s"));
    l_pF->addRow(tr("Rate"), m_pRate);
    m_pBursts = new QTableWidget(0, 5);
    m_pBursts->setHorizontalHeaderLabels({tr("t (s)"), tr("min"), tr("max"), tr("repeat"), tr("every (s)")});
    m_pBursts->verticalHeader()->setVisible(false);
    m_pBursts->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_pBursts->setMaximumHeight(110);
    m_pBursts->setToolTip(tr("A burst fires 'min..max' particles at emitter time t, then 'repeat' more times every 'every' seconds (-1 = while active)"));
    auto* l_pBurstButtons = new QWidget;
    {
        auto* l_pH = new QHBoxLayout(l_pBurstButtons);
        l_pH->setContentsMargins(0, 0, 0, 0);
        auto* l_pAdd = new QToolButton;
        l_pAdd->setText(tr("+ Burst"));
        auto* l_pDel = new QToolButton;
        l_pDel->setText(tr("− Burst"));
        l_pH->addWidget(l_pAdd);
        l_pH->addWidget(l_pDel);
        l_pH->addStretch(1);
        connect(l_pAdd, &QToolButton::clicked, this, [this] {
            m_pDoc->f_EditEmitter(tr("Add burst"), [](Emitter& l_oM) {
                Burst l_oB;
                l_oB.m_fT = l_oM.m_vBursts.empty() ? 0.0f : l_oM.m_vBursts.back().m_fT + 0.5f;
                l_oM.m_vBursts.push_back(l_oB);
            });
        });
        connect(l_pDel, &QToolButton::clicked, this, [this] {
            const int l_iRow = m_pBursts->currentRow() >= 0 ? m_pBursts->currentRow() : m_pBursts->rowCount() - 1;
            if (l_iRow < 0) return;
            m_pDoc->f_EditEmitter(tr("Delete burst"), [l_iRow](Emitter& l_oM) {
                if (l_iRow < int(l_oM.m_vBursts.size())) l_oM.m_vBursts.erase(l_oM.m_vBursts.begin() + l_iRow);
            });
        });
    }
    l_pF->addRow(tr("Bursts"), m_pBursts);
    l_pF->addRow(QString(), l_pBurstButtons);
    connect(m_pRate, &QDoubleSpinBox::valueChanged, this, [l_fnEm](double l_dX) { l_fnEm(QStringLiteral("rate"), tr("Rate"), [l_dX](Emitter& l_oM) { l_oM.m_fRate = float(l_dX); }); });
    // Queued: the refresh that follows replaces the table's items, not while one is signalling.
    connect(m_pBursts, &QTableWidget::itemChanged, this, [this] {
        if (!m_bUpdating) QMetaObject::invokeMethod(this, &InspectorDock::f_CommitBursts, Qt::QueuedConnection);
    });

    // ---- Shape
    f_Section(l_pEv, tr("Shape"), &m_pShapeForm);
    l_pF = m_pShapeForm;
    m_pShapeType = new QComboBox;
    for (int l_iT = 0; l_iT <= Shape::Ring; l_iT++) m_pShapeType->addItem(QString::fromLatin1(toms::fx::f_ShapeName(Shape::Type(l_iT))));
    m_pLength = f_Dspin(0, 10000, 1, 1, tr(" px"));
    m_pAngle = f_Dspin(-360, 360, 1, 5, tr("°"));
    m_pWidth = f_Dspin(0, 10000, 1, 1, tr(" px"));
    m_pHeight = f_Dspin(0, 10000, 1, 1, tr(" px"));
    m_pRadius = f_Dspin(0, 10000, 1, 1, tr(" px"));
    m_pThickness = f_Dspin(0, 10000, 1, 1, tr(" px"));
    m_pArcFrom = f_Dspin(-360, 360, 0, 5, tr("°"));
    m_pArcTo = f_Dspin(-360, 720, 0, 5, tr("°"));
    m_pEdge = new QCheckBox(tr("only on the outline"));
    m_pOutward = new QCheckBox(tr("fly away from the centre"));
    m_pOutward->setToolTip(tr("Instead of Direction: each particle leaves away from the shape's centre (a point: every direction)"));
    l_pF->addRow(tr("Type"), m_pShapeType);
    l_pF->addRow(tr("Length"), m_pLength);
    l_pF->addRow(tr("Angle"), m_pAngle);
    l_pF->addRow(tr("Width"), m_pWidth);
    l_pF->addRow(tr("Height"), m_pHeight);
    l_pF->addRow(tr("Radius"), m_pRadius);
    l_pF->addRow(tr("Thickness"), m_pThickness);
    l_pF->addRow(tr("Arc from"), m_pArcFrom);
    l_pF->addRow(tr("Arc to"), m_pArcTo);
    l_pF->addRow(tr("Edge"), m_pEdge);
    l_pF->addRow(tr("Outward"), m_pOutward);
    connect(m_pShapeType, &QComboBox::currentIndexChanged, this, [this, l_fnEm](int l_iI) {
        f_UpdateShapeRows();
        l_fnEm(QStringLiteral("shape"), tr("Shape"), [l_iI](Emitter& l_oM) {
            l_oM.m_oShape.m_eType = Shape::Type(l_iI);
            // A sensible size so the new shape is visible.
            if (l_iI == Shape::Line && l_oM.m_oShape.m_fLength <= 0) l_oM.m_oShape.m_fLength = 40;
            if (l_iI == Shape::Box && l_oM.m_oShape.m_fWidth <= 0) { l_oM.m_oShape.m_fWidth = 40; l_oM.m_oShape.m_fHeight = 20; }
            if ((l_iI == Shape::Circle || l_iI == Shape::Ring) && l_oM.m_oShape.m_fRadius <= 0) l_oM.m_oShape.m_fRadius = 20;
            if (l_iI == Shape::Ring && l_oM.m_oShape.m_fThickness <= 0) l_oM.m_oShape.m_fThickness = 4;
        });
    });
    auto l_fnShapeField = [&](QDoubleSpinBox* l_pS, const char* l_pszKey, std::function<void(Shape&, float)> l_fnSet) {
        connect(l_pS, &QDoubleSpinBox::valueChanged, this, [l_fnEm, l_pszKey, l_fnSet](double l_dX) {
            l_fnEm(QString::fromLatin1(l_pszKey), tr("Shape"), [l_fnSet, l_dX](Emitter& l_oM) { l_fnSet(l_oM.m_oShape, float(l_dX)); });
        });
    };
    l_fnShapeField(m_pLength, "slength", [](Shape& l_oS, float l_fX) { l_oS.m_fLength = l_fX; });
    l_fnShapeField(m_pAngle, "sangle", [](Shape& l_oS, float l_fX) { l_oS.m_fAngle = l_fX; });
    l_fnShapeField(m_pWidth, "swidth", [](Shape& l_oS, float l_fX) { l_oS.m_fWidth = l_fX; });
    l_fnShapeField(m_pHeight, "sheight", [](Shape& l_oS, float l_fX) { l_oS.m_fHeight = l_fX; });
    l_fnShapeField(m_pRadius, "sradius", [](Shape& l_oS, float l_fX) { l_oS.m_fRadius = l_fX; });
    l_fnShapeField(m_pThickness, "sthick", [](Shape& l_oS, float l_fX) { l_oS.m_fThickness = l_fX; });
    l_fnShapeField(m_pArcFrom, "sarc0", [](Shape& l_oS, float l_fX) { l_oS.m_fArcFrom = l_fX; });
    l_fnShapeField(m_pArcTo, "sarc1", [](Shape& l_oS, float l_fX) { l_oS.m_fArcTo = l_fX; });
    connect(m_pEdge, &QCheckBox::toggled, this, [l_fnEm](bool l_bOn) { l_fnEm(QStringLiteral("sedge"), tr("Shape edge"), [l_bOn](Emitter& l_oM) { l_oM.m_oShape.m_bEdge = l_bOn; }); });
    connect(m_pOutward, &QCheckBox::toggled, this, [l_fnEm](bool l_bOn) { l_fnEm(QStringLiteral("sout"), tr("Outward"), [l_bOn](Emitter& l_oM) { l_oM.m_oShape.m_bOutward = l_bOn; }); });

    // ---- Start values
    f_Section(l_pEv, tr("Start values"), &l_pF);
    m_pLife = new RangeEdit(0.01, 600, 2, 0.1, tr(" s"));
    m_pDirection = f_Dspin(-360, 360, 0, 5, tr("°"));
    m_pDirection->setToolTip(tr("-90 = up, 0 = right, 90 = down"));
    m_pSpread = f_Dspin(0, 180, 0, 5, tr("°"));
    m_pSpread->setPrefix(QStringLiteral("± "));
    m_pSpeed = new RangeEdit(0, 100000, 1, 5, tr(" px/s"));
    m_pSize = new RangeEdit(0, 10000, 1, 1, tr(" px"));
    m_pRotation = new RangeEdit(-3600, 3600, 0, 5, tr("°"));
    m_pSpin = new RangeEdit(-36000, 36000, 0, 10, tr("°/s"));
    auto* l_pColors = new QWidget;
    {
        auto* l_pH = new QHBoxLayout(l_pColors);
        l_pH->setContentsMargins(0, 0, 0, 0);
        m_pColor = new ColorButton;
        m_pColor2 = new ColorButton;
        m_pRandomColor = new QCheckBox(tr("random between"));
        l_pH->addWidget(m_pColor);
        l_pH->addWidget(m_pRandomColor);
        l_pH->addWidget(m_pColor2);
        l_pH->addStretch(1);
    }
    m_pSprite = new QComboBox;
    m_pSprite->setEditable(true);
    m_pSprite->setInsertPolicy(QComboBox::NoInsert);
    m_pSprite->setToolTip(tr("The sprite as \"atlas:name\" (or double-click one in the Sprites dock). A flipbook replaces it."));
    l_pF->addRow(tr("Life"), m_pLife);
    l_pF->addRow(tr("Direction"), m_pDirection);
    l_pF->addRow(tr("Spread"), m_pSpread);
    l_pF->addRow(tr("Speed"), m_pSpeed);
    l_pF->addRow(tr("Size"), m_pSize);
    l_pF->addRow(tr("Rotation"), m_pRotation);
    l_pF->addRow(tr("Spin"), m_pSpin);
    l_pF->addRow(tr("Colour"), l_pColors);
    l_pF->addRow(tr("Sprite"), m_pSprite);
    auto l_fnRange = [&](RangeEdit* l_pR, const char* l_pszKey, const QString& l_oText, std::function<void(Emitter&, const Range&)> l_fnSet) {
        connect(l_pR, &RangeEdit::f_Changed, this, [l_fnEm, l_pszKey, l_oText, l_fnSet](const Range& l_oX) {
            l_fnEm(QString::fromLatin1(l_pszKey), l_oText, [l_fnSet, l_oX](Emitter& l_oM) { l_fnSet(l_oM, l_oX); });
        });
    };
    l_fnRange(m_pLife, "life", tr("Life"), [](Emitter& l_oM, const Range& l_oR) { l_oM.m_oLife = l_oR; });
    l_fnRange(m_pSpeed, "speed", tr("Speed"), [](Emitter& l_oM, const Range& l_oR) { l_oM.m_oSpeed = l_oR; });
    l_fnRange(m_pSize, "size", tr("Size"), [](Emitter& l_oM, const Range& l_oR) { l_oM.m_oSize = l_oR; });
    l_fnRange(m_pRotation, "rotation", tr("Rotation"), [](Emitter& l_oM, const Range& l_oR) { l_oM.m_oRotation = l_oR; });
    l_fnRange(m_pSpin, "spin", tr("Spin"), [](Emitter& l_oM, const Range& l_oR) { l_oM.m_oSpin = l_oR; });
    connect(m_pDirection, &QDoubleSpinBox::valueChanged, this, [l_fnEm](double l_dX) { l_fnEm(QStringLiteral("dir"), tr("Direction"), [l_dX](Emitter& l_oM) { l_oM.m_fDirection = float(l_dX); }); });
    connect(m_pSpread, &QDoubleSpinBox::valueChanged, this, [l_fnEm](double l_dX) { l_fnEm(QStringLiteral("spread"), tr("Spread"), [l_dX](Emitter& l_oM) { l_oM.m_fSpread = float(l_dX); }); });
    connect(m_pColor, &ColorButton::f_Changed, this, [this, l_fnEm](const glm::vec4& l_v4C) {
        const bool l_bRnd = m_pRandomColor->isChecked();
        l_fnEm(QStringLiteral("color"), tr("Colour"), [l_v4C, l_bRnd](Emitter& l_oM) {
            l_oM.m_v4Color = l_v4C;
            if (!l_bRnd) l_oM.m_v4Color2 = l_v4C;
        });
    });
    connect(m_pColor2, &ColorButton::f_Changed, this, [l_fnEm](const glm::vec4& l_v4C) { l_fnEm(QStringLiteral("color2"), tr("Colour"), [l_v4C](Emitter& l_oM) { l_oM.m_v4Color2 = l_v4C; }); });
    connect(m_pRandomColor, &QCheckBox::toggled, this, [this, l_fnEm](bool l_bOn) {
        m_pColor2->setEnabled(l_bOn);
        l_fnEm(QStringLiteral("rcolor"), tr("Random colour"), [l_bOn](Emitter& l_oM) {
            if (!l_bOn) l_oM.m_v4Color2 = l_oM.m_v4Color;
            else if (l_oM.m_v4Color2 == l_oM.m_v4Color) l_oM.m_v4Color2 = glm::vec4(1.0f);
        });
    });
    auto l_fnCommitSprite = [this, l_fnEm] {
        const QString l_oS = m_pSprite->currentText().trimmed();
        if (m_pDoc->f_Emitter() && f_Qs(m_pDoc->f_Emitter()->m_strSprite) == l_oS) return;
        l_fnEm(QStringLiteral("sprite"), tr("Sprite"), [l_oS](Emitter& l_oM) { l_oM.m_strSprite = l_oS.toStdString(); });
    };
    connect(m_pSprite, &QComboBox::activated, this, l_fnCommitSprite);
    connect(m_pSprite->lineEdit(), &QLineEdit::editingFinished, this, l_fnCommitSprite);

    // ---- Over life
    f_Section(l_pEv, tr("Over life"), &l_pF);
    m_pColorCurve = new GradientEdit;
    m_pSizeCurve = new CurveEdit;
    m_pSpeedCurve = new CurveEdit;
    m_pSpinCurve = new CurveEdit;
    l_pF->addRow(tr("Colour"), m_pColorCurve);
    l_pF->addRow(tr("Size ×"), m_pSizeCurve);
    l_pF->addRow(tr("Speed ×"), m_pSpeedCurve);
    l_pF->addRow(tr("Spin ×"), m_pSpinCurve);
    connect(m_pColorCurve, &GradientEdit::f_Changed, this, [this](const toms::fx::Curve<glm::vec4>& l_oC) {
        if (!m_bUpdating) m_pDoc->f_EditEmitter(tr("Colour over life"), [l_oC](Emitter& l_oM) { l_oM.m_oColorOverLife = l_oC; },
                                            QStringLiteral("ccurve%1").arg(m_pColorCurve->f_Gesture()));
    });
    auto l_fnCurve = [&](CurveEdit* l_pW, const char* l_pszKey, const QString& l_oText, std::function<void(Emitter&, const toms::fx::Curve<float>&)> l_fnSet) {
        connect(l_pW, &CurveEdit::f_Changed, this, [this, l_pW, l_pszKey, l_oText, l_fnSet](const toms::fx::Curve<float>& l_oC) {
            if (!m_bUpdating) m_pDoc->f_EditEmitter(l_oText, [l_fnSet, l_oC](Emitter& l_oM) { l_fnSet(l_oM, l_oC); }, QStringLiteral("%1%2").arg(QLatin1String(l_pszKey)).arg(l_pW->f_Gesture()));
        });
    };
    l_fnCurve(m_pSizeCurve, "zcurve", tr("Size over life"), [](Emitter& l_oM, const toms::fx::Curve<float>& l_oC) { l_oM.m_oSizeOverLife = l_oC; });
    l_fnCurve(m_pSpeedCurve, "vcurve", tr("Speed over life"), [](Emitter& l_oM, const toms::fx::Curve<float>& l_oC) { l_oM.m_oSpeedOverLife = l_oC; });
    l_fnCurve(m_pSpinCurve, "scurve", tr("Spin over life"), [](Emitter& l_oM, const toms::fx::Curve<float>& l_oC) { l_oM.m_oSpinOverLife = l_oC; });

    // ---- Forces
    f_Section(l_pEv, tr("Forces"), &l_pF);
    m_pGravity = new Vec2Edit(-100000, 100000, 1);
    m_pGravity->setToolTip(tr("px/s²; y down: [0, 300] falls, [0, -30] rises"));
    m_pDrag = f_Dspin(0, 100, 2, 0.1, tr(" /s"));
    m_pDrag->setToolTip(tr("Slows particles down: velocity × e^(-drag·t)"));
    m_pRadial = new RangeEdit(-100000, 100000, 1, 5, tr(" px/s²"));
    m_pRadial->setToolTip(tr("Away from the emitter (negative: towards it)"));
    m_pTangential = new RangeEdit(-100000, 100000, 1, 5, tr(" px/s²"));
    m_pTangential->setToolTip(tr("Around the emitter, clockwise (negative: anticlockwise): orbits, swirls"));
    l_pF->addRow(tr("Gravity"), m_pGravity);
    l_pF->addRow(tr("Drag"), m_pDrag);
    l_pF->addRow(tr("Radial"), m_pRadial);
    l_pF->addRow(tr("Tangential"), m_pTangential);
    connect(m_pGravity, &Vec2Edit::f_Changed, this, [l_fnEm](const glm::vec2& l_v2G) { l_fnEm(QStringLiteral("gravity"), tr("Gravity"), [l_v2G](Emitter& l_oM) { l_oM.m_v2Gravity = l_v2G; }); });
    connect(m_pDrag, &QDoubleSpinBox::valueChanged, this, [l_fnEm](double l_dX) { l_fnEm(QStringLiteral("drag"), tr("Drag"), [l_dX](Emitter& l_oM) { l_oM.m_fDrag = float(l_dX); }); });
    l_fnRange(m_pRadial, "radial", tr("Radial"), [](Emitter& l_oM, const Range& l_oR) { l_oM.m_oRadial = l_oR; });
    l_fnRange(m_pTangential, "tangential", tr("Tangential"), [](Emitter& l_oM, const Range& l_oR) { l_oM.m_oTangential = l_oR; });

    // ---- Flipbook
    f_Section(l_pEv, tr("Flipbook"), &l_pF);
    m_pFrames = new QListWidget;
    m_pFrames->setDragDropMode(QAbstractItemView::InternalMove);
    m_pFrames->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_pFrames->setMaximumHeight(110);
    m_pFrames->setToolTip(tr("Drop sprites here (several at once: in their order); drag to reorder; Delete removes. "
                            "Frames replace the Sprite."));
    new FramesFilter(m_pFrames, [this] { QMetaObject::invokeMethod(this, &InspectorDock::f_CommitFrames, Qt::QueuedConnection); });
    connect(m_pFrames->model(), &QAbstractItemModel::rowsMoved, this, [this] {
        if (!m_bUpdating) QMetaObject::invokeMethod(this, &InspectorDock::f_CommitFrames, Qt::QueuedConnection);
    });
    m_pFrameMode = new QComboBox;
    m_pFrameMode->addItems({tr("over the life (all frames once)"), tr("loop at fps")});
    m_pFps = f_Dspin(0.1, 240, 1, 1, tr(" fps"));
    m_pRandomFrame = new QCheckBox(tr("each particle starts on a random frame"));
    l_pF->addRow(tr("Frames"), m_pFrames);
    l_pF->addRow(tr("Mode"), m_pFrameMode);
    l_pF->addRow(tr("Speed"), m_pFps);
    l_pF->addRow(tr("Start"), m_pRandomFrame);
    connect(m_pFrameMode, &QComboBox::currentIndexChanged, this, [this, l_fnEm](int l_iI) {
        m_pFps->setEnabled(l_iI == 1);
        l_fnEm(QStringLiteral("fmode"), tr("Flipbook mode"), [l_iI](Emitter& l_oM) { l_oM.m_bFramesByFps = l_iI == 1; });
    });
    connect(m_pFps, &QDoubleSpinBox::valueChanged, this, [l_fnEm](double l_dX) { l_fnEm(QStringLiteral("fps"), tr("Flipbook fps"), [l_dX](Emitter& l_oM) { l_oM.m_fFps = float(l_dX); }); });
    connect(m_pRandomFrame, &QCheckBox::toggled, this, [l_fnEm](bool l_bOn) { l_fnEm(QStringLiteral("frand"), tr("Random frame"), [l_bOn](Emitter& l_oM) { l_oM.m_bRandomFrame = l_bOn; }); });

    // ---- Render
    f_Section(l_pEv, tr("Render"), &l_pF);
    m_pBlend = new QComboBox;
    m_pBlend->addItems({QStringLiteral("normal"), QStringLiteral("add"), QStringLiteral("multiply"), QStringLiteral("screen"), tr("custom…")});
    m_pBlend->setToolTip(tr("normal: paints over · add: adds light (fire, sparks, magic) · multiply: darkens (shadows) · "
                           "screen: soft light. multiply / screen / custom draw as normal in the game until phase 2b."));
    m_pSrc = new QComboBox;
    m_pDst = new QComboBox;
    for (int l_iI = 0; l_iI < Blend::FactorCount; l_iI++) {
        m_pSrc->addItem(QString::fromLatin1(Blend::f_FactorName(Blend::Factor(l_iI))));
        m_pDst->addItem(QString::fromLatin1(Blend::f_FactorName(Blend::Factor(l_iI))));
    }
    auto* l_pCustom = new QWidget;
    {
        auto* l_pH = new QHBoxLayout(l_pCustom);
        l_pH->setContentsMargins(0, 0, 0, 0);
        l_pH->addWidget(new QLabel(tr("src")));
        l_pH->addWidget(m_pSrc, 1);
        l_pH->addWidget(new QLabel(tr("dst")));
        l_pH->addWidget(m_pDst, 1);
    }
    m_pOrder = new QSpinBox;
    m_pOrder->setRange(-1000, 1000);
    m_pOrder->setToolTip(tr("Among this effect's emitters: higher draws in front"));
    m_pAlign = new QCheckBox(tr("turn to the direction of motion"));
    m_pOldestOnTop = new QCheckBox(tr("older particles over newer ones"));
    l_pF->addRow(tr("Blend"), m_pBlend);
    l_pF->addRow(tr("Factors"), l_pCustom);
    l_pF->addRow(tr("Order"), m_pOrder);
    l_pF->addRow(tr("Align"), m_pAlign);
    l_pF->addRow(tr("Stacking"), m_pOldestOnTop);
    m_pSimulation = new QComboBox;
    m_pSimulation->addItems({tr("auto: GPU above the game's particle threshold"), tr("CPU"), tr("GPU (when the device can)")});
    m_pSimulation->setToolTip(tr("Where the game simulates this emitter.\n"
                                "auto: on the GPU when Max particles is above the game setting particleGpuThreshold "
                                "(desktop 5000, phones 3000).\n"
                                "GPU: on the GPU whenever the device has compute shaders (not in the browser or on GLES phones: "
                                "there it falls back to the CPU).\n"
                                "CPU: always on the CPU.\n"
                                "The editor's preview always simulates on the CPU; both give the same particles."));
    l_pF->addRow(tr("Simulation"), m_pSimulation);
    connect(m_pSimulation, &QComboBox::currentIndexChanged, this, [l_fnEm](int l_iI) {
        using Sim = toms::fx::Emitter::Simulation;
        const Sim l_eS = l_iI == 2 ? Sim::Gpu : l_iI == 1 ? Sim::Cpu : Sim::Auto;
        l_fnEm(QStringLiteral("simulation"), tr("Simulation"), [l_eS](Emitter& l_oM) { l_oM.m_eSimulation = l_eS; });
    });
    connect(m_pBlend, &QComboBox::activated, this, [this, l_fnEm](int l_iI) {
        static const Blend l_arrPresets[] = {Blend::f_Normal(), Blend::f_Add(), Blend::f_Multiply(), Blend::f_Screen()};
        if (l_iI < 4) l_fnEm(QStringLiteral("blend"), tr("Blend"), [l_iI](Emitter& l_oM) { l_oM.m_oBlend = l_arrPresets[l_iI]; });
        f_Refresh();   // shows / hides the factors
    });
    auto l_fnFactor = [this, l_fnEm](QComboBox* l_pC, bool l_bSrc) {
        connect(l_pC, &QComboBox::activated, this, [l_fnEm, l_bSrc](int l_iI) {
            l_fnEm(QStringLiteral("factor"), tr("Blend factor"), [l_iI, l_bSrc](Emitter& l_oM) { (l_bSrc ? l_oM.m_oBlend.m_eSrc : l_oM.m_oBlend.m_eDst) = Blend::Factor(l_iI); });
        });
    };
    l_fnFactor(m_pSrc, true);
    l_fnFactor(m_pDst, false);
    connect(m_pOrder, &QSpinBox::valueChanged, this, [l_fnEm](int l_iX) { l_fnEm(QStringLiteral("order"), tr("Order"), [l_iX](Emitter& l_oM) { l_oM.m_iOrder = l_iX; }); });
    connect(m_pAlign, &QCheckBox::toggled, this, [l_fnEm](bool l_bOn) { l_fnEm(QStringLiteral("align"), tr("Align to motion"), [l_bOn](Emitter& l_oM) { l_oM.m_bAlignToVelocity = l_bOn; }); });
    connect(m_pOldestOnTop, &QCheckBox::toggled, this, [l_fnEm](bool l_bOn) { l_fnEm(QStringLiteral("oldest"), tr("Stacking"), [l_bOn](Emitter& l_oM) { l_oM.m_bOldestOnTop = l_bOn; }); });

    connect(l_pDoc, &ParticleDocument::f_FileChanged, this, &InspectorDock::f_Refresh);
    connect(l_pDoc, &ParticleDocument::f_SelectionChanged, this, &InspectorDock::f_Refresh);
    connect(l_pDoc, &ParticleDocument::f_AtlasChanged, this, &InspectorDock::f_RefreshSprites);
    f_RefreshSprites();
    f_Refresh();
}

void InspectorDock::f_RefreshSprites()
{
    const QString l_oCur = m_pSprite->currentText();
    const QSignalBlocker l_oB(m_pSprite);
    m_pSprite->clear();
    for (const QString& l_oR : m_pDoc->f_SpriteRefs()) {
        const QImage l_oImg = m_pDoc->f_SpriteImage(l_oR);
        m_pSprite->addItem(l_oImg.isNull() ? QIcon() : QIcon(QPixmap::fromImage(l_oImg.scaled(20, 20, Qt::KeepAspectRatio, Qt::SmoothTransformation))), l_oR);
    }
    m_pSprite->setEditText(l_oCur);
}

void InspectorDock::f_UpdateShapeRows()
{
    const int l_iT = m_pShapeType->currentIndex();
    auto l_fnShow = [this](QWidget* l_pW, bool l_bOn) { m_pShapeForm->setRowVisible(l_pW, l_bOn); };
    l_fnShow(m_pLength, l_iT == Shape::Line);
    l_fnShow(m_pAngle, l_iT == Shape::Line);
    l_fnShow(m_pWidth, l_iT == Shape::Box);
    l_fnShow(m_pHeight, l_iT == Shape::Box);
    l_fnShow(m_pRadius, l_iT == Shape::Circle || l_iT == Shape::Ring);
    l_fnShow(m_pThickness, l_iT == Shape::Ring);
    l_fnShow(m_pArcFrom, l_iT == Shape::Circle || l_iT == Shape::Ring);
    l_fnShow(m_pArcTo, l_iT == Shape::Circle || l_iT == Shape::Ring);
    l_fnShow(m_pEdge, l_iT == Shape::Box || l_iT == Shape::Circle);
}

void InspectorDock::f_Refresh()
{
    m_bUpdating = true;
    const Effect* l_pE = m_pDoc->f_Effect();
    const Emitter* l_pM = m_pDoc->f_Emitter();
    m_pEffectSection->setEnabled(l_pE != nullptr);
    m_pEmitterSections->setVisible(l_pM != nullptr);
    m_pNone->setVisible(l_pM == nullptr);
    setWindowTitle(l_pM ? tr("Inspector: %1").arg(f_Qs(l_pM->m_strName)) : l_pE ? tr("Inspector: %1").arg(f_Qs(l_pE->m_strName)) : tr("Inspector"));
    if (l_pE) {
        if (!m_pEffectName->hasFocus()) m_pEffectName->setText(f_Qs(l_pE->m_strName));
        m_pDuration->setValue(l_pE->m_fDuration);
        m_pLoop->setChecked(l_pE->m_bLoop);
        m_pLoop->setEnabled(l_pE->m_fDuration > 0);
        m_pPrewarm->setValue(l_pE->m_fPrewarm);
        m_pSeed->setValue(int(l_pE->m_uSeed));
    }
    if (l_pM) {
        if (!m_pEmitterName->hasFocus()) m_pEmitterName->setText(f_Qs(l_pM->m_strName));
        m_pOffset->f_SetValue(l_pM->m_v2Offset);
        m_pStart->setValue(l_pM->m_fStart);
        m_pStop->setValue(l_pM->m_fStop);
        m_pSpace->setCurrentIndex(l_pM->m_bLocalSpace ? 1 : 0);
        m_pMax->setValue(l_pM->m_iMaxParticles);
        m_pRate->setValue(l_pM->m_fRate);
        m_pBursts->setRowCount(int(l_pM->m_vBursts.size()));
        for (int l_iI = 0; l_iI < int(l_pM->m_vBursts.size()); l_iI++) {
            const Burst& l_oB = l_pM->m_vBursts[size_t(l_iI)];
            const double l_arrVals[] = {l_oB.m_fT, l_oB.m_oCount.m_fMin, l_oB.m_oCount.m_fMax, double(l_oB.m_iRepeat), l_oB.m_fInterval};
            for (int l_iC = 0; l_iC < 5; l_iC++) m_pBursts->setItem(l_iI, l_iC, new QTableWidgetItem(QString::number(l_arrVals[l_iC], 'g', 6)));
        }
        m_pShapeType->setCurrentIndex(int(l_pM->m_oShape.m_eType));
        m_pLength->setValue(l_pM->m_oShape.m_fLength);
        m_pAngle->setValue(l_pM->m_oShape.m_fAngle);
        m_pWidth->setValue(l_pM->m_oShape.m_fWidth);
        m_pHeight->setValue(l_pM->m_oShape.m_fHeight);
        m_pRadius->setValue(l_pM->m_oShape.m_fRadius);
        m_pThickness->setValue(l_pM->m_oShape.m_fThickness);
        m_pArcFrom->setValue(l_pM->m_oShape.m_fArcFrom);
        m_pArcTo->setValue(l_pM->m_oShape.m_fArcTo);
        m_pEdge->setChecked(l_pM->m_oShape.m_bEdge);
        m_pOutward->setChecked(l_pM->m_oShape.m_bOutward);
        f_UpdateShapeRows();
        m_pLife->f_SetValue(l_pM->m_oLife);
        m_pDirection->setValue(l_pM->m_fDirection);
        m_pDirection->setEnabled(!l_pM->m_oShape.m_bOutward);
        m_pSpread->setValue(l_pM->m_fSpread);
        m_pSpeed->f_SetValue(l_pM->m_oSpeed);
        m_pSize->f_SetValue(l_pM->m_oSize);
        m_pRotation->f_SetValue(l_pM->m_oRotation);
        m_pSpin->f_SetValue(l_pM->m_oSpin);
        m_pColor->f_SetColor(l_pM->m_v4Color);
        m_pColor2->f_SetColor(l_pM->m_v4Color2);
        m_pRandomColor->setChecked(l_pM->m_v4Color != l_pM->m_v4Color2);
        m_pColor2->setEnabled(l_pM->m_v4Color != l_pM->m_v4Color2);
        if (!m_pSprite->lineEdit()->hasFocus()) m_pSprite->setEditText(f_Qs(l_pM->m_strSprite));
        m_pSprite->setEnabled(l_pM->m_vFrames.empty());
        m_pColorCurve->f_SetCurve(l_pM->m_oColorOverLife);
        m_pSizeCurve->f_SetCurve(l_pM->m_oSizeOverLife);
        m_pSpeedCurve->f_SetCurve(l_pM->m_oSpeedOverLife);
        m_pSpinCurve->f_SetCurve(l_pM->m_oSpinOverLife);
        m_pGravity->f_SetValue(l_pM->m_v2Gravity);
        m_pDrag->setValue(l_pM->m_fDrag);
        m_pRadial->f_SetValue(l_pM->m_oRadial);
        m_pTangential->f_SetValue(l_pM->m_oTangential);
        m_pFrames->clear();
        for (const std::string& l_strFr : l_pM->m_vFrames) m_pFrames->addItem(f_Qs(l_strFr));
        m_pFrameMode->setCurrentIndex(l_pM->m_bFramesByFps ? 1 : 0);
        m_pFps->setValue(l_pM->m_fFps);
        m_pFps->setEnabled(l_pM->m_bFramesByFps);
        m_pRandomFrame->setChecked(l_pM->m_bRandomFrame);
        const QString l_oPreset = f_Qs(l_pM->m_oBlend.f_PresetName());
        const bool l_bCustomBlend = l_oPreset.isEmpty() || m_pBlend->currentIndex() == 4;
        m_pBlend->setCurrentIndex(l_oPreset.isEmpty() ? 4 : (l_bCustomBlend && m_pBlend->currentIndex() == 4 ? 4 : m_pBlend->findText(l_oPreset)));
        m_pSrc->setCurrentIndex(int(l_pM->m_oBlend.m_eSrc));
        m_pDst->setCurrentIndex(int(l_pM->m_oBlend.m_eDst));
        for (QWidget* l_pW : {static_cast<QWidget*>(m_pSrc), static_cast<QWidget*>(m_pDst)}) l_pW->setEnabled(m_pBlend->currentIndex() == 4);
        m_pOrder->setValue(l_pM->m_iOrder);
        m_pAlign->setChecked(l_pM->m_bAlignToVelocity);
        m_pOldestOnTop->setChecked(l_pM->m_bOldestOnTop);
        m_pSimulation->setCurrentIndex(l_pM->m_eSimulation == Emitter::Simulation::Gpu ? 2 : l_pM->m_eSimulation == Emitter::Simulation::Cpu ? 1 : 0);
    }
    m_bUpdating = false;
}

void InspectorDock::f_CommitBursts()
{
    std::vector<Burst> l_vBursts;
    for (int l_iR = 0; l_iR < m_pBursts->rowCount(); l_iR++) {
        auto l_fnVal = [&](int l_iC, double l_dDef) {
            const QTableWidgetItem* l_pIt = m_pBursts->item(l_iR, l_iC);
            bool l_bOk = false;
            const double l_dV = l_pIt ? l_pIt->text().toDouble(&l_bOk) : l_dDef;
            return l_bOk ? l_dV : l_dDef;
        };
        Burst l_oB;
        l_oB.m_fT = float(std::max(0.0, l_fnVal(0, 0)));
        const float l_fLo = float(std::max(0.0, l_fnVal(1, 1))), l_fHi = float(std::max(0.0, l_fnVal(2, l_fLo)));
        l_oB.m_oCount = Range(std::min(l_fLo, l_fHi), std::max(l_fLo, l_fHi));
        l_oB.m_iRepeat = int(l_fnVal(3, 0));
        l_oB.m_fInterval = float(std::max(0.0, l_fnVal(4, 0.5)));
        l_vBursts.push_back(l_oB);
    }
    m_pDoc->f_EditEmitter(tr("Bursts"), [&](Emitter& l_oM) { l_oM.m_vBursts = l_vBursts; }, f_MergeKey(QStringLiteral("bursts")));
}

void InspectorDock::f_CommitFrames()
{
    std::vector<std::string> l_vFrames;
    for (int l_iI = 0; l_iI < m_pFrames->count(); l_iI++) l_vFrames.push_back(m_pFrames->item(l_iI)->text().toStdString());
    m_pDoc->f_EditEmitter(tr("Flipbook frames"), [&](Emitter& l_oM) { l_oM.m_vFrames = l_vFrames; });
}

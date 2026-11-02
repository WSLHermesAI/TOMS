#include "AnimationDock.h"

#include "AtlasDocument.h"
#include "Icons.h"
#include "Theme.h"

#include <QCheckBox>
#include <QCollator>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

using atlas::Animation;
using atlas::Project;

namespace {

QString f_Qs(const std::string& l_strS) { return QString::fromStdString(l_strS); }
std::string f_U8(const QString& l_oS) { return l_oS.toStdString(); }

// Works on a const or a mutable project (returns a pointer of matching constness).
template <class P>
auto f_FindAnimation(P& l_oP, const QString& l_oName) -> decltype(&l_oP.m_vAnimations.front())
{
    for (auto& l_oA : l_oP.m_vAnimations)
        if (f_Qs(l_oA.m_strName) == l_oName) return &l_oA;
    return nullptr;
}

QToolButton* f_ToolButton(QWidget* l_pParent, Icons::Id l_eIcon, const QString& l_oTip)
{
    auto* l_pB = new QToolButton(l_pParent);
    l_pB->setIcon(Icons::f_Icon(l_eIcon));
    l_pB->setToolTip(l_oTip);
    l_pB->setAutoRaise(true);
    return l_pB;
}

}  // namespace

// Plays one animation: each frame for its time, frames aligned on their pivots so the motion
// reads the way the game will draw it.
class AnimationPreview : public QWidget
{
public:
    AnimationPreview(AtlasDocument* l_pDoc, QWidget* l_pParent)
        : QWidget(l_pParent)
        , m_pDoc(l_pDoc)
        , m_pTimer(new QTimer(this))
    {
        setMinimumSize(120, 100);
        m_pTimer->setSingleShot(true);
        connect(m_pTimer, &QTimer::timeout, this, [this] { f_Step(); });
    }

    void f_SetAnimation(const QString& l_oName)
    {
        if (l_oName != m_oName) m_iFrame = 0;
        m_oName = l_oName;
        update();
        if (m_bPlaying) f_Schedule();
    }
    bool f_IsPlaying() const { return m_bPlaying; }
    void f_SetPlaying(bool l_bOn)
    {
        m_bPlaying = l_bOn;
        if (l_bOn) f_Schedule();
        else m_pTimer->stop();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter l_oP(this);
        const Theme::Colors& l_oTc = Theme::f_Colors();
        l_oP.fillRect(rect(), l_oTc.m_oCanvasBackground);
        const Animation* l_pA = f_Animation();
        if (!l_pA || l_pA->m_vFrames.empty()) {
            l_oP.setPen(palette().color(QPalette::PlaceholderText));
            l_oP.drawText(rect(), Qt::AlignCenter, l_pA ? tr("No frames") : tr("No animation"));
            return;
        }
        // One scale and one anchor for all frames: from the largest extents around the pivot.
        double l_dLeft = 0, l_dRight = 1, l_dTop = 0, l_dBottom = 1;
        for (const atlas::AnimFrame& l_oF : l_pA->m_vFrames) {
            const QImage l_oImg = m_pDoc->f_SpriteImage(f_Qs(l_oF.m_strSprite));
            if (l_oImg.isNull()) continue;
            const QPointF l_oPv = f_Pivot(f_Qs(l_oF.m_strSprite), l_oImg.size());
            l_dLeft = std::max(l_dLeft, l_oPv.x());
            l_dRight = std::max(l_dRight, l_oImg.width() - l_oPv.x());
            l_dTop = std::max(l_dTop, l_oPv.y());
            l_dBottom = std::max(l_dBottom, l_oImg.height() - l_oPv.y());
        }
        const QRectF l_oArea = QRectF(rect()).adjusted(8, 8, -8, -22);
        double l_dScale = std::min(l_oArea.width() / (l_dLeft + l_dRight), l_oArea.height() / (l_dTop + l_dBottom));
        if (l_dScale >= 1) l_dScale = std::floor(l_dScale);   // crisp pixel art
        const QPointF l_oAnchor(l_oArea.center().x() + (l_dLeft - l_dRight) * l_dScale / 2, l_oArea.center().y() + (l_dTop - l_dBottom) * l_dScale / 2);

        const atlas::AnimFrame& l_oF = l_pA->m_vFrames[size_t(m_iFrame) % l_pA->m_vFrames.size()];
        const QImage l_oImg = m_pDoc->f_SpriteImage(f_Qs(l_oF.m_strSprite));
        if (!l_oImg.isNull()) {
            const QPointF l_oPv = f_Pivot(f_Qs(l_oF.m_strSprite), l_oImg.size());
            const QRectF l_oTarget(l_oAnchor - l_oPv * l_dScale, QSizeF(l_oImg.size()) * l_dScale);
            l_oP.setRenderHint(QPainter::SmoothPixmapTransform, l_dScale < 1);
            l_oP.drawImage(l_oTarget, l_oImg);
        }
        l_oP.setPen(QPen(l_oTc.m_oPivot, 1));
        l_oP.drawLine(QLineF(l_oAnchor.x() - 4, l_oAnchor.y(), l_oAnchor.x() + 4, l_oAnchor.y()));
        l_oP.drawLine(QLineF(l_oAnchor.x(), l_oAnchor.y() - 4, l_oAnchor.x(), l_oAnchor.y() + 4));
        l_oP.setPen(palette().color(QPalette::PlaceholderText));
        l_oP.drawText(QRectF(rect()).adjusted(6, 0, -6, -4), Qt::AlignBottom | Qt::AlignLeft,
                   tr("%1 / %2  %3").arg(m_iFrame % int(l_pA->m_vFrames.size()) + 1).arg(l_pA->m_vFrames.size()).arg(f_Qs(l_oF.m_strSprite)));
    }

private:
    const Animation* f_Animation() const
    {
        return f_FindAnimation(m_pDoc->f_Project(), m_oName);
    }
    QPointF f_Pivot(const QString& l_oSprite, const QSize& l_oSize) const
    {
        const atlas::SpriteDef l_oD = m_pDoc->f_SpriteDef(l_oSprite);
        const float* l_pPv = l_oD.m_bHasPivot ? l_oD.m_arrPivot : m_pDoc->f_Project().m_oSettings.m_arrDefaultPivot;
        return QPointF(l_pPv[0] * l_oSize.width(), l_pPv[1] * l_oSize.height());
    }
    void f_Schedule()
    {
        const Animation* l_pA = f_Animation();
        if (!l_pA || l_pA->m_vFrames.empty()) return;
        const float l_fT = l_pA->m_vFrames[size_t(m_iFrame) % l_pA->m_vFrames.size()].m_fTime;
        m_pTimer->start(std::max(16, int(std::lround(l_fT * 1000))));
    }
    void f_Step()
    {
        const Animation* l_pA = f_Animation();
        if (!l_pA || l_pA->m_vFrames.empty()) return;
        const int l_iN = int(l_pA->m_vFrames.size());
        if (m_iFrame + 1 >= l_iN && !l_pA->m_bLoop) {
            m_bPlaying = false;   // a one-shot animation stops on its last frame
            update();
            return;
        }
        m_iFrame = (m_iFrame + 1) % l_iN;
        update();
        f_Schedule();
    }

    AtlasDocument* m_pDoc;
    QTimer* m_pTimer;
    QString m_oName;
    int m_iFrame = 0;
    bool m_bPlaying = true;
};

AnimationDock::AnimationDock(AtlasDocument* l_pDoc, QWidget* l_pParent)
    : QDockWidget(tr("Animations"), l_pParent)
    , m_pDoc(l_pDoc)
{
    setObjectName(QStringLiteral("AnimationsDock"));
    auto* l_pSplit = new QSplitter(Qt::Horizontal, this);

    // Animations.
    auto* l_pLeft = new QWidget(l_pSplit);
    m_pList = new QListWidget(l_pLeft);
    auto* l_pAdd = f_ToolButton(l_pLeft, Icons::Id::Add, tr("New animation"));
    auto* l_pRemove = f_ToolButton(l_pLeft, Icons::Id::Remove, tr("Delete animation"));
    auto* l_pLbar = new QHBoxLayout;
    l_pLbar->addWidget(new QLabel(tr("Animations"), l_pLeft));
    l_pLbar->addStretch(1);
    l_pLbar->addWidget(l_pAdd);
    l_pLbar->addWidget(l_pRemove);
    auto* l_pLl = new QVBoxLayout(l_pLeft);
    l_pLl->setContentsMargins(4, 2, 4, 4);
    l_pLl->addLayout(l_pLbar);
    l_pLl->addWidget(m_pList, 1);

    // Frames.
    auto* l_pMid = new QWidget(l_pSplit);
    m_pFrames = new QTableWidget(0, 2, l_pMid);
    m_pFrames->setHorizontalHeaderLabels({tr("Sprite"), tr("Time (s)")});
    m_pFrames->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_pFrames->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_pFrames->verticalHeader()->setDefaultSectionSize(22);
    m_pFrames->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pLoop = new QCheckBox(tr("Loop"), l_pMid);
    m_pAddFrames = f_ToolButton(l_pMid, Icons::Id::Add, tr("Add the selected sprites as frames"));
    m_pRemoveFrames = f_ToolButton(l_pMid, Icons::Id::Remove, tr("Remove the selected frames"));
    m_pUp = f_ToolButton(l_pMid, Icons::Id::Up, tr("Move frame up"));
    m_pDown = f_ToolButton(l_pMid, Icons::Id::Down, tr("Move frame down"));
    auto* l_pMbar = new QHBoxLayout;
    l_pMbar->addWidget(new QLabel(tr("Frames"), l_pMid));
    l_pMbar->addWidget(m_pLoop);
    l_pMbar->addStretch(1);
    for (QToolButton* l_pB : {m_pAddFrames, m_pRemoveFrames, m_pUp, m_pDown}) l_pMbar->addWidget(l_pB);
    auto* l_pMl = new QVBoxLayout(l_pMid);
    l_pMl->setContentsMargins(4, 2, 4, 4);
    l_pMl->addLayout(l_pMbar);
    l_pMl->addWidget(m_pFrames, 1);

    // Preview.
    auto* l_pRight = new QWidget(l_pSplit);
    m_pPreview = new AnimationPreview(l_pDoc, l_pRight);
    m_pPlay = f_ToolButton(l_pRight, Icons::Id::Pause, tr("Play / pause"));
    auto* l_pRbar = new QHBoxLayout;
    l_pRbar->addWidget(new QLabel(tr("Preview"), l_pRight));
    l_pRbar->addStretch(1);
    l_pRbar->addWidget(m_pPlay);
    auto* l_pRl = new QVBoxLayout(l_pRight);
    l_pRl->setContentsMargins(4, 2, 4, 4);
    l_pRl->addLayout(l_pRbar);
    l_pRl->addWidget(m_pPreview, 1);

    l_pSplit->addWidget(l_pLeft);
    l_pSplit->addWidget(l_pMid);
    l_pSplit->addWidget(l_pRight);
    l_pSplit->setStretchFactor(0, 1);
    l_pSplit->setStretchFactor(1, 2);
    l_pSplit->setStretchFactor(2, 1);
    setWidget(l_pSplit);

    connect(l_pAdd, &QToolButton::clicked, this, &AnimationDock::f_OnAdd);
    connect(l_pRemove, &QToolButton::clicked, this, &AnimationDock::f_OnRemove);
    connect(m_pAddFrames, &QToolButton::clicked, this, &AnimationDock::f_OnAddFrames);
    connect(m_pRemoveFrames, &QToolButton::clicked, this, &AnimationDock::f_OnRemoveFrames);
    connect(m_pUp, &QToolButton::clicked, this, [this] { f_OnMoveFrame(-1); });
    connect(m_pDown, &QToolButton::clicked, this, [this] { f_OnMoveFrame(1); });
    connect(m_pPlay, &QToolButton::clicked, this, [this] {
        m_pPreview->f_SetPlaying(!m_pPreview->f_IsPlaying());
        m_pPlay->setIcon(Icons::f_Icon(m_pPreview->f_IsPlaying() ? Icons::Id::Pause : Icons::Id::Play));
    });
    connect(m_pList, &QListWidget::currentRowChanged, this, [this] {
        if (m_bUpdating) return;
        f_RefreshFrames();
    });
    connect(m_pList, &QListWidget::itemChanged, this, [this](QListWidgetItem* l_pItem) {
        if (m_bUpdating) return;
        const QString l_oFrom = l_pItem->data(Qt::UserRole).toString(), l_oTo = l_pItem->text().trimmed();
        if (l_oTo.isEmpty() || f_FindAnimation(m_pDoc->f_Project(), l_oTo)) {   // empty or taken: put the old name back
            m_bUpdating = true;
            l_pItem->setText(l_oFrom);
            m_bUpdating = false;
            return;
        }
        m_pDoc->f_Edit(tr("Rename animation"), [&](Project& l_oP) {
            if (Animation* l_pA = f_FindAnimation(l_oP, l_oFrom)) l_pA->m_strName = f_U8(l_oTo);
        });
    });
    connect(m_pLoop, &QCheckBox::toggled, this, [this](bool l_bOn) {
        f_EditCurrent(l_bOn ? tr("Loop animation") : tr("Play once"), [&](Animation& l_oA) { l_oA.m_bLoop = l_bOn; });
    });
    connect(m_pFrames, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* l_pItem) {
        if (m_bUpdating) return;
        const int l_iRow = l_pItem->row();
        if (l_pItem->column() == 0) {
            const std::string l_strSprite = f_U8(l_pItem->text().trimmed());
            f_EditCurrent(tr("Frame sprite"), [&](Animation& l_oA) {
                if (l_iRow < int(l_oA.m_vFrames.size())) l_oA.m_vFrames[size_t(l_iRow)].m_strSprite = l_strSprite;
            });
        } else {
            const float l_fT = std::max(0.001f, l_pItem->data(Qt::EditRole).toFloat());
            f_EditCurrent(tr("Frame time"), [&](Animation& l_oA) {
                if (l_iRow < int(l_oA.m_vFrames.size())) l_oA.m_vFrames[size_t(l_iRow)].m_fTime = l_fT;
            }, QStringLiteral("frametime%1").arg(l_iRow));
        }
    });
    connect(l_pDoc, &AtlasDocument::f_ProjectChanged, this, &AnimationDock::f_Refresh);
    connect(l_pDoc, &AtlasDocument::f_BuildFinished, this, [this] {
        f_MarkMissingSprites();
        m_pPreview->update();
    });
    connect(l_pDoc, &AtlasDocument::f_SelectionChanged, this, [this] { m_pAddFrames->setEnabled(!m_pDoc->f_Selection().isEmpty() && m_pList->currentItem()); });
    f_Refresh();
}

QString AnimationDock::f_CurrentAnimation() const
{
    const QListWidgetItem* l_pItem = m_pList->currentItem();
    return l_pItem ? l_pItem->data(Qt::UserRole).toString() : QString();
}

void AnimationDock::f_Refresh()
{
    const QString l_oKeep = f_CurrentAnimation();
    m_bUpdating = true;
    m_pList->clear();
    for (const Animation& l_oA : m_pDoc->f_Project().m_vAnimations) {
        auto* l_pItem = new QListWidgetItem(f_Qs(l_oA.m_strName), m_pList);
        l_pItem->setData(Qt::UserRole, f_Qs(l_oA.m_strName));
        l_pItem->setFlags(l_pItem->flags() | Qt::ItemIsEditable);
        l_pItem->setIcon(Icons::f_Icon(Icons::Id::Play));
    }
    int l_iRow = 0;
    for (int l_iI = 0; l_iI < m_pList->count(); l_iI++)
        if (m_pList->item(l_iI)->data(Qt::UserRole).toString() == l_oKeep) l_iRow = l_iI;
    if (m_pList->count()) m_pList->setCurrentRow(l_iRow);
    m_bUpdating = false;
    f_RefreshFrames();
}

void AnimationDock::f_RefreshFrames()
{
    const QString l_oName = f_CurrentAnimation();
    const Animation* l_pA = f_FindAnimation(m_pDoc->f_Project(), l_oName);
    m_bUpdating = true;
    const int l_iKeepRow = m_pFrames->currentRow();
    m_pFrames->setRowCount(l_pA ? int(l_pA->m_vFrames.size()) : 0);
    if (l_pA) {
        for (size_t l_nI = 0; l_nI < l_pA->m_vFrames.size(); l_nI++) {
            const atlas::AnimFrame& l_oF = l_pA->m_vFrames[l_nI];
            auto* l_pSprite = new QTableWidgetItem(f_Qs(l_oF.m_strSprite));
            auto* l_pTime = new QTableWidgetItem;
            l_pTime->setData(Qt::EditRole, double(l_oF.m_fTime));
            m_pFrames->setItem(int(l_nI), 0, l_pSprite);
            m_pFrames->setItem(int(l_nI), 1, l_pTime);
        }
        if (l_iKeepRow >= 0 && l_iKeepRow < m_pFrames->rowCount()) m_pFrames->selectRow(l_iKeepRow);
    }
    m_pLoop->setChecked(l_pA && l_pA->m_bLoop);
    f_MarkMissingSprites();
    m_bUpdating = false;
    for (QWidget* l_pW : std::initializer_list<QWidget*>{m_pFrames, m_pLoop, m_pRemoveFrames, m_pUp, m_pDown}) l_pW->setEnabled(l_pA != nullptr);
    m_pAddFrames->setEnabled(l_pA && !m_pDoc->f_Selection().isEmpty());
    m_pPreview->f_SetAnimation(l_oName);
}

void AnimationDock::f_MarkMissingSprites()
{
    // Only recolours: rebuilding the rows would close an editor the user may have open.
    for (int l_iR = 0; l_iR < m_pFrames->rowCount(); l_iR++)
        if (QTableWidgetItem* l_pItem = m_pFrames->item(l_iR, 0)) {
            const bool l_bMissing = m_pDoc->f_Snapshot() && !m_pDoc->f_SpriteExists(l_pItem->text());
            const QSignalBlocker l_oBlock(m_pFrames);   // a colour change is not an edit
            l_pItem->setData(Qt::ForegroundRole, l_bMissing ? QVariant(QColor(0xf0, 0x4a, 0x5a)) : QVariant());
            l_pItem->setToolTip(l_bMissing ? tr("No sprite with this name") : QString());
        }
}

void AnimationDock::f_EditCurrent(const QString& l_oText, const std::function<void(Animation&)>& l_fnChange, const QString& l_oMergeKey)
{
    const QString l_oName = f_CurrentAnimation();
    if (m_bUpdating || l_oName.isEmpty()) return;
    m_pDoc->f_Edit(l_oText, [&](Project& l_oP) {
        if (Animation* l_pA = f_FindAnimation(l_oP, l_oName)) l_fnChange(*l_pA);
    }, l_oMergeKey.isEmpty() ? QString() : QStringLiteral("anim.%1.%2").arg(l_oName, l_oMergeKey));
}

void AnimationDock::f_OnAdd()
{
    QString l_oName = QStringLiteral("anim");
    for (int l_iI = 1; f_FindAnimation(m_pDoc->f_Project(), l_oName); l_iI++) l_oName = QStringLiteral("anim_%1").arg(l_iI);
    // New animations start with the selected sprites, in natural name order (run_2 before run_10).
    QStringList l_oSprites = m_pDoc->f_Selection();
    QCollator l_oColl;
    l_oColl.setNumericMode(true);
    std::sort(l_oSprites.begin(), l_oSprites.end(), [&](const QString& l_oA, const QString& l_oB) { return l_oColl.compare(l_oA, l_oB) < 0; });
    m_pDoc->f_Edit(tr("New animation"), [&](Project& l_oP) {
        Animation l_oA;
        l_oA.m_strName = f_U8(l_oName);
        for (const QString& l_oS : l_oSprites) l_oA.m_vFrames.push_back({f_U8(l_oS), 0.1f});
        l_oP.m_vAnimations.push_back(l_oA);
    });
    for (int l_iI = 0; l_iI < m_pList->count(); l_iI++)
        if (m_pList->item(l_iI)->data(Qt::UserRole).toString() == l_oName) {
            m_pList->setCurrentRow(l_iI);
            m_pList->editItem(m_pList->item(l_iI));
        }
}

void AnimationDock::f_OnRemove()
{
    const QString l_oName = f_CurrentAnimation();
    if (l_oName.isEmpty()) return;
    m_pDoc->f_Edit(tr("Delete animation %1").arg(l_oName), [&](Project& l_oP) {
        l_oP.m_vAnimations.erase(std::remove_if(l_oP.m_vAnimations.begin(), l_oP.m_vAnimations.end(),
                                          [&](const Animation& l_oA) { return f_Qs(l_oA.m_strName) == l_oName; }),
                           l_oP.m_vAnimations.end());
    });
}

void AnimationDock::f_OnAddFrames()
{
    QStringList l_oSprites = m_pDoc->f_Selection();
    QCollator l_oColl;
    l_oColl.setNumericMode(true);
    std::sort(l_oSprites.begin(), l_oSprites.end(), [&](const QString& l_oA, const QString& l_oB) { return l_oColl.compare(l_oA, l_oB) < 0; });
    // Insert after the selected frame, or at the end.
    const int l_iAt = m_pFrames->currentRow() >= 0 ? m_pFrames->currentRow() + 1 : m_pFrames->rowCount();
    f_EditCurrent(tr("Add frames"), [&](Animation& l_oA) {
        std::vector<atlas::AnimFrame> l_vAdd;
        for (const QString& l_oS : l_oSprites) l_vAdd.push_back({f_U8(l_oS), 0.1f});
        l_oA.m_vFrames.insert(l_oA.m_vFrames.begin() + std::min<size_t>(size_t(l_iAt), l_oA.m_vFrames.size()), l_vAdd.begin(), l_vAdd.end());
    });
}

void AnimationDock::f_OnRemoveFrames()
{
    QList<int> l_oRows;
    for (const QModelIndex& l_oIdx : m_pFrames->selectionModel()->selectedRows()) l_oRows << l_oIdx.row();
    if (l_oRows.isEmpty()) return;
    std::sort(l_oRows.begin(), l_oRows.end(), std::greater<int>());
    f_EditCurrent(tr("Remove frames"), [&](Animation& l_oA) {
        for (int l_iR : l_oRows)
            if (l_iR < int(l_oA.m_vFrames.size())) l_oA.m_vFrames.erase(l_oA.m_vFrames.begin() + l_iR);
    });
}

void AnimationDock::f_OnMoveFrame(int l_iDelta)
{
    const int l_iRow = m_pFrames->currentRow();
    const int l_iTo = l_iRow + l_iDelta;
    if (l_iRow < 0 || l_iTo < 0 || l_iTo >= m_pFrames->rowCount()) return;
    f_EditCurrent(tr("Move frame"), [&](Animation& l_oA) { std::swap(l_oA.m_vFrames[size_t(l_iRow)], l_oA.m_vFrames[size_t(l_iTo)]); });
    m_pFrames->selectRow(l_iTo);
}

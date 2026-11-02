#include "AnimClipsDock.h"

#include "Icons.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QApplication>
#include <QListWidget>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolBar>
#include <QVBoxLayout>

using toms::anim::Clip;

ClipFields ClipFields::f_Create(AnimDocument* l_pDoc, QWidget* l_pParent, bool* l_pUpdating)
{
    ClipFields l_oF;
    l_oF.m_pLength = new QDoubleSpinBox(l_pParent);
    l_oF.m_pLength->setRange(0, 3600);
    l_oF.m_pLength->setDecimals(3);
    l_oF.m_pLength->setSingleStep(0.05);
    l_oF.m_pLength->setSuffix(QStringLiteral(" s"));
    l_oF.m_pLength->setSpecialValueText(QObject::tr("auto (last key)"));
    l_oF.m_pLength->setKeyboardTracking(false);
    l_oF.m_pLength->setToolTip(QObject::tr("Clip length in seconds; 0 = up to the last key"));
    l_oF.m_pPlayCount = new QSpinBox(l_pParent);
    l_oF.m_pPlayCount->setRange(-1, 9999);
    l_oF.m_pPlayCount->setSpecialValueText(QObject::tr("loop forever"));
    l_oF.m_pPlayCount->setKeyboardTracking(false);
    l_oF.m_pPlayCount->setToolTip(QObject::tr("Times to play; -1 = loop forever"));
    l_oF.m_pStay = new QCheckBox(QObject::tr("Stay at last frame"), l_pParent);
    l_oF.m_pStay->setToolTip(QObject::tr("After the last play keep showing the end (else the clip disappears)"));
    QObject::connect(l_oF.m_pLength, &QDoubleSpinBox::valueChanged, l_pParent, [l_pDoc, l_pUpdating](double l_dV) {
        if (*l_pUpdating) return;
        l_pDoc->f_EditClip(QObject::tr("Clip length"), [l_dV](Clip& l_oC) { l_oC.m_fLength = float(l_dV); }, QStringLiteral("clip.length"));
    });
    QObject::connect(l_oF.m_pPlayCount, &QSpinBox::valueChanged, l_pParent, [l_pDoc, l_pUpdating](int l_iV) {
        if (*l_pUpdating) return;
        l_pDoc->f_EditClip(QObject::tr("Play count"), [l_iV](Clip& l_oC) { l_oC.m_iPlayCount = l_iV; }, QStringLiteral("clip.playCount"));
    });
    QObject::connect(l_oF.m_pStay, &QCheckBox::toggled, l_pParent, [l_pDoc, l_pUpdating](bool l_bOn) {
        if (*l_pUpdating) return;
        l_pDoc->f_EditClip(QObject::tr("Stay at last frame"), [l_bOn](Clip& l_oC) { l_oC.m_bStayAtLastFrame = l_bOn; });
    });
    return l_oF;
}

void ClipFields::f_Refresh(const AnimDocument* l_pDoc) const
{
    const Clip* l_pC = l_pDoc->f_Clip();
    for (QWidget* l_pW : std::initializer_list<QWidget*>{m_pLength, m_pPlayCount, m_pStay}) l_pW->setEnabled(l_pC != nullptr);
    if (!l_pC) return;
    const QSignalBlocker l_oB1(m_pLength), l_oB2(m_pPlayCount), l_oB3(m_pStay);
    m_pLength->setValue(l_pC->m_fLength);
    m_pPlayCount->setValue(l_pC->m_iPlayCount);
    m_pStay->setChecked(l_pC->m_bStayAtLastFrame);
}

AnimClipsDock::AnimClipsDock(AnimDocument* l_pDoc, QWidget* l_pParent)
    : QDockWidget(tr("Clips"), l_pParent)
    , m_pDoc(l_pDoc)
    , m_pList(new QListWidget(this))
{
    setObjectName(QStringLiteral("AnimClipsDock"));
    auto* l_pBody = new QWidget(this);
    auto* l_pTb = new QToolBar(l_pBody);
    l_pTb->setIconSize(QSize(16, 16));
    QAction* l_pAdd = l_pTb->addAction(Icons::f_Icon(Icons::Id::Add), tr("Add clip"));
    QAction* l_pDup = l_pTb->addAction(Icons::f_Icon(Icons::Id::Duplicate), tr("Duplicate clip"));
    QAction* l_pRen = l_pTb->addAction(Icons::f_Icon(Icons::Id::Rename), tr("Rename clip"));
    QAction* l_pDel = l_pTb->addAction(Icons::f_Icon(Icons::Id::Remove), tr("Delete clip"));
    connect(l_pAdd, &QAction::triggered, this, [this] { m_pDoc->f_AddClip(QStringLiteral("clip")); });
    // The toolbar acts on the highlighted row (a single click), which need not be the open clip.
    auto l_fnRow = [this] { return m_pList->currentRow() >= 0 ? m_pList->currentRow() : m_pDoc->f_ClipIndex(); };
    connect(l_pDup, &QAction::triggered, this, [this, l_fnRow] { m_pDoc->f_DuplicateClip(l_fnRow()); });
    connect(l_pRen, &QAction::triggered, this, &AnimClipsDock::f_BeginRename);
    connect(l_pDel, &QAction::triggered, this, [this, l_fnRow] { m_pDoc->f_DeleteClip(l_fnRow()); });
    l_pRen->setToolTip(tr("Rename clip (F2)"));

    const ClipFields l_oF = ClipFields::f_Create(l_pDoc, l_pBody, &m_bUpdating);
    m_pLength = l_oF.m_pLength;
    m_pPlayCount = l_oF.m_pPlayCount;
    m_pStay = l_oF.m_pStay;
    auto* l_pForm = new QFormLayout();
    l_pForm->setContentsMargins(2, 2, 2, 2);
    l_pForm->addRow(tr("Length"), m_pLength);
    l_pForm->addRow(tr("Plays"), m_pPlayCount);
    l_pForm->addRow(QString(), m_pStay);

    // Double-click (or Enter) opens a clip; F2 or the Rename button renames it.
    m_pList->setEditTriggers(QAbstractItemView::EditKeyPressed);
    m_pList->setToolTip(tr("Double-click a clip to open it (F2 renames)"));
    auto* l_pL = new QVBoxLayout(l_pBody);
    l_pL->setContentsMargins(4, 2, 4, 4);
    l_pL->setSpacing(2);
    l_pL->addWidget(l_pTb);
    l_pL->addWidget(m_pList, 1);
    l_pL->addLayout(l_pForm);
    setWidget(l_pBody);

    connect(l_pDoc, &AnimDocument::f_FileChanged, this, &AnimClipsDock::f_Rebuild);
    connect(l_pDoc, &AnimDocument::f_ClipChanged, this, &AnimClipsDock::f_Rebuild);
    connect(m_pList, &QListWidget::itemActivated, this, [this](QListWidgetItem* l_pIt) {
        const int l_iRow = m_pList->row(l_pIt);
        if (f_OpenClip(l_iRow)) emit f_ClipActivated(l_iRow);
    });
    connect(m_pList, &QListWidget::itemClicked, this, [this](QListWidgetItem* l_pIt) {
        if (m_pList->row(l_pIt) != m_pDoc->f_ClipIndex())
            emit m_pDoc->f_Message(tr("Double-click “%1” to open and play it (the viewport shows the bold clip).").arg(l_pIt->text()));
    });
    connect(m_pList, &QListWidget::itemChanged, this, [this](QListWidgetItem* l_pIt) {
        if (m_bUpdating) return;
        if (!m_pDoc->f_RenameClip(m_pList->row(l_pIt), l_pIt->text())) f_Rebuild();
    });
    f_Rebuild();
}

bool AnimClipsDock::f_OpenClip(int l_iRow, int l_iDiscard)
{
    const auto& l_vClips = m_pDoc->f_File().m_vClips;
    if (l_iRow < 0 || l_iRow >= int(l_vClips.size())) return false;
    if (l_iRow == m_pDoc->f_ClipIndex()) return true;
    if (m_pDoc->f_ClipModified()) {
        const QString l_oCur = QString::fromStdString(l_vClips[size_t(m_pDoc->f_ClipIndex())].m_strName);
        const QString l_oNext = QString::fromStdString(l_vClips[size_t(l_iRow)].m_strName);
        bool l_bYes = l_iDiscard == 1;
        if (l_iDiscard < 0)
            l_bYes = QMessageBox::question(this, tr("Open Clip"),
                                        tr("The clip \u201c%1\u201d has changes.\n\nDiscard all changes to \u201c%1\u201d and open \u201c%2\u201d?\n"
                                           "(Edit > Undo brings them back.)").arg(l_oCur, l_oNext),
                                        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
        if (!l_bYes) {
            f_Rebuild();   // the highlight goes back to the open clip
            return false;
        }
        m_pDoc->f_DiscardClipChanges();
    }
    m_pDoc->f_SetClipIndex(l_iRow);
    return true;
}

void AnimClipsDock::f_BeginRename()
{
    if (QListWidgetItem* l_pIt = m_pList->currentItem()) m_pList->editItem(l_pIt);
}

void AnimClipsDock::f_Rebuild()
{
    m_bUpdating = true;
    const auto& l_vClips = m_pDoc->f_File().m_vClips;
    if (m_pList->count() != int(l_vClips.size())) {
        m_pList->clear();
        for (size_t l_nI = 0; l_nI < l_vClips.size(); l_nI++) m_pList->addItem(QString());
    }
    for (size_t l_nI = 0; l_nI < l_vClips.size(); l_nI++) {
        QListWidgetItem* l_pIt = m_pList->item(int(l_nI));
        l_pIt->setText(QString::fromStdString(l_vClips[l_nI].m_strName));
        l_pIt->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
        const Clip& l_oC = l_vClips[l_nI];
        l_pIt->setToolTip(tr("%1 s, %2").arg(l_oC.f_Duration(), 0, 'f', 3).arg(l_oC.m_iPlayCount < 0 ? tr("loops") : tr("plays %1×").arg(l_oC.m_iPlayCount)));
        l_pIt->setIcon(Icons::f_Icon(l_oC.m_iPlayCount < 0 ? Icons::Id::Loop : Icons::Id::Play));
        QFont l_oFont = m_pList->font();
        l_oFont.setBold(int(l_nI) == m_pDoc->f_ClipIndex());   // the open clip
        l_pIt->setFont(l_oFont);
    }
    m_pList->setCurrentRow(m_pDoc->f_ClipIndex());
    ClipFields{m_pLength, m_pPlayCount, m_pStay}.f_Refresh(m_pDoc);
    m_bUpdating = false;
}

#include "EffectsDock.h"

#include "Icons.h"
#include "ParticleDocument.h"
#include "ParticlePlayback.h"

#include <QApplication>
#include <QHeaderView>
#include <QTimer>
#include <QMessageBox>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

using toms::fx::Effect;
using toms::fx::Emitter;

namespace {
constexpr int g_iEffectRole = Qt::UserRole + 1;
constexpr int g_iEmitterRole = Qt::UserRole + 2;   // -1 = the effect row
QString f_Qs(const std::string& l_strS) { return QString::fromStdString(l_strS); }
}

EffectsDock::EffectsDock(ParticleDocument* l_pDoc, ParticlePlayback* l_pPlayback, QWidget* l_pParent)
    : QDockWidget(tr("Effects"), l_pParent)
    , m_pDoc(l_pDoc)
    , m_pPlay(l_pPlayback)
    , m_pTree(new QTreeWidget(this))
    , m_pBar(new QToolBar(this))
{
    setObjectName(QStringLiteral("ParticleEffectsDock"));
    m_pTree->setHeaderHidden(true);
    m_pTree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_pTree->setToolTip(tr("Checkbox: show the emitter in the preview (not saved). Double-click / F2: rename."));
    m_pBar->setIconSize(QSize(16, 16));
    m_pBar->addAction(Icons::f_Icon(Icons::Id::Add), tr("Effect"), this, &EffectsDock::f_AddEffect)->setToolTip(tr("Add an effect"));
    m_pBar->addAction(Icons::f_Icon(Icons::Id::Node), tr("Emitter"), this, &EffectsDock::f_AddEmitter)->setToolTip(tr("Add an emitter to the effect"));
    m_pBar->addAction(Icons::f_Icon(Icons::Id::Duplicate), tr("Duplicate"), this, &EffectsDock::f_DuplicateSelected)->setToolTip(tr("Duplicate the emitter"));
    m_pBar->addAction(Icons::f_Icon(Icons::Id::Remove), tr("Delete"), this, &EffectsDock::f_DeleteSelected)->setToolTip(tr("Delete the emitter (or the effect)"));
    m_pBar->addAction(Icons::f_Icon(Icons::Id::Up), tr("Up"), this, [this] { m_pDoc->f_MoveEmitter(m_pDoc->f_EmitterIndex(), -1); })->setToolTip(tr("Move the emitter up"));
    m_pBar->addAction(Icons::f_Icon(Icons::Id::Down), tr("Down"), this, [this] { m_pDoc->f_MoveEmitter(m_pDoc->f_EmitterIndex(), 1); })->setToolTip(tr("Move the emitter down"));
    m_pBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    auto* l_pBody = new QWidget(this);
    auto* l_pL = new QVBoxLayout(l_pBody);
    l_pL->setContentsMargins(2, 2, 2, 2);
    l_pL->setSpacing(2);
    l_pL->addWidget(m_pBar);
    l_pL->addWidget(m_pTree, 1);
    setWidget(l_pBody);

    connect(m_pTree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* l_pIt) {
        if (m_bRebuilding || !l_pIt) return;
        m_pDoc->f_Select(l_pIt->data(0, g_iEffectRole).toInt(), l_pIt->data(0, g_iEmitterRole).toInt());
    });
    connect(m_pTree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* l_pIt) {
        if (m_bRebuilding) return;
        const int l_iEf = l_pIt->data(0, g_iEffectRole).toInt(), l_iEm = l_pIt->data(0, g_iEmitterRole).toInt();
        const QString l_oName = l_pIt->text(0).trimmed();
        m_pDoc->f_Select(l_iEf, l_iEm);
        if (l_iEm >= 0) {
            const bool l_bShown = l_pIt->checkState(0) == Qt::Checked;
            if (l_bShown == m_pPlay->f_IsHidden(l_iEm)) m_pPlay->f_SetHidden(l_iEm, !l_bShown);
            if (!l_oName.isEmpty() && m_pDoc->f_Emitter() && f_Qs(m_pDoc->f_Emitter()->m_strName) != l_oName)
                m_pDoc->f_EditEmitter(tr("Rename emitter"), [&](Emitter& l_oM) { l_oM.m_strName = l_oName.toStdString(); });
        } else if (!l_oName.isEmpty() && m_pDoc->f_Effect() && f_Qs(m_pDoc->f_Effect()->m_strName) != l_oName) {
            m_pDoc->f_EditEffect(tr("Rename effect"), [&](Effect& l_oE) { l_oE.m_strName = l_oName.toStdString(); });
        }
        f_ScheduleRebuild();   // a refused / empty name shows the stored one again
    });
    connect(l_pDoc, &ParticleDocument::f_FileChanged, this, &EffectsDock::f_ScheduleRebuild);
    connect(l_pDoc, &ParticleDocument::f_SelectionChanged, this, &EffectsDock::f_SyncSelection);
    f_Rebuild();
}

// Not while an item of the tree is still sending a signal (its edit may be what changed the file).
void EffectsDock::f_ScheduleRebuild()
{
    if (m_bPending) return;
    m_bPending = true;
    QTimer::singleShot(0, this, [this] {
        m_bPending = false;
        f_Rebuild();
    });
}

void EffectsDock::f_Rebuild()
{
    m_bRebuilding = true;
    m_pTree->clear();
    const auto& l_vEffects = m_pDoc->f_File().m_vEffects;
    for (int l_iI = 0; l_iI < int(l_vEffects.size()); l_iI++) {
        const Effect& l_oE = l_vEffects[size_t(l_iI)];
        auto* l_pTop = new QTreeWidgetItem(m_pTree, {f_Qs(l_oE.m_strName)});
        l_pTop->setData(0, g_iEffectRole, l_iI);
        l_pTop->setData(0, g_iEmitterRole, -1);
        l_pTop->setFlags(l_pTop->flags() | Qt::ItemIsEditable);
        l_pTop->setIcon(0, Icons::f_Icon(Icons::Id::Play));
        l_pTop->setToolTip(0, l_oE.m_fDuration > 0 ? tr("%1 s%2").arg(l_oE.m_fDuration).arg(l_oE.m_bLoop ? tr(", loops") : QString()) : tr("endless"));
        for (int l_iJ = 0; l_iJ < int(l_oE.m_vEmitters.size()); l_iJ++) {
            const Emitter& l_oM = l_oE.m_vEmitters[size_t(l_iJ)];
            auto* l_pIt = new QTreeWidgetItem(l_pTop, {f_Qs(l_oM.m_strName)});
            l_pIt->setData(0, g_iEffectRole, l_iI);
            l_pIt->setData(0, g_iEmitterRole, l_iJ);
            l_pIt->setFlags(l_pIt->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
            l_pIt->setCheckState(0, l_iI == m_pDoc->f_EffectIndex() && m_pPlay->f_IsHidden(l_iJ) ? Qt::Unchecked : Qt::Checked);
            QImage l_oImg = m_pDoc->f_SpriteImage(f_Qs(l_oM.m_vFrames.empty() ? l_oM.m_strSprite : l_oM.m_vFrames.front()));
            if (!l_oImg.isNull()) l_pIt->setIcon(0, QIcon(QPixmap::fromImage(l_oImg.scaled(16, 16, Qt::KeepAspectRatio, Qt::SmoothTransformation))));
            l_pIt->setToolTip(0, tr("%1 · %2").arg(f_Qs(l_oM.m_oBlend.f_PresetName().empty() ? std::string("custom blend") : l_oM.m_oBlend.f_PresetName()),
                                                 l_oM.m_vFrames.empty() ? f_Qs(l_oM.m_strSprite) : tr("%n frame(s)", nullptr, int(l_oM.m_vFrames.size()))));
        }
        l_pTop->setExpanded(true);
    }
    m_bRebuilding = false;
    f_SyncSelection();
}

void EffectsDock::f_SyncSelection()
{
    m_bRebuilding = true;
    QTreeWidgetItem* l_pWant = nullptr;
    for (int l_iI = 0; l_iI < m_pTree->topLevelItemCount() && !l_pWant; l_iI++) {
        QTreeWidgetItem* l_pTop = m_pTree->topLevelItem(l_iI);
        if (l_pTop->data(0, g_iEffectRole).toInt() != m_pDoc->f_EffectIndex()) continue;
        l_pWant = l_pTop;
        for (int l_iJ = 0; l_iJ < l_pTop->childCount(); l_iJ++)
            if (l_pTop->child(l_iJ)->data(0, g_iEmitterRole).toInt() == m_pDoc->f_EmitterIndex()) l_pWant = l_pTop->child(l_iJ);
    }
    if (l_pWant) m_pTree->setCurrentItem(l_pWant);
    m_bRebuilding = false;
}

void EffectsDock::f_AddEffect()
{
    Effect l_oE;
    l_oE.m_strName = m_pDoc->f_UniqueEffectName(QStringLiteral("effect")).toStdString();
    Emitter l_oM;
    l_oM.m_strName = "emitter";
    if (m_pDoc->f_AtlasCount() > 0) l_oM.m_strSprite = m_pDoc->f_AtlasIndexOf(QStringLiteral("fx")) >= 0 ? "fx:dot" : m_pDoc->f_SpriteRefs().value(0).toStdString();
    l_oE.m_vEmitters.push_back(l_oM);
    m_pDoc->f_AddEffect(l_oE);
}

void EffectsDock::f_AddEmitter()
{
    if (!m_pDoc->f_Effect()) return f_AddEffect();
    Emitter l_oM;
    if (const Emitter* l_pCur = m_pDoc->f_Emitter()) l_oM.m_strSprite = l_pCur->m_strSprite;   // the same look as the one selected
    else if (m_pDoc->f_AtlasCount() > 0) l_oM.m_strSprite = m_pDoc->f_AtlasIndexOf(QStringLiteral("fx")) >= 0 ? "fx:dot" : m_pDoc->f_SpriteRefs().value(0).toStdString();
    m_pDoc->f_AddEmitter(l_oM);
}

void EffectsDock::f_DuplicateSelected()
{
    if (m_pDoc->f_EmitterIndex() >= 0) m_pDoc->f_DuplicateEmitter(m_pDoc->f_EmitterIndex());
    else if (const Effect* l_pE = m_pDoc->f_Effect()) m_pDoc->f_AddEffect(*l_pE);
}

void EffectsDock::f_DeleteSelected()
{
    if (m_pDoc->f_EmitterIndex() >= 0) {
        m_pDoc->f_RemoveEmitter(m_pDoc->f_EmitterIndex());
        return;
    }
    const Effect* l_pE = m_pDoc->f_Effect();
    if (!l_pE) return;
    if (!qApp->property("toms.selftest").toBool() &&
        QMessageBox::question(this, tr("Delete Effect"), tr("Delete the effect '%1' and its %n emitter(s)?", nullptr, int(l_pE->m_vEmitters.size()))
                                                             .arg(f_Qs(l_pE->m_strName))) != QMessageBox::Yes)
        return;
    m_pDoc->f_RemoveEffect(m_pDoc->f_EffectIndex());
}

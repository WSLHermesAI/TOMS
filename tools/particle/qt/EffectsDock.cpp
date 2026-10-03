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
constexpr int kEffectRole = Qt::UserRole + 1;
constexpr int kEmitterRole = Qt::UserRole + 2;   // -1 = the effect row
QString qs(const std::string& s) { return QString::fromStdString(s); }
}

EffectsDock::EffectsDock(ParticleDocument* doc, ParticlePlayback* playback, QWidget* parent)
    : QDockWidget(tr("Effects"), parent)
    , m_doc(doc)
    , m_play(playback)
    , m_tree(new QTreeWidget(this))
    , m_bar(new QToolBar(this))
{
    setObjectName(QStringLiteral("ParticleEffectsDock"));
    m_tree->setHeaderHidden(true);
    m_tree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_tree->setToolTip(tr("Checkbox: show the emitter in the preview (not saved). Double-click / F2: rename."));
    m_bar->setIconSize(QSize(16, 16));
    m_bar->addAction(Icons::icon(Icons::Id::Add), tr("Effect"), this, &EffectsDock::addEffect)->setToolTip(tr("Add an effect"));
    m_bar->addAction(Icons::icon(Icons::Id::Node), tr("Emitter"), this, &EffectsDock::addEmitter)->setToolTip(tr("Add an emitter to the effect"));
    m_bar->addAction(Icons::icon(Icons::Id::Duplicate), tr("Duplicate"), this, &EffectsDock::duplicateSelected)->setToolTip(tr("Duplicate the emitter"));
    m_bar->addAction(Icons::icon(Icons::Id::Remove), tr("Delete"), this, &EffectsDock::deleteSelected)->setToolTip(tr("Delete the emitter (or the effect)"));
    m_bar->addAction(Icons::icon(Icons::Id::Up), tr("Up"), this, [this] { m_doc->moveEmitter(m_doc->emitterIndex(), -1); })->setToolTip(tr("Move the emitter up"));
    m_bar->addAction(Icons::icon(Icons::Id::Down), tr("Down"), this, [this] { m_doc->moveEmitter(m_doc->emitterIndex(), 1); })->setToolTip(tr("Move the emitter down"));
    m_bar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    auto* body = new QWidget(this);
    auto* l = new QVBoxLayout(body);
    l->setContentsMargins(2, 2, 2, 2);
    l->setSpacing(2);
    l->addWidget(m_bar);
    l->addWidget(m_tree, 1);
    setWidget(body);

    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* it) {
        if (m_rebuilding || !it) return;
        m_doc->select(it->data(0, kEffectRole).toInt(), it->data(0, kEmitterRole).toInt());
    });
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it) {
        if (m_rebuilding) return;
        const int ef = it->data(0, kEffectRole).toInt(), em = it->data(0, kEmitterRole).toInt();
        const QString name = it->text(0).trimmed();
        m_doc->select(ef, em);
        if (em >= 0) {
            const bool shown = it->checkState(0) == Qt::Checked;
            if (shown == m_play->isHidden(em)) m_play->setHidden(em, !shown);
            if (!name.isEmpty() && m_doc->emitter() && qs(m_doc->emitter()->name) != name)
                m_doc->editEmitter(tr("Rename emitter"), [&](Emitter& m) { m.name = name.toStdString(); });
        } else if (!name.isEmpty() && m_doc->effect() && qs(m_doc->effect()->name) != name) {
            m_doc->editEffect(tr("Rename effect"), [&](Effect& e) { e.name = name.toStdString(); });
        }
        scheduleRebuild();   // a refused / empty name shows the stored one again
    });
    connect(doc, &ParticleDocument::fileChanged, this, &EffectsDock::scheduleRebuild);
    connect(doc, &ParticleDocument::selectionChanged, this, &EffectsDock::syncSelection);
    rebuild();
}

// Not while an item of the tree is still sending a signal (its edit may be what changed the file).
void EffectsDock::scheduleRebuild()
{
    if (m_pending) return;
    m_pending = true;
    QTimer::singleShot(0, this, [this] {
        m_pending = false;
        rebuild();
    });
}

void EffectsDock::rebuild()
{
    m_rebuilding = true;
    m_tree->clear();
    const auto& effects = m_doc->file().effects;
    for (int i = 0; i < int(effects.size()); i++) {
        const Effect& e = effects[size_t(i)];
        auto* top = new QTreeWidgetItem(m_tree, {qs(e.name)});
        top->setData(0, kEffectRole, i);
        top->setData(0, kEmitterRole, -1);
        top->setFlags(top->flags() | Qt::ItemIsEditable);
        top->setIcon(0, Icons::icon(Icons::Id::Play));
        top->setToolTip(0, e.duration > 0 ? tr("%1 s%2").arg(e.duration).arg(e.loop ? tr(", loops") : QString()) : tr("endless"));
        for (int j = 0; j < int(e.emitters.size()); j++) {
            const Emitter& m = e.emitters[size_t(j)];
            auto* it = new QTreeWidgetItem(top, {qs(m.name)});
            it->setData(0, kEffectRole, i);
            it->setData(0, kEmitterRole, j);
            it->setFlags(it->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
            it->setCheckState(0, i == m_doc->effectIndex() && m_play->isHidden(j) ? Qt::Unchecked : Qt::Checked);
            QImage img = m_doc->spriteImage(qs(m.frames.empty() ? m.sprite : m.frames.front()));
            if (!img.isNull()) it->setIcon(0, QIcon(QPixmap::fromImage(img.scaled(16, 16, Qt::KeepAspectRatio, Qt::SmoothTransformation))));
            it->setToolTip(0, tr("%1 · %2").arg(qs(m.blend.presetName().empty() ? std::string("custom blend") : m.blend.presetName()),
                                                 m.frames.empty() ? qs(m.sprite) : tr("%n frame(s)", nullptr, int(m.frames.size()))));
        }
        top->setExpanded(true);
    }
    m_rebuilding = false;
    syncSelection();
}

void EffectsDock::syncSelection()
{
    m_rebuilding = true;
    QTreeWidgetItem* want = nullptr;
    for (int i = 0; i < m_tree->topLevelItemCount() && !want; i++) {
        QTreeWidgetItem* top = m_tree->topLevelItem(i);
        if (top->data(0, kEffectRole).toInt() != m_doc->effectIndex()) continue;
        want = top;
        for (int j = 0; j < top->childCount(); j++)
            if (top->child(j)->data(0, kEmitterRole).toInt() == m_doc->emitterIndex()) want = top->child(j);
    }
    if (want) m_tree->setCurrentItem(want);
    m_rebuilding = false;
}

void EffectsDock::addEffect()
{
    Effect e;
    e.name = m_doc->uniqueEffectName(QStringLiteral("effect")).toStdString();
    Emitter m;
    m.name = "emitter";
    if (m_doc->atlasCount() > 0) m.sprite = m_doc->atlasIndexOf(QStringLiteral("fx")) >= 0 ? "fx:dot" : m_doc->spriteRefs().value(0).toStdString();
    e.emitters.push_back(m);
    m_doc->addEffect(e);
}

void EffectsDock::addEmitter()
{
    if (!m_doc->effect()) return addEffect();
    Emitter m;
    if (const Emitter* cur = m_doc->emitter()) m.sprite = cur->sprite;   // the same look as the one selected
    else if (m_doc->atlasCount() > 0) m.sprite = m_doc->atlasIndexOf(QStringLiteral("fx")) >= 0 ? "fx:dot" : m_doc->spriteRefs().value(0).toStdString();
    m_doc->addEmitter(m);
}

void EffectsDock::duplicateSelected()
{
    if (m_doc->emitterIndex() >= 0) m_doc->duplicateEmitter(m_doc->emitterIndex());
    else if (const Effect* e = m_doc->effect()) m_doc->addEffect(*e);
}

void EffectsDock::deleteSelected()
{
    if (m_doc->emitterIndex() >= 0) {
        m_doc->removeEmitter(m_doc->emitterIndex());
        return;
    }
    const Effect* e = m_doc->effect();
    if (!e) return;
    if (!qApp->property("toms.selftest").toBool() &&
        QMessageBox::question(this, tr("Delete Effect"), tr("Delete the effect '%1' and its %n emitter(s)?", nullptr, int(e->emitters.size()))
                                                             .arg(qs(e->name))) != QMessageBox::Yes)
        return;
    m_doc->removeEffect(m_doc->effectIndex());
}

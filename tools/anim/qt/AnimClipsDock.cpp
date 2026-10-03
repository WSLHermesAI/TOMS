#include "AnimClipsDock.h"

#include "Icons.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QListWidget>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolBar>
#include <QVBoxLayout>

using toms::anim::Clip;

ClipFields ClipFields::create(AnimDocument* doc, QWidget* parent, bool* updating)
{
    ClipFields f;
    f.length = new QDoubleSpinBox(parent);
    f.length->setRange(0, 3600);
    f.length->setDecimals(3);
    f.length->setSingleStep(0.05);
    f.length->setSuffix(QStringLiteral(" s"));
    f.length->setSpecialValueText(QObject::tr("auto (last key)"));
    f.length->setKeyboardTracking(false);
    f.length->setToolTip(QObject::tr("Clip length in seconds; 0 = up to the last key"));
    f.playCount = new QSpinBox(parent);
    f.playCount->setRange(-1, 9999);
    f.playCount->setSpecialValueText(QObject::tr("loop forever"));
    f.playCount->setKeyboardTracking(false);
    f.playCount->setToolTip(QObject::tr("Times to play; -1 = loop forever"));
    f.stay = new QCheckBox(QObject::tr("Stay at last frame"), parent);
    f.stay->setToolTip(QObject::tr("After the last play keep showing the end (else the clip disappears)"));
    QObject::connect(f.length, &QDoubleSpinBox::valueChanged, parent, [doc, updating](double v) {
        if (*updating) return;
        doc->editClip(QObject::tr("Clip length"), [v](Clip& c) { c.length = float(v); }, QStringLiteral("clip.length"));
    });
    QObject::connect(f.playCount, &QSpinBox::valueChanged, parent, [doc, updating](int v) {
        if (*updating) return;
        doc->editClip(QObject::tr("Play count"), [v](Clip& c) { c.playCount = v; }, QStringLiteral("clip.playCount"));
    });
    QObject::connect(f.stay, &QCheckBox::toggled, parent, [doc, updating](bool on) {
        if (*updating) return;
        doc->editClip(QObject::tr("Stay at last frame"), [on](Clip& c) { c.stayAtLastFrame = on; });
    });
    return f;
}

void ClipFields::refresh(const AnimDocument* doc) const
{
    const Clip* c = doc->clip();
    for (QWidget* w : std::initializer_list<QWidget*>{length, playCount, stay}) w->setEnabled(c != nullptr);
    if (!c) return;
    const QSignalBlocker b1(length), b2(playCount), b3(stay);
    length->setValue(c->length);
    playCount->setValue(c->playCount);
    stay->setChecked(c->stayAtLastFrame);
}

AnimClipsDock::AnimClipsDock(AnimDocument* doc, QWidget* parent)
    : QDockWidget(tr("Clips"), parent)
    , m_doc(doc)
    , m_list(new QListWidget(this))
{
    setObjectName(QStringLiteral("AnimClipsDock"));
    auto* body = new QWidget(this);
    auto* tb = new QToolBar(body);
    tb->setIconSize(QSize(16, 16));
    QAction* add = tb->addAction(Icons::icon(Icons::Id::Add), tr("Add clip"));
    QAction* dup = tb->addAction(Icons::icon(Icons::Id::Duplicate), tr("Duplicate clip"));
    QAction* ren = tb->addAction(Icons::icon(Icons::Id::Rename), tr("Rename clip"));
    QAction* del = tb->addAction(Icons::icon(Icons::Id::Remove), tr("Delete clip"));
    connect(add, &QAction::triggered, this, [this] { m_doc->addClip(QStringLiteral("clip")); });
    connect(dup, &QAction::triggered, this, [this] { m_doc->duplicateClip(m_doc->clipIndex()); });
    connect(ren, &QAction::triggered, this, &AnimClipsDock::beginRename);
    connect(del, &QAction::triggered, this, [this] { m_doc->deleteClip(m_doc->clipIndex()); });

    const ClipFields f = ClipFields::create(doc, body, &m_updating);
    m_length = f.length;
    m_playCount = f.playCount;
    m_stay = f.stay;
    auto* form = new QFormLayout();
    form->setContentsMargins(2, 2, 2, 2);
    form->addRow(tr("Length"), m_length);
    form->addRow(tr("Plays"), m_playCount);
    form->addRow(QString(), m_stay);

    m_list->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    auto* l = new QVBoxLayout(body);
    l->setContentsMargins(4, 2, 4, 4);
    l->setSpacing(2);
    l->addWidget(tb);
    l->addWidget(m_list, 1);
    l->addLayout(form);
    setWidget(body);

    connect(doc, &AnimDocument::fileChanged, this, &AnimClipsDock::rebuild);
    connect(doc, &AnimDocument::clipChanged, this, &AnimClipsDock::rebuild);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_updating && row >= 0) m_doc->setClipIndex(row);
    });
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* it) {
        if (m_updating) return;
        if (!m_doc->renameClip(m_list->row(it), it->text())) rebuild();
    });
    rebuild();
}

void AnimClipsDock::beginRename()
{
    if (QListWidgetItem* it = m_list->currentItem()) m_list->editItem(it);
}

void AnimClipsDock::rebuild()
{
    m_updating = true;
    const auto& clips = m_doc->file().clips;
    if (m_list->count() != int(clips.size())) {
        m_list->clear();
        for (size_t i = 0; i < clips.size(); i++) m_list->addItem(QString());
    }
    for (size_t i = 0; i < clips.size(); i++) {
        QListWidgetItem* it = m_list->item(int(i));
        it->setText(QString::fromStdString(clips[i].name));
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
        const Clip& c = clips[i];
        it->setToolTip(tr("%1 s, %2").arg(c.duration(), 0, 'f', 3).arg(c.playCount < 0 ? tr("loops") : tr("plays %1×").arg(c.playCount)));
        it->setIcon(Icons::icon(c.playCount < 0 ? Icons::Id::Loop : Icons::Id::Play));
    }
    m_list->setCurrentRow(m_doc->clipIndex());
    ClipFields{m_length, m_playCount, m_stay}.refresh(m_doc);
    m_updating = false;
}

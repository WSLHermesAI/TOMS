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
    // The toolbar acts on the highlighted row (a single click), which need not be the open clip.
    auto row = [this] { return m_list->currentRow() >= 0 ? m_list->currentRow() : m_doc->clipIndex(); };
    connect(dup, &QAction::triggered, this, [this, row] { m_doc->duplicateClip(row()); });
    connect(ren, &QAction::triggered, this, &AnimClipsDock::beginRename);
    connect(del, &QAction::triggered, this, [this, row] { m_doc->deleteClip(row()); });
    ren->setToolTip(tr("Rename clip (F2)"));

    const ClipFields f = ClipFields::create(doc, body, &m_updating);
    m_length = f.length;
    m_playCount = f.playCount;
    m_stay = f.stay;
    auto* form = new QFormLayout();
    form->setContentsMargins(2, 2, 2, 2);
    form->addRow(tr("Length"), m_length);
    form->addRow(tr("Plays"), m_playCount);
    form->addRow(QString(), m_stay);

    // Double-click (or Enter) opens a clip; F2 or the Rename button renames it.
    m_list->setEditTriggers(QAbstractItemView::EditKeyPressed);
    m_list->setToolTip(tr("Double-click a clip to open it (F2 renames)"));
    auto* l = new QVBoxLayout(body);
    l->setContentsMargins(4, 2, 4, 4);
    l->setSpacing(2);
    l->addWidget(tb);
    l->addWidget(m_list, 1);
    l->addLayout(form);
    setWidget(body);

    connect(doc, &AnimDocument::fileChanged, this, &AnimClipsDock::rebuild);
    connect(doc, &AnimDocument::clipChanged, this, &AnimClipsDock::rebuild);
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem* it) { openClip(m_list->row(it)); });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        if (m_list->row(it) != m_doc->clipIndex())
            emit m_doc->message(tr("Double-click “%1” to open it (the viewport shows the bold clip).").arg(it->text()));
    });
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem* it) {
        if (m_updating) return;
        if (!m_doc->renameClip(m_list->row(it), it->text())) rebuild();
    });
    rebuild();
}

bool AnimClipsDock::openClip(int row, int discard)
{
    const auto& clips = m_doc->file().clips;
    if (row < 0 || row >= int(clips.size())) return false;
    if (row == m_doc->clipIndex()) return true;
    if (m_doc->clipModified()) {
        const QString cur = QString::fromStdString(clips[size_t(m_doc->clipIndex())].name);
        const QString next = QString::fromStdString(clips[size_t(row)].name);
        bool yes = discard == 1;
        if (discard < 0)
            yes = QMessageBox::question(this, tr("Open Clip"),
                                        tr("The clip \u201c%1\u201d has changes.\n\nDiscard all changes to \u201c%1\u201d and open \u201c%2\u201d?\n"
                                           "(Edit > Undo brings them back.)").arg(cur, next),
                                        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
        if (!yes) {
            rebuild();   // the highlight goes back to the open clip
            return false;
        }
        m_doc->discardClipChanges();
    }
    m_doc->setClipIndex(row);
    return true;
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
        QFont font = m_list->font();
        font.setBold(int(i) == m_doc->clipIndex());   // the open clip
        it->setFont(font);
    }
    m_list->setCurrentRow(m_doc->clipIndex());
    ClipFields{m_length, m_playCount, m_stay}.refresh(m_doc);
    m_updating = false;
}

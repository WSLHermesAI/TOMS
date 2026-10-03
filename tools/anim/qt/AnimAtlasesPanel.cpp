#include "AnimAtlasesPanel.h"

#include "Icons.h"

#include <QApplication>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QToolButton* toolButton(QWidget* parent, Icons::Id icon, const QString& tip)
{
    auto* b = new QToolButton(parent);
    b->setIcon(Icons::icon(icon));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setIconSize(QSize(16, 16));
    return b;
}

// --selftest runs without anyone to answer a dialog: prompts take their default.
bool selftestRunning() { return qApp && qApp->property("toms.selftest").toBool(); }

}  // namespace

AnimAtlasesPanel::AnimAtlasesPanel(AnimDocument* doc, QWidget* parent)
    : QWidget(parent)
    , m_doc(doc)
{
    // Three columns -- the id, the path (elided in the middle) and the sprite count -- so a long
    // relative path never needs a horizontal scroll bar.
    m_list = new QTreeWidget(this);
    m_list->setObjectName(QStringLiteral("AnimAtlasesList"));
    m_list->setColumnCount(3);
    m_list->setHeaderHidden(true);
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);   // ids: double-click (below), F2
    m_list->setTextElideMode(Qt::ElideMiddle);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(ColId, QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(ColPath, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(ColCount, QHeaderView::ResizeToContents);
    m_list->setToolTip(tr("Sprite references name an atlas by its id (id:name); a bare name is looked up in each atlas in this "
                          "order, the first that has it wins"));
    m_add = toolButton(this, Icons::Id::Add, tr("Add an atlas (stored relative to the .anim)"));
    m_remove = toolButton(this, Icons::Id::Remove, tr("Remove the selected atlas from the list"));
    m_up = toolButton(this, Icons::Id::Up, tr("Look the selected atlas up earlier"));
    m_down = toolButton(this, Icons::Id::Down, tr("Look the selected atlas up later"));
    m_qualify = new QToolButton(this);
    m_qualify->setText(tr("Qualify"));
    m_qualify->setAutoRaise(true);
    m_qualify->setToolTip(tr("Qualify sprite references: store the atlas with every bare sprite name (name -> id:name), "
                             "using the atlas the lookup picks now"));
    m_reload = toolButton(this, Icons::Id::Loop, tr("Reload every atlas from disk (Ctrl+Shift+R)"));

    auto* buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(1);
    for (QToolButton* b : {m_add, m_remove, m_up, m_down, m_qualify}) buttons->addWidget(b);
    buttons->addStretch(1);
    buttons->addWidget(m_reload);
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(2);
    l->addWidget(m_list);
    l->addLayout(buttons);

    connect(m_add, &QToolButton::clicked, this, &AnimAtlasesPanel::addRequested);
    connect(m_remove, &QToolButton::clicked, this, [this] { removeAtlas(currentIndex()); });
    connect(m_up, &QToolButton::clicked, this, [this] {
        const int i = currentIndex();
        if (m_doc->moveAtlas(i, -1)) setCurrentIndex(i - 1);
    });
    connect(m_down, &QToolButton::clicked, this, [this] {
        const int i = currentIndex();
        if (m_doc->moveAtlas(i, 1)) setCurrentIndex(i + 1);
    });
    connect(m_qualify, &QToolButton::clicked, m_doc, &AnimDocument::qualifySpriteReferences);
    connect(m_reload, &QToolButton::clicked, m_doc, &AnimDocument::reloadAtlases);
    connect(m_list, &QTreeWidget::currentItemChanged, this, &AnimAtlasesPanel::updateButtons);
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it, int) {
        if (it->flags() & Qt::ItemIsEditable) m_list->editItem(it, ColId);
    });
    connect(m_list, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it, int col) {
        if (m_updating || col != ColId) return;
        const int row = m_list->indexOfTopLevelItem(it);
        const QString id = it->text(ColId).trimmed();
        // Rebuilding inside itemChanged would delete the item being committed: do it right after.
        QMetaObject::invokeMethod(this, [this, row, id] {
            if (!renameId(row, id)) rebuild();   // refused or unchanged: show the old id again
        }, Qt::QueuedConnection);
    });
    connect(doc, &AnimDocument::atlasChanged, this, &AnimAtlasesPanel::rebuild);
    connect(doc, &AnimDocument::filePathChanged, this, &AnimAtlasesPanel::rebuild);
    connect(doc, &AnimDocument::fileChanged, this, &AnimAtlasesPanel::updateButtons);
    rebuild();
}

int AnimAtlasesPanel::currentIndex() const
{
    const int row = m_list->indexOfTopLevelItem(m_list->currentItem());
    return row >= 0 && row < m_doc->atlasCount() ? row : -1;
}

void AnimAtlasesPanel::setCurrentIndex(int index)
{
    if (index >= 0 && index < m_doc->atlasCount()) m_list->setCurrentItem(m_list->topLevelItem(index));
}

void AnimAtlasesPanel::warn(const QString& title, const QString& text)
{
    emit m_doc->message(text);
    if (!selftestRunning()) QMessageBox::warning(this, title, text);
}

bool AnimAtlasesPanel::renameId(int index, const QString& id)
{
    if (index < 0 || index >= m_doc->atlasCount() || id == m_doc->atlasId(index)) return false;
    QString err;
    if (m_doc->renameAtlasId(index, id, &err)) return true;
    if (!err.isEmpty()) warn(tr("Atlas Id"), err);
    return false;
}

bool AnimAtlasesPanel::removeAtlas(int index)
{
    if (index < 0 || index >= m_doc->atlasCount()) return false;
    const int refs = m_doc->atlasReferenceCount(index);
    if (refs > 0 && !selftestRunning() &&
        QMessageBox::question(this, tr("Remove Atlas"),
                              tr("%n sprite reference(s) use the atlas '%1' (%2).\n\nWithout it they become errors "
                                 "(see Problems). Remove it anyway?", nullptr, refs)
                                  .arg(m_doc->atlasId(index), m_doc->storedAtlasPath(index)),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return false;
    const QString id = m_doc->atlasId(index);
    if (!m_doc->removeAtlas(index)) return false;
    if (refs > 0) emit m_doc->message(tr("Removed the atlas '%1': %n sprite reference(s) no longer resolve.", nullptr, refs).arg(id));
    return true;
}

void AnimAtlasesPanel::rebuild()
{
    const int keep = currentIndex();
    m_updating = true;
    m_list->clear();
    const QColor bad(236, 96, 96);
    QFont idFont = m_list->font();
    idFont.setBold(true);
    for (int i = 0; i < m_doc->atlasCount(); i++) {
        const AnimDocument::LoadedAtlas& a = m_doc->atlasAt(i);
        auto* it = new QTreeWidgetItem(m_list);
        it->setFlags(it->flags() | Qt::ItemIsEditable);
        it->setText(ColId, m_doc->atlasId(i));
        it->setFont(ColId, idFont);
        it->setText(ColPath, m_doc->storedAtlasPath(i));
        QString tip = QDir::toNativeSeparators(a.path);
        if (a.ok) {
            it->setIcon(ColId, Icons::icon(Icons::Id::Grid));
            it->setText(ColCount, a.spriteNames.size() == 1 ? tr("1 sprite") : tr("%1 sprites").arg(a.spriteNames.size()));
        } else {
            it->setIcon(ColId, Icons::icon(Icons::Id::Error));
            it->setText(ColCount, tr("not loaded"));
            for (int c = 0; c < 3; c++) it->setForeground(c, bad);
            tip += QStringLiteral("\n") + a.error;
        }
        if (m_doc->atlasPathIsAbsoluteInFile(i)) tip += tr("\nThe file names it with an absolute path: it is stored relative on save.");
        else if (m_doc->filePath().isEmpty()) tip += tr("\nUntitled file: the path becomes relative when the .anim is saved.");
        it->setToolTip(ColId, tr("Id '%1': sprites from this atlas are stored as %1:name. Double-click to rename it "
                                 "(every reference is rewritten).").arg(m_doc->atlasId(i)) +
                                  QStringLiteral("\n") + tip);
        it->setToolTip(ColPath, tip);
        it->setToolTip(ColCount, tip);
        it->setTextAlignment(ColCount, Qt::AlignRight | Qt::AlignVCenter);
    }
    if (m_doc->atlasCount() == 0) {
        auto* it = new QTreeWidgetItem(m_list, {tr("(no atlas: + adds one)")});
        it->setFirstColumnSpanned(true);
        it->setFlags(Qt::NoItemFlags);
    }
    // Compact: as tall as its rows (2..5).
    const int rows = std::clamp(m_list->topLevelItemCount(), 2, 5);
    const int rowH = std::max(m_list->sizeHintForRow(0), std::max(m_list->fontMetrics().height(), m_list->iconSize().height()) + 6);
    m_list->setFixedHeight(rows * rowH + 2 * m_list->frameWidth());
    setCurrentIndex(keep);
    m_updating = false;
    updateButtons();
}

void AnimAtlasesPanel::updateButtons()
{
    const int i = currentIndex();
    m_remove->setEnabled(i >= 0);
    m_up->setEnabled(i > 0);
    m_down->setEnabled(i >= 0 && i + 1 < m_doc->atlasCount());
    m_reload->setEnabled(m_doc->atlasCount() > 0);
    m_qualify->setEnabled(m_doc->bareSpriteReferenceCount() > 0);
}

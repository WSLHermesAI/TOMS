#include "ReferencesDialog.h"

#include "atlas_store.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageReader>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

constexpr int kThumb = 28;
constexpr int kMaxThumbnails = 600;   // past this many rows the list stays text only

enum Column { StatusCol, NameCol, FileCol };

QIcon thumbnail(const QString& file)
{
    QImageReader reader(file);
    QSize size = reader.size();
    if (size.isValid() && (size.width() > kThumb || size.height() > kThumb)) reader.setScaledSize(size.scaled(kThumb, kThumb, Qt::KeepAspectRatio));
    const QImage img = reader.read();
    if (img.isNull()) return QIcon();
    QPixmap pm(kThumb, kThumb);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.drawImage(QPoint((kThumb - img.width()) / 2, (kThumb - img.height()) / 2), img);
    return QIcon(pm);
}

}  // namespace

ReferencesDialog::ReferencesDialog(AtlasDocument* doc, QWidget* parent)
    : QDialog(parent)
    , m_doc(doc)
    , m_variant(doc->variant())
{
    setWindowTitle(m_variant.isEmpty() ? tr("Import From References") : tr("Import From Reference — %1").arg(m_variant));
    m_header = new QLabel(this);
    m_header->setWordWrap(true);
    m_header->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_table = new QTableWidget(0, 3, this);
    m_table->setHorizontalHeaderLabels({tr("Status"), tr("Name"), tr("File")});
    m_table->horizontalHeader()->setSectionResizeMode(StatusCol, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(NameCol, QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(NameCol, 220);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(kThumb + 6);
    m_table->setIconSize(QSize(kThumb, kThumb));
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    m_table->setSortingEnabled(false);
    connect(m_table, &QTableWidget::itemChanged, this, &ReferencesDialog::updateCount);
    // Clicking anywhere on a row toggles it.
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row) {
        QTableWidgetItem* it = m_table->item(row, StatusCol);
        if (it && (it->flags() & Qt::ItemIsEnabled)) it->setCheckState(it->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
    });

    m_summary = new QLabel(this);
    m_summary->setForegroundRole(QPalette::PlaceholderText);
    auto* pickChanged = new QPushButton(tr("New + Changed"), this);
    auto* pickNone = new QPushButton(tr("None"), this);
    auto* rescan = new QPushButton(tr("Rescan"), this);
    connect(pickChanged, &QPushButton::clicked, this, [this] { checkAll(true, true); });
    connect(pickNone, &QPushButton::clicked, this, [this] { checkAll(false, false); });
    connect(rescan, &QPushButton::clicked, this, &ReferencesDialog::scan);
    auto* tools = new QHBoxLayout();
    tools->addWidget(m_summary, 1);
    tools->addWidget(pickChanged);
    tools->addWidget(pickNone);
    tools->addWidget(rescan);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_import = buttons->addButton(tr("Import"), QDialogButtonBox::AcceptRole);
    m_import->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* l = new QVBoxLayout(this);
    l->addWidget(m_header);
    l->addWidget(m_table, 1);
    l->addLayout(tools);
    l->addWidget(buttons);
    resize(820, 560);
    scan();
}

void ReferencesDialog::scan()
{
    const atlas::Project& p = m_doc->project();
    QStringList folders;
    if (const atlas::Variant* v = m_variant.isEmpty() ? nullptr : p.findVariant(m_variant.toStdString())) {
        if (!v->reference.empty()) folders << QDir::toNativeSeparators(m_doc->resolvePath(qs(v->reference)));
        m_header->setText(folders.isEmpty()
                              ? tr("Variant <b>%1</b> has no reference folder. Set one in Properties → Variant.").arg(m_variant.toHtmlEscaped())
                              : tr("Replacement art for variant <b>%1</b> from<br>%2<br>A PNG replaces the image of the same name.")
                                    .arg(m_variant.toHtmlEscaped(), folders.first().toHtmlEscaped()));
    } else {
        for (const atlas::SourceFolder& f : p.references) folders << QDir::toNativeSeparators(m_doc->resolvePath(qs(f.path))).toHtmlEscaped();
        m_header->setText(folders.isEmpty() ? tr("The project has no reference folders. Add one in Properties → References.")
                                            : tr("Art in the reference folders:<br>%1").arg(folders.join(QStringLiteral("<br>"))));
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const std::vector<atlas::ReferenceImage> refs = atlas::scanReferences(p, atlas::fileImageSource(), m_variant.toStdString());
    const QSignalBlocker block(m_table);
    m_table->setRowCount(int(refs.size()));
    const bool thumbs = refs.size() <= size_t(kMaxThumbnails);
    for (size_t i = 0; i < refs.size(); i++) {
        const atlas::ReferenceImage& r = refs[i];
        const int row = int(i);
        QString status;
        QColor color;
        switch (r.status) {
        case atlas::ReferenceImage::New:
            status = tr("New");
            color = QColor(0x3f, 0xb9, 0x50);
            break;
        case atlas::ReferenceImage::Changed:
            status = tr("Changed");
            color = QColor(0xe0, 0xa8, 0x2e);
            break;
        case atlas::ReferenceImage::Same:
            status = tr("Same");
            color = QApplication::palette().color(QPalette::Disabled, QPalette::Text);
            break;
        }
        auto* st = new QTableWidgetItem(status);
        st->setForeground(color);
        st->setData(Qt::UserRole, int(r.status));
        if (r.status == atlas::ReferenceImage::Same) {
            st->setFlags(Qt::ItemIsUserCheckable);   // shown, not importable: nothing would change
            st->setCheckState(Qt::Unchecked);
            st->setToolTip(tr("Already in the project with the same pixels"));
        } else {
            st->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            st->setCheckState(Qt::Checked);
            st->setToolTip(r.status == atlas::ReferenceImage::New ? tr("Not in the project yet") : tr("The pixels differ from the project's"));
        }
        m_table->setItem(row, StatusCol, st);
        auto* name = new QTableWidgetItem(qs(r.name));
        if (thumbs) name->setIcon(thumbnail(qs(r.file)));
        if (r.status == atlas::ReferenceImage::Same) name->setForeground(color);
        m_table->setItem(row, NameCol, name);
        auto* file = new QTableWidgetItem(QDir::toNativeSeparators(m_doc->toProjectPath(qs(r.file))));
        file->setData(Qt::UserRole, qs(r.file));
        file->setToolTip(QDir::toNativeSeparators(qs(r.file)));
        if (r.status == atlas::ReferenceImage::Same) file->setForeground(color);
        m_table->setItem(row, FileCol, file);
    }
    QApplication::restoreOverrideCursor();
    updateCount();
}

void ReferencesDialog::checkAll(bool newOnes, bool changed)
{
    const QSignalBlocker block(m_table);
    for (int row = 0; row < m_table->rowCount(); row++) {
        QTableWidgetItem* it = m_table->item(row, StatusCol);
        const int s = it->data(Qt::UserRole).toInt();
        if (s == atlas::ReferenceImage::Same) continue;
        const bool on = s == atlas::ReferenceImage::New ? newOnes : changed;
        it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    }
    updateCount();
}

void ReferencesDialog::updateCount()
{
    int counts[3] = {0, 0, 0}, checked = 0;
    for (int row = 0; row < m_table->rowCount(); row++) {
        QTableWidgetItem* it = m_table->item(row, StatusCol);
        if (!it) continue;
        counts[std::clamp(it->data(Qt::UserRole).toInt(), 0, 2)]++;
        checked += it->checkState() == Qt::Checked;
    }
    m_summary->setText(tr("%1 new · %2 changed · %3 same").arg(counts[0]).arg(counts[1]).arg(counts[2]));
    m_import->setText(checked ? tr("Import %1").arg(checked) : tr("Import"));
    m_import->setEnabled(checked > 0);
}

QList<ImageFile> ReferencesDialog::chosen() const
{
    QList<ImageFile> out;
    for (int row = 0; row < m_table->rowCount(); row++)
        if (m_table->item(row, StatusCol)->checkState() == Qt::Checked)
            out.push_back({m_table->item(row, NameCol)->text(), m_table->item(row, FileCol)->data(Qt::UserRole).toString()});
    return out;
}

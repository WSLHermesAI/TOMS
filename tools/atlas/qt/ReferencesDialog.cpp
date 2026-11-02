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

QString f_Qs(const std::string& l_strS) { return QString::fromStdString(l_strS); }

constexpr int g_iThumb = 28;
constexpr int g_iMaxThumbnails = 600;   // past this many rows the list stays text only

enum Column { StatusCol, NameCol, FileCol };

QIcon f_Thumbnail(const QString& l_oFile)
{
    QImageReader l_oReader(l_oFile);
    QSize l_oSize = l_oReader.size();
    if (l_oSize.isValid() && (l_oSize.width() > g_iThumb || l_oSize.height() > g_iThumb)) l_oReader.setScaledSize(l_oSize.scaled(g_iThumb, g_iThumb, Qt::KeepAspectRatio));
    const QImage l_oImg = l_oReader.read();
    if (l_oImg.isNull()) return QIcon();
    QPixmap l_oPm(g_iThumb, g_iThumb);
    l_oPm.fill(Qt::transparent);
    QPainter l_oP(&l_oPm);
    l_oP.drawImage(QPoint((g_iThumb - l_oImg.width()) / 2, (g_iThumb - l_oImg.height()) / 2), l_oImg);
    return QIcon(l_oPm);
}

}  // namespace

ReferencesDialog::ReferencesDialog(AtlasDocument* l_pDoc, QWidget* l_pParent)
    : QDialog(l_pParent)
    , m_pDoc(l_pDoc)
    , m_oVariant(l_pDoc->f_Variant())
{
    setWindowTitle(m_oVariant.isEmpty() ? tr("Import From References") : tr("Import From Reference — %1").arg(m_oVariant));
    m_pHeader = new QLabel(this);
    m_pHeader->setWordWrap(true);
    m_pHeader->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_pTable = new QTableWidget(0, 3, this);
    m_pTable->setHorizontalHeaderLabels({tr("Status"), tr("Name"), tr("File")});
    m_pTable->horizontalHeader()->setSectionResizeMode(StatusCol, QHeaderView::ResizeToContents);
    m_pTable->horizontalHeader()->setSectionResizeMode(NameCol, QHeaderView::Interactive);
    m_pTable->horizontalHeader()->setStretchLastSection(true);
    m_pTable->setColumnWidth(NameCol, 220);
    m_pTable->verticalHeader()->hide();
    m_pTable->verticalHeader()->setDefaultSectionSize(g_iThumb + 6);
    m_pTable->setIconSize(QSize(g_iThumb, g_iThumb));
    m_pTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pTable->setShowGrid(false);
    m_pTable->setAlternatingRowColors(true);
    m_pTable->setSortingEnabled(false);
    connect(m_pTable, &QTableWidget::itemChanged, this, &ReferencesDialog::f_UpdateCount);
    // Clicking anywhere on a row toggles it.
    connect(m_pTable, &QTableWidget::cellDoubleClicked, this, [this](int l_iRow) {
        QTableWidgetItem* l_pIt = m_pTable->item(l_iRow, StatusCol);
        if (l_pIt && (l_pIt->flags() & Qt::ItemIsEnabled)) l_pIt->setCheckState(l_pIt->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
    });

    m_pSummary = new QLabel(this);
    m_pSummary->setForegroundRole(QPalette::PlaceholderText);
    auto* l_pPickChanged = new QPushButton(tr("New + Changed"), this);
    auto* l_pPickNone = new QPushButton(tr("None"), this);
    auto* l_pRescan = new QPushButton(tr("Rescan"), this);
    connect(l_pPickChanged, &QPushButton::clicked, this, [this] { f_CheckAll(true, true); });
    connect(l_pPickNone, &QPushButton::clicked, this, [this] { f_CheckAll(false, false); });
    connect(l_pRescan, &QPushButton::clicked, this, &ReferencesDialog::f_Scan);
    auto* l_pTools = new QHBoxLayout();
    l_pTools->addWidget(m_pSummary, 1);
    l_pTools->addWidget(l_pPickChanged);
    l_pTools->addWidget(l_pPickNone);
    l_pTools->addWidget(l_pRescan);

    auto* l_pButtons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_pImport = l_pButtons->addButton(tr("Import"), QDialogButtonBox::AcceptRole);
    m_pImport->setDefault(true);
    connect(l_pButtons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(l_pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* l_pL = new QVBoxLayout(this);
    l_pL->addWidget(m_pHeader);
    l_pL->addWidget(m_pTable, 1);
    l_pL->addLayout(l_pTools);
    l_pL->addWidget(l_pButtons);
    resize(820, 560);
    f_Scan();
}

void ReferencesDialog::f_Scan()
{
    const atlas::Project& l_oP = m_pDoc->f_Project();
    QStringList l_oFolders;
    if (const atlas::Variant* l_pV = m_oVariant.isEmpty() ? nullptr : l_oP.f_FindVariant(m_oVariant.toStdString())) {
        if (!l_pV->m_strReference.empty()) l_oFolders << QDir::toNativeSeparators(m_pDoc->f_ResolvePath(f_Qs(l_pV->m_strReference)));
        m_pHeader->setText(l_oFolders.isEmpty()
                              ? tr("Variant <b>%1</b> has no reference folder. Set one in Properties → Variant.").arg(m_oVariant.toHtmlEscaped())
                              : tr("Replacement art for variant <b>%1</b> from<br>%2<br>A PNG replaces the image of the same name.")
                                    .arg(m_oVariant.toHtmlEscaped(), l_oFolders.first().toHtmlEscaped()));
    } else {
        for (const atlas::SourceFolder& l_oF : l_oP.m_vReferences) l_oFolders << QDir::toNativeSeparators(m_pDoc->f_ResolvePath(f_Qs(l_oF.m_strPath))).toHtmlEscaped();
        m_pHeader->setText(l_oFolders.isEmpty() ? tr("The project has no reference folders. Add one in Properties → References.")
                                            : tr("Art in the reference folders:<br>%1").arg(l_oFolders.join(QStringLiteral("<br>"))));
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const std::vector<atlas::ReferenceImage> l_vRefs = atlas::f_ScanReferences(l_oP, atlas::f_FileImageSource(), m_oVariant.toStdString());
    const QSignalBlocker l_oBlock(m_pTable);
    m_pTable->setRowCount(int(l_vRefs.size()));
    const bool l_bThumbs = l_vRefs.size() <= size_t(g_iMaxThumbnails);
    for (size_t l_nI = 0; l_nI < l_vRefs.size(); l_nI++) {
        const atlas::ReferenceImage& l_oR = l_vRefs[l_nI];
        const int l_iRow = int(l_nI);
        QString l_oStatus;
        QColor l_oColor;
        switch (l_oR.m_eStatus) {
        case atlas::ReferenceImage::New:
            l_oStatus = tr("New");
            l_oColor = QColor(0x3f, 0xb9, 0x50);
            break;
        case atlas::ReferenceImage::Changed:
            l_oStatus = tr("Changed");
            l_oColor = QColor(0xe0, 0xa8, 0x2e);
            break;
        case atlas::ReferenceImage::Same:
            l_oStatus = tr("Same");
            l_oColor = QApplication::palette().color(QPalette::Disabled, QPalette::Text);
            break;
        }
        auto* l_pSt = new QTableWidgetItem(l_oStatus);
        l_pSt->setForeground(l_oColor);
        l_pSt->setData(Qt::UserRole, int(l_oR.m_eStatus));
        if (l_oR.m_eStatus == atlas::ReferenceImage::Same) {
            l_pSt->setFlags(Qt::ItemIsUserCheckable);   // shown, not importable: nothing would change
            l_pSt->setCheckState(Qt::Unchecked);
            l_pSt->setToolTip(tr("Already in the project with the same pixels"));
        } else {
            l_pSt->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            l_pSt->setCheckState(Qt::Checked);
            l_pSt->setToolTip(l_oR.m_eStatus == atlas::ReferenceImage::New ? tr("Not in the project yet") : tr("The pixels differ from the project's"));
        }
        m_pTable->setItem(l_iRow, StatusCol, l_pSt);
        auto* l_pName = new QTableWidgetItem(f_Qs(l_oR.m_strName));
        if (l_bThumbs) l_pName->setIcon(f_Thumbnail(f_Qs(l_oR.m_strFile)));
        if (l_oR.m_eStatus == atlas::ReferenceImage::Same) l_pName->setForeground(l_oColor);
        m_pTable->setItem(l_iRow, NameCol, l_pName);
        auto* l_pFile = new QTableWidgetItem(QDir::toNativeSeparators(m_pDoc->f_ToProjectPath(f_Qs(l_oR.m_strFile))));
        l_pFile->setData(Qt::UserRole, f_Qs(l_oR.m_strFile));
        l_pFile->setToolTip(QDir::toNativeSeparators(f_Qs(l_oR.m_strFile)));
        if (l_oR.m_eStatus == atlas::ReferenceImage::Same) l_pFile->setForeground(l_oColor);
        m_pTable->setItem(l_iRow, FileCol, l_pFile);
    }
    QApplication::restoreOverrideCursor();
    f_UpdateCount();
}

void ReferencesDialog::f_CheckAll(bool l_bNewOnes, bool l_bChanged)
{
    const QSignalBlocker l_oBlock(m_pTable);
    for (int l_iRow = 0; l_iRow < m_pTable->rowCount(); l_iRow++) {
        QTableWidgetItem* l_pIt = m_pTable->item(l_iRow, StatusCol);
        const int l_iS = l_pIt->data(Qt::UserRole).toInt();
        if (l_iS == atlas::ReferenceImage::Same) continue;
        const bool l_bOn = l_iS == atlas::ReferenceImage::New ? l_bNewOnes : l_bChanged;
        l_pIt->setCheckState(l_bOn ? Qt::Checked : Qt::Unchecked);
    }
    f_UpdateCount();
}

void ReferencesDialog::f_UpdateCount()
{
    int l_arrCounts[3] = {0, 0, 0}, l_iChecked = 0;
    for (int l_iRow = 0; l_iRow < m_pTable->rowCount(); l_iRow++) {
        QTableWidgetItem* l_pIt = m_pTable->item(l_iRow, StatusCol);
        if (!l_pIt) continue;
        l_arrCounts[std::clamp(l_pIt->data(Qt::UserRole).toInt(), 0, 2)]++;
        l_iChecked += l_pIt->checkState() == Qt::Checked;
    }
    m_pSummary->setText(tr("%1 new · %2 changed · %3 same").arg(l_arrCounts[0]).arg(l_arrCounts[1]).arg(l_arrCounts[2]));
    m_pImport->setText(l_iChecked ? tr("Import %1").arg(l_iChecked) : tr("Import"));
    m_pImport->setEnabled(l_iChecked > 0);
}

QList<ImageFile> ReferencesDialog::f_Chosen() const
{
    QList<ImageFile> l_oOut;
    for (int l_iRow = 0; l_iRow < m_pTable->rowCount(); l_iRow++)
        if (m_pTable->item(l_iRow, StatusCol)->checkState() == Qt::Checked)
            l_oOut.push_back({m_pTable->item(l_iRow, NameCol)->text(), m_pTable->item(l_iRow, FileCol)->data(Qt::UserRole).toString()});
    return l_oOut;
}

#include "GridSliceDialog.h"

#include "Theme.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QSpinBox* f_Spin(int l_iLo, int l_iHi, int l_iValue, QWidget* l_pParent)
{
    auto* l_pS = new QSpinBox(l_pParent);
    l_pS->setRange(l_iLo, l_iHi);
    l_pS->setValue(l_iValue);
    return l_pS;
}

bool f_IsTransparent(const QImage& l_oImg, const QRect& l_oR)
{
    const QRect l_oArea = l_oR.intersected(l_oImg.rect());
    for (int l_iY = l_oArea.top(); l_iY <= l_oArea.bottom(); l_iY++) {
        const QRgb* l_pLine = reinterpret_cast<const QRgb*>(l_oImg.constScanLine(l_iY));
        for (int l_iX = l_oArea.left(); l_iX <= l_oArea.right(); l_iX++)
            if (qAlpha(l_pLine[l_iX]) > 0) return false;
    }
    return true;
}

}  // namespace

GridSliceDialog::GridSliceDialog(const QImage& l_oImage, const QString& l_oParentName, std::function<bool(const QString&)> l_fnExists,
                                 QWidget* l_pParent)
    : QDialog(l_pParent)
    , m_oImage(l_oImage.convertToFormat(QImage::Format_ARGB32_Premultiplied))
    , m_fnExists(std::move(l_fnExists))
{
    setWindowTitle(tr("Slice into Grid — %1").arg(l_oParentName));
    const int l_iW = std::max(1, m_oImage.width()), l_iH = std::max(1, m_oImage.height());

    m_pByCount = new QRadioButton(tr("Rows × columns"), this);
    auto* l_pBySize = new QRadioButton(tr("Cell size"), this);
    auto* l_pGroup = new QButtonGroup(this);
    l_pGroup->addButton(m_pByCount);
    l_pGroup->addButton(l_pBySize);
    m_pByCount->setChecked(true);
    m_pRows = f_Spin(1, l_iH, 1, this);
    m_pCols = f_Spin(1, l_iW, 4, this);
    m_pCellW = f_Spin(1, l_iW, std::max(1, l_iW / 4), this);
    m_pCellH = f_Spin(1, l_iH, l_iH, this);
    m_pMargin = f_Spin(0, std::min(l_iW, l_iH) / 2, 0, this);
    m_pSpacing = f_Spin(0, std::max(l_iW, l_iH), 0, this);
    m_pPrefix = new QLineEdit(l_oParentName + QLatin1Char('_'), this);
    m_pStart = f_Spin(0, 100000, 0, this);
    m_pSkipEmpty = new QCheckBox(tr("Skip fully transparent cells"), this);
    m_pSkipEmpty->setChecked(true);

    auto l_fnPair = [this](QWidget* l_pA, const QString& l_oSep, QWidget* l_pB) {
        auto* l_pRow = new QWidget(this);
        auto* l_pL = new QHBoxLayout(l_pRow);
        l_pL->setContentsMargins(0, 0, 0, 0);
        l_pL->addWidget(l_pA, 1);
        l_pL->addWidget(new QLabel(l_oSep, l_pRow));
        l_pL->addWidget(l_pB, 1);
        return l_pRow;
    };
    auto* l_pForm = new QFormLayout;
    l_pForm->addRow(m_pByCount, l_fnPair(m_pRows, tr("rows ×"), m_pCols));
    l_pForm->addRow(l_pBySize, l_fnPair(m_pCellW, QStringLiteral("×"), m_pCellH));
    l_pForm->addRow(tr("Margin"), m_pMargin);
    l_pForm->addRow(tr("Spacing"), m_pSpacing);
    l_pForm->addRow(tr("Name prefix"), l_fnPair(m_pPrefix, tr("first index"), m_pStart));
    l_pForm->addRow(QString(), m_pSkipEmpty);

    m_pPreview = new QLabel(this);
    m_pPreview->setAlignment(Qt::AlignCenter);
    m_pPreview->setMinimumSize(280, 200);
    m_pSummary = new QLabel(this);
    m_pSummary->setWordWrap(true);
    m_pButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_pButtons->button(QDialogButtonBox::Ok)->setText(tr("Create"));

    auto* l_pTop = new QHBoxLayout;
    l_pTop->addLayout(l_pForm);
    l_pTop->addWidget(m_pPreview, 1);
    auto* l_pLayout = new QVBoxLayout(this);
    l_pLayout->addLayout(l_pTop, 1);
    l_pLayout->addWidget(m_pSummary);
    l_pLayout->addWidget(m_pButtons);

    connect(m_pButtons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_pButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    for (QSpinBox* l_pS : {m_pRows, m_pCols, m_pCellW, m_pCellH, m_pMargin, m_pSpacing, m_pStart})
        connect(l_pS, &QSpinBox::valueChanged, this, &GridSliceDialog::f_Recompute);
    connect(m_pByCount, &QRadioButton::toggled, this, &GridSliceDialog::f_Recompute);
    connect(m_pPrefix, &QLineEdit::textChanged, this, &GridSliceDialog::f_Recompute);
    connect(m_pSkipEmpty, &QCheckBox::toggled, this, &GridSliceDialog::f_Recompute);
    f_Recompute();
}

void GridSliceDialog::f_Recompute()
{
    const bool l_bByCount = m_pByCount->isChecked();
    m_pRows->setEnabled(l_bByCount);
    m_pCols->setEnabled(l_bByCount);
    m_pCellW->setEnabled(!l_bByCount);
    m_pCellH->setEnabled(!l_bByCount);

    const int l_iW = m_oImage.width(), l_iH = m_oImage.height();
    const int l_iMargin = m_pMargin->value(), l_iSpacing = m_pSpacing->value();
    const int l_iInnerW = l_iW - 2 * l_iMargin, l_iInnerH = l_iH - 2 * l_iMargin;
    int l_iRows, l_iCols, l_iCw, l_iCh;
    if (l_bByCount) {
        l_iRows = m_pRows->value();
        l_iCols = m_pCols->value();
        l_iCw = (l_iInnerW - (l_iCols - 1) * l_iSpacing) / l_iCols;
        l_iCh = (l_iInnerH - (l_iRows - 1) * l_iSpacing) / l_iRows;
    } else {
        l_iCw = m_pCellW->value();
        l_iCh = m_pCellH->value();
        l_iCols = (l_iInnerW + l_iSpacing) / (l_iCw + l_iSpacing);
        l_iRows = (l_iInnerH + l_iSpacing) / (l_iCh + l_iSpacing);
    }

    m_vCells.clear();
    int l_iSkipped = 0, l_iTaken = 0;
    if (l_iCw > 0 && l_iCh > 0 && l_iRows > 0 && l_iCols > 0) {
        int l_iIndex = m_pStart->value();
        for (int l_iR = 0; l_iR < l_iRows; l_iR++)
            for (int l_iC = 0; l_iC < l_iCols; l_iC++) {
                const QRect l_oCell(l_iMargin + l_iC * (l_iCw + l_iSpacing), l_iMargin + l_iR * (l_iCh + l_iSpacing), l_iCw, l_iCh);
                if (m_pSkipEmpty->isChecked() && f_IsTransparent(m_oImage, l_oCell)) {
                    l_iSkipped++;
                    continue;
                }
                const QString l_oName = m_pPrefix->text().trimmed() + QString::number(l_iIndex++);
                if (m_fnExists && m_fnExists(l_oName)) l_iTaken++;
                m_vCells.push_back({l_oName, atlas::IRect{l_oCell.x(), l_oCell.y(), l_oCell.width(), l_oCell.height()}});
            }
    }

    // Preview: the image with the cells that will be created.
    const QSize l_oBox(280, 200);
    const double l_dScale = std::min({double(l_oBox.width()) / std::max(1, l_iW), double(l_oBox.height()) / std::max(1, l_iH), 8.0});
    QPixmap l_oPm(QSize(std::max(1, int(l_iW * l_dScale)), std::max(1, int(l_iH * l_dScale))));
    l_oPm.fill(Theme::f_Colors().m_oCheckerDark);
    {
        QPainter l_oP(&l_oPm);
        l_oP.drawImage(QRectF(0, 0, l_oPm.width(), l_oPm.height()), m_oImage);
        l_oP.setPen(QPen(Theme::f_Colors().m_oChildOutline, 1));
        for (const auto& l_pairC : m_vCells)
            l_oP.drawRect(QRectF(l_pairC.second.m_iX * l_dScale, l_pairC.second.m_iY * l_dScale, l_pairC.second.m_iW * l_dScale - 1, l_pairC.second.m_iH * l_dScale - 1));
    }
    m_pPreview->setPixmap(l_oPm);

    QString l_oSummary = tr("%n child sprite(s)", nullptr, int(m_vCells.size()));
    if (!m_vCells.empty()) l_oSummary += tr(" of %1 × %2 px: %3 … %4").arg(l_iCw).arg(l_iCh).arg(m_vCells.front().first, m_vCells.back().first);
    if (l_iSkipped) l_oSummary += QStringLiteral("  ") + tr("(%n empty cell(s) skipped)", nullptr, l_iSkipped);
    if (l_iTaken) l_oSummary += QStringLiteral("\n") + tr("%n name(s) already exist — change the prefix or first index.", nullptr, l_iTaken);
    m_pSummary->setText(l_oSummary);
    m_pButtons->button(QDialogButtonBox::Ok)->setEnabled(!m_vCells.empty() && l_iTaken == 0 && !m_pPrefix->text().trimmed().isEmpty());
}

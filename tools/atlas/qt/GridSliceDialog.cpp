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

QSpinBox* spin(int lo, int hi, int value, QWidget* parent)
{
    auto* s = new QSpinBox(parent);
    s->setRange(lo, hi);
    s->setValue(value);
    return s;
}

bool isTransparent(const QImage& img, const QRect& r)
{
    const QRect area = r.intersected(img.rect());
    for (int y = area.top(); y <= area.bottom(); y++) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = area.left(); x <= area.right(); x++)
            if (qAlpha(line[x]) > 0) return false;
    }
    return true;
}

}  // namespace

GridSliceDialog::GridSliceDialog(const QImage& image, const QString& parentName, std::function<bool(const QString&)> exists,
                                 QWidget* parent)
    : QDialog(parent)
    , m_image(image.convertToFormat(QImage::Format_ARGB32_Premultiplied))
    , m_exists(std::move(exists))
{
    setWindowTitle(tr("Slice into Grid — %1").arg(parentName));
    const int w = std::max(1, m_image.width()), h = std::max(1, m_image.height());

    m_byCount = new QRadioButton(tr("Rows × columns"), this);
    auto* bySize = new QRadioButton(tr("Cell size"), this);
    auto* group = new QButtonGroup(this);
    group->addButton(m_byCount);
    group->addButton(bySize);
    m_byCount->setChecked(true);
    m_rows = spin(1, h, 1, this);
    m_cols = spin(1, w, 4, this);
    m_cellW = spin(1, w, std::max(1, w / 4), this);
    m_cellH = spin(1, h, h, this);
    m_margin = spin(0, std::min(w, h) / 2, 0, this);
    m_spacing = spin(0, std::max(w, h), 0, this);
    m_prefix = new QLineEdit(parentName + QLatin1Char('_'), this);
    m_start = spin(0, 100000, 0, this);
    m_skipEmpty = new QCheckBox(tr("Skip fully transparent cells"), this);
    m_skipEmpty->setChecked(true);

    auto pair = [this](QWidget* a, const QString& sep, QWidget* b) {
        auto* row = new QWidget(this);
        auto* l = new QHBoxLayout(row);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(a, 1);
        l->addWidget(new QLabel(sep, row));
        l->addWidget(b, 1);
        return row;
    };
    auto* form = new QFormLayout;
    form->addRow(m_byCount, pair(m_rows, tr("rows ×"), m_cols));
    form->addRow(bySize, pair(m_cellW, QStringLiteral("×"), m_cellH));
    form->addRow(tr("Margin"), m_margin);
    form->addRow(tr("Spacing"), m_spacing);
    form->addRow(tr("Name prefix"), pair(m_prefix, tr("first index"), m_start));
    form->addRow(QString(), m_skipEmpty);

    m_preview = new QLabel(this);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumSize(280, 200);
    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Create"));

    auto* top = new QHBoxLayout;
    top->addLayout(form);
    top->addWidget(m_preview, 1);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(top, 1);
    layout->addWidget(m_summary);
    layout->addWidget(m_buttons);

    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    for (QSpinBox* s : {m_rows, m_cols, m_cellW, m_cellH, m_margin, m_spacing, m_start})
        connect(s, &QSpinBox::valueChanged, this, &GridSliceDialog::recompute);
    connect(m_byCount, &QRadioButton::toggled, this, &GridSliceDialog::recompute);
    connect(m_prefix, &QLineEdit::textChanged, this, &GridSliceDialog::recompute);
    connect(m_skipEmpty, &QCheckBox::toggled, this, &GridSliceDialog::recompute);
    recompute();
}

void GridSliceDialog::recompute()
{
    const bool byCount = m_byCount->isChecked();
    m_rows->setEnabled(byCount);
    m_cols->setEnabled(byCount);
    m_cellW->setEnabled(!byCount);
    m_cellH->setEnabled(!byCount);

    const int W = m_image.width(), H = m_image.height();
    const int margin = m_margin->value(), spacing = m_spacing->value();
    const int innerW = W - 2 * margin, innerH = H - 2 * margin;
    int rows, cols, cw, ch;
    if (byCount) {
        rows = m_rows->value();
        cols = m_cols->value();
        cw = (innerW - (cols - 1) * spacing) / cols;
        ch = (innerH - (rows - 1) * spacing) / rows;
    } else {
        cw = m_cellW->value();
        ch = m_cellH->value();
        cols = (innerW + spacing) / (cw + spacing);
        rows = (innerH + spacing) / (ch + spacing);
    }

    m_cells.clear();
    int skipped = 0, taken = 0;
    if (cw > 0 && ch > 0 && rows > 0 && cols > 0) {
        int index = m_start->value();
        for (int r = 0; r < rows; r++)
            for (int c = 0; c < cols; c++) {
                const QRect cell(margin + c * (cw + spacing), margin + r * (ch + spacing), cw, ch);
                if (m_skipEmpty->isChecked() && isTransparent(m_image, cell)) {
                    skipped++;
                    continue;
                }
                const QString name = m_prefix->text().trimmed() + QString::number(index++);
                if (m_exists && m_exists(name)) taken++;
                m_cells.push_back({name, atlas::IRect{cell.x(), cell.y(), cell.width(), cell.height()}});
            }
    }

    // Preview: the image with the cells that will be created.
    const QSize box(280, 200);
    const double scale = std::min({double(box.width()) / std::max(1, W), double(box.height()) / std::max(1, H), 8.0});
    QPixmap pm(QSize(std::max(1, int(W * scale)), std::max(1, int(H * scale))));
    pm.fill(Theme::colors().checkerDark);
    {
        QPainter p(&pm);
        p.drawImage(QRectF(0, 0, pm.width(), pm.height()), m_image);
        p.setPen(QPen(Theme::colors().childOutline, 1));
        for (const auto& c : m_cells)
            p.drawRect(QRectF(c.second.x * scale, c.second.y * scale, c.second.w * scale - 1, c.second.h * scale - 1));
    }
    m_preview->setPixmap(pm);

    QString summary = tr("%n child sprite(s)", nullptr, int(m_cells.size()));
    if (!m_cells.empty()) summary += tr(" of %1 × %2 px: %3 … %4").arg(cw).arg(ch).arg(m_cells.front().first, m_cells.back().first);
    if (skipped) summary += QStringLiteral("  ") + tr("(%n empty cell(s) skipped)", nullptr, skipped);
    if (taken) summary += QStringLiteral("\n") + tr("%n name(s) already exist — change the prefix or first index.", nullptr, taken);
    m_summary->setText(summary);
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(!m_cells.empty() && taken == 0 && !m_prefix->text().trimmed().isEmpty());
}

#pragma once

#include "atlas_image.h"

#include <QDialog>
#include <QImage>

#include <functional>
#include <utility>
#include <vector>

class QCheckBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QRadioButton;
class QSpinBox;

// Slice a sprite into a grid of child sprites (what the old PI editor's "Divide" did, but the
// result is child rects, not new images). Rows x columns or a cell size, with an outer margin,
// spacing between cells and a name prefix; cells that are fully transparent can be skipped.
class GridSliceDialog : public QDialog
{
    Q_OBJECT

public:
    // exists: whether a sprite name is taken (the dialog refuses to create duplicates).
    GridSliceDialog(const QImage& image, const QString& parentName, std::function<bool(const QString&)> exists,
                    QWidget* parent = nullptr);

    // Names and rects (in the parent's pixels) of the cells to create, row by row.
    std::vector<std::pair<QString, atlas::IRect>> cells() const { return m_cells; }

private:
    void recompute();

    QImage m_image;
    std::function<bool(const QString&)> m_exists;
    std::vector<std::pair<QString, atlas::IRect>> m_cells;

    QRadioButton* m_byCount;
    QSpinBox *m_rows, *m_cols, *m_cellW, *m_cellH, *m_margin, *m_spacing, *m_start;
    QLineEdit* m_prefix;
    QCheckBox* m_skipEmpty;
    QLabel* m_preview;
    QLabel* m_summary;
    QDialogButtonBox* m_buttons;
};

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
    GridSliceDialog(const QImage& l_oImage, const QString& l_oParentName, std::function<bool(const QString&)> l_fnExists,
                    QWidget* l_pParent = nullptr);

    // Names and rects (in the parent's pixels) of the cells to create, row by row.
    std::vector<std::pair<QString, atlas::IRect>> f_Cells() const { return m_vCells; }

private:
    void f_Recompute();

    QImage m_oImage;
    std::function<bool(const QString&)> m_fnExists;
    std::vector<std::pair<QString, atlas::IRect>> m_vCells;

    QRadioButton* m_pByCount;
    QSpinBox *m_pRows, *m_pCols, *m_pCellW, *m_pCellH, *m_pMargin, *m_pSpacing, *m_pStart;
    QLineEdit* m_pPrefix;
    QCheckBox* m_pSkipEmpty;
    QLabel* m_pPreview;
    QLabel* m_pSummary;
    QDialogButtonBox* m_pButtons;
};

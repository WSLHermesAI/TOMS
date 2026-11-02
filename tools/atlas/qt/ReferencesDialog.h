#pragma once

#include "AtlasDocument.h"

#include <QDialog>

class QLabel;
class QPushButton;
class QTableWidget;

// Sprite > Import From References: the PNGs in the reference folders (the shown variant's
// reference folder when the canvas shows a variant) compared with what the project holds --
// New, Changed or Same. New and Changed rows start checked; chosen() is what to import.
class ReferencesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ReferencesDialog(AtlasDocument* l_pDoc, QWidget* l_pParent = nullptr);

    QString f_Variant() const { return m_oVariant; }
    QList<ImageFile> f_Chosen() const;

private:
    void f_Scan();
    void f_UpdateCount();
    void f_CheckAll(bool l_bNewOnes, bool l_bChanged);

    AtlasDocument* m_pDoc;
    QString m_oVariant;
    QLabel* m_pHeader;
    QLabel* m_pSummary;
    QTableWidget* m_pTable;
    QPushButton* m_pImport;
};

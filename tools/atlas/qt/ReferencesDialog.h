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
    explicit ReferencesDialog(AtlasDocument* doc, QWidget* parent = nullptr);

    QString variant() const { return m_variant; }
    QList<ImageFile> chosen() const;

private:
    void scan();
    void updateCount();
    void checkAll(bool newOnes, bool changed);

    AtlasDocument* m_doc;
    QString m_variant;
    QLabel* m_header;
    QLabel* m_summary;
    QTableWidget* m_table;
    QPushButton* m_import;
};

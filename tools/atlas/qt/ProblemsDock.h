#pragma once

#include <QDockWidget>

class AtlasDocument;
class QPlainTextEdit;
class QTableWidget;
class QTabWidget;

// Bottom dock: the last build's diagnostics (double-click selects the sprite) and a log of what
// the editor did (opened, saved, exported, files changed on disk).
class ProblemsDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit ProblemsDock(AtlasDocument* l_pDoc, QWidget* l_pParent = nullptr);

public slots:
    void f_AppendLog(const QString& l_oText);

private:
    void f_Refresh();

    AtlasDocument* m_pDoc;
    QTabWidget* m_pTabs;
    QTableWidget* m_pTable;
    QPlainTextEdit* m_pLog;
};

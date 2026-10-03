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
    explicit ProblemsDock(AtlasDocument* doc, QWidget* parent = nullptr);

public slots:
    void appendLog(const QString& text);

private:
    void refresh();

    AtlasDocument* m_doc;
    QTabWidget* m_tabs;
    QTableWidget* m_table;
    QPlainTextEdit* m_log;
};

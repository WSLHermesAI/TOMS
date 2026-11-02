#pragma once

#include "AnimDocument.h"
#include "anim_check.h"

#include <QDockWidget>

class QPlainTextEdit;
class QTableWidget;
class QTabWidget;

// Problems (anim_check.h: missing sprites, overlapping key times, clip names, the atlas) -- the
// same list `anim_editor --headless check` prints; double-click selects the node -- and a log of
// what the editor did (opened, saved, events fired during playback).
class AnimProblemsDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit AnimProblemsDock(AnimDocument* l_pDoc, QWidget* l_pParent = nullptr);
    const std::vector<animed::Problem>& f_Problems() const { return m_vProblems; }
    QString f_LogText() const;

public slots:
    void f_AppendLog(const QString& l_oText);
    void f_Refresh();

private:
    AnimDocument* m_pDoc;
    QTabWidget* m_pTabs;
    QTableWidget* m_pTable;
    QPlainTextEdit* m_pLog;
    std::vector<animed::Problem> m_vProblems;
};

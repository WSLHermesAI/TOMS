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
    explicit AnimProblemsDock(AnimDocument* doc, QWidget* parent = nullptr);
    const std::vector<animed::Problem>& problems() const { return m_problems; }
    QString logText() const;

public slots:
    void appendLog(const QString& text);
    void refresh();

private:
    AnimDocument* m_doc;
    QTabWidget* m_tabs;
    QTableWidget* m_table;
    QPlainTextEdit* m_log;
    std::vector<animed::Problem> m_problems;
};

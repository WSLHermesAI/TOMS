#include "AnimProblemsDock.h"

#include "Icons.h"

#include <QHeaderView>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QTabWidget>
#include <QTime>

using animed::Problem;

AnimProblemsDock::AnimProblemsDock(AnimDocument* doc, QWidget* parent)
    : QDockWidget(tr("Problems"), parent)
    , m_doc(doc)
    , m_tabs(new QTabWidget(this))
    , m_table(new QTableWidget(0, 3, m_tabs))
    , m_log(new QPlainTextEdit(m_tabs))
{
    setObjectName(QStringLiteral("AnimProblemsDock"));
    m_table->setHorizontalHeaderLabels({tr("Level"), tr("Where"), tr("Message")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(1, 220);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(22);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(2000);
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_tabs->setDocumentMode(true);
    m_tabs->addTab(m_table, tr("Problems"));
    m_tabs->addTab(m_log, tr("Log"));
    setWidget(m_tabs);

    connect(doc, &AnimDocument::fileChanged, this, &AnimProblemsDock::refresh);
    connect(doc, &AnimDocument::atlasChanged, this, &AnimProblemsDock::refresh);
    connect(doc, &AnimDocument::message, this, &AnimProblemsDock::appendLog);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row) {
        if (row < 0 || row >= int(m_problems.size())) return;
        const Problem& p = m_problems[size_t(row)];
        if (p.clip < 0) return;
        m_doc->setClipIndex(p.clip);
        m_doc->selectNode(p.path);
    });
    refresh();
}

void AnimProblemsDock::appendLog(const QString& text)
{
    m_log->appendPlainText(QStringLiteral("[%1] %2").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), text));
}

QString AnimProblemsDock::logText() const { return m_log->toPlainText(); }

void AnimProblemsDock::refresh()
{
    m_problems = animed::checkAnim(m_doc->file(), m_doc->checkAtlases());
    m_table->setRowCount(int(m_problems.size()));
    for (size_t i = 0; i < m_problems.size(); i++) {
        const Problem& p = m_problems[i];
        const Icons::Id icon = p.level == Problem::Error ? Icons::Id::Error : p.level == Problem::Warning ? Icons::Id::Warning : Icons::Id::Info;
        m_table->setItem(int(i), 0, new QTableWidgetItem(Icons::icon(icon), QString::fromLatin1(animed::levelName(p.level))));
        m_table->setItem(int(i), 1, new QTableWidgetItem(QString::fromStdString(p.where)));
        auto* msg = new QTableWidgetItem(QString::fromStdString(p.message));
        msg->setToolTip(msg->text());
        m_table->setItem(int(i), 2, msg);
    }
    const int errors = animed::countLevel(m_problems, Problem::Error), warnings = animed::countLevel(m_problems, Problem::Warning);
    setWindowTitle(errors + warnings ? tr("Problems (%1)").arg(errors + warnings) : tr("Problems"));
    m_tabs->setTabText(0, m_problems.empty() ? tr("Problems") : tr("Problems (%1)").arg(m_problems.size()));
    m_tabs->setTabIcon(0, errors ? Icons::icon(Icons::Id::Error) : warnings ? Icons::icon(Icons::Id::Warning) : QIcon());
}

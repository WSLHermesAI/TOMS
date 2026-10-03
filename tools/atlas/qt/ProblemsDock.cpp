#include "ProblemsDock.h"

#include "AtlasDocument.h"
#include "Icons.h"

#include <QHeaderView>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QTabWidget>
#include <QTime>

ProblemsDock::ProblemsDock(AtlasDocument* doc, QWidget* parent)
    : QDockWidget(tr("Problems"), parent)
    , m_doc(doc)
    , m_tabs(new QTabWidget(this))
    , m_table(new QTableWidget(0, 3, m_tabs))
    , m_log(new QPlainTextEdit(m_tabs))
{
    setObjectName(QStringLiteral("ProblemsDock"));
    m_table->setHorizontalHeaderLabels({tr("Level"), tr("Sprite"), tr("Message")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(1, 180);
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
    m_tabs->addTab(m_table, tr("Diagnostics"));
    m_tabs->addTab(m_log, tr("Log"));
    setWidget(m_tabs);

    connect(doc, &AtlasDocument::buildFinished, this, &ProblemsDock::refresh);
    connect(doc, &AtlasDocument::projectReset, this, &ProblemsDock::refresh);
    connect(doc, &AtlasDocument::message, this, &ProblemsDock::appendLog);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row) {
        const QString sprite = m_table->item(row, 1)->text();
        if (m_doc->spriteExists(sprite)) m_doc->setSelection({sprite});
    });
}

void ProblemsDock::appendLog(const QString& text)
{
    m_log->appendPlainText(QStringLiteral("[%1] %2").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), text));
}

void ProblemsDock::refresh()
{
    const BuildSnapshotPtr snap = m_doc->snapshot();
    const std::vector<atlas::Diagnostic> none;
    const std::vector<atlas::Diagnostic>& diags = snap ? snap->result.diagnostics : none;
    m_table->setRowCount(int(diags.size()));
    int errors = 0, warnings = 0;
    for (size_t i = 0; i < diags.size(); i++) {
        const atlas::Diagnostic& d = diags[i];
        errors += d.level == atlas::Diagnostic::Error;
        warnings += d.level == atlas::Diagnostic::Warning;
        const Icons::Id icon = d.level == atlas::Diagnostic::Error ? Icons::Id::Error
                             : d.level == atlas::Diagnostic::Warning ? Icons::Id::Warning : Icons::Id::Info;
        const QString level = d.level == atlas::Diagnostic::Error ? tr("error")
                            : d.level == atlas::Diagnostic::Warning ? tr("warning") : tr("info");
        m_table->setItem(int(i), 0, new QTableWidgetItem(Icons::icon(icon), level));
        m_table->setItem(int(i), 1, new QTableWidgetItem(QString::fromStdString(d.sprite)));
        auto* msg = new QTableWidgetItem(QString::fromStdString(d.message));
        msg->setToolTip(msg->text());
        m_table->setItem(int(i), 2, msg);
    }
    setWindowTitle(errors + warnings ? tr("Problems (%1)").arg(errors + warnings) : tr("Problems"));
    m_tabs->setTabText(0, errors + warnings ? tr("Diagnostics (%1)").arg(errors + warnings) : tr("Diagnostics"));
    m_tabs->setTabIcon(0, errors ? Icons::icon(Icons::Id::Error) : warnings ? Icons::icon(Icons::Id::Warning) : QIcon());
}

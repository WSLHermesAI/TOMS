#include "ProblemsDock.h"

#include "AtlasDocument.h"
#include "Icons.h"

#include <QHeaderView>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QTabWidget>
#include <QTime>

ProblemsDock::ProblemsDock(AtlasDocument* l_pDoc, QWidget* l_pParent)
    : QDockWidget(tr("Problems"), l_pParent)
    , m_pDoc(l_pDoc)
    , m_pTabs(new QTabWidget(this))
    , m_pTable(new QTableWidget(0, 3, m_pTabs))
    , m_pLog(new QPlainTextEdit(m_pTabs))
{
    setObjectName(QStringLiteral("ProblemsDock"));
    m_pTable->setHorizontalHeaderLabels({tr("Level"), tr("Sprite"), tr("Message")});
    m_pTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_pTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_pTable->horizontalHeader()->setStretchLastSection(true);
    m_pTable->setColumnWidth(1, 180);
    m_pTable->verticalHeader()->hide();
    m_pTable->verticalHeader()->setDefaultSectionSize(22);
    m_pTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pTable->setShowGrid(false);
    m_pTable->setAlternatingRowColors(true);

    m_pLog->setReadOnly(true);
    m_pLog->setMaximumBlockCount(2000);
    m_pLog->setLineWrapMode(QPlainTextEdit::NoWrap);

    m_pTabs->setDocumentMode(true);
    m_pTabs->addTab(m_pTable, tr("Diagnostics"));
    m_pTabs->addTab(m_pLog, tr("Log"));
    setWidget(m_pTabs);

    connect(l_pDoc, &AtlasDocument::f_BuildFinished, this, &ProblemsDock::f_Refresh);
    connect(l_pDoc, &AtlasDocument::f_ProjectReset, this, &ProblemsDock::f_Refresh);
    connect(l_pDoc, &AtlasDocument::f_Message, this, &ProblemsDock::f_AppendLog);
    connect(m_pTable, &QTableWidget::cellDoubleClicked, this, [this](int l_iRow) {
        const QString l_oSprite = m_pTable->item(l_iRow, 1)->text();
        if (m_pDoc->f_SpriteExists(l_oSprite)) m_pDoc->f_SetSelection({l_oSprite});
    });
}

void ProblemsDock::f_AppendLog(const QString& l_oText)
{
    m_pLog->appendPlainText(QStringLiteral("[%1] %2").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), l_oText));
}

void ProblemsDock::f_Refresh()
{
    const BuildSnapshotPtr l_spSnap = m_pDoc->f_Snapshot();
    const std::vector<atlas::Diagnostic> l_vNone;
    const std::vector<atlas::Diagnostic>& l_vDiags = l_spSnap ? l_spSnap->m_oResult.m_vDiagnostics : l_vNone;
    m_pTable->setRowCount(int(l_vDiags.size()));
    int l_iErrors = 0, l_iWarnings = 0;
    for (size_t l_nI = 0; l_nI < l_vDiags.size(); l_nI++) {
        const atlas::Diagnostic& l_oD = l_vDiags[l_nI];
        l_iErrors += l_oD.m_eLevel == atlas::Diagnostic::Error;
        l_iWarnings += l_oD.m_eLevel == atlas::Diagnostic::Warning;
        const Icons::Id l_eIcon = l_oD.m_eLevel == atlas::Diagnostic::Error ? Icons::Id::Error
                             : l_oD.m_eLevel == atlas::Diagnostic::Warning ? Icons::Id::Warning : Icons::Id::Info;
        const QString l_oLevel = l_oD.m_eLevel == atlas::Diagnostic::Error ? tr("error")
                            : l_oD.m_eLevel == atlas::Diagnostic::Warning ? tr("warning") : tr("info");
        m_pTable->setItem(int(l_nI), 0, new QTableWidgetItem(Icons::f_Icon(l_eIcon), l_oLevel));
        m_pTable->setItem(int(l_nI), 1, new QTableWidgetItem(QString::fromStdString(l_oD.m_strSprite)));
        auto* l_pMsg = new QTableWidgetItem(QString::fromStdString(l_oD.m_strMessage));
        l_pMsg->setToolTip(l_pMsg->text());
        m_pTable->setItem(int(l_nI), 2, l_pMsg);
    }
    setWindowTitle(l_iErrors + l_iWarnings ? tr("Problems (%1)").arg(l_iErrors + l_iWarnings) : tr("Problems"));
    m_pTabs->setTabText(0, l_iErrors + l_iWarnings ? tr("Diagnostics (%1)").arg(l_iErrors + l_iWarnings) : tr("Diagnostics"));
    m_pTabs->setTabIcon(0, l_iErrors ? Icons::f_Icon(Icons::Id::Error) : l_iWarnings ? Icons::f_Icon(Icons::Id::Warning) : QIcon());
}

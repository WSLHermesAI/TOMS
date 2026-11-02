#include "AnimProblemsDock.h"

#include "Icons.h"

#include <QHeaderView>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QTabWidget>
#include <QTime>

using animed::Problem;

AnimProblemsDock::AnimProblemsDock(AnimDocument* l_pDoc, QWidget* l_pParent)
    : QDockWidget(tr("Problems"), l_pParent)
    , m_pDoc(l_pDoc)
    , m_pTabs(new QTabWidget(this))
    , m_pTable(new QTableWidget(0, 3, m_pTabs))
    , m_pLog(new QPlainTextEdit(m_pTabs))
{
    setObjectName(QStringLiteral("AnimProblemsDock"));
    m_pTable->setHorizontalHeaderLabels({tr("Level"), tr("Where"), tr("Message")});
    m_pTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_pTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_pTable->horizontalHeader()->setStretchLastSection(true);
    m_pTable->setColumnWidth(1, 220);
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
    m_pTabs->addTab(m_pTable, tr("Problems"));
    m_pTabs->addTab(m_pLog, tr("Log"));
    setWidget(m_pTabs);

    connect(l_pDoc, &AnimDocument::f_FileChanged, this, &AnimProblemsDock::f_Refresh);
    connect(l_pDoc, &AnimDocument::f_AtlasChanged, this, &AnimProblemsDock::f_Refresh);
    connect(l_pDoc, &AnimDocument::f_Message, this, &AnimProblemsDock::f_AppendLog);
    connect(m_pTable, &QTableWidget::cellDoubleClicked, this, [this](int l_iRow) {
        if (l_iRow < 0 || l_iRow >= int(m_vProblems.size())) return;
        const Problem& l_oP = m_vProblems[size_t(l_iRow)];
        if (l_oP.m_iClip < 0) return;
        m_pDoc->f_SetClipIndex(l_oP.m_iClip);
        m_pDoc->f_SelectNode(l_oP.m_vPath);
    });
    f_Refresh();
}

void AnimProblemsDock::f_AppendLog(const QString& l_oText)
{
    m_pLog->appendPlainText(QStringLiteral("[%1] %2").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), l_oText));
}

QString AnimProblemsDock::f_LogText() const { return m_pLog->toPlainText(); }

void AnimProblemsDock::f_Refresh()
{
    m_vProblems = animed::f_CheckAnim(m_pDoc->f_File(), m_pDoc->f_CheckAtlases());
    m_pTable->setRowCount(int(m_vProblems.size()));
    for (size_t l_nI = 0; l_nI < m_vProblems.size(); l_nI++) {
        const Problem& l_oP = m_vProblems[l_nI];
        const Icons::Id l_eIcon = l_oP.m_eLevel == Problem::Error ? Icons::Id::Error : l_oP.m_eLevel == Problem::Warning ? Icons::Id::Warning : Icons::Id::Info;
        m_pTable->setItem(int(l_nI), 0, new QTableWidgetItem(Icons::f_Icon(l_eIcon), QString::fromLatin1(animed::f_LevelName(l_oP.m_eLevel))));
        m_pTable->setItem(int(l_nI), 1, new QTableWidgetItem(QString::fromStdString(l_oP.m_strWhere)));
        auto* l_pMsg = new QTableWidgetItem(QString::fromStdString(l_oP.m_strMessage));
        l_pMsg->setToolTip(l_pMsg->text());
        m_pTable->setItem(int(l_nI), 2, l_pMsg);
    }
    const int l_iErrors = animed::f_CountLevel(m_vProblems, Problem::Error), l_iWarnings = animed::f_CountLevel(m_vProblems, Problem::Warning);
    setWindowTitle(l_iErrors + l_iWarnings ? tr("Problems (%1)").arg(l_iErrors + l_iWarnings) : tr("Problems"));
    m_pTabs->setTabText(0, m_vProblems.empty() ? tr("Problems") : tr("Problems (%1)").arg(m_vProblems.size()));
    m_pTabs->setTabIcon(0, l_iErrors ? Icons::f_Icon(Icons::Id::Error) : l_iWarnings ? Icons::f_Icon(Icons::Id::Warning) : QIcon());
}

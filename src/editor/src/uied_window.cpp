// uied_window.cpp -- see uied_window.h.
#include "uied_window.h"

#include "Language.h"
#include "bgfx_viewport.h"
#include "game.h"
#include "rml_ui.h"
#include "uied_code_edit.h"
#include "uied_logic.h"

#include <RmlUi/Core.h>

#include <algorithm>
#include <QFont>

#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDesktopServices>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextDocumentLayout>
#include <QSaveFile>
#include <QSplitter>
#include <QStatusBar>
#include <QTextDocument>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

using toms::next::RmlUi;

UiEditorWindow::UiEditorWindow(QWidget* l_pParent) : QMainWindow(l_pParent) {
    setWindowTitle(tr("TOMS UI Editor"));
    resize(1600, 900);
    m_oPreviewTimer.setSingleShot(true);
    m_oPreviewTimer.setInterval(300);
    connect(&m_oPreviewTimer, &QTimer::timeout, this, &UiEditorWindow::f_PreviewNow);
    f_BuildUi();
    m_oUiDir = QDir(m_pView->f_AssetDir()).filePath(QStringLiteral("ui"));
    m_oScenariosDir = QDir::cleanPath(QDir(m_pView->f_AssetDir()).filePath(QStringLiteral("../../tests/scenarios")));
    m_oDocsDir = QDir::cleanPath(QDir(m_pView->f_AssetDir()).filePath(QStringLiteral("../../docs")));
    m_oTutorialDir = QDir(m_oDocsDir).filePath(QStringLiteral("examples/ui_tutorial"));
    m_oTutorialFiles = uied::f_UiFiles(m_oTutorialDir);
    m_oPracticeTimer.setInterval(200);
    connect(&m_oPracticeTimer, &QTimer::timeout, this, &UiEditorWindow::f_LoadPractice);
    f_RefreshFileList();
    f_FillPreviewList();
    m_pWatcher = new QFileSystemWatcher(this);
    for (const QString& l_oF : uied::f_UiFiles(m_oUiDir) + m_oTutorialFiles) m_pWatcher->addPath(f_PathOf(l_oF));
    connect(m_pWatcher, &QFileSystemWatcher::fileChanged, this, &UiEditorWindow::f_DiskChanged);
    // RmlUi's warnings and errors: the Problems list (queued: RmlUi may log in the middle of a reload).
    RmlUi::f_SetLogListener([this](int l_iLevel, const std::string& l_strMessage) {
        if (l_iLevel < 1) return;
        const QString l_oM = QString::fromStdString(l_strMessage);
        QMetaObject::invokeMethod(this, [this, l_iLevel, l_oM] { f_AddProblem(l_iLevel, l_oM); }, Qt::QueuedConnection);
    });
    // The languages are known once the game runs.
    m_oLangTimer.setInterval(500);
    connect(&m_oLangTimer, &QTimer::timeout, this, &UiEditorWindow::f_FillLanguages);
    m_oLangTimer.start();
    // The hint: what the text cursor is on glows in the game (updateHint), re-placed as things move.
    m_oHintTimer.setInterval(60);
    connect(&m_oHintTimer, &QTimer::timeout, this, &UiEditorWindow::f_UpdateHint);
    connect(m_pEdit, &QPlainTextEdit::cursorPositionChanged, this, &UiEditorWindow::f_CursorMoved);
    // What the preview shows: its documents and their style sheets, at the top of the file list.
    m_oRelatedTimer.setInterval(500);
    connect(&m_oRelatedTimer, &QTimer::timeout, this, &UiEditorWindow::f_UpdateRelated);
    m_oRelatedTimer.start();
    const QStringList l_oAll = uied::f_UiFiles(m_oUiDir);
    if (l_oAll.contains(QStringLiteral("battle.rml"))) f_OpenFile(QStringLiteral("battle.rml"));
    else if (!l_oAll.isEmpty()) f_OpenFile(l_oAll.first());
}

UiEditorWindow::~UiEditorWindow() {
    RmlUi::f_SetLogListener(nullptr);
    for (auto l_oIt = m_oBuffers.begin(); l_oIt != m_oBuffers.end(); ++l_oIt) RmlUi::f_ClearVirtualFile(l_oIt.key().toStdString());
}

void UiEditorWindow::f_BuildUi() {
    m_pFiles = new QListWidget;
    m_pEdit = new UiCodeEdit;
    m_pEdit->setPlaceholderText(tr("Pick a file on the left"));
    m_pView = new BgfxViewport;
    m_pView->setMinimumSize(512, 384);

    auto* l_pSplit = new QSplitter(Qt::Horizontal);
    l_pSplit->addWidget(m_pFiles);
    l_pSplit->addWidget(m_pEdit);
    l_pSplit->addWidget(m_pView);
    l_pSplit->setStretchFactor(0, 0);
    l_pSplit->setStretchFactor(1, 3);
    l_pSplit->setStretchFactor(2, 4);
    l_pSplit->setSizes({180, 620, 800});
    setCentralWidget(l_pSplit);

    m_pProblems = new QListWidget;
    auto* l_pProblemsDock = new QDockWidget(tr("Problems"), this);
    l_pProblemsDock->setObjectName(QStringLiteral("ProblemsDock"));
    l_pProblemsDock->setWidget(m_pProblems);
    addDockWidget(Qt::BottomDockWidgetArea, l_pProblemsDock);

    auto* l_pInspector = new QWidget;
    auto* l_pIl = new QVBoxLayout(l_pInspector);
    l_pIl->setContentsMargins(4, 4, 4, 4);
    m_pInspectLabel = new QLabel(tr("Inspect (Ctrl+I), then click an element in the game."));
    m_pInspectLabel->setWordWrap(true);
    m_pInspectLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_pRules = new QListWidget;
    l_pIl->addWidget(m_pInspectLabel);
    l_pIl->addWidget(m_pRules);
    auto* l_pInspectDock = new QDockWidget(tr("Inspector"), this);
    l_pInspectDock->setObjectName(QStringLiteral("InspectorDock"));
    l_pInspectDock->setWidget(l_pInspector);
    addDockWidget(Qt::BottomDockWidgetArea, l_pInspectDock);
    splitDockWidget(l_pProblemsDock, l_pInspectDock, Qt::Horizontal);

    // ---- actions ----
    auto* l_pSaveAct = new QAction(tr("&Save"), this);
    l_pSaveAct->setShortcut(QKeySequence::Save);
    connect(l_pSaveAct, &QAction::triggered, this, [this] { if (!m_oCurrent.isEmpty()) f_Save(m_oCurrent); });
    auto* l_pSaveAllAct = new QAction(tr("Save &All"), this);
    l_pSaveAllAct->setShortcut(QKeySequence(tr("Ctrl+Shift+S")));
    connect(l_pSaveAllAct, &QAction::triggered, this, &UiEditorWindow::f_SaveAll);
    auto* l_pRevertAct = new QAction(tr("&Revert File"), this);
    l_pRevertAct->setToolTip(tr("Drop this file's changes: back to the text on disk"));
    connect(l_pRevertAct, &QAction::triggered, this, [this] { if (!m_oCurrent.isEmpty()) f_Revert(m_oCurrent); });
    auto* l_pInspectAct = new QAction(tr("&Inspect"), this);
    l_pInspectAct->setCheckable(true);
    l_pInspectAct->setShortcut(QKeySequence(tr("Ctrl+I")));
    l_pInspectAct->setToolTip(tr("A click in the game picks the UI element under the mouse instead of pressing it (Ctrl+I)"));
    connect(l_pInspectAct, &QAction::toggled, this, [this](bool l_bOn) {
        m_pView->f_SetInspectMode(l_bOn);
        statusBar()->showMessage(l_bOn ? tr("Inspect: click an element in the game") : tr("Inspect off: clicks go to the game"), 4000);
    });
    connect(m_pView, &BgfxViewport::f_InspectClicked, this, &UiEditorWindow::f_InspectNow);
    auto* l_pDebuggerAct = new QAction(tr("RmlUi &Debugger"), this);
    l_pDebuggerAct->setShortcut(QKeySequence(Qt::Key_F8));
    l_pDebuggerAct->setToolTip(tr("RmlUi's own inspector over the game: the element tree, computed styles, layout boxes (F8)"));
    connect(l_pDebuggerAct, &QAction::triggered, this, [this] {
        if (RmlUi* l_pR = m_pView->f_Session().f_Rml()) l_pR->f_ToggleDebugger();
    });
    auto* l_pRestartAct = new QAction(tr("Restart &Preview"), this);
    l_pRestartAct->setShortcut(QKeySequence(Qt::Key_F5));
    l_pRestartAct->setToolTip(tr("Start the chosen preview again (F5)"));
    connect(l_pRestartAct, &QAction::triggered, this, [this] { f_StartPreview(m_pPreview->currentIndex()); });

    m_pPreview = new QComboBox;
    m_pPreview->setToolTip(tr("What the game shows: the title, or a test scenario (tests/scenarios: a battle, a dialogue, the store, ...)"));
    m_pPreview->setMinimumContentsLength(28);
    connect(m_pPreview, qOverload<int>(&QComboBox::activated), this, &UiEditorWindow::f_StartPreview);
    m_pLang = new QComboBox;
    m_pLang->setToolTip(tr("The game's text in this language (only here: the game's own setting is not changed)"));
    connect(m_pLang, qOverload<int>(&QComboBox::activated), this, [this](int l_iI) {
        if (Game* l_pG = m_pView->f_Session().f_Game()) l_pG->f_PreviewLanguage(m_pLang->itemData(l_iI).toString().toStdString());
    });

    QMenu* l_pFile = menuBar()->addMenu(tr("&File"));
    l_pFile->addAction(l_pSaveAct);
    l_pFile->addAction(l_pSaveAllAct);
    l_pFile->addAction(l_pRevertAct);
    l_pFile->addSeparator();
    l_pFile->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);
    QMenu* l_pViewMenu = menuBar()->addMenu(tr("&Preview"));
    l_pViewMenu->addAction(l_pRestartAct);
    l_pViewMenu->addAction(l_pInspectAct);
    auto* l_pHintAct = new QAction(tr("&Glow What the Cursor Is On"), this);
    l_pHintAct->setCheckable(true);
    l_pHintAct->setChecked(true);
    l_pHintAct->setShortcut(QKeySequence(tr("Ctrl+G")));
    l_pHintAct->setToolTip(tr("The element (in an .rml) or the rule's elements (in an .rcss) under the text cursor glow in the game (Ctrl+G)"));
    connect(l_pHintAct, &QAction::toggled, this, [this](bool l_bOn) {
        m_bHintOn = l_bOn;
        if (l_bOn) f_CursorMoved();
        else { f_DropHint(); m_oHintTimer.stop(); }
    });
    l_pViewMenu->addAction(l_pHintAct);
    l_pViewMenu->addAction(l_pDebuggerAct);
    l_pViewMenu->addSeparator();
    l_pViewMenu->addAction(l_pProblemsDock->toggleViewAction());
    l_pViewMenu->addAction(l_pInspectDock->toggleViewAction());
    QMenu* l_pHelp = menuBar()->addMenu(tr("&Help"));
    l_pHelp->addAction(tr("UI Editor &Tutorial"), QKeySequence(Qt::Key_F1), this, &UiEditorWindow::f_OpenTutorial);
    l_pHelp->addAction(tr("Open &Practice Screen"), this, &UiEditorWindow::f_OpenPracticeScreen);

    auto* l_pBar = addToolBar(tr("Main"));
    l_pBar->setObjectName(QStringLiteral("UiEditorToolBar"));
    l_pBar->setMovable(false);
    l_pBar->addAction(l_pSaveAct);
    l_pBar->addAction(l_pRevertAct);
    l_pBar->addSeparator();
    l_pBar->addWidget(new QLabel(tr(" Preview ")));
    l_pBar->addWidget(m_pPreview);
    l_pBar->addAction(l_pRestartAct);
    l_pBar->addSeparator();
    l_pBar->addWidget(new QLabel(tr(" Language ")));
    l_pBar->addWidget(m_pLang);
    l_pBar->addSeparator();
    l_pBar->addAction(l_pInspectAct);
    l_pBar->addAction(l_pDebuggerAct);

    connect(m_pFiles, &QListWidget::currentRowChanged, this, [this](int l_iRow) {
        if (l_iRow >= 0) f_OpenFile(m_pFiles->item(l_iRow)->data(Qt::UserRole).toString());
    });
    connect(m_pProblems, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* l_pIt) { f_OpenProblem(m_pProblems->row(l_pIt)); });
    connect(m_pRules, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* l_pIt) { f_OpenRule(m_pRules->row(l_pIt)); });
    connect(m_pView, &BgfxViewport::f_StatusMessage, this, [this](const QString& l_oM) { statusBar()->showMessage(l_oM, 4000); });
    connect(m_pView, &BgfxViewport::f_StartFailed, this, [this](const QString& l_oTitle, const QString& l_oText) {
        if (m_bQuiet) { statusBar()->showMessage(l_oTitle + QStringLiteral(": ") + l_oText); return; }
        QMessageBox::warning(this, l_oTitle, l_oText);
    });
    statusBar()->showMessage(tr("Type to see the change in the game. Ctrl+S saves; nothing on disk changes before that."));
}

// ---- files ----

QString UiEditorWindow::f_PathOf(const QString& l_oName) const {
    return QDir(m_oTutorialFiles.contains(l_oName) ? m_oTutorialDir : m_oUiDir).filePath(l_oName);
}

QString UiEditorWindow::f_ReadDisk(const QString& l_oName, bool* l_pCrlf) const {
    QFile l_oF(f_PathOf(l_oName));
    if (!l_oF.open(QIODevice::ReadOnly)) return QString();
    QString l_oT = QString::fromUtf8(l_oF.readAll());
    if (l_pCrlf) *l_pCrlf = l_oT.contains(QStringLiteral("\r\n"));
    l_oT.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    return l_oT;
}

void UiEditorWindow::f_RefreshFileList() {
    const QSignalBlocker l_oBlock(m_pFiles);
    const QStringList l_oAll = uied::f_UiFiles(m_oUiDir);
    m_pFiles->clear();
    auto l_fnHeader = [this](const QString& l_oTitle) {   // a group's title: not a file, not selectable
        auto* l_pIt = new QListWidgetItem(l_oTitle);
        l_pIt->setFlags(Qt::NoItemFlags);
        QFont l_oF = l_pIt->font();
        l_oF.setBold(true);
        l_pIt->setFont(l_oF);
        m_pFiles->addItem(l_pIt);
    };
    auto l_fnAddFile = [this](const QString& l_oF) {
        auto* l_pIt = new QListWidgetItem((f_IsModified(l_oF) ? QStringLiteral("● ") : QStringLiteral("   ")) + l_oF);
        l_pIt->setData(Qt::UserRole, l_oF);
        const QString l_oWhy = m_oRelatedWhy.value(l_oF);
        if (l_oWhy == QStringLiteral("shown")) l_pIt->setToolTip(tr("The game shows this document now"));
        else if (!l_oWhy.isEmpty()) l_pIt->setToolTip(tr("A style sheet %1 links").arg(l_oWhy));
        m_pFiles->addItem(l_pIt);
        if (l_oF == m_oCurrent) m_pFiles->setCurrentItem(l_pIt);
    };
    QStringList l_oShown;
    for (const QString& l_oF : m_oRelated)
        if (l_oAll.contains(l_oF)) l_oShown << l_oF;
    if (!l_oShown.isEmpty()) {
        l_fnHeader(tr("In this preview"));
        for (const QString& l_oF : l_oShown) l_fnAddFile(l_oF);
        l_fnHeader(tr("Other files"));
    }
    for (const QString& l_oF : l_oAll)
        if (!l_oShown.contains(l_oF)) l_fnAddFile(l_oF);
    for (const QString& l_oF : m_oTutorialFiles) {   // the tutorial's practice screen, after the game's own files
        auto* l_pIt = new QListWidgetItem((f_IsModified(l_oF) ? QStringLiteral("● ") : QStringLiteral("   ")) + l_oF + tr("  (tutorial)"));
        l_pIt->setData(Qt::UserRole, l_oF);
        l_pIt->setToolTip(tr("The UI editor tutorial's practice screen (docs/examples/ui_tutorial): Preview \"Practice screen\" shows it"));
        m_pFiles->addItem(l_pIt);
        if (l_oF == m_oCurrent) m_pFiles->setCurrentItem(l_pIt);
    }
}

int UiEditorWindow::f_FileCount() const { return m_pFiles->count(); }

bool UiEditorWindow::f_OpenFile(const QString& l_oName) {
    if (!QFileInfo::exists(f_PathOf(l_oName))) return false;
    if (!m_oBuffers.contains(l_oName)) {
        Buffer l_oB;
        l_oB.m_oDisk = f_ReadDisk(l_oName, &l_oB.m_bCrlf);
        l_oB.m_pDoc = new QTextDocument(this);
        l_oB.m_pDoc->setDocumentLayout(new QPlainTextDocumentLayout(l_oB.m_pDoc));
        l_oB.m_pDoc->setDefaultFont(m_pEdit->font());
        m_bLoading = true;
        l_oB.m_pDoc->setPlainText(l_oB.m_oDisk);
        m_bLoading = false;
        l_oB.m_pDoc->setModified(false);
        l_oB.m_pHighlighter = new UiHighlighter(l_oB.m_pDoc, l_oName.endsWith(QStringLiteral(".rcss")) ? UiHighlighter::Kind::Rcss
                                                                                      : UiHighlighter::Kind::Rml);
        connect(l_oB.m_pDoc, &QTextDocument::contentsChanged, this, [this, l_oName] { if (!m_bLoading) f_Edited(l_oName); });
        m_oBuffers.insert(l_oName, l_oB);
    }
    m_oCurrent = l_oName;
    m_pEdit->setDocument(m_oBuffers[l_oName].m_pDoc);
    f_RefreshFileList();
    setWindowTitle(tr("TOMS UI Editor — %1").arg(l_oName));
    return true;
}

QString UiEditorWindow::f_Text(const QString& l_oName) const {
    return m_oBuffers.contains(l_oName) ? m_oBuffers[l_oName].m_pDoc->toPlainText() : QString();
}

void UiEditorWindow::f_SetText(const QString& l_oName, const QString& l_oT) {
    if (!f_OpenFile(l_oName)) return;
    QTextCursor l_oC(m_oBuffers[l_oName].m_pDoc);
    l_oC.select(QTextCursor::Document);
    l_oC.insertText(l_oT);   // one undo step, like a paste
}

bool UiEditorWindow::f_IsModified(const QString& l_oName) const {
    return m_oBuffers.contains(l_oName) && m_oBuffers[l_oName].m_pDoc->toPlainText() != m_oBuffers[l_oName].m_oDisk;
}

void UiEditorWindow::f_Edited(const QString& l_oName) {
    Q_UNUSED(l_oName);
    f_RefreshFileList();
    m_oPreviewTimer.start();   // a pause in typing previews it
}

void UiEditorWindow::f_PreviewNow() {
    m_oPreviewTimer.stop();
    for (auto l_oIt = m_oBuffers.begin(); l_oIt != m_oBuffers.end(); ++l_oIt) {
        if (f_IsModified(l_oIt.key())) RmlUi::f_SetVirtualFile(l_oIt.key().toStdString(), l_oIt.value().m_pDoc->toPlainText().toStdString());
        else RmlUi::f_ClearVirtualFile(l_oIt.key().toStdString());
    }
    m_pProblems->clear();
    m_oProblemTargets.clear();
    f_DropHint();   // ours, not the game's: made again over the reloaded documents
    m_pView->f_Session().f_ReloadUi();
    if (m_bPracticeOn) {   // the game reloads its own documents; the practice screen is ours
        RmlUi* l_pR = m_pView->f_Session().f_Rml();
        if (m_pPracticeDoc && l_pR && l_pR->f_Context() == m_pPracticeCtx) m_pPracticeDoc->Close();
        m_pPracticeDoc = nullptr;
        f_LoadPractice();
    }
}

bool UiEditorWindow::f_Save(const QString& l_oName) {
    if (!m_oBuffers.contains(l_oName)) return false;
    Buffer& l_oB = m_oBuffers[l_oName];
    QString l_oT = l_oB.m_pDoc->toPlainText();
    QString l_oOut = l_oB.m_bCrlf ? QString(l_oT).replace(QStringLiteral("\n"), QStringLiteral("\r\n")) : l_oT;
    m_oOwnWrite = l_oName;
    QSaveFile l_oF(f_PathOf(l_oName));
    const bool l_bOk = l_oF.open(QIODevice::WriteOnly) && l_oF.write(l_oOut.toUtf8()) >= 0 && l_oF.commit();
    if (!l_bOk) {
        m_oOwnWrite.clear();
        const QString l_oMsg = tr("%1 could not be saved: %2").arg(l_oName, l_oF.errorString());
        if (m_bQuiet) statusBar()->showMessage(l_oMsg);
        else QMessageBox::warning(this, tr("Save"), l_oMsg);
        return false;
    }
    l_oB.m_oDisk = l_oT;
    l_oB.m_pDoc->setModified(false);
    // QSaveFile replaces the file: watch the new one
    const QString l_oPath = f_PathOf(l_oName);
    m_pWatcher->removePath(l_oPath);
    m_pWatcher->addPath(l_oPath);
    f_PreviewNow();   // the file on disk is the preview now
    f_RefreshFileList();
    statusBar()->showMessage(tr("Saved %1").arg(l_oName), 3000);
    return true;
}

bool UiEditorWindow::f_SaveAll() {
    bool l_bOk = true;
    for (const QString& l_oN : m_oBuffers.keys())
        if (f_IsModified(l_oN)) l_bOk = f_Save(l_oN) && l_bOk;
    return l_bOk;
}

void UiEditorWindow::f_Revert(const QString& l_oName) {
    if (!m_oBuffers.contains(l_oName)) return;
    Buffer& l_oB = m_oBuffers[l_oName];
    l_oB.m_oDisk = f_ReadDisk(l_oName, &l_oB.m_bCrlf);
    QTextCursor l_oC(l_oB.m_pDoc);
    l_oC.select(QTextCursor::Document);
    l_oC.insertText(l_oB.m_oDisk);
    l_oB.m_pDoc->setModified(false);
    f_PreviewNow();
    f_RefreshFileList();
}

void UiEditorWindow::f_DiskChanged(const QString& l_oPath) {
    const QString l_oName = QFileInfo(l_oPath).fileName();
    if (l_oName == m_oOwnWrite) { m_oOwnWrite.clear(); return; }
    if (!m_pWatcher->files().contains(l_oPath) && QFileInfo::exists(l_oPath)) m_pWatcher->addPath(l_oPath);
    if (!m_oBuffers.contains(l_oName)) return;
    Buffer& l_oB = m_oBuffers[l_oName];
    const QString l_oNow = f_ReadDisk(l_oName, &l_oB.m_bCrlf);
    if (l_oNow == l_oB.m_oDisk) return;
    if (!f_IsModified(l_oName)) {   // not changed here: take the new text
        l_oB.m_oDisk = l_oNow;
        m_bLoading = true;
        l_oB.m_pDoc->setPlainText(l_oNow);
        m_bLoading = false;
        l_oB.m_pDoc->setModified(false);
        statusBar()->showMessage(tr("%1 changed on disk: reloaded").arg(l_oName), 4000);
    } else {
        l_oB.m_oDisk = l_oNow;   // ours stays; saving it replaces theirs
        statusBar()->showMessage(tr("%1 changed on disk, and here too: your text is kept (Revert File takes theirs)").arg(l_oName));
    }
    f_RefreshFileList();
}

bool UiEditorWindow::f_AskToSave() {
    QStringList l_oChanged;
    for (const QString& l_oN : m_oBuffers.keys())
        if (f_IsModified(l_oN)) l_oChanged << l_oN;
    if (l_oChanged.isEmpty() || m_bQuiet) return true;
    const auto l_eR = QMessageBox::question(this, tr("Unsaved changes"),
                                         tr("These files have changes that are not saved:\n%1\n\nSave them?").arg(l_oChanged.join(QLatin1Char('\n'))),
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (l_eR == QMessageBox::Cancel) return false;
    return l_eR == QMessageBox::Discard || f_SaveAll();
}

void UiEditorWindow::closeEvent(QCloseEvent* l_pE) {
    if (!f_AskToSave()) { l_pE->ignore(); return; }
    l_pE->accept();
}

// ---- the preview ----

void UiEditorWindow::f_FillPreviewList() {
    m_pPreview->clear();
    m_oPreviewPaths.clear();
    m_pPreview->addItem(tr("Title screen"));
    m_oPreviewPaths << QString();
    if (!m_oTutorialFiles.isEmpty()) {
        m_pPreview->addItem(tr("Practice screen (tutorial)"));
        m_oPreviewPaths << QStringLiteral("practice");
    }
    // the test scenarios by screen (the player menu's tabs each their own), in the game's order
    QDir l_oD(m_oScenariosDir);
    struct Entry { int m_iOrder; QString m_oLabel, m_oPath; };
    QList<Entry> l_oEntries;
    for (const QString& l_oName : l_oD.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString l_oFile = l_oD.filePath(l_oName + QStringLiteral("/scenario.json"));
        if (!QFileInfo::exists(l_oFile)) continue;
        int l_iOrder = 0;
        const QString l_oLabel = uied::f_PreviewLabel(uied::f_ReadScenario(l_oFile), l_oName, &l_iOrder);
        l_oEntries.push_back({l_iOrder, l_oLabel, l_oD.filePath(l_oName)});
    }
    std::stable_sort(l_oEntries.begin(), l_oEntries.end(), [](const Entry& l_oA, const Entry& l_oB) { return l_oA.m_iOrder < l_oB.m_iOrder; });
    for (const Entry& l_oE : l_oEntries) {
        m_pPreview->addItem(l_oE.m_oLabel);
        m_oPreviewPaths << l_oE.m_oPath;
    }
}

void UiEditorWindow::f_StartPreviewAt(int l_iIndex) {
    m_pPreview->setCurrentIndex(l_iIndex);
    f_StartPreview(l_iIndex);
}

void UiEditorWindow::f_StartPreview(int l_iIndex) {
    if (l_iIndex < 0 || l_iIndex >= m_oPreviewPaths.size()) return;
    m_bOpenRelated = true;   // its first document opens once the game shows it (updateRelated)
    m_bPracticeOn = m_oPreviewPaths[l_iIndex] == QStringLiteral("practice");
    m_pPracticeDoc = nullptr;   // a restarted game has a new UI context: the old document went with the old one
    m_pPracticeCtx = nullptr;
    f_PreviewNow();
    if (m_bPracticeOn) {   // over the map of a new game, so the HUD's values ({{hud.res_line}}) are real
        QString l_oErr;
        if (!m_pView->f_StartScenario(QDir(m_oScenariosDir).filePath(QStringLiteral("stage_new_game")), l_oErr))
            statusBar()->showMessage(l_oErr);
        m_oPracticeTimer.start();
        return;
    }
    if (m_oPreviewPaths[l_iIndex].isEmpty()) {
        m_pView->f_RestartGame();
        return;
    }
    QString l_oErr;
    if (!m_pView->f_StartScenario(m_oPreviewPaths[l_iIndex], l_oErr)) {
        if (m_bQuiet) statusBar()->showMessage(l_oErr);
        else QMessageBox::warning(this, tr("Preview"), tr("The scenario could not start:\n%1").arg(l_oErr));
    }
}

void UiEditorWindow::f_LoadPractice() {
    RmlUi* l_pR = m_pView->f_Session().f_Rml();
    if (!m_bPracticeOn) { m_oPracticeTimer.stop(); return; }
    if (!l_pR || !l_pR->f_Ready() || !m_pView->f_Session().f_Game() || m_pPracticeDoc) return;   // not up yet: the timer tries again
    m_oPracticeTimer.stop();
    Rml::Context* l_pCtx = l_pR->f_Context();
    m_pPracticeDoc = l_pCtx->LoadDocument(f_PathOf(QStringLiteral("practice.rml")).toStdString());
    m_pPracticeCtx = l_pCtx;
    if (m_pPracticeDoc) m_pPracticeDoc->Show();
    else statusBar()->showMessage(tr("practice.rml could not be loaded: see Problems"));
}

void UiEditorWindow::f_OpenPracticeScreen() {
    const int l_iI = m_oPreviewPaths.indexOf(QStringLiteral("practice"));
    if (l_iI < 0) {
        statusBar()->showMessage(tr("The practice screen (docs/examples/ui_tutorial) is not in this copy of the editor's repo"));
        return;
    }
    f_StartPreviewAt(l_iI);
    f_OpenFile(QStringLiteral("practice.rcss"));
}

void UiEditorWindow::f_OpenTutorial() {
    const QString l_oDoc = QDir(m_oDocsDir).filePath(Language::f_Current() == Language::Lang::Chinese ? QStringLiteral("zh_TW/37_UI_EDITOR.md")
                                                                                                : QStringLiteral("10_UI_EDITOR.md"));
    if (!QFileInfo::exists(l_oDoc) || !QDesktopServices::openUrl(QUrl::fromLocalFile(l_oDoc))) {
        const QString l_oMsg = tr("The tutorial is docs/gameplay/10_UI_EDITOR.md in the TOMS repo; it could not be opened from here.");
        if (m_bQuiet) statusBar()->showMessage(l_oMsg);
        else QMessageBox::information(this, tr("UI Editor Tutorial"), l_oMsg);
    }
}

void UiEditorWindow::f_FillLanguages() {
    Game* l_pG = m_pView->f_Session().f_Game();
    if (!l_pG) return;
    m_oLangTimer.stop();
    const QSignalBlocker l_oBlock(m_pLang);
    m_pLang->clear();
    for (const auto& l_oLi : l_pG->f_Locale().f_Languages())
        m_pLang->addItem(QStringLiteral("%1  (%2)").arg(QString::fromStdString(l_oLi.m_strName), QString::fromStdString(l_oLi.m_strCode)),
                       QString::fromStdString(l_oLi.m_strCode));
    m_pLang->setCurrentIndex(m_pLang->findData(QString::fromStdString(l_pG->f_Locale().f_LanguageCode())));
}

// ---- problems and the inspector ----

void UiEditorWindow::f_AddProblem(int l_iLevel, const QString& l_oMessage) {
    const uied::Problem l_oP = uied::f_ParseProblem(l_iLevel, l_oMessage);
    QString l_oWhere = l_oP.m_oFile.isEmpty() ? QString() : l_oP.m_iLine ? QStringLiteral("%1:%2  ").arg(l_oP.m_oFile).arg(l_oP.m_iLine) : l_oP.m_oFile + QStringLiteral("  ");
    auto* l_pIt = new QListWidgetItem((l_iLevel >= 2 ? QStringLiteral("⛔ ") : QStringLiteral("⚠ ")) + l_oWhere + l_oP.m_oText);
    m_pProblems->addItem(l_pIt);
    m_oProblemTargets << (l_oP.m_oFile.isEmpty() ? QString() : QStringLiteral("%1:%2").arg(l_oP.m_oFile).arg(l_oP.m_iLine));
}

int UiEditorWindow::f_ProblemCount() const { return m_pProblems->count(); }

int UiEditorWindow::f_ProblemAt(const QString& l_oFile) const {
    for (const QString& l_oT : m_oProblemTargets)
        if (l_oT.startsWith(l_oFile + QLatin1Char(':'))) return l_oT.section(QLatin1Char(':'), 1).toInt();
    return 0;
}

void UiEditorWindow::f_OpenProblem(int l_iRow) {
    if (l_iRow < 0 || l_iRow >= m_oProblemTargets.size() || m_oProblemTargets[l_iRow].isEmpty()) return;
    const QStringList l_oParts = m_oProblemTargets[l_iRow].split(QLatin1Char(':'));
    if (f_OpenFile(l_oParts[0]) && l_oParts.size() > 1 && l_oParts[1].toInt() > 0) m_pEdit->f_GoToLine(l_oParts[1].toInt());
}

void UiEditorWindow::f_OpenRule(int l_iRow) {
    if (l_iRow < 0 || l_iRow >= m_oRuleTargets.size()) return;
    const QStringList l_oParts = m_oRuleTargets[l_iRow].split(QLatin1Char(':'));
    if (l_oParts.size() == 2 && f_OpenFile(l_oParts[0])) m_pEdit->f_GoToLine(l_oParts[1].toInt());
}

void UiEditorWindow::f_InspectNow() {
    m_pRules->clear();
    m_oRuleTargets.clear();
    RmlUi* l_pR = m_pView->f_Session().f_Rml();
    Rml::Context* l_pCtx = l_pR ? l_pR->f_Context() : nullptr;
    Rml::Element* l_pEl = l_pCtx ? l_pCtx->GetHoverElement() : nullptr;
    if (!l_pEl || l_pEl == l_pEl->GetOwnerDocument()) {
        m_pInspectLabel->setText(tr("Nothing under the mouse: point at a part of the screen and click again."));
        return;
    }
    auto l_fnDescribe = [](Rml::Element* l_pE) {
        QString l_oS = QString::fromStdString(l_pE->GetTagName());
        if (!l_pE->GetId().empty()) l_oS += QLatin1Char('#') + QString::fromStdString(l_pE->GetId());
        for (const QString& l_oC : QString::fromStdString(l_pE->GetClassNames()).split(QLatin1Char(' '), Qt::SkipEmptyParts))
            l_oS += QLatin1Char('.') + l_oC;
        return l_oS;
    };
    QStringList l_oChain;
    for (Rml::Element* l_pE = l_pEl; l_pE && l_pE != l_pEl->GetOwnerDocument(); l_pE = l_pE->GetParentNode()) l_oChain.prepend(l_fnDescribe(l_pE));
    const QString l_oDocFile = QFileInfo(QString::fromStdString(l_pEl->GetOwnerDocument()->GetSourceURL())).fileName();
    m_pInspectLabel->setText(tr("<b>%1</b> in %2<br>%3").arg(l_fnDescribe(l_pEl).toHtmlEscaped(), l_oDocFile, l_oChain.join(QStringLiteral(" › ")).toHtmlEscaped()));

    // its line in the .rml: its id, else the first element like it, else the nearest parent with an id
    if (f_OpenFile(l_oDocFile)) {
        const QString l_oRml = f_Text(l_oDocFile);
        int l_iLine = 0;
        for (Rml::Element* l_pE = l_pEl; l_pE && !l_iLine && l_pE != l_pEl->GetOwnerDocument(); l_pE = l_pE->GetParentNode()) {
            l_iLine = uied::f_LineOfId(l_oRml, QString::fromStdString(l_pE->GetId()));
            if (!l_iLine && l_pE == l_pEl)
                l_iLine = uied::f_LineOfTagWithClasses(l_oRml, QString::fromStdString(l_pE->GetTagName()),
                                                   QString::fromStdString(l_pE->GetClassNames()).split(QLatin1Char(' '), Qt::SkipEmptyParts));
        }
        if (l_iLine) m_pEdit->f_GoToLine(l_iLine);
        // the rules of the sheets this document links that name it
        const QString l_oId = QString::fromStdString(l_pEl->GetId());
        const QStringList l_oClasses = QString::fromStdString(l_pEl->GetClassNames()).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const QString l_oTag = QString::fromStdString(l_pEl->GetTagName());
        for (const QString& l_oHref : uied::f_LinkedStyleSheets(l_oRml)) {
            const QString l_oSheet = QFileInfo(l_oHref).fileName();
            const QString l_oCss = m_oBuffers.contains(l_oSheet) ? f_Text(l_oSheet) : f_ReadDisk(l_oSheet);
            for (const uied::RuleHit& l_oH : uied::f_RulesFor(l_oSheet, l_oCss, l_oId, l_oClasses, l_oTag)) {
                m_pRules->addItem(QStringLiteral("%1:%2   %3").arg(l_oH.m_oFile).arg(l_oH.m_iLine).arg(l_oH.m_oText));
                m_oRuleTargets << QStringLiteral("%1:%2").arg(l_oH.m_oFile).arg(l_oH.m_iLine);
            }
        }
        if (m_pRules->count() == 0) m_pRules->addItem(tr("(no rule names it: it takes only what it inherits)"));
    }
}

QString UiEditorWindow::f_InspectSummary() const { return m_pInspectLabel->text(); }

void UiEditorWindow::f_PlaceCursor(int l_iPos) {
    QTextCursor l_oC = m_pEdit->textCursor();
    l_oC.setPosition(std::clamp(l_iPos, 0, m_pEdit->document()->characterCount() - 1));
    m_pEdit->setTextCursor(l_oC);
}

// ---- the hint: what the text cursor is on, glowing in the game ----

void UiEditorWindow::f_CursorMoved() {
    if (!m_bHintOn || m_oCurrent.isEmpty()) return;
    const QString l_oT = m_pEdit->toPlainText();
    const int l_iPos = m_pEdit->textCursor().position();
    QStringList l_oSel;
    if (m_oCurrent.endsWith(QStringLiteral(".rml"))) l_oSel = uied::f_SelectorsAtRml(l_oT, l_iPos);
    else if (m_oCurrent.endsWith(QStringLiteral(".rcss"))) l_oSel = uied::f_SelectorsAtRcss(l_oT, l_iPos);
    m_oHintFile = m_oCurrent.endsWith(QStringLiteral(".rml")) ? m_oCurrent : QString();
    if (l_oSel == m_oHintSelectors) return;
    m_oHintSelectors = l_oSel;
    f_UpdateHint();
    if (l_oSel.isEmpty()) m_oHintTimer.stop();
    else m_oHintTimer.start();
}

void UiEditorWindow::f_DropHint() {
    RmlUi* l_pR = m_pView->f_Session().f_Rml();
    if (m_pHintDoc && l_pR && l_pR->f_Context() == m_pHintCtx) m_pHintDoc->Close();
    m_pHintDoc = nullptr;
    m_pHintCtx = nullptr;
}

void UiEditorWindow::f_UpdateHint() {
    RmlUi* l_pR = m_pView->f_Session().f_Rml();
    Rml::Context* l_pCtx = l_pR && l_pR->f_Ready() ? l_pR->f_Context() : nullptr;
    if (!l_pCtx) return;
    if (l_pCtx != m_pHintCtx) { m_pHintDoc = nullptr; m_pHintCtx = l_pCtx; }   // a restarted game: its old document went with it
    // the elements: in the shown documents (an .rml's hint only in its own), each selector of the cursor's
    std::vector<Rml::Element*> l_vFound;
    for (int l_iI = 0; l_iI < l_pCtx->GetNumDocuments() && !m_oHintSelectors.isEmpty(); l_iI++) {
        Rml::ElementDocument* l_pD = l_pCtx->GetDocument(l_iI);
        if (!l_pD || l_pD == m_pHintDoc || !l_pD->IsVisible()) continue;
        if (!m_oHintFile.isEmpty() && QFileInfo(QString::fromStdString(l_pD->GetSourceURL())).fileName() != m_oHintFile) continue;
        for (const QString& l_oS : m_oHintSelectors) {
            Rml::ElementList l_vEls;
            l_pD->QuerySelectorAll(l_vEls, l_oS.toStdString());
            for (Rml::Element* l_pE : l_vEls)
                if (l_pE->IsVisible() && l_vFound.size() < 64 && std::find(l_vFound.begin(), l_vFound.end(), l_pE) == l_vFound.end()) l_vFound.push_back(l_pE);
        }
    }
    m_iHintCount = (int)l_vFound.size();
    if (l_vFound.empty()) {   // nothing on screen: no frame (the status says why)
        if (m_pHintDoc) while (m_pHintDoc->GetNumChildren() > 0) m_pHintDoc->RemoveChild(m_pHintDoc->GetChild(0));
        if (!m_oHintSelectors.isEmpty())
            statusBar()->showMessage(tr("%1: nothing on screen now (hidden, or another screen)").arg(m_oHintSelectors.join(QStringLiteral(", "))), 2000);
        return;
    }
    if (!m_pHintDoc) {
        m_pHintDoc = l_pCtx->LoadDocumentFromMemory(R"RML(<rml><head><style>
body { position: absolute; left: 0; top: 0; width: 100%; height: 100%; z-index: 100000; pointer-events: none; }
.uied-hint { position: absolute; border-width: 3px; border-color: #ffd54a; background-color: #ffd54a2c;
             pointer-events: none; animation: 0.7s cubic-in-out infinite alternate uied-pulse; }
@keyframes uied-pulse { from { opacity: 0.25; } to { opacity: 1; } }
</style></head><body></body></rml>)RML", "[ui_editor hint]");
        if (!m_pHintDoc) return;
        m_pHintDoc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
        statusBar()->showMessage(tr("Glowing %1 (%2 on screen)").arg(m_oHintSelectors.join(QStringLiteral(", "))).arg(l_vFound.size()), 3000);
    }
    m_pHintDoc->PullToFront();
    // one frame per element, where it is now (things move: a damage number, a rotated screen)
    while (m_pHintDoc->GetNumChildren() > (int)l_vFound.size()) m_pHintDoc->RemoveChild(m_pHintDoc->GetChild(m_pHintDoc->GetNumChildren() - 1));
    while (m_pHintDoc->GetNumChildren() < (int)l_vFound.size()) {
        Rml::ElementPtr l_upBox = m_pHintDoc->CreateElement("div");
        l_upBox->SetClass("uied-hint", true);
        m_pHintDoc->AppendChild(std::move(l_upBox));
    }
    for (size_t l_nK = 0; l_nK < l_vFound.size(); l_nK++) {
        const Rml::Vector2f l_oO = l_vFound[l_nK]->GetAbsoluteOffset(Rml::BoxArea::Border);
        const Rml::Vector2f l_oSz = l_vFound[l_nK]->GetBox().GetSize(Rml::BoxArea::Border);
        Rml::Element* l_pBox = m_pHintDoc->GetChild((int)l_nK);
        l_pBox->SetProperty("left", std::to_string(l_oO.x - 3.0f) + "px");
        l_pBox->SetProperty("top", std::to_string(l_oO.y - 3.0f) + "px");
        l_pBox->SetProperty("width", std::to_string(std::max(l_oSz.x, 2.0f)) + "px");
        l_pBox->SetProperty("height", std::to_string(std::max(l_oSz.y, 2.0f)) + "px");
    }
}

// The documents the game's UI shows now (each .rml), then the style sheets each links, in that order without repeats.
void UiEditorWindow::f_UpdateRelated() {
    RmlUi* l_pR = m_pView->f_Session().f_Rml();
    Rml::Context* l_pCtx = l_pR && l_pR->f_Ready() ? l_pR->f_Context() : nullptr;
    if (!l_pCtx) return;
    QStringList l_oFiles;
    QMap<QString, QString> l_oWhy;
    for (int l_iI = 0; l_iI < l_pCtx->GetNumDocuments(); l_iI++) {
        Rml::ElementDocument* l_pD = l_pCtx->GetDocument(l_iI);
        if (!l_pD || !l_pD->IsVisible() || l_pD == m_pHintDoc) continue;   // (the hint is ours, not a screen of the game)
        const QString l_oRml = QFileInfo(QString::fromStdString(l_pD->GetSourceURL())).fileName();
        if (l_oRml.isEmpty() || l_oFiles.contains(l_oRml)) continue;
        l_oFiles << l_oRml;
        l_oWhy[l_oRml] = QStringLiteral("shown");
        const QString l_oSrc = m_oBuffers.contains(l_oRml) ? f_Text(l_oRml) : f_ReadDisk(l_oRml);
        for (const QString& l_oHref : uied::f_LinkedStyleSheets(l_oSrc)) {
            const QString l_oSheet = QFileInfo(l_oHref).fileName();
            if (l_oFiles.contains(l_oSheet)) continue;
            l_oFiles << l_oSheet;
            l_oWhy[l_oSheet] = l_oRml;
        }
    }
    // the .rml files first (what is on screen), then the sheets
    std::stable_partition(l_oFiles.begin(), l_oFiles.end(), [](const QString& l_oF) { return l_oF.endsWith(QStringLiteral(".rml")); });
    if (l_oFiles == m_oRelated) return;
    m_oRelated = l_oFiles;
    m_oRelatedWhy = l_oWhy;
    f_RefreshFileList();
    QStringList l_oRmls;
    for (const QString& l_oF : l_oFiles)
        if (l_oF.endsWith(QStringLiteral(".rml"))) l_oRmls << l_oF;
    if (!l_oRmls.isEmpty())
        statusBar()->showMessage(tr("This preview shows %1 (its files are at the top of the list)").arg(l_oRmls.join(QStringLiteral(", "))), 6000);
    // a new preview: open its document, unless the file being edited is already one of its files
    if (m_bOpenRelated && !l_oRmls.isEmpty()) {
        m_bOpenRelated = false;
        if (!m_oRelated.contains(m_oCurrent)) f_OpenFile(l_oRmls.last());   // the last shown is the one on top
    }
}
int UiEditorWindow::f_InspectRuleCount() const { return m_oRuleTargets.size(); }

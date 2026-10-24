// uied_window.cpp -- see uied_window.h.
#include "uied_window.h"

#include "Language.h"
#include "bgfx_viewport.h"
#include "game.h"
#include "rml_ui.h"
#include "uied_code_edit.h"
#include "uied_logic.h"

#include <RmlUi/Core.h>

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

UiEditorWindow::UiEditorWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("TOMS UI Editor"));
    resize(1600, 900);
    previewTimer_.setSingleShot(true);
    previewTimer_.setInterval(300);
    connect(&previewTimer_, &QTimer::timeout, this, &UiEditorWindow::previewNow);
    buildUi();
    uiDir_ = QDir(view_->assetDir()).filePath(QStringLiteral("ui"));
    scenariosDir_ = QDir::cleanPath(QDir(view_->assetDir()).filePath(QStringLiteral("../../tests/scenarios")));
    docsDir_ = QDir::cleanPath(QDir(view_->assetDir()).filePath(QStringLiteral("../../docs")));
    tutorialDir_ = QDir(docsDir_).filePath(QStringLiteral("examples/ui_tutorial"));
    tutorialFiles_ = uied::uiFiles(tutorialDir_);
    practiceTimer_.setInterval(200);
    connect(&practiceTimer_, &QTimer::timeout, this, &UiEditorWindow::loadPractice);
    refreshFileList();
    fillPreviewList();
    watcher_ = new QFileSystemWatcher(this);
    for (const QString& f : uied::uiFiles(uiDir_) + tutorialFiles_) watcher_->addPath(pathOf(f));
    connect(watcher_, &QFileSystemWatcher::fileChanged, this, &UiEditorWindow::diskChanged);
    // RmlUi's warnings and errors: the Problems list (queued: RmlUi may log in the middle of a reload).
    RmlUi::setLogListener([this](int level, const std::string& message) {
        if (level < 1) return;
        const QString m = QString::fromStdString(message);
        QMetaObject::invokeMethod(this, [this, level, m] { addProblem(level, m); }, Qt::QueuedConnection);
    });
    // The languages are known once the game runs.
    langTimer_.setInterval(500);
    connect(&langTimer_, &QTimer::timeout, this, &UiEditorWindow::fillLanguages);
    langTimer_.start();
    const QStringList all = uied::uiFiles(uiDir_);
    if (all.contains(QStringLiteral("battle.rml"))) openFile(QStringLiteral("battle.rml"));
    else if (!all.isEmpty()) openFile(all.first());
}

UiEditorWindow::~UiEditorWindow() {
    RmlUi::setLogListener(nullptr);
    for (auto it = buffers_.begin(); it != buffers_.end(); ++it) RmlUi::clearVirtualFile(it.key().toStdString());
}

void UiEditorWindow::buildUi() {
    files_ = new QListWidget;
    edit_ = new UiCodeEdit;
    edit_->setPlaceholderText(tr("Pick a file on the left"));
    view_ = new BgfxViewport;
    view_->setMinimumSize(512, 384);

    auto* split = new QSplitter(Qt::Horizontal);
    split->addWidget(files_);
    split->addWidget(edit_);
    split->addWidget(view_);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 3);
    split->setStretchFactor(2, 4);
    split->setSizes({180, 620, 800});
    setCentralWidget(split);

    problems_ = new QListWidget;
    auto* problemsDock = new QDockWidget(tr("Problems"), this);
    problemsDock->setObjectName(QStringLiteral("ProblemsDock"));
    problemsDock->setWidget(problems_);
    addDockWidget(Qt::BottomDockWidgetArea, problemsDock);

    auto* inspector = new QWidget;
    auto* il = new QVBoxLayout(inspector);
    il->setContentsMargins(4, 4, 4, 4);
    inspectLabel_ = new QLabel(tr("Inspect (Ctrl+I), then click an element in the game."));
    inspectLabel_->setWordWrap(true);
    inspectLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rules_ = new QListWidget;
    il->addWidget(inspectLabel_);
    il->addWidget(rules_);
    auto* inspectDock = new QDockWidget(tr("Inspector"), this);
    inspectDock->setObjectName(QStringLiteral("InspectorDock"));
    inspectDock->setWidget(inspector);
    addDockWidget(Qt::BottomDockWidgetArea, inspectDock);
    splitDockWidget(problemsDock, inspectDock, Qt::Horizontal);

    // ---- actions ----
    auto* saveAct = new QAction(tr("&Save"), this);
    saveAct->setShortcut(QKeySequence::Save);
    connect(saveAct, &QAction::triggered, this, [this] { if (!current_.isEmpty()) save(current_); });
    auto* saveAllAct = new QAction(tr("Save &All"), this);
    saveAllAct->setShortcut(QKeySequence(tr("Ctrl+Shift+S")));
    connect(saveAllAct, &QAction::triggered, this, &UiEditorWindow::saveAll);
    auto* revertAct = new QAction(tr("&Revert File"), this);
    revertAct->setToolTip(tr("Drop this file's changes: back to the text on disk"));
    connect(revertAct, &QAction::triggered, this, [this] { if (!current_.isEmpty()) revert(current_); });
    auto* inspectAct = new QAction(tr("&Inspect"), this);
    inspectAct->setCheckable(true);
    inspectAct->setShortcut(QKeySequence(tr("Ctrl+I")));
    inspectAct->setToolTip(tr("A click in the game picks the UI element under the mouse instead of pressing it (Ctrl+I)"));
    connect(inspectAct, &QAction::toggled, this, [this](bool on) {
        view_->setInspectMode(on);
        statusBar()->showMessage(on ? tr("Inspect: click an element in the game") : tr("Inspect off: clicks go to the game"), 4000);
    });
    connect(view_, &BgfxViewport::inspectClicked, this, &UiEditorWindow::inspectNow);
    auto* debuggerAct = new QAction(tr("RmlUi &Debugger"), this);
    debuggerAct->setShortcut(QKeySequence(Qt::Key_F8));
    debuggerAct->setToolTip(tr("RmlUi's own inspector over the game: the element tree, computed styles, layout boxes (F8)"));
    connect(debuggerAct, &QAction::triggered, this, [this] {
        if (RmlUi* r = view_->session().rml()) r->toggleDebugger();
    });
    auto* restartAct = new QAction(tr("Restart &Preview"), this);
    restartAct->setShortcut(QKeySequence(Qt::Key_F5));
    restartAct->setToolTip(tr("Start the chosen preview again (F5)"));
    connect(restartAct, &QAction::triggered, this, [this] { startPreview(preview_->currentIndex()); });

    preview_ = new QComboBox;
    preview_->setToolTip(tr("What the game shows: the title, or a test scenario (tests/scenarios: a battle, a dialogue, the store, ...)"));
    preview_->setMinimumContentsLength(28);
    connect(preview_, qOverload<int>(&QComboBox::activated), this, &UiEditorWindow::startPreview);
    lang_ = new QComboBox;
    lang_->setToolTip(tr("The game's text in this language (only here: the game's own setting is not changed)"));
    connect(lang_, qOverload<int>(&QComboBox::activated), this, [this](int i) {
        if (Game* g = view_->session().game()) g->previewLanguage(lang_->itemData(i).toString().toStdString());
    });

    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(saveAct);
    file->addAction(saveAllAct);
    file->addAction(revertAct);
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);
    QMenu* viewMenu = menuBar()->addMenu(tr("&Preview"));
    viewMenu->addAction(restartAct);
    viewMenu->addAction(inspectAct);
    viewMenu->addAction(debuggerAct);
    viewMenu->addSeparator();
    viewMenu->addAction(problemsDock->toggleViewAction());
    viewMenu->addAction(inspectDock->toggleViewAction());
    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("UI Editor &Tutorial"), QKeySequence(Qt::Key_F1), this, &UiEditorWindow::openTutorial);
    help->addAction(tr("Open &Practice Screen"), this, &UiEditorWindow::openPracticeScreen);

    auto* bar = addToolBar(tr("Main"));
    bar->setObjectName(QStringLiteral("UiEditorToolBar"));
    bar->setMovable(false);
    bar->addAction(saveAct);
    bar->addAction(revertAct);
    bar->addSeparator();
    bar->addWidget(new QLabel(tr(" Preview ")));
    bar->addWidget(preview_);
    bar->addAction(restartAct);
    bar->addSeparator();
    bar->addWidget(new QLabel(tr(" Language ")));
    bar->addWidget(lang_);
    bar->addSeparator();
    bar->addAction(inspectAct);
    bar->addAction(debuggerAct);

    connect(files_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0) openFile(files_->item(row)->data(Qt::UserRole).toString());
    });
    connect(problems_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) { openProblem(problems_->row(it)); });
    connect(rules_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) { openRule(rules_->row(it)); });
    connect(view_, &BgfxViewport::statusMessage, this, [this](const QString& m) { statusBar()->showMessage(m, 4000); });
    connect(view_, &BgfxViewport::startFailed, this, [this](const QString& title, const QString& text) {
        if (quiet_) { statusBar()->showMessage(title + QStringLiteral(": ") + text); return; }
        QMessageBox::warning(this, title, text);
    });
    statusBar()->showMessage(tr("Type to see the change in the game. Ctrl+S saves; nothing on disk changes before that."));
}

// ---- files ----

QString UiEditorWindow::pathOf(const QString& name) const {
    return QDir(tutorialFiles_.contains(name) ? tutorialDir_ : uiDir_).filePath(name);
}

QString UiEditorWindow::readDisk(const QString& name, bool* crlf) const {
    QFile f(pathOf(name));
    if (!f.open(QIODevice::ReadOnly)) return QString();
    QString t = QString::fromUtf8(f.readAll());
    if (crlf) *crlf = t.contains(QStringLiteral("\r\n"));
    t.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    return t;
}

void UiEditorWindow::refreshFileList() {
    const QSignalBlocker block(files_);
    const QStringList all = uied::uiFiles(uiDir_);
    files_->clear();
    for (const QString& f : all) {
        auto* it = new QListWidgetItem((isModified(f) ? QStringLiteral("● ") : QStringLiteral("   ")) + f);
        it->setData(Qt::UserRole, f);
        files_->addItem(it);
        if (f == current_) files_->setCurrentItem(it);
    }
    for (const QString& f : tutorialFiles_) {   // the tutorial's practice screen, after the game's own files
        auto* it = new QListWidgetItem((isModified(f) ? QStringLiteral("● ") : QStringLiteral("   ")) + f + tr("  (tutorial)"));
        it->setData(Qt::UserRole, f);
        it->setToolTip(tr("The UI editor tutorial's practice screen (docs/examples/ui_tutorial): Preview \"Practice screen\" shows it"));
        files_->addItem(it);
        if (f == current_) files_->setCurrentItem(it);
    }
}

int UiEditorWindow::fileCount() const { return files_->count(); }

bool UiEditorWindow::openFile(const QString& name) {
    if (!QFileInfo::exists(pathOf(name))) return false;
    if (!buffers_.contains(name)) {
        Buffer b;
        b.disk = readDisk(name, &b.crlf);
        b.doc = new QTextDocument(this);
        b.doc->setDocumentLayout(new QPlainTextDocumentLayout(b.doc));
        b.doc->setDefaultFont(edit_->font());
        loading_ = true;
        b.doc->setPlainText(b.disk);
        loading_ = false;
        b.doc->setModified(false);
        b.highlighter = new UiHighlighter(b.doc, name.endsWith(QStringLiteral(".rcss")) ? UiHighlighter::Kind::Rcss
                                                                                      : UiHighlighter::Kind::Rml);
        connect(b.doc, &QTextDocument::contentsChanged, this, [this, name] { if (!loading_) edited(name); });
        buffers_.insert(name, b);
    }
    current_ = name;
    edit_->setDocument(buffers_[name].doc);
    refreshFileList();
    setWindowTitle(tr("TOMS UI Editor — %1").arg(name));
    return true;
}

QString UiEditorWindow::text(const QString& name) const {
    return buffers_.contains(name) ? buffers_[name].doc->toPlainText() : QString();
}

void UiEditorWindow::setText(const QString& name, const QString& t) {
    if (!openFile(name)) return;
    QTextCursor c(buffers_[name].doc);
    c.select(QTextCursor::Document);
    c.insertText(t);   // one undo step, like a paste
}

bool UiEditorWindow::isModified(const QString& name) const {
    return buffers_.contains(name) && buffers_[name].doc->toPlainText() != buffers_[name].disk;
}

void UiEditorWindow::edited(const QString& name) {
    Q_UNUSED(name);
    refreshFileList();
    previewTimer_.start();   // a pause in typing previews it
}

void UiEditorWindow::previewNow() {
    previewTimer_.stop();
    for (auto it = buffers_.begin(); it != buffers_.end(); ++it) {
        if (isModified(it.key())) RmlUi::setVirtualFile(it.key().toStdString(), it.value().doc->toPlainText().toStdString());
        else RmlUi::clearVirtualFile(it.key().toStdString());
    }
    problems_->clear();
    problemTargets_.clear();
    view_->session().reloadUi();
    if (practiceOn_) {   // the game reloads its own documents; the practice screen is ours
        RmlUi* r = view_->session().rml();
        if (practiceDoc_ && r && r->context() == practiceCtx_) practiceDoc_->Close();
        practiceDoc_ = nullptr;
        loadPractice();
    }
}

bool UiEditorWindow::save(const QString& name) {
    if (!buffers_.contains(name)) return false;
    Buffer& b = buffers_[name];
    QString t = b.doc->toPlainText();
    QString out = b.crlf ? QString(t).replace(QStringLiteral("\n"), QStringLiteral("\r\n")) : t;
    ownWrite_ = name;
    QSaveFile f(pathOf(name));
    const bool ok = f.open(QIODevice::WriteOnly) && f.write(out.toUtf8()) >= 0 && f.commit();
    if (!ok) {
        ownWrite_.clear();
        const QString msg = tr("%1 could not be saved: %2").arg(name, f.errorString());
        if (quiet_) statusBar()->showMessage(msg);
        else QMessageBox::warning(this, tr("Save"), msg);
        return false;
    }
    b.disk = t;
    b.doc->setModified(false);
    // QSaveFile replaces the file: watch the new one
    const QString path = pathOf(name);
    watcher_->removePath(path);
    watcher_->addPath(path);
    previewNow();   // the file on disk is the preview now
    refreshFileList();
    statusBar()->showMessage(tr("Saved %1").arg(name), 3000);
    return true;
}

bool UiEditorWindow::saveAll() {
    bool ok = true;
    for (const QString& n : buffers_.keys())
        if (isModified(n)) ok = save(n) && ok;
    return ok;
}

void UiEditorWindow::revert(const QString& name) {
    if (!buffers_.contains(name)) return;
    Buffer& b = buffers_[name];
    b.disk = readDisk(name, &b.crlf);
    QTextCursor c(b.doc);
    c.select(QTextCursor::Document);
    c.insertText(b.disk);
    b.doc->setModified(false);
    previewNow();
    refreshFileList();
}

void UiEditorWindow::diskChanged(const QString& path) {
    const QString name = QFileInfo(path).fileName();
    if (name == ownWrite_) { ownWrite_.clear(); return; }
    if (!watcher_->files().contains(path) && QFileInfo::exists(path)) watcher_->addPath(path);
    if (!buffers_.contains(name)) return;
    Buffer& b = buffers_[name];
    const QString now = readDisk(name, &b.crlf);
    if (now == b.disk) return;
    if (!isModified(name)) {   // not changed here: take the new text
        b.disk = now;
        loading_ = true;
        b.doc->setPlainText(now);
        loading_ = false;
        b.doc->setModified(false);
        statusBar()->showMessage(tr("%1 changed on disk: reloaded").arg(name), 4000);
    } else {
        b.disk = now;   // ours stays; saving it replaces theirs
        statusBar()->showMessage(tr("%1 changed on disk, and here too: your text is kept (Revert File takes theirs)").arg(name));
    }
    refreshFileList();
}

bool UiEditorWindow::askToSave() {
    QStringList changed;
    for (const QString& n : buffers_.keys())
        if (isModified(n)) changed << n;
    if (changed.isEmpty() || quiet_) return true;
    const auto r = QMessageBox::question(this, tr("Unsaved changes"),
                                         tr("These files have changes that are not saved:\n%1\n\nSave them?").arg(changed.join(QLatin1Char('\n'))),
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Cancel) return false;
    return r == QMessageBox::Discard || saveAll();
}

void UiEditorWindow::closeEvent(QCloseEvent* e) {
    if (!askToSave()) { e->ignore(); return; }
    e->accept();
}

// ---- the preview ----

void UiEditorWindow::fillPreviewList() {
    preview_->clear();
    previewPaths_.clear();
    preview_->addItem(tr("Title screen"));
    previewPaths_ << QString();
    if (!tutorialFiles_.isEmpty()) {
        preview_->addItem(tr("Practice screen (tutorial)"));
        previewPaths_ << QStringLiteral("practice");
    }
    QDir d(scenariosDir_);
    for (const QString& name : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QFile f(d.filePath(name + QStringLiteral("/scenario.json")));
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QString scene = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("scene")).toString();
        preview_->addItem(tr("%1 — %2").arg(scene.isEmpty() ? tr("game") : scene, name));
        previewPaths_ << d.filePath(name);
    }
}

void UiEditorWindow::startPreviewAt(int index) {
    preview_->setCurrentIndex(index);
    startPreview(index);
}

void UiEditorWindow::startPreview(int index) {
    if (index < 0 || index >= previewPaths_.size()) return;
    practiceOn_ = previewPaths_[index] == QStringLiteral("practice");
    practiceDoc_ = nullptr;   // a restarted game has a new UI context: the old document went with the old one
    practiceCtx_ = nullptr;
    previewNow();
    if (practiceOn_) {   // over the map of a new game, so the HUD's values ({{hud.res_line}}) are real
        QString err;
        if (!view_->startScenario(QDir(scenariosDir_).filePath(QStringLiteral("stage_new_game")), err))
            statusBar()->showMessage(err);
        practiceTimer_.start();
        return;
    }
    if (previewPaths_[index].isEmpty()) {
        view_->restartGame();
        return;
    }
    QString err;
    if (!view_->startScenario(previewPaths_[index], err)) {
        if (quiet_) statusBar()->showMessage(err);
        else QMessageBox::warning(this, tr("Preview"), tr("The scenario could not start:\n%1").arg(err));
    }
}

void UiEditorWindow::loadPractice() {
    RmlUi* r = view_->session().rml();
    if (!practiceOn_) { practiceTimer_.stop(); return; }
    if (!r || !r->ready() || !view_->session().game() || practiceDoc_) return;   // not up yet: the timer tries again
    practiceTimer_.stop();
    Rml::Context* ctx = r->context();
    practiceDoc_ = ctx->LoadDocument(pathOf(QStringLiteral("practice.rml")).toStdString());
    practiceCtx_ = ctx;
    if (practiceDoc_) practiceDoc_->Show();
    else statusBar()->showMessage(tr("practice.rml could not be loaded: see Problems"));
}

void UiEditorWindow::openPracticeScreen() {
    const int i = previewPaths_.indexOf(QStringLiteral("practice"));
    if (i < 0) {
        statusBar()->showMessage(tr("The practice screen (docs/examples/ui_tutorial) is not in this copy of the editor's repo"));
        return;
    }
    startPreviewAt(i);
    openFile(QStringLiteral("practice.rcss"));
}

void UiEditorWindow::openTutorial() {
    const QString doc = QDir(docsDir_).filePath(Language::current() == Language::Lang::Chinese ? QStringLiteral("zh_TW/37_UI_EDITOR.md")
                                                                                                : QStringLiteral("37_UI_EDITOR.md"));
    if (!QFileInfo::exists(doc) || !QDesktopServices::openUrl(QUrl::fromLocalFile(doc))) {
        const QString msg = tr("The tutorial is docs/37_UI_EDITOR.md in the TOMS repo; it could not be opened from here.");
        if (quiet_) statusBar()->showMessage(msg);
        else QMessageBox::information(this, tr("UI Editor Tutorial"), msg);
    }
}

void UiEditorWindow::fillLanguages() {
    Game* g = view_->session().game();
    if (!g) return;
    langTimer_.stop();
    const QSignalBlocker block(lang_);
    lang_->clear();
    for (const auto& li : g->locale().languages())
        lang_->addItem(QStringLiteral("%1  (%2)").arg(QString::fromStdString(li.name), QString::fromStdString(li.code)),
                       QString::fromStdString(li.code));
    lang_->setCurrentIndex(lang_->findData(QString::fromStdString(g->locale().languageCode())));
}

// ---- problems and the inspector ----

void UiEditorWindow::addProblem(int level, const QString& message) {
    const uied::Problem p = uied::parseProblem(level, message);
    QString where = p.file.isEmpty() ? QString() : p.line ? QStringLiteral("%1:%2  ").arg(p.file).arg(p.line) : p.file + QStringLiteral("  ");
    auto* it = new QListWidgetItem((level >= 2 ? QStringLiteral("⛔ ") : QStringLiteral("⚠ ")) + where + p.text);
    problems_->addItem(it);
    problemTargets_ << (p.file.isEmpty() ? QString() : QStringLiteral("%1:%2").arg(p.file).arg(p.line));
}

int UiEditorWindow::problemCount() const { return problems_->count(); }

int UiEditorWindow::problemAt(const QString& file) const {
    for (const QString& t : problemTargets_)
        if (t.startsWith(file + QLatin1Char(':'))) return t.section(QLatin1Char(':'), 1).toInt();
    return 0;
}

void UiEditorWindow::openProblem(int row) {
    if (row < 0 || row >= problemTargets_.size() || problemTargets_[row].isEmpty()) return;
    const QStringList parts = problemTargets_[row].split(QLatin1Char(':'));
    if (openFile(parts[0]) && parts.size() > 1 && parts[1].toInt() > 0) edit_->goToLine(parts[1].toInt());
}

void UiEditorWindow::openRule(int row) {
    if (row < 0 || row >= ruleTargets_.size()) return;
    const QStringList parts = ruleTargets_[row].split(QLatin1Char(':'));
    if (parts.size() == 2 && openFile(parts[0])) edit_->goToLine(parts[1].toInt());
}

void UiEditorWindow::inspectNow() {
    rules_->clear();
    ruleTargets_.clear();
    RmlUi* r = view_->session().rml();
    Rml::Context* ctx = r ? r->context() : nullptr;
    Rml::Element* el = ctx ? ctx->GetHoverElement() : nullptr;
    if (!el || el == el->GetOwnerDocument()) {
        inspectLabel_->setText(tr("Nothing under the mouse: point at a part of the screen and click again."));
        return;
    }
    auto describe = [](Rml::Element* e) {
        QString s = QString::fromStdString(e->GetTagName());
        if (!e->GetId().empty()) s += QLatin1Char('#') + QString::fromStdString(e->GetId());
        for (const QString& c : QString::fromStdString(e->GetClassNames()).split(QLatin1Char(' '), Qt::SkipEmptyParts))
            s += QLatin1Char('.') + c;
        return s;
    };
    QStringList chain;
    for (Rml::Element* e = el; e && e != el->GetOwnerDocument(); e = e->GetParentNode()) chain.prepend(describe(e));
    const QString docFile = QFileInfo(QString::fromStdString(el->GetOwnerDocument()->GetSourceURL())).fileName();
    inspectLabel_->setText(tr("<b>%1</b> in %2<br>%3").arg(describe(el).toHtmlEscaped(), docFile, chain.join(QStringLiteral(" › ")).toHtmlEscaped()));

    // its line in the .rml: its id, else the first element like it, else the nearest parent with an id
    if (openFile(docFile)) {
        const QString rml = text(docFile);
        int line = 0;
        for (Rml::Element* e = el; e && !line && e != el->GetOwnerDocument(); e = e->GetParentNode()) {
            line = uied::lineOfId(rml, QString::fromStdString(e->GetId()));
            if (!line && e == el)
                line = uied::lineOfTagWithClasses(rml, QString::fromStdString(e->GetTagName()),
                                                   QString::fromStdString(e->GetClassNames()).split(QLatin1Char(' '), Qt::SkipEmptyParts));
        }
        if (line) edit_->goToLine(line);
        // the rules of the sheets this document links that name it
        const QString id = QString::fromStdString(el->GetId());
        const QStringList classes = QString::fromStdString(el->GetClassNames()).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const QString tag = QString::fromStdString(el->GetTagName());
        for (const QString& href : uied::linkedStyleSheets(rml)) {
            const QString sheet = QFileInfo(href).fileName();
            const QString css = buffers_.contains(sheet) ? text(sheet) : readDisk(sheet);
            for (const uied::RuleHit& h : uied::rulesFor(sheet, css, id, classes, tag)) {
                rules_->addItem(QStringLiteral("%1:%2   %3").arg(h.file).arg(h.line).arg(h.text));
                ruleTargets_ << QStringLiteral("%1:%2").arg(h.file).arg(h.line);
            }
        }
        if (rules_->count() == 0) rules_->addItem(tr("(no rule names it: it takes only what it inherits)"));
    }
}

QString UiEditorWindow::inspectSummary() const { return inspectLabel_->text(); }
int UiEditorWindow::inspectRuleCount() const { return ruleTargets_.size(); }

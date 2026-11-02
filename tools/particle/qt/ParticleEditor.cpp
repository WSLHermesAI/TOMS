#include "ParticleEditor.h"

#include "EffectsDock.h"
#include "FxSpritesDock.h"
#include "AutoBackup.h"
#include "Icons.h"
#include "InspectorDock.h"
#include "ParticleDocument.h"
#include "ParticlePlayback.h"
#include "ParticleViewport.h"
#include "TimelineDock.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QUndoStack>

using toms::fx::Effect;
using toms::fx::Emitter;

namespace {

constexpr int g_iMaxRecent = 10;
bool f_SelftestRunning() { return qApp && qApp->property("toms.selftest").toBool(); }
QString f_Qs(const std::string& l_strS) { return QString::fromStdString(l_strS); }

toms::fx::ParticleFile f_LoadPresets()
{
    toms::fx::ParticleFile l_oF;
    QFile l_oIn(QStringLiteral(":/presets/fx_recipes.particle"));
    if (l_oIn.open(QIODevice::ReadOnly)) toms::fx::f_ParseParticles(l_oIn.readAll().toStdString(), l_oF);
    return l_oF;
}

}  // namespace

QSettings ParticleEditor::f_Settings() { return QSettings(QStringLiteral("TOMS"), QStringLiteral("ParticleEditor")); }

bool ParticleEditor::f_GameRendererWanted()
{
    const QString l_oPlatform = QGuiApplication::platformName();
    if (l_oPlatform == QLatin1String("offscreen") || l_oPlatform == QLatin1String("minimal")) return false;
    return f_Settings().value(QStringLiteral("preview/gameRenderer"), true).toBool();
}

void ParticleEditor::f_SetGpuThreshold(int l_iN)
{
    m_pPlay->f_SetGpuRenderer(m_pPlay->f_GpuRenderer(), l_iN);
    if (m_pGpuThreshold && m_pGpuThreshold->value() != l_iN) {
        const QSignalBlocker l_oB(m_pGpuThreshold);
        m_pGpuThreshold->setValue(l_iN);
    }
}

ParticleEditor::ParticleEditor(QMainWindow* l_pHost)
    : QObject(l_pHost)
    , m_pHost(l_pHost)
    , m_pDoc(new ParticleDocument(this))
    , m_pPlay(new ParticlePlayback(m_pDoc, this))
    , m_pViewport(new ParticleViewport(m_pDoc, m_pPlay, f_GameRendererWanted()))
{
    m_pDoc->f_NewFile();
    connect(m_pDoc, &ParticleDocument::f_FileChanged, this, &ParticleEditor::f_TitleChanged);
    connect(m_pDoc, &ParticleDocument::f_Message, this, [this](const QString& l_oT) {
        m_pHost->statusBar()->showMessage(l_oT, 6000);
        if (m_pProblems) m_pProblems->addItem(new QListWidgetItem(Icons::f_Icon(Icons::Id::Info), l_oT));
    });
}

void ParticleEditor::f_Install()
{
    f_CreateActions();
    m_pHost->setCentralWidget(m_pViewport);
    f_CreateDocks();
    f_CreateMenus();
    f_CreateToolBars();
    connect(m_pViewport, &ParticleViewport::f_SpritesDropped, this, &ParticleEditor::f_AddEmitterFromSprites);
    connect(m_pDoc, &ParticleDocument::f_FileChanged, this, &ParticleEditor::f_RefreshProblems);
    connect(m_pDoc, &ParticleDocument::f_AtlasChanged, this, &ParticleEditor::f_RefreshProblems);
    connect(m_pPlay, &ParticlePlayback::f_StateChanged, this, &ParticleEditor::f_UpdatePlayButton);
    connect(m_pViewport, &ParticleViewport::f_RendererChanged, this, [this] {
        m_pHost->statusBar()->showMessage(tr("Preview: %1").arg(m_pViewport->f_RendererName()), 8000);
        if (m_pProblems) m_pProblems->addItem(new QListWidgetItem(Icons::f_Icon(Icons::Id::Info), tr("Preview: %1").arg(m_pViewport->f_RendererName())));
    });
    f_RefreshProblems();
    f_UpdatePlayButton();
}

QString ParticleEditor::f_Title() const { return m_pDoc->f_DisplayName() + QStringLiteral("[*] - Particle Editor"); }

void ParticleEditor::f_CreateActions()
{
    auto l_fnMake = [this](const QString& l_oText, const QKeySequence& l_oKey, Icons::Id l_eIcon, auto l_oSlot) {
        auto* l_pA = new QAction(Icons::f_Icon(l_eIcon), l_oText, this);
        if (!l_oKey.isEmpty()) l_pA->setShortcut(l_oKey);
        connect(l_pA, &QAction::triggered, this, l_oSlot);
        return l_pA;
    };
    m_pNewAct = l_fnMake(tr("&New"), QKeySequence::New, Icons::Id::New, [this] {
        if (f_MaybeSave()) m_pDoc->f_NewFile();
    });
    m_pOpenAct = l_fnMake(tr("&Open…"), QKeySequence::Open, Icons::Id::Open, [this] {
        if (!f_MaybeSave()) return;
        const QString l_oPath = QFileDialog::getOpenFileName(m_pHost, tr("Open Particle Effects"), f_Settings().value(QStringLiteral("lastDir")).toString(),
                                                          tr("Particle effects (*.particle);;All files (*)"));
        if (!l_oPath.isEmpty()) f_OpenFile(l_oPath);
    });
    m_pSaveAct = l_fnMake(tr("&Save"), QKeySequence::Save, Icons::Id::Save, [this] { f_Save(); });
    m_pSaveAsAct = l_fnMake(tr("Save &As…"), QKeySequence::SaveAs, Icons::Id::Save, [this] { f_SaveAs(); });
    m_pAddAtlasAct = l_fnMake(tr("Add &Atlas…"), QKeySequence(), Icons::Id::Image, [this] { f_OnAddAtlas(); });
    m_pQuitAct = l_fnMake(tr("&Quit"), QKeySequence::Quit, Icons::Id::Back, [this] { m_pHost->close(); });
    m_pUndoAct = m_pDoc->f_UndoStack()->createUndoAction(this, tr("&Undo"));
    m_pUndoAct->setShortcut(QKeySequence::Undo);
    m_pRedoAct = m_pDoc->f_UndoStack()->createRedoAction(this, tr("&Redo"));
    m_pRedoAct->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)});

    m_pPlayAct = l_fnMake(tr("Play"), QKeySequence(Qt::Key_Space), Icons::Id::Play, [this] { m_pPlay->f_TogglePlay(); });
    m_pPlayAct->setShortcutContext(Qt::WidgetWithChildrenShortcut);   // Space in a text field still types
    m_pRestartAct = l_fnMake(tr("Restart"), QKeySequence(Qt::Key_R), Icons::Id::Back, [this] { m_pPlay->f_Restart(); });
    m_pRestartAct->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_pStepAct = l_fnMake(tr("Step"), QKeySequence(Qt::Key_Period), Icons::Id::Forward, [this] { m_pPlay->f_StepFrame(); });
    m_pStepAct->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_pAutoRestartAct = l_fnMake(tr("Replay one-shots"), QKeySequence(), Icons::Id::Loop, [this](bool l_bOn) { m_pPlay->f_SetAutoRestart(l_bOn); });
    m_pAutoRestartAct->setCheckable(true);
    m_pAutoRestartAct->setChecked(true);
    m_pAutoRestartAct->setToolTip(tr("A one-shot effect plays again 0.5 s after it ends"));
    m_pGridAct = l_fnMake(tr("Grid"), QKeySequence(Qt::Key_G), Icons::Id::Grid, [this](bool l_bOn) { m_pViewport->f_SetGridVisible(l_bOn); });
    m_pGridAct->setCheckable(true);
    m_pCenterAct = l_fnMake(tr("Back to 0,0"), QKeySequence(Qt::Key_Home), Icons::Id::Fit, [this] { m_pPlay->f_SetOrigin(QPointF(0, 0), true); });
    m_pFitAct = l_fnMake(tr("Fit View"), QKeySequence(Qt::Key_F), Icons::Id::Fit, [this] { m_pViewport->f_FitToView(); });
    m_pDiceAct = l_fnMake(tr("New Seed"), QKeySequence(), Icons::Id::Duplicate, [this] { m_pPlay->f_RandomizeSeed(); });
    m_pDiceAct->setToolTip(tr("Another random seed: the same effect with other particles"));
    for (QAction* l_pA : {m_pPlayAct, m_pRestartAct, m_pStepAct, m_pGridAct, m_pFitAct, m_pCenterAct}) m_pViewport->addAction(l_pA);
}

void ParticleEditor::f_CreateDocks()
{
    m_pEffects = new EffectsDock(m_pDoc, m_pPlay, m_pHost);
    m_pInspector = new InspectorDock(m_pDoc, m_pHost);
    m_pTimeline = new TimelineDock(m_pDoc, m_pPlay, m_pHost);
    m_pSprites = new FxSpritesDock(m_pDoc, m_pHost);
    m_pHistory = f_CreateHistoryDock(m_pDoc->f_UndoStack(), m_pHost);
    m_pBackup = new AutoBackup(m_pDoc->f_UndoStack(), QStringLiteral(".particle"), [this] { return m_pDoc->f_FilePath(); },
                              [this](const QString& l_oP, QString* l_pE) { return m_pDoc->f_WriteBackup(l_oP, l_pE); }, this);
    connect(m_pBackup, &AutoBackup::f_Message, this, [this](const QString& l_oT) { m_pHost->statusBar()->showMessage(l_oT, 5000); });
    m_pProblemsDock = new QDockWidget(tr("Problems / Log"), m_pHost);
    m_pProblemsDock->setObjectName(QStringLiteral("ParticleProblemsDock"));
    m_pProblems = new QListWidget(m_pProblemsDock);
    m_pProblems->setToolTip(tr("Double-click a problem to select its effect / emitter"));
    m_pProblemsDock->setWidget(m_pProblems);
    connect(m_pSprites, &FxSpritesDock::f_AddAtlasRequested, this, &ParticleEditor::f_OnAddAtlas);
    connect(m_pProblems, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* l_pIt) {
        const QString l_oWhere = l_pIt->data(Qt::UserRole).toString();   // "effect/emitter"
        const QString l_oEffect = l_oWhere.section(QLatin1Char('/'), 0, 0), l_oEmitter = l_oWhere.section(QLatin1Char('/'), 1);
        const auto& l_vEs = m_pDoc->f_File().m_vEffects;
        for (int l_iI = 0; l_iI < int(l_vEs.size()); l_iI++) {
            if (f_Qs(l_vEs[size_t(l_iI)].m_strName) != l_oEffect) continue;
            int l_iEm = -1;
            for (int l_iJ = 0; l_iJ < int(l_vEs[size_t(l_iI)].m_vEmitters.size()); l_iJ++)
                if (f_Qs(l_vEs[size_t(l_iI)].m_vEmitters[size_t(l_iJ)].m_strName) == l_oEmitter) l_iEm = l_iJ;
            m_pDoc->f_Select(l_iI, l_iEm);
            return;
        }
    });

    m_pHost->addDockWidget(Qt::LeftDockWidgetArea, m_pEffects);
    m_pHost->addDockWidget(Qt::LeftDockWidgetArea, m_pSprites);
    m_pHost->addDockWidget(Qt::RightDockWidgetArea, m_pInspector);
    m_pHost->tabifyDockWidget(m_pInspector, m_pHistory);
    m_pInspector->raise();
    m_pHost->addDockWidget(Qt::BottomDockWidgetArea, m_pTimeline);
    m_pHost->addDockWidget(Qt::BottomDockWidgetArea, m_pProblemsDock);
    m_pHost->tabifyDockWidget(m_pTimeline, m_pProblemsDock);
    m_pTimeline->raise();
    m_pHost->resizeDocks({m_pEffects, m_pInspector}, {240, 380}, Qt::Horizontal);
    m_pHost->resizeDocks({m_pEffects, m_pSprites}, {300, 300}, Qt::Vertical);
    m_pHost->resizeDocks({m_pTimeline}, {140}, Qt::Vertical);
}

void ParticleEditor::f_CreateMenus()
{
    QMenu* l_pFile = m_pHost->menuBar()->addMenu(tr("&File"));
    l_pFile->addAction(m_pNewAct);
    l_pFile->addAction(m_pOpenAct);
    m_pRecentMenu = l_pFile->addMenu(tr("Open &Recent"));
    l_pFile->addSeparator();
    l_pFile->addAction(m_pSaveAct);
    l_pFile->addAction(m_pSaveAsAct);
    l_pFile->addSeparator();
    l_pFile->addAction(m_pAddAtlasAct);
    l_pFile->addSeparator();
    m_pBackup->f_AddMenu(l_pFile, m_pHost);
    l_pFile->addSeparator();
    l_pFile->addAction(m_pQuitAct);
    f_RebuildRecentMenu();

    QMenu* l_pEdit = m_pHost->menuBar()->addMenu(tr("&Edit"));
    l_pEdit->addAction(m_pUndoAct);
    l_pEdit->addAction(m_pRedoAct);

    QMenu* l_pEffect = m_pHost->menuBar()->addMenu(tr("E&ffect"));
    l_pEffect->addAction(Icons::f_Icon(Icons::Id::Add), tr("Add &Effect"), m_pEffects, &EffectsDock::f_AddEffect);
    l_pEffect->addAction(Icons::f_Icon(Icons::Id::Node), tr("Add E&mitter"), QKeySequence(Qt::CTRL | Qt::Key_E), m_pEffects, &EffectsDock::f_AddEmitter);
    l_pEffect->addAction(Icons::f_Icon(Icons::Id::Duplicate), tr("&Duplicate"), QKeySequence(Qt::CTRL | Qt::Key_D), m_pEffects, &EffectsDock::f_DuplicateSelected);
    l_pEffect->addAction(Icons::f_Icon(Icons::Id::Remove), tr("De&lete"), m_pEffects, &EffectsDock::f_DeleteSelected);
    l_pEffect->addSeparator();
    m_pPresetMenu = l_pEffect->addMenu(Icons::f_Icon(Icons::Id::Import), tr("Add &Preset"));
    for (const QString& l_oName : f_PresetNames()) m_pPresetMenu->addAction(l_oName, this, [this, l_oName] { f_AddPreset(l_oName); });

    QMenu* l_pPlay = m_pHost->menuBar()->addMenu(tr("&Play"));
    for (QAction* l_pA : {m_pPlayAct, m_pRestartAct, m_pStepAct, m_pAutoRestartAct, m_pDiceAct, m_pCenterAct}) l_pPlay->addAction(l_pA);

    m_pViewMenu = m_pHost->menuBar()->addMenu(tr("&View"));
    m_pViewMenu->addAction(m_pFitAct);
    m_pViewMenu->addAction(m_pGridAct);
    CanvasView::f_AddCoordinatesAction(m_pViewMenu);
    m_pGameRendererAct = m_pViewMenu->addAction(tr("Preview with the Game Renderer (after a restart)"));
    m_pGameRendererAct->setCheckable(true);
    m_pGameRendererAct->setChecked(f_Settings().value(QStringLiteral("preview/gameRenderer"), true).toBool());
    m_pGameRendererAct->setToolTip(tr("Draw the preview with toms_game's renderer (bgfx): the same pixels and GPU particle simulation "
                                     "as the game. Off: QPainter, simulated on the CPU."));
    connect(m_pGameRendererAct, &QAction::toggled, this, [this](bool l_bOn) {
        f_Settings().setValue(QStringLiteral("preview/gameRenderer"), l_bOn);
        m_pHost->statusBar()->showMessage(tr("The preview renderer changes the next time the editor starts."), 8000);
    });
    m_pViewMenu->addSeparator();
    for (QDockWidget* l_pD : {static_cast<QDockWidget*>(m_pEffects), static_cast<QDockWidget*>(m_pSprites), static_cast<QDockWidget*>(m_pInspector),
                           static_cast<QDockWidget*>(m_pTimeline), m_pProblemsDock, m_pHistory})
        m_pViewMenu->addAction(l_pD->toggleViewAction());
    m_pViewMenu->addSeparator();

    QMenu* l_pHelp = m_pHost->menuBar()->addMenu(tr("&Help"));
    l_pHelp->addAction(tr("&Command Line…"), this, [this] {
        QMessageBox::information(m_pHost, tr("Command Line"),
                                 tr("<pre>particle_editor [file.particle]\n"
                                    "particle_editor --headless check file.particle\n"
                                    "particle_editor --headless render file.particle#effect out.png [frames] [every]\n"
                                    "particle_editor --selftest file.particle outdir</pre>"
                                    "In the game: <code>toms_game --fx=file.particle#effect</code>. See docs/engine/07_PARTICLES.md."));
    });
}

void ParticleEditor::f_CreateToolBars()
{
    QToolBar* l_pMain = m_pHost->addToolBar(tr("File"));
    l_pMain->setObjectName(QStringLiteral("ParticleFileBar"));
    for (QAction* l_pA : {m_pNewAct, m_pOpenAct, m_pSaveAct}) l_pMain->addAction(l_pA);
    l_pMain->addSeparator();
    l_pMain->addAction(m_pUndoAct);
    l_pMain->addAction(m_pRedoAct);

    QToolBar* l_pT = m_pHost->addToolBar(tr("Playback"));
    l_pT->setObjectName(QStringLiteral("ParticlePlaybackBar"));
    l_pT->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    for (QAction* l_pA : {m_pPlayAct, m_pRestartAct, m_pStepAct, m_pAutoRestartAct}) l_pT->addAction(l_pA);
    l_pT->addSeparator();
    l_pT->addWidget(new QLabel(tr(" Speed ")));
    m_pSpeed = new QComboBox;
    for (const char* l_pszS : {"0.1×", "0.25×", "0.5×", "1×", "2×"}) m_pSpeed->addItem(QString::fromUtf8(l_pszS));
    m_pSpeed->setCurrentIndex(3);
    connect(m_pSpeed, &QComboBox::currentIndexChanged, this, [this](int l_iI) {
        static const float l_arrSpeeds[] = {0.1f, 0.25f, 0.5f, 1.0f, 2.0f};
        m_pPlay->f_SetSpeed(l_arrSpeeds[l_iI]);
    });
    l_pT->addWidget(m_pSpeed);
    l_pT->addWidget(new QLabel(tr("  Seed ")));
    m_pSeed = new QSpinBox;
    m_pSeed->setRange(1, 999999);
    m_pSeed->setValue(int(m_pPlay->f_Seed()));
    m_pSeed->setToolTip(tr("The preview's seed: the same seed gives the same particles"));
    connect(m_pSeed, &QSpinBox::valueChanged, this, [this](int l_iV) {
        if (uint32_t(l_iV) != m_pPlay->f_Seed()) m_pPlay->f_SetSeed(uint32_t(l_iV));
    });
    l_pT->addWidget(m_pSeed);
    l_pT->addAction(m_pDiceAct);
    l_pT->addWidget(new QLabel(tr("  GPU sim above ")));
    m_pGpuThreshold = new QSpinBox;
    m_pGpuThreshold->setRange(0, 10000000);
    m_pGpuThreshold->setSingleStep(1000);
    m_pGpuThreshold->setSpecialValueText(tr("never"));
    m_pGpuThreshold->setValue(m_pPlay->f_GpuThreshold());
    m_pGpuThreshold->setToolTip(tr("Like the game setting particleGpuThreshold (desktop 5000, phones 3000): \"auto\" emitters with "
                                  "more particles than this are simulated on the GPU. Only with the game renderer."));
    connect(m_pGpuThreshold, &QSpinBox::valueChanged, this, [this](int l_iV) { f_SetGpuThreshold(l_iV); });
    l_pT->addWidget(m_pGpuThreshold);
    l_pT->addSeparator();
    l_pT->addWidget(new QLabel(tr(" Background ")));
    m_pBackground = new QComboBox;
    m_pBackground->addItems({tr("Checker"), tr("Dark"), tr("Light"), tr("Picture…")});
    connect(m_pBackground, &QComboBox::activated, this, [this](int l_iI) {
        if (l_iI < 3) {
            m_pViewport->f_SetBackground(ParticleViewport::Background(l_iI));
            return;
        }
        const QString l_oPath = QFileDialog::getOpenFileName(m_pHost, tr("Background Picture"), f_Settings().value(QStringLiteral("lastDir")).toString(),
                                                          tr("Images (*.png *.jpg *.bmp)"));
        if (l_oPath.isEmpty() || !m_pViewport->f_SetBackgroundImage(l_oPath)) m_pBackground->setCurrentIndex(int(m_pViewport->f_Background()));
    });
    l_pT->addWidget(m_pBackground);
    l_pT->addAction(m_pGridAct);
    l_pT->addAction(m_pCenterAct);
}

void ParticleEditor::f_UpdatePlayButton()
{
    m_pPlayAct->setText(m_pPlay->f_Playing() ? tr("Pause") : tr("Play"));
    m_pPlayAct->setIcon(Icons::f_Icon(m_pPlay->f_Playing() ? Icons::Id::Pause : Icons::Id::Play));
    if (m_pSeed && uint32_t(m_pSeed->value()) != m_pPlay->f_Seed()) {
        const QSignalBlocker l_oB(m_pSeed);
        m_pSeed->setValue(int(m_pPlay->f_Seed()));
    }
}

void ParticleEditor::f_RefreshProblems()
{
    if (!m_pProblems) return;
    // Keep the log lines (no "where"); replace the problems.
    for (int l_iI = m_pProblems->count() - 1; l_iI >= 0; l_iI--)
        if (!m_pProblems->item(l_iI)->data(Qt::UserRole).toString().isEmpty() || m_pProblems->item(l_iI)->data(Qt::UserRole + 1).toBool())
            delete m_pProblems->takeItem(l_iI);
    int l_iRow = 0, l_iErrors = 0;
    for (const toms::fx::Problem& l_oP : m_pDoc->f_Problems()) {
        auto* l_pIt = new QListWidgetItem(Icons::f_Icon(l_oP.m_bWarning ? Icons::Id::Warning : Icons::Id::Error),
                                       QStringLiteral("%1: %2").arg(f_Qs(l_oP.m_strWhere), f_Qs(l_oP.m_strText)));
        l_pIt->setData(Qt::UserRole, f_Qs(l_oP.m_strWhere));
        l_pIt->setData(Qt::UserRole + 1, true);
        m_pProblems->insertItem(l_iRow++, l_pIt);
        l_iErrors += !l_oP.m_bWarning;
    }
    m_pProblemsDock->setWindowTitle(l_iRow ? tr("Problems (%1) / Log").arg(l_iRow) : tr("Problems / Log"));
    if (l_iErrors && !f_SelftestRunning()) m_pProblemsDock->raise();
}

bool ParticleEditor::f_OpenFile(const QString& l_oPath)
{
    QString l_oErr;
    if (!m_pDoc->f_Open(l_oPath, &l_oErr)) {
        if (!f_SelftestRunning()) QMessageBox::warning(m_pHost, tr("Open"), tr("Could not open %1:\n\n%2").arg(QDir::toNativeSeparators(l_oPath), l_oErr));
        m_pDoc->f_Message(tr("Open %1: %2").arg(QDir::toNativeSeparators(l_oPath), l_oErr));
        return false;
    }
    m_pDoc->f_Message(tr("Opened %1").arg(QDir::toNativeSeparators(l_oPath)));
    f_AddRecentFile(l_oPath);
    m_pPlay->f_Restart();
    return true;
}

bool ParticleEditor::f_MaybeSave()
{
    if (!m_pDoc->f_IsDirty() || f_SelftestRunning()) return true;
    const auto l_eR = QMessageBox::warning(m_pHost, tr("Particle Editor"), tr("%1 has unsaved changes. Save them?").arg(m_pDoc->f_DisplayName()),
                                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (l_eR == QMessageBox::Save) return f_Save();
    return l_eR == QMessageBox::Discard;
}

bool ParticleEditor::f_Save() { return m_pDoc->f_FilePath().isEmpty() ? f_SaveAs() : f_SaveTo(m_pDoc->f_FilePath()); }

bool ParticleEditor::f_SaveAs()
{
    QString l_oStart = m_pDoc->f_FilePath();
    if (l_oStart.isEmpty()) l_oStart = QDir(f_Settings().value(QStringLiteral("lastDir")).toString()).filePath(QStringLiteral("effects.particle"));
    const QString l_oPath = QFileDialog::getSaveFileName(m_pHost, tr("Save Particle Effects"), l_oStart, tr("Particle effects (*.particle)"));
    return !l_oPath.isEmpty() && f_SaveTo(l_oPath);
}

bool ParticleEditor::f_SaveTo(const QString& l_oPath)
{
    QString l_oErr;
    if (!m_pDoc->f_SaveTo(l_oPath, &l_oErr)) {
        if (!f_SelftestRunning()) QMessageBox::warning(m_pHost, tr("Save"), tr("Could not save %1:\n\n%2").arg(QDir::toNativeSeparators(l_oPath), l_oErr));
        m_pDoc->f_Message(tr("Save %1: %2").arg(QDir::toNativeSeparators(l_oPath), l_oErr));
        return false;
    }
    m_pDoc->f_Message(tr("Saved %1").arg(QDir::toNativeSeparators(l_oPath)));
    f_AddRecentFile(l_oPath);
    return true;
}

void ParticleEditor::f_OnAddAtlas()
{
    const QString l_oStart = m_pDoc->f_AtlasCount() ? m_pDoc->f_AtlasAt(m_pDoc->f_AtlasCount() - 1).m_oPath : f_Settings().value(QStringLiteral("lastDir")).toString();
    const QStringList l_oPaths = QFileDialog::getOpenFileNames(m_pHost, tr("Add Atlas"), l_oStart, tr("Sprite atlases (*.atlas);;All files (*)"));
    for (const QString& l_oP : l_oPaths) {
        QString l_oErr;
        if (!m_pDoc->f_AddAtlas(l_oP, &l_oErr)) m_pDoc->f_Message(tr("Add atlas: %1").arg(l_oErr));
    }
}

QStringList ParticleEditor::f_PresetNames() const
{
    QStringList l_oOut;
    for (const Effect& l_oE : f_LoadPresets().m_vEffects) l_oOut << f_Qs(l_oE.m_strName);
    return l_oOut;
}

bool ParticleEditor::f_AddPreset(const QString& l_oName)
{
    const toms::fx::ParticleFile l_oPresets = f_LoadPresets();
    const Effect* l_pE = l_oPresets.f_Find(l_oName.toStdString());
    if (!l_pE) return false;
    // The atlases its sprites name ("fx:dot", "game:coin"): the TOMS ones next to the fx atlas.
    QStringList l_oNeed;
    for (const Emitter& l_oM : l_pE->m_vEmitters) {
        std::vector<std::string> l_vRefs = l_oM.m_vFrames;
        l_vRefs.push_back(l_oM.m_strSprite);
        for (const std::string& l_strR : l_vRefs) {
            const QString l_oId = f_Qs(toms::anim::f_ParseSpriteRef(l_strR).m_strAtlas);
            if (!l_oId.isEmpty() && m_pDoc->f_AtlasIndexOf(l_oId) < 0 && !l_oNeed.contains(l_oId)) l_oNeed << l_oId;
        }
    }
    const QString l_oFxAtlas = ParticleDocument::f_DefaultFxAtlas();
    for (const QString& l_oId : l_oNeed) {
        const QString l_oPath = l_oFxAtlas.isEmpty() ? QString() : QFileInfo(l_oFxAtlas).absoluteDir().filePath(l_oId + QStringLiteral(".atlas"));
        QString l_oErr;
        if (l_oPath.isEmpty() || !QFileInfo::exists(l_oPath)) m_pDoc->f_Message(tr("Preset %1: no atlas '%2' found; add it with File > Add Atlas").arg(l_oName, l_oId));
        else if (!m_pDoc->f_AddAtlas(l_oPath, &l_oErr)) m_pDoc->f_Message(tr("Preset %1: %2").arg(l_oName, l_oErr));
    }
    const bool l_bOk = m_pDoc->f_AddEffect(*l_pE);
    if (l_bOk) m_pPlay->f_Restart();
    return l_bOk;
}

void ParticleEditor::f_AddEmitterFromSprites(const QStringList& l_oRefs, const QPointF& l_oPos)
{
    if (l_oRefs.isEmpty()) return;
    if (!m_pDoc->f_Effect()) m_pEffects->f_AddEffect();
    Emitter l_oM;
    l_oM.m_strName = toms::anim::f_ParseSpriteRef(l_oRefs.first().toStdString()).m_strName;
    l_oM.m_v2Offset = glm::vec2(float(std::round(l_oPos.x())), float(std::round(l_oPos.y())));
    if (l_oRefs.size() == 1) l_oM.m_strSprite = l_oRefs.first().toStdString();
    else
        for (const QString& l_oR : l_oRefs) l_oM.m_vFrames.push_back(l_oR.toStdString());
    m_pDoc->f_AddEmitter(l_oM);
}

void ParticleEditor::f_AddRecentFile(const QString& l_oPath)
{
    if (f_SelftestRunning()) return;
    QSettings l_oS = f_Settings();
    QStringList l_oRecent = l_oS.value(QStringLiteral("recentFiles")).toStringList();
    const QString l_oAbs = QFileInfo(l_oPath).absoluteFilePath();
    l_oRecent.removeAll(l_oAbs);
    l_oRecent.prepend(l_oAbs);
    while (l_oRecent.size() > g_iMaxRecent) l_oRecent.removeLast();
    l_oS.setValue(QStringLiteral("recentFiles"), l_oRecent);
    l_oS.setValue(QStringLiteral("lastDir"), QFileInfo(l_oAbs).absolutePath());
    f_RebuildRecentMenu();
}

void ParticleEditor::f_RebuildRecentMenu()
{
    if (!m_pRecentMenu) return;
    m_pRecentMenu->clear();
    const QStringList l_oRecent = f_Settings().value(QStringLiteral("recentFiles")).toStringList();
    for (const QString& l_oF : l_oRecent)
        m_pRecentMenu->addAction(QDir::toNativeSeparators(l_oF), this, [this, l_oF] {
            if (f_MaybeSave()) f_OpenFile(l_oF);
        });
    m_pRecentMenu->setEnabled(!l_oRecent.isEmpty());
}

void ParticleEditor::f_ReadSettings()
{
    QSettings l_oS = f_Settings();
    m_pGridAct->setChecked(l_oS.value(QStringLiteral("grid"), false).toBool());
    m_pViewport->f_SetGridVisible(m_pGridAct->isChecked());
    const int l_iBg = l_oS.value(QStringLiteral("background"), 0).toInt();
    const QString l_oImg = l_oS.value(QStringLiteral("backgroundImage")).toString();
    if (l_iBg == int(ParticleViewport::Background::Image) && !l_oImg.isEmpty() && m_pViewport->f_SetBackgroundImage(l_oImg)) m_pBackground->setCurrentIndex(3);
    else if (l_iBg >= 0 && l_iBg < 3) {
        m_pViewport->f_SetBackground(ParticleViewport::Background(l_iBg));
        m_pBackground->setCurrentIndex(l_iBg);
    }
}

void ParticleEditor::f_WriteSettings() const
{
    QSettings l_oS = f_Settings();
    l_oS.setValue(QStringLiteral("grid"), m_pGridAct->isChecked());
    l_oS.setValue(QStringLiteral("background"), int(m_pViewport->f_Background()));
    l_oS.setValue(QStringLiteral("backgroundImage"), m_pViewport->f_BackgroundImagePath());
}

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

constexpr int kMaxRecent = 10;
bool selftestRunning() { return qApp && qApp->property("toms.selftest").toBool(); }
QString qs(const std::string& s) { return QString::fromStdString(s); }

toms::fx::ParticleFile loadPresets()
{
    toms::fx::ParticleFile f;
    QFile in(QStringLiteral(":/presets/fx_recipes.particle"));
    if (in.open(QIODevice::ReadOnly)) toms::fx::parseParticles(in.readAll().toStdString(), f);
    return f;
}

}  // namespace

QSettings ParticleEditor::settings() { return QSettings(QStringLiteral("TOMS"), QStringLiteral("ParticleEditor")); }

bool ParticleEditor::gameRendererWanted()
{
    const QString platform = QGuiApplication::platformName();
    if (platform == QLatin1String("offscreen") || platform == QLatin1String("minimal")) return false;
    return settings().value(QStringLiteral("preview/gameRenderer"), true).toBool();
}

void ParticleEditor::setGpuThreshold(int n)
{
    m_play->setGpuRenderer(m_play->gpuRenderer(), n);
    if (m_gpuThreshold && m_gpuThreshold->value() != n) {
        const QSignalBlocker b(m_gpuThreshold);
        m_gpuThreshold->setValue(n);
    }
}

ParticleEditor::ParticleEditor(QMainWindow* host)
    : QObject(host)
    , m_host(host)
    , m_doc(new ParticleDocument(this))
    , m_play(new ParticlePlayback(m_doc, this))
    , m_viewport(new ParticleViewport(m_doc, m_play, gameRendererWanted()))
{
    m_doc->newFile();
    connect(m_doc, &ParticleDocument::fileChanged, this, &ParticleEditor::titleChanged);
    connect(m_doc, &ParticleDocument::message, this, [this](const QString& t) {
        m_host->statusBar()->showMessage(t, 6000);
        if (m_problems) m_problems->addItem(new QListWidgetItem(Icons::icon(Icons::Id::Info), t));
    });
}

void ParticleEditor::install()
{
    createActions();
    m_host->setCentralWidget(m_viewport);
    createDocks();
    createMenus();
    createToolBars();
    connect(m_viewport, &ParticleViewport::spritesDropped, this, &ParticleEditor::addEmitterFromSprites);
    connect(m_doc, &ParticleDocument::fileChanged, this, &ParticleEditor::refreshProblems);
    connect(m_doc, &ParticleDocument::atlasChanged, this, &ParticleEditor::refreshProblems);
    connect(m_play, &ParticlePlayback::stateChanged, this, &ParticleEditor::updatePlayButton);
    connect(m_viewport, &ParticleViewport::rendererChanged, this, [this] {
        m_host->statusBar()->showMessage(tr("Preview: %1").arg(m_viewport->rendererName()), 8000);
        if (m_problems) m_problems->addItem(new QListWidgetItem(Icons::icon(Icons::Id::Info), tr("Preview: %1").arg(m_viewport->rendererName())));
    });
    refreshProblems();
    updatePlayButton();
}

QString ParticleEditor::title() const { return m_doc->displayName() + QStringLiteral("[*] - Particle Editor"); }

void ParticleEditor::createActions()
{
    auto make = [this](const QString& text, const QKeySequence& key, Icons::Id icon, auto slot) {
        auto* a = new QAction(Icons::icon(icon), text, this);
        if (!key.isEmpty()) a->setShortcut(key);
        connect(a, &QAction::triggered, this, slot);
        return a;
    };
    m_newAct = make(tr("&New"), QKeySequence::New, Icons::Id::New, [this] {
        if (maybeSave()) m_doc->newFile();
    });
    m_openAct = make(tr("&Open…"), QKeySequence::Open, Icons::Id::Open, [this] {
        if (!maybeSave()) return;
        const QString path = QFileDialog::getOpenFileName(m_host, tr("Open Particle Effects"), settings().value(QStringLiteral("lastDir")).toString(),
                                                          tr("Particle effects (*.particle);;All files (*)"));
        if (!path.isEmpty()) openFile(path);
    });
    m_saveAct = make(tr("&Save"), QKeySequence::Save, Icons::Id::Save, [this] { save(); });
    m_saveAsAct = make(tr("Save &As…"), QKeySequence::SaveAs, Icons::Id::Save, [this] { saveAs(); });
    m_addAtlasAct = make(tr("Add &Atlas…"), QKeySequence(), Icons::Id::Image, [this] { onAddAtlas(); });
    m_quitAct = make(tr("&Quit"), QKeySequence::Quit, Icons::Id::Back, [this] { m_host->close(); });
    m_undoAct = m_doc->undoStack()->createUndoAction(this, tr("&Undo"));
    m_undoAct->setShortcut(QKeySequence::Undo);
    m_redoAct = m_doc->undoStack()->createRedoAction(this, tr("&Redo"));
    m_redoAct->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)});

    m_playAct = make(tr("Play"), QKeySequence(Qt::Key_Space), Icons::Id::Play, [this] { m_play->togglePlay(); });
    m_playAct->setShortcutContext(Qt::WidgetWithChildrenShortcut);   // Space in a text field still types
    m_restartAct = make(tr("Restart"), QKeySequence(Qt::Key_R), Icons::Id::Back, [this] { m_play->restart(); });
    m_restartAct->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_stepAct = make(tr("Step"), QKeySequence(Qt::Key_Period), Icons::Id::Forward, [this] { m_play->stepFrame(); });
    m_stepAct->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_autoRestartAct = make(tr("Replay one-shots"), QKeySequence(), Icons::Id::Loop, [this](bool on) { m_play->setAutoRestart(on); });
    m_autoRestartAct->setCheckable(true);
    m_autoRestartAct->setChecked(true);
    m_autoRestartAct->setToolTip(tr("A one-shot effect plays again 0.5 s after it ends"));
    m_gridAct = make(tr("Grid"), QKeySequence(Qt::Key_G), Icons::Id::Grid, [this](bool on) { m_viewport->setGridVisible(on); });
    m_gridAct->setCheckable(true);
    m_centerAct = make(tr("Back to 0,0"), QKeySequence(Qt::Key_Home), Icons::Id::Fit, [this] { m_play->setOrigin(QPointF(0, 0), true); });
    m_fitAct = make(tr("Fit View"), QKeySequence(Qt::Key_F), Icons::Id::Fit, [this] { m_viewport->fitToView(); });
    m_diceAct = make(tr("New Seed"), QKeySequence(), Icons::Id::Duplicate, [this] { m_play->randomizeSeed(); });
    m_diceAct->setToolTip(tr("Another random seed: the same effect with other particles"));
    for (QAction* a : {m_playAct, m_restartAct, m_stepAct, m_gridAct, m_fitAct, m_centerAct}) m_viewport->addAction(a);
}

void ParticleEditor::createDocks()
{
    m_effects = new EffectsDock(m_doc, m_play, m_host);
    m_inspector = new InspectorDock(m_doc, m_host);
    m_timeline = new TimelineDock(m_doc, m_play, m_host);
    m_sprites = new FxSpritesDock(m_doc, m_host);
    m_history = createHistoryDock(m_doc->undoStack(), m_host);
    m_backup = new AutoBackup(m_doc->undoStack(), QStringLiteral(".particle"), [this] { return m_doc->filePath(); },
                              [this](const QString& p, QString* e) { return m_doc->writeBackup(p, e); }, this);
    connect(m_backup, &AutoBackup::message, this, [this](const QString& t) { m_host->statusBar()->showMessage(t, 5000); });
    m_problemsDock = new QDockWidget(tr("Problems / Log"), m_host);
    m_problemsDock->setObjectName(QStringLiteral("ParticleProblemsDock"));
    m_problems = new QListWidget(m_problemsDock);
    m_problems->setToolTip(tr("Double-click a problem to select its effect / emitter"));
    m_problemsDock->setWidget(m_problems);
    connect(m_sprites, &FxSpritesDock::addAtlasRequested, this, &ParticleEditor::onAddAtlas);
    connect(m_problems, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        const QString where = it->data(Qt::UserRole).toString();   // "effect/emitter"
        const QString effect = where.section(QLatin1Char('/'), 0, 0), emitter = where.section(QLatin1Char('/'), 1);
        const auto& es = m_doc->file().effects;
        for (int i = 0; i < int(es.size()); i++) {
            if (qs(es[size_t(i)].name) != effect) continue;
            int em = -1;
            for (int j = 0; j < int(es[size_t(i)].emitters.size()); j++)
                if (qs(es[size_t(i)].emitters[size_t(j)].name) == emitter) em = j;
            m_doc->select(i, em);
            return;
        }
    });

    m_host->addDockWidget(Qt::LeftDockWidgetArea, m_effects);
    m_host->addDockWidget(Qt::LeftDockWidgetArea, m_sprites);
    m_host->addDockWidget(Qt::RightDockWidgetArea, m_inspector);
    m_host->tabifyDockWidget(m_inspector, m_history);
    m_inspector->raise();
    m_host->addDockWidget(Qt::BottomDockWidgetArea, m_timeline);
    m_host->addDockWidget(Qt::BottomDockWidgetArea, m_problemsDock);
    m_host->tabifyDockWidget(m_timeline, m_problemsDock);
    m_timeline->raise();
    m_host->resizeDocks({m_effects, m_inspector}, {240, 380}, Qt::Horizontal);
    m_host->resizeDocks({m_effects, m_sprites}, {300, 300}, Qt::Vertical);
    m_host->resizeDocks({m_timeline}, {140}, Qt::Vertical);
}

void ParticleEditor::createMenus()
{
    QMenu* file = m_host->menuBar()->addMenu(tr("&File"));
    file->addAction(m_newAct);
    file->addAction(m_openAct);
    m_recentMenu = file->addMenu(tr("Open &Recent"));
    file->addSeparator();
    file->addAction(m_saveAct);
    file->addAction(m_saveAsAct);
    file->addSeparator();
    file->addAction(m_addAtlasAct);
    file->addSeparator();
    m_backup->addMenu(file, m_host);
    file->addSeparator();
    file->addAction(m_quitAct);
    rebuildRecentMenu();

    QMenu* edit = m_host->menuBar()->addMenu(tr("&Edit"));
    edit->addAction(m_undoAct);
    edit->addAction(m_redoAct);

    QMenu* effect = m_host->menuBar()->addMenu(tr("E&ffect"));
    effect->addAction(Icons::icon(Icons::Id::Add), tr("Add &Effect"), m_effects, &EffectsDock::addEffect);
    effect->addAction(Icons::icon(Icons::Id::Node), tr("Add E&mitter"), QKeySequence(Qt::CTRL | Qt::Key_E), m_effects, &EffectsDock::addEmitter);
    effect->addAction(Icons::icon(Icons::Id::Duplicate), tr("&Duplicate"), QKeySequence(Qt::CTRL | Qt::Key_D), m_effects, &EffectsDock::duplicateSelected);
    effect->addAction(Icons::icon(Icons::Id::Remove), tr("De&lete"), m_effects, &EffectsDock::deleteSelected);
    effect->addSeparator();
    m_presetMenu = effect->addMenu(Icons::icon(Icons::Id::Import), tr("Add &Preset"));
    for (const QString& name : presetNames()) m_presetMenu->addAction(name, this, [this, name] { addPreset(name); });

    QMenu* play = m_host->menuBar()->addMenu(tr("&Play"));
    for (QAction* a : {m_playAct, m_restartAct, m_stepAct, m_autoRestartAct, m_diceAct, m_centerAct}) play->addAction(a);

    m_viewMenu = m_host->menuBar()->addMenu(tr("&View"));
    m_viewMenu->addAction(m_fitAct);
    m_viewMenu->addAction(m_gridAct);
    CanvasView::addCoordinatesAction(m_viewMenu);
    m_gameRendererAct = m_viewMenu->addAction(tr("Preview with the Game Renderer (after a restart)"));
    m_gameRendererAct->setCheckable(true);
    m_gameRendererAct->setChecked(settings().value(QStringLiteral("preview/gameRenderer"), true).toBool());
    m_gameRendererAct->setToolTip(tr("Draw the preview with toms_game's renderer (bgfx): the same pixels and GPU particle simulation "
                                     "as the game. Off: QPainter, simulated on the CPU."));
    connect(m_gameRendererAct, &QAction::toggled, this, [this](bool on) {
        settings().setValue(QStringLiteral("preview/gameRenderer"), on);
        m_host->statusBar()->showMessage(tr("The preview renderer changes the next time the editor starts."), 8000);
    });
    m_viewMenu->addSeparator();
    for (QDockWidget* d : {static_cast<QDockWidget*>(m_effects), static_cast<QDockWidget*>(m_sprites), static_cast<QDockWidget*>(m_inspector),
                           static_cast<QDockWidget*>(m_timeline), m_problemsDock, m_history})
        m_viewMenu->addAction(d->toggleViewAction());
    m_viewMenu->addSeparator();

    QMenu* help = m_host->menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&Command Line…"), this, [this] {
        QMessageBox::information(m_host, tr("Command Line"),
                                 tr("<pre>particle_editor [file.particle]\n"
                                    "particle_editor --headless check file.particle\n"
                                    "particle_editor --headless render file.particle#effect out.png [frames] [every]\n"
                                    "particle_editor --selftest file.particle outdir</pre>"
                                    "In the game: <code>toms_game --fx=file.particle#effect</code>. See docs/17_PARTICLES.md."));
    });
}

void ParticleEditor::createToolBars()
{
    QToolBar* main = m_host->addToolBar(tr("File"));
    main->setObjectName(QStringLiteral("ParticleFileBar"));
    for (QAction* a : {m_newAct, m_openAct, m_saveAct}) main->addAction(a);
    main->addSeparator();
    main->addAction(m_undoAct);
    main->addAction(m_redoAct);

    QToolBar* t = m_host->addToolBar(tr("Playback"));
    t->setObjectName(QStringLiteral("ParticlePlaybackBar"));
    t->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    for (QAction* a : {m_playAct, m_restartAct, m_stepAct, m_autoRestartAct}) t->addAction(a);
    t->addSeparator();
    t->addWidget(new QLabel(tr(" Speed ")));
    m_speed = new QComboBox;
    for (const char* s : {"0.1×", "0.25×", "0.5×", "1×", "2×"}) m_speed->addItem(QString::fromUtf8(s));
    m_speed->setCurrentIndex(3);
    connect(m_speed, &QComboBox::currentIndexChanged, this, [this](int i) {
        static const float speeds[] = {0.1f, 0.25f, 0.5f, 1.0f, 2.0f};
        m_play->setSpeed(speeds[i]);
    });
    t->addWidget(m_speed);
    t->addWidget(new QLabel(tr("  Seed ")));
    m_seed = new QSpinBox;
    m_seed->setRange(1, 999999);
    m_seed->setValue(int(m_play->seed()));
    m_seed->setToolTip(tr("The preview's seed: the same seed gives the same particles"));
    connect(m_seed, &QSpinBox::valueChanged, this, [this](int v) {
        if (uint32_t(v) != m_play->seed()) m_play->setSeed(uint32_t(v));
    });
    t->addWidget(m_seed);
    t->addAction(m_diceAct);
    t->addWidget(new QLabel(tr("  GPU sim above ")));
    m_gpuThreshold = new QSpinBox;
    m_gpuThreshold->setRange(0, 10000000);
    m_gpuThreshold->setSingleStep(1000);
    m_gpuThreshold->setSpecialValueText(tr("never"));
    m_gpuThreshold->setValue(m_play->gpuThreshold());
    m_gpuThreshold->setToolTip(tr("Like the game setting particleGpuThreshold (desktop 5000, phones 3000): \"auto\" emitters with "
                                  "more particles than this are simulated on the GPU. Only with the game renderer."));
    connect(m_gpuThreshold, &QSpinBox::valueChanged, this, [this](int v) { setGpuThreshold(v); });
    t->addWidget(m_gpuThreshold);
    t->addSeparator();
    t->addWidget(new QLabel(tr(" Background ")));
    m_background = new QComboBox;
    m_background->addItems({tr("Checker"), tr("Dark"), tr("Light"), tr("Picture…")});
    connect(m_background, &QComboBox::activated, this, [this](int i) {
        if (i < 3) {
            m_viewport->setBackground(ParticleViewport::Background(i));
            return;
        }
        const QString path = QFileDialog::getOpenFileName(m_host, tr("Background Picture"), settings().value(QStringLiteral("lastDir")).toString(),
                                                          tr("Images (*.png *.jpg *.bmp)"));
        if (path.isEmpty() || !m_viewport->setBackgroundImage(path)) m_background->setCurrentIndex(int(m_viewport->background()));
    });
    t->addWidget(m_background);
    t->addAction(m_gridAct);
    t->addAction(m_centerAct);
}

void ParticleEditor::updatePlayButton()
{
    m_playAct->setText(m_play->playing() ? tr("Pause") : tr("Play"));
    m_playAct->setIcon(Icons::icon(m_play->playing() ? Icons::Id::Pause : Icons::Id::Play));
    if (m_seed && uint32_t(m_seed->value()) != m_play->seed()) {
        const QSignalBlocker b(m_seed);
        m_seed->setValue(int(m_play->seed()));
    }
}

void ParticleEditor::refreshProblems()
{
    if (!m_problems) return;
    // Keep the log lines (no "where"); replace the problems.
    for (int i = m_problems->count() - 1; i >= 0; i--)
        if (!m_problems->item(i)->data(Qt::UserRole).toString().isEmpty() || m_problems->item(i)->data(Qt::UserRole + 1).toBool())
            delete m_problems->takeItem(i);
    int row = 0, errors = 0;
    for (const toms::fx::Problem& p : m_doc->problems()) {
        auto* it = new QListWidgetItem(Icons::icon(p.warning ? Icons::Id::Warning : Icons::Id::Error),
                                       QStringLiteral("%1: %2").arg(qs(p.where), qs(p.text)));
        it->setData(Qt::UserRole, qs(p.where));
        it->setData(Qt::UserRole + 1, true);
        m_problems->insertItem(row++, it);
        errors += !p.warning;
    }
    m_problemsDock->setWindowTitle(row ? tr("Problems (%1) / Log").arg(row) : tr("Problems / Log"));
    if (errors && !selftestRunning()) m_problemsDock->raise();
}

bool ParticleEditor::openFile(const QString& path)
{
    QString err;
    if (!m_doc->open(path, &err)) {
        if (!selftestRunning()) QMessageBox::warning(m_host, tr("Open"), tr("Could not open %1:\n\n%2").arg(QDir::toNativeSeparators(path), err));
        m_doc->message(tr("Open %1: %2").arg(QDir::toNativeSeparators(path), err));
        return false;
    }
    m_doc->message(tr("Opened %1").arg(QDir::toNativeSeparators(path)));
    addRecentFile(path);
    m_play->restart();
    return true;
}

bool ParticleEditor::maybeSave()
{
    if (!m_doc->isDirty() || selftestRunning()) return true;
    const auto r = QMessageBox::warning(m_host, tr("Particle Editor"), tr("%1 has unsaved changes. Save them?").arg(m_doc->displayName()),
                                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Save) return save();
    return r == QMessageBox::Discard;
}

bool ParticleEditor::save() { return m_doc->filePath().isEmpty() ? saveAs() : saveTo(m_doc->filePath()); }

bool ParticleEditor::saveAs()
{
    QString start = m_doc->filePath();
    if (start.isEmpty()) start = QDir(settings().value(QStringLiteral("lastDir")).toString()).filePath(QStringLiteral("effects.particle"));
    const QString path = QFileDialog::getSaveFileName(m_host, tr("Save Particle Effects"), start, tr("Particle effects (*.particle)"));
    return !path.isEmpty() && saveTo(path);
}

bool ParticleEditor::saveTo(const QString& path)
{
    QString err;
    if (!m_doc->saveTo(path, &err)) {
        if (!selftestRunning()) QMessageBox::warning(m_host, tr("Save"), tr("Could not save %1:\n\n%2").arg(QDir::toNativeSeparators(path), err));
        m_doc->message(tr("Save %1: %2").arg(QDir::toNativeSeparators(path), err));
        return false;
    }
    m_doc->message(tr("Saved %1").arg(QDir::toNativeSeparators(path)));
    addRecentFile(path);
    return true;
}

void ParticleEditor::onAddAtlas()
{
    const QString start = m_doc->atlasCount() ? m_doc->atlasAt(m_doc->atlasCount() - 1).path : settings().value(QStringLiteral("lastDir")).toString();
    const QStringList paths = QFileDialog::getOpenFileNames(m_host, tr("Add Atlas"), start, tr("Sprite atlases (*.atlas);;All files (*)"));
    for (const QString& p : paths) {
        QString err;
        if (!m_doc->addAtlas(p, &err)) m_doc->message(tr("Add atlas: %1").arg(err));
    }
}

QStringList ParticleEditor::presetNames() const
{
    QStringList out;
    for (const Effect& e : loadPresets().effects) out << qs(e.name);
    return out;
}

bool ParticleEditor::addPreset(const QString& name)
{
    const toms::fx::ParticleFile presets = loadPresets();
    const Effect* e = presets.find(name.toStdString());
    if (!e) return false;
    // The atlases its sprites name ("fx:dot", "game:coin"): the TOMS ones next to the fx atlas.
    QStringList need;
    for (const Emitter& m : e->emitters) {
        std::vector<std::string> refs = m.frames;
        refs.push_back(m.sprite);
        for (const std::string& r : refs) {
            const QString id = qs(toms::anim::parseSpriteRef(r).atlas);
            if (!id.isEmpty() && m_doc->atlasIndexOf(id) < 0 && !need.contains(id)) need << id;
        }
    }
    const QString fxAtlas = ParticleDocument::defaultFxAtlas();
    for (const QString& id : need) {
        const QString path = fxAtlas.isEmpty() ? QString() : QFileInfo(fxAtlas).absoluteDir().filePath(id + QStringLiteral(".atlas"));
        QString err;
        if (path.isEmpty() || !QFileInfo::exists(path)) m_doc->message(tr("Preset %1: no atlas '%2' found; add it with File > Add Atlas").arg(name, id));
        else if (!m_doc->addAtlas(path, &err)) m_doc->message(tr("Preset %1: %2").arg(name, err));
    }
    const bool ok = m_doc->addEffect(*e);
    if (ok) m_play->restart();
    return ok;
}

void ParticleEditor::addEmitterFromSprites(const QStringList& refs, const QPointF& pos)
{
    if (refs.isEmpty()) return;
    if (!m_doc->effect()) m_effects->addEffect();
    Emitter m;
    m.name = toms::anim::parseSpriteRef(refs.first().toStdString()).name;
    m.offset = glm::vec2(float(std::round(pos.x())), float(std::round(pos.y())));
    if (refs.size() == 1) m.sprite = refs.first().toStdString();
    else
        for (const QString& r : refs) m.frames.push_back(r.toStdString());
    m_doc->addEmitter(m);
}

void ParticleEditor::addRecentFile(const QString& path)
{
    if (selftestRunning()) return;
    QSettings s = settings();
    QStringList recent = s.value(QStringLiteral("recentFiles")).toStringList();
    const QString abs = QFileInfo(path).absoluteFilePath();
    recent.removeAll(abs);
    recent.prepend(abs);
    while (recent.size() > kMaxRecent) recent.removeLast();
    s.setValue(QStringLiteral("recentFiles"), recent);
    s.setValue(QStringLiteral("lastDir"), QFileInfo(abs).absolutePath());
    rebuildRecentMenu();
}

void ParticleEditor::rebuildRecentMenu()
{
    if (!m_recentMenu) return;
    m_recentMenu->clear();
    const QStringList recent = settings().value(QStringLiteral("recentFiles")).toStringList();
    for (const QString& f : recent)
        m_recentMenu->addAction(QDir::toNativeSeparators(f), this, [this, f] {
            if (maybeSave()) openFile(f);
        });
    m_recentMenu->setEnabled(!recent.isEmpty());
}

void ParticleEditor::readSettings()
{
    QSettings s = settings();
    m_gridAct->setChecked(s.value(QStringLiteral("grid"), false).toBool());
    m_viewport->setGridVisible(m_gridAct->isChecked());
    const int bg = s.value(QStringLiteral("background"), 0).toInt();
    const QString img = s.value(QStringLiteral("backgroundImage")).toString();
    if (bg == int(ParticleViewport::Background::Image) && !img.isEmpty() && m_viewport->setBackgroundImage(img)) m_background->setCurrentIndex(3);
    else if (bg >= 0 && bg < 3) {
        m_viewport->setBackground(ParticleViewport::Background(bg));
        m_background->setCurrentIndex(bg);
    }
}

void ParticleEditor::writeSettings() const
{
    QSettings s = settings();
    s.setValue(QStringLiteral("grid"), m_gridAct->isChecked());
    s.setValue(QStringLiteral("background"), int(m_viewport->background()));
    s.setValue(QStringLiteral("backgroundImage"), m_viewport->backgroundImagePath());
}

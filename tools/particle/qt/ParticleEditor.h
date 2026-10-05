#pragma once

#include <QObject>
#include <QSettings>

class EffectsDock;
class FxSpritesDock;
class InspectorDock;
class ParticleDocument;
class ParticlePlayback;
class ParticleViewport;
class QAction;
class QComboBox;
class QDockWidget;
class QListWidget;
class QMainWindow;
class QMenu;
class QSpinBox;
class QToolBar;
class TimelineDock;

// The particle editor as a unit a host window installs (like AnimEditor): it owns the document
// and the preview, and creates the viewport, the docks, the menus and the toolbars. The host
// (ParticleMainWindow today, the studio app later) provides the QMainWindow, the theme and the
// window settings, and asks maybeSave() before closing.
class ParticleEditor : public QObject
{
    Q_OBJECT

public:
    explicit ParticleEditor(QMainWindow* host);
    void install();

    QString title() const;
    QMenu* viewMenu() const { return m_viewMenu; }
    ParticleDocument* document() const { return m_doc; }
    ParticlePlayback* playback() const { return m_play; }
    ParticleViewport* viewport() const { return m_viewport; }
    EffectsDock* effectsDock() const { return m_effects; }
    InspectorDock* inspector() const { return m_inspector; }
    TimelineDock* timeline() const { return m_timeline; }
    FxSpritesDock* spritesDock() const { return m_sprites; }
    QListWidget* problemsList() const { return m_problems; }

    bool openFile(const QString& path);
    bool maybeSave();
    bool save();
    bool saveAs();
    bool saveTo(const QString& path);
    // The preset effects (docs/examples/fx_recipes.particle, built in) and adding one: atlases it
    // needs that the file does not have yet are added too (the TOMS fx / game atlases).
    QStringList presetNames() const;
    bool addPreset(const QString& name);
    // Sprites dropped into the viewport: a new emitter at `pos` (several = a flipbook).
    void addEmitterFromSprites(const QStringList& refs, const QPointF& pos);

    // The preview draws with the game's renderer (bgfx) unless this is off (View menu; the setting
    // "preview/gameRenderer") or the platform has no windows (the offscreen selftest).
    static bool gameRendererWanted();
    void setGpuThreshold(int n);   // the preview's particleGpuThreshold (as the game's setting)

    static QSettings settings();
    void readSettings();
    void writeSettings() const;

signals:
    void titleChanged();

private:
    void createActions();
    void createDocks();
    void createMenus();
    void createToolBars();
    void refreshProblems();
    void onAddAtlas();
    void addRecentFile(const QString& path);
    void rebuildRecentMenu();
    void updatePlayButton();

    QMainWindow* m_host;
    ParticleDocument* m_doc;
    class AutoBackup* m_backup = nullptr;
    QDockWidget* m_history = nullptr;
    ParticlePlayback* m_play;
    ParticleViewport* m_viewport;
    EffectsDock* m_effects = nullptr;
    InspectorDock* m_inspector = nullptr;
    TimelineDock* m_timeline = nullptr;
    FxSpritesDock* m_sprites = nullptr;
    QDockWidget* m_problemsDock = nullptr;
    QListWidget* m_problems = nullptr;

    QMenu *m_viewMenu = nullptr, *m_recentMenu = nullptr, *m_presetMenu = nullptr;
    QAction *m_newAct, *m_openAct, *m_saveAct, *m_saveAsAct, *m_addAtlasAct, *m_quitAct, *m_undoAct, *m_redoAct;
    QAction *m_playAct, *m_restartAct, *m_stepAct, *m_autoRestartAct, *m_gridAct, *m_centerAct, *m_fitAct, *m_diceAct;
    QComboBox *m_speed = nullptr, *m_background = nullptr;
    QSpinBox* m_seed = nullptr;
    QSpinBox* m_gpuThreshold = nullptr;
    QAction* m_gameRendererAct = nullptr;
};

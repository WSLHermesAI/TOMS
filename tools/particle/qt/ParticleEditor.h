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
    explicit ParticleEditor(QMainWindow* l_pHost);
    void f_Install();

    QString f_Title() const;
    QMenu* f_ViewMenu() const { return m_pViewMenu; }
    ParticleDocument* f_Document() const { return m_pDoc; }
    ParticlePlayback* f_Playback() const { return m_pPlay; }
    ParticleViewport* f_Viewport() const { return m_pViewport; }
    EffectsDock* f_EffectsDock() const { return m_pEffects; }
    InspectorDock* f_Inspector() const { return m_pInspector; }
    TimelineDock* f_Timeline() const { return m_pTimeline; }
    FxSpritesDock* f_SpritesDock() const { return m_pSprites; }
    QListWidget* f_ProblemsList() const { return m_pProblems; }

    bool f_OpenFile(const QString& l_oPath);
    bool f_MaybeSave();
    bool f_Save();
    bool f_SaveAs();
    bool f_SaveTo(const QString& l_oPath);
    // The preset effects (docs/examples/fx_recipes.particle, built in) and adding one: atlases it
    // needs that the file does not have yet are added too (the TOMS fx / game atlases).
    QStringList f_PresetNames() const;
    bool f_AddPreset(const QString& l_oName);
    // Sprites dropped into the viewport: a new emitter at `pos` (several = a flipbook).
    void f_AddEmitterFromSprites(const QStringList& l_oRefs, const QPointF& l_oPos);

    // The preview draws with the game's renderer (bgfx) unless this is off (View menu; the setting
    // "preview/gameRenderer") or the platform has no windows (the offscreen selftest).
    static bool f_GameRendererWanted();
    void f_SetGpuThreshold(int l_iN);   // the preview's particleGpuThreshold (as the game's setting)

    static QSettings f_Settings();
    void f_ReadSettings();
    void f_WriteSettings() const;

signals:
    void f_TitleChanged();

private:
    void f_CreateActions();
    void f_CreateDocks();
    void f_CreateMenus();
    void f_CreateToolBars();
    void f_RefreshProblems();
    void f_OnAddAtlas();
    void f_AddRecentFile(const QString& l_oPath);
    void f_RebuildRecentMenu();
    void f_UpdatePlayButton();

    QMainWindow* m_pHost;
    ParticleDocument* m_pDoc;
    class AutoBackup* m_pBackup = nullptr;
    QDockWidget* m_pHistory = nullptr;
    ParticlePlayback* m_pPlay;
    ParticleViewport* m_pViewport;
    EffectsDock* m_pEffects = nullptr;
    InspectorDock* m_pInspector = nullptr;
    TimelineDock* m_pTimeline = nullptr;
    FxSpritesDock* m_pSprites = nullptr;
    QDockWidget* m_pProblemsDock = nullptr;
    QListWidget* m_pProblems = nullptr;

    QMenu *m_pViewMenu = nullptr, *m_pRecentMenu = nullptr, *m_pPresetMenu = nullptr;
    QAction *m_pNewAct, *m_pOpenAct, *m_pSaveAct, *m_pSaveAsAct, *m_pAddAtlasAct, *m_pQuitAct, *m_pUndoAct, *m_pRedoAct;
    QAction *m_pPlayAct, *m_pRestartAct, *m_pStepAct, *m_pAutoRestartAct, *m_pGridAct, *m_pCenterAct, *m_pFitAct, *m_pDiceAct;
    QComboBox *m_pSpeed = nullptr, *m_pBackground = nullptr;
    QSpinBox* m_pSeed = nullptr;
    QSpinBox* m_pGpuThreshold = nullptr;
    QAction* m_pGameRendererAct = nullptr;
};

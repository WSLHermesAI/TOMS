#pragma once

#include <QList>
#include <QObject>
#include <QSettings>

class AnimClipsDock;
class AnimDocument;
class AnimEventsDock;
class AnimKeyListDock;
class AnimNodesDock;
class AnimProblemsDock;
class AnimPropertiesDock;
class AnimSpritesDock;
class AnimTransport;
class AnimViewport;
class QAction;
class QActionGroup;
class QDockWidget;
class QMainWindow;
class QMenu;
class QToolBar;

// The animation editor as a unit a host window installs: it owns the document and creates the
// central viewport, the docks, the menus / actions and the toolbars. The host (AnimMainWindow
// today, the studio app's plugin host in Phase 2) only provides the QMainWindow, the theme and
// window settings, and asks maybeSave() before closing.
class AnimEditor : public QObject
{
    Q_OBJECT

public:
    explicit AnimEditor(QMainWindow* host);

    // Puts the viewport, docks, menus and toolbars on the host.
    void install();

    QString title() const;   // "name.anim[*] - Anim Editor" style, without the app name
    QList<QMenu*> menus() const { return m_menus; }
    QMenu* viewMenu() const { return m_viewMenu; }
    QList<QDockWidget*> docks() const;

    AnimDocument* document() const { return m_doc; }
    AnimViewport* viewport() const { return m_viewport; }
    AnimNodesDock* nodesDock() const { return m_nodes; }
    AnimSpritesDock* spritesDock() const { return m_sprites; }
    AnimPropertiesDock* propertiesDock() const { return m_props; }
    AnimKeyListDock* keyListDock() const { return m_keys; }
    AnimEventsDock* eventsDock() const { return m_events; }
    AnimProblemsDock* problemsDock() const { return m_problems; }
    AnimTransport* transport() const { return m_transport; }

    bool openFile(const QString& path, bool askForAtlas = true);
    bool maybeSave();
    bool save();
    bool saveAs(const QString& startDir = QString());   // the dialog opens in startDir when given
    bool saveTo(const QString& path);
    // Adds an atlas (one undo step). An untitled file is saved first (Save As, starting in the
    // atlas's folder): atlas paths are stored relative to the .anim. False = not added.
    bool addAtlasFile(const QString& path);
    // Sprites dropped on a time area: asks for the time gap (Yes / No), then one sprite key per
    // reference on the selected node at time, time + gap, ... (one undo step). False = nothing inserted.
    bool insertSpriteKeys(const QStringList& refs, float time);

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
    void addRecentFile(const QString& path);
    void rebuildRecentMenu();
    QString lastDir() const;
    void onAddAtlas();
    void addSpriteNode();
    void moveSelected(int delta);
    void updateActions();

    QMainWindow* m_host;
    AnimDocument* m_doc;
    AnimViewport* m_viewport;
    AnimClipsDock* m_clips = nullptr;
    AnimNodesDock* m_nodes = nullptr;
    AnimSpritesDock* m_sprites = nullptr;
    AnimPropertiesDock* m_props = nullptr;
    AnimKeyListDock* m_keys = nullptr;
    AnimEventsDock* m_events = nullptr;
    AnimProblemsDock* m_problems = nullptr;
    QDockWidget* m_timeline = nullptr;
    AnimTransport* m_transport = nullptr;

    QList<QMenu*> m_menus;
    QMenu* m_recentMenu = nullptr;
    QMenu* m_viewMenu = nullptr;
    QToolBar* m_mainBar = nullptr;
    QAction *m_newAct, *m_openAct, *m_saveAct, *m_saveAsAct, *m_addAtlasAct, *m_reloadAtlasAct, *m_quitAct;
    QAction *m_undoAct, *m_redoAct;
    QAction *m_addChildAct, *m_addSiblingAct, *m_addSpriteAct, *m_dupAct, *m_deleteAct, *m_renameAct, *m_upAct, *m_downAct;
    QAction *m_moveAct, *m_rotateAct, *m_scaleAct, *m_autoKeyAct, *m_snapAct, *m_gridAct;
    QAction *m_insertKeyAct, *m_qualifyAct, *m_fitAct, *m_zoomInAct, *m_zoomOutAct, *m_frameAct;
    QAction *m_helpCliAct, *m_aboutAct;
};

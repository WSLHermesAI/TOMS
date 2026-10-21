// editor_window.h -- the toms_editor main window.
//
//   Tab "Play (bgfx)"          BgfxViewport running the real game code on bgfx
//   Tab "Stage editor"          the existing Qt stage editor (src/editor/stage), embedded unmodified
//   Dock "Stages"               double-click loads a stage into the running game
//   Dock "Scenarios"            double-click starts a test scenario in the Play tab (tests/scenarios/, docs/31)
//   Dock "Session"              renderer, asset folder, fps, quads, draw calls, toggles
#pragma once
#include <QMainWindow>

class BgfxViewport;
class QLabel;
class QListWidget;
class QTreeWidget;
class QTabWidget;

class EditorWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit EditorWindow(QWidget* parent = nullptr);
    // Starts a scenario (a folder with scenario.json, or the file) in the Play tab; a message box when it cannot.
    void playScenario(const QString& path);
    // --frames=<n> --screenshot=<png>: grab the viewport and the whole window, then quit.
    void runSmokeTest(int frames, const QString& png);

private:
    void buildStageDock();
    void buildSessionDock();
    void buildMenus();
    void fillStageList();
    void buildScenarioDock();
    void fillScenarioList();
    QString scenarioRoot() const;

    QTabWidget* tabs_ = nullptr;
    BgfxViewport* viewport_ = nullptr;
    QListWidget* stageList_ = nullptr;
    QTreeWidget* scenarioList_ = nullptr;
    QString extraScenarioDir_;   // File > Open scenario folder...: a folder of scenarios outside tests/
    QLabel* rendererLabel_ = nullptr;
    QLabel* statsLabel_ = nullptr;
};

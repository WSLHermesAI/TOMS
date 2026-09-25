// editor_window.h -- the toms_editor main window.
//
//   Tab "Play (bgfx)"          BgfxViewport running the real game code on bgfx
//   Tab "Stage editor"          the existing Qt stage editor (TOMS/editor/src), embedded unmodified
//   Dock "Stages"               double-click loads a stage into the running game
//   Dock "Session"              renderer, asset folder, fps, quads, draw calls, toggles
#pragma once
#include <QMainWindow>

class BgfxViewport;
class QLabel;
class QListWidget;
class QTabWidget;

class EditorWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit EditorWindow(QWidget* parent = nullptr);
    // --frames=<n> --screenshot=<png>: grab the viewport and the whole window, then quit.
    void runSmokeTest(int frames, const QString& png);

private:
    void buildStageDock();
    void buildSessionDock();
    void buildMenus();
    void fillStageList();

    QTabWidget* tabs_ = nullptr;
    BgfxViewport* viewport_ = nullptr;
    QListWidget* stageList_ = nullptr;
    QLabel* rendererLabel_ = nullptr;
    QLabel* statsLabel_ = nullptr;
};

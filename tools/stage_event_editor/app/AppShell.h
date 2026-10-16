// AppShell.h -- the app's windows: the stage editor and the event editor, live together, either
//
//   Tabs              one window, a tab each (Ctrl+1 / Ctrl+2, or ⇄ in either editor)
//   Separate windows  a window each: move, resize, maximise or minimise them independently (e.g. one per monitor)
//
// View > Tabs / Separate windows switches at any time (kept between runs, with each window's size and place). The
// editors are moved, not recreated: nothing open is lost. In separate windows, closing one hides it (Window menu brings
// it back); closing the last one quits, after asking about unsaved changes.
#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>

class EventEditor;
class QMainWindow;
class QTabWidget;
class StageEditor;

class AppShell : public QObject {
    Q_OBJECT
public:
    enum Mode { Tabs, Windows };
    AppShell(StageEditor* stage, EventEditor* events, QObject* parent = nullptr);
    ~AppShell() override;

    void start(int forceMode = -1);   // the saved mode (or the forced one), shown
    Mode mode() const { return m_mode; }
    void setMode(Mode m);
    void showStage();
    void showEvents();
    QStringList selfTest();           // switches modes and checks the editors move with them
    QList<QWidget*> windows() const;  // the open top-level windows (--shot)

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    QMainWindow* makeWindow(const QString& objectName);
    void buildMenus(QMainWindow* w);
    void detach();
    void saveGeometry();
    void updateTitles();
    bool confirmQuit();
    bool hasUnsaved() const;
    void quit();

    StageEditor* m_stage;
    EventEditor* m_events;
    Mode m_mode = Tabs;
    QPointer<QMainWindow> m_main, m_stageWin, m_eventWin;
    QPointer<QTabWidget> m_tabs;
    bool m_quitting = false;
};

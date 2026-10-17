// AppShell.h -- the app's window, as Visual Studio's document windows: the stage editor and the event editor are two
// dockable panes of one main window, tabbed along its top by default ("Stage" | "Events").
//
//   * drag a tab out (or ⧉ in its title) and that editor floats as a window of its own: resize it, maximise it (□),
//     put it on another monitor;
//   * drag it back over the main window (or double-click its title, or ⧉ again) and it docks: as a tab, or beside the
//     other editor (split);
//   * View > Dock both as tabs puts everything back; Ctrl+1 / Ctrl+2 bring each editor to the front.
// The layout (docked or floating, sizes, places) is kept between runs. The editors are moved, never recreated: nothing
// open is lost. Closing the main window quits, after asking about unsaved changes.
#pragma once

#include <QDockWidget>
#include <QMainWindow>
#include <QObject>
#include <QPointer>
#include <QStringList>

class EventEditor;
class StageEditor;

class AppShell : public QObject {
    Q_OBJECT
public:
    // The shell's window takes both editors and deletes them with itself.
    AppShell(StageEditor* stage, EventEditor* events, QObject* parent = nullptr);
    ~AppShell() override;

    // Shows the window with the saved layout; floatEvents: the event editor starts floating (--windows).
    void start(bool floatEvents = false);
    void showStage();
    void showEvents();
    void dockAsTabs();                     // both editors docked, as tabs
    // Everything where it starts: the window's size and place, both editors docked as tabs, every panel docked at its
    // starting size, a details window over the middle. View > Default layout.
    void defaultLayout();
    void setFloating(QDockWidget* d, bool floating);
    QDockWidget* stageDock() const { return m_stageDock; }
    QDockWidget* eventDock() const { return m_eventDock; }
    QStringList selfTest();                // floats and docks the editors and checks they move with it
    QList<QWidget*> windows() const;       // the main window and any floating editor (--shot)

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    QDockWidget* makeDock(QWidget* editor, const QString& objectName, const QString& title);
    void updateTitleBar(QDockWidget* d);
    void raise(QDockWidget* d);
    void buildMenus();
    void saveLayout();
    void updateTitles();
    bool confirmQuit();
    bool hasUnsaved() const;
    void quit();

    StageEditor* m_stage;
    EventEditor* m_events;
    QPointer<QMainWindow> m_main;
    QPointer<QDockWidget> m_stageDock, m_eventDock;
    bool m_quitting = false;
};

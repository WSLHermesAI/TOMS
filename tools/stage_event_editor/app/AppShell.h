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
    AppShell(StageEditor* l_pStage, EventEditor* l_pEvents, QObject* l_pParent = nullptr);
    ~AppShell() override;

    // Shows the window with the saved layout; floatEvents: the event editor starts floating (--windows).
    void f_Start(bool l_bFloatEvents = false);
    void f_ShowStage();
    void f_ShowEvents();
    void f_DockAsTabs();                     // both editors docked, as tabs
    // Everything where it starts: the window's size and place, both editors docked as tabs, every panel docked at its
    // starting size, a details window over the middle. View > Default layout.
    void f_DefaultLayout();
    void f_SetFloating(QDockWidget* l_pD, bool l_bFloating);
    QDockWidget* f_StageDock() const { return m_oStageDock; }
    QDockWidget* f_EventDock() const { return m_oEventDock; }
    QStringList f_SelfTest();                // floats and docks the editors and checks they move with it
    QList<QWidget*> f_Windows() const;       // the main window and any floating editor (--shot)

protected:
    bool eventFilter(QObject* l_pO, QEvent* l_pE) override;

private:
    QDockWidget* f_MakeDock(QWidget* l_pEditor, const QString& l_oObjectName, const QString& l_oTitle);
    void f_UpdateTitleBar(QDockWidget* l_pD);
    void f_Raise(QDockWidget* l_pD);
    void f_BuildMenus();
    void f_SaveLayout();
    void f_UpdateTitles();
    bool f_ConfirmQuit();
    bool f_HasUnsaved() const;
    void f_Quit();

    StageEditor* m_pStage;
    EventEditor* m_pEvents;
    QPointer<QMainWindow> m_oMain;
    QPointer<QDockWidget> m_oStageDock, m_oEventDock;
    bool m_bQuitting = false;
};

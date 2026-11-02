#pragma once

#include <QMainWindow>

class AnimEditor;
class QAction;

// The thin host: a QMainWindow that installs the AnimEditor, adds the theme switch, keeps the
// window title / geometry and asks the editor before closing. Phase 2 replaces it with the studio
// app's plugin host.
class AnimMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit AnimMainWindow(QWidget* l_pParent = nullptr);
    AnimEditor* f_Editor() const { return m_pEditor; }
    void f_SetDarkTheme(bool l_bDark);

protected:
    void closeEvent(QCloseEvent* l_pE) override;

private:
    void f_UpdateTitle();

    AnimEditor* m_pEditor;
    QAction *m_pDarkAct, *m_pLightAct;
};

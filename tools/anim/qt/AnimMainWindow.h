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
    explicit AnimMainWindow(QWidget* parent = nullptr);
    AnimEditor* editor() const { return m_editor; }
    void setDarkTheme(bool dark);

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void updateTitle();

    AnimEditor* m_editor;
    QAction *m_darkAct, *m_lightAct;
};

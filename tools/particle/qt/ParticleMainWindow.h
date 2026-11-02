#pragma once

#include <QMainWindow>

class ParticleEditor;
class QAction;

// The thin host: a QMainWindow that installs the ParticleEditor, adds the theme switch, keeps
// the window title / geometry and asks the editor before closing (as AnimMainWindow does).
class ParticleMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit ParticleMainWindow(QWidget* l_pParent = nullptr);
    ParticleEditor* f_Editor() const { return m_pEditor; }
    void f_SetDarkTheme(bool l_bDark);

protected:
    void closeEvent(QCloseEvent* l_pE) override;

private:
    void f_UpdateTitle();

    ParticleEditor* m_pEditor;
    QAction *m_pDarkAct, *m_pLightAct;
};

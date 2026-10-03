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
    explicit ParticleMainWindow(QWidget* parent = nullptr);
    ParticleEditor* editor() const { return m_editor; }
    void setDarkTheme(bool dark);

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void updateTitle();

    ParticleEditor* m_editor;
    QAction *m_darkAct, *m_lightAct;
};

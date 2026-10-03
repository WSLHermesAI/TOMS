#include "AnimMainWindow.h"

#include "AnimDocument.h"
#include "AnimEditor.h"
#include "Theme.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QMenu>
#include <QStatusBar>

namespace {
constexpr int kStateVersion = 1;   // bump when the dock layout changes incompatibly
}

AnimMainWindow::AnimMainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_editor(new AnimEditor(this))
{
    setObjectName(QStringLiteral("AnimEditorMainWindow"));
    m_editor->install();
    statusBar();

    m_darkAct = new QAction(tr("&Dark Theme"), this);
    m_lightAct = new QAction(tr("&Light Theme"), this);
    auto* themes = new QActionGroup(this);
    for (QAction* a : {m_darkAct, m_lightAct}) {
        a->setCheckable(true);
        themes->addAction(a);
        m_editor->viewMenu()->addAction(a);
    }
    connect(m_darkAct, &QAction::triggered, this, [this] { setDarkTheme(true); });
    connect(m_lightAct, &QAction::triggered, this, [this] { setDarkTheme(false); });
    connect(m_editor, &AnimEditor::titleChanged, this, &AnimMainWindow::updateTitle);

    QSettings s = AnimEditor::settings();
    setDarkTheme(s.value(QStringLiteral("theme"), QStringLiteral("dark")).toString() != QLatin1String("light"));
    if (!restoreGeometry(s.value(QStringLiteral("geometry")).toByteArray())) resize(1440, 900);
    restoreState(s.value(QStringLiteral("windowState")).toByteArray(), kStateVersion);
    m_editor->readSettings();
    updateTitle();
}

void AnimMainWindow::setDarkTheme(bool dark)
{
    Theme::apply(dark ? Theme::Mode::Dark : Theme::Mode::Light);
    (dark ? m_darkAct : m_lightAct)->setChecked(true);
    for (QWidget* w : QApplication::allWidgets()) w->update();
}

void AnimMainWindow::updateTitle()
{
    setWindowTitle(m_editor->title());
    setWindowModified(m_editor->document()->isDirty());
}

void AnimMainWindow::closeEvent(QCloseEvent* e)
{
    if (!m_editor->maybeSave()) {
        e->ignore();
        return;
    }
    if (!qApp->property("toms.selftest").toBool()) {
        QSettings s = AnimEditor::settings();
        s.setValue(QStringLiteral("geometry"), saveGeometry());
        s.setValue(QStringLiteral("windowState"), saveState(kStateVersion));
        s.setValue(QStringLiteral("theme"), Theme::mode() == Theme::Mode::Dark ? QStringLiteral("dark") : QStringLiteral("light"));
        m_editor->writeSettings();
    }
    e->accept();
}

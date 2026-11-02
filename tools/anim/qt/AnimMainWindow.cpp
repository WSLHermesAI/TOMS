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
constexpr int g_iStateVersion = 1;   // bump when the dock layout changes incompatibly
}

AnimMainWindow::AnimMainWindow(QWidget* l_pParent)
    : QMainWindow(l_pParent)
    , m_pEditor(new AnimEditor(this))
{
    setObjectName(QStringLiteral("AnimEditorMainWindow"));
    m_pEditor->f_Install();
    statusBar();

    m_pDarkAct = new QAction(tr("&Dark Theme"), this);
    m_pLightAct = new QAction(tr("&Light Theme"), this);
    auto* l_pThemes = new QActionGroup(this);
    for (QAction* l_pA : {m_pDarkAct, m_pLightAct}) {
        l_pA->setCheckable(true);
        l_pThemes->addAction(l_pA);
        m_pEditor->f_ViewMenu()->addAction(l_pA);
    }
    connect(m_pDarkAct, &QAction::triggered, this, [this] { f_SetDarkTheme(true); });
    connect(m_pLightAct, &QAction::triggered, this, [this] { f_SetDarkTheme(false); });
    connect(m_pEditor, &AnimEditor::f_TitleChanged, this, &AnimMainWindow::f_UpdateTitle);

    QSettings l_oS = AnimEditor::f_Settings();
    f_SetDarkTheme(l_oS.value(QStringLiteral("theme"), QStringLiteral("dark")).toString() != QLatin1String("light"));
    if (!restoreGeometry(l_oS.value(QStringLiteral("geometry")).toByteArray())) resize(1440, 900);
    restoreState(l_oS.value(QStringLiteral("windowState")).toByteArray(), g_iStateVersion);
    m_pEditor->f_ReadSettings();
    f_UpdateTitle();
}

void AnimMainWindow::f_SetDarkTheme(bool l_bDark)
{
    Theme::f_Apply(l_bDark ? Theme::Mode::Dark : Theme::Mode::Light);
    (l_bDark ? m_pDarkAct : m_pLightAct)->setChecked(true);
    for (QWidget* l_pW : QApplication::allWidgets()) l_pW->update();
}

void AnimMainWindow::f_UpdateTitle()
{
    setWindowTitle(m_pEditor->f_Title());
    setWindowModified(m_pEditor->f_Document()->f_IsDirty());
}

void AnimMainWindow::closeEvent(QCloseEvent* l_pE)
{
    if (!m_pEditor->f_MaybeSave()) {
        l_pE->ignore();
        return;
    }
    if (!qApp->property("toms.selftest").toBool()) {
        QSettings l_oS = AnimEditor::f_Settings();
        l_oS.setValue(QStringLiteral("geometry"), saveGeometry());
        l_oS.setValue(QStringLiteral("windowState"), saveState(g_iStateVersion));
        l_oS.setValue(QStringLiteral("theme"), Theme::f_Mode() == Theme::Mode::Dark ? QStringLiteral("dark") : QStringLiteral("light"));
        m_pEditor->f_WriteSettings();
    }
    l_pE->accept();
}

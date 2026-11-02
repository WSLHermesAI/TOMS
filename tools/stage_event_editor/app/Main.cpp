// Main.cpp -- stage_event_editor: the stage and event editors as one Windows app (docs/gameplay/02_STAGE_EDITOR.md section 6).
//
//   stage_event_editor [--root=<TOMS folder>] [--windows]      (--windows: the event editor starts floating)
//   stage_event_editor -platform offscreen --selftest [--root=<TOMS folder>]     (CTest: stage_editor.app_selftest)
//   stage_event_editor --shot=<png> [--floor=<id>] [--select=<x>,<y>] [--event=<id>] [--evtab=<tab>] [--windows]
//       saves each window (its widgets, and the map as the game renderer drew it) after a few seconds, then quits;
//       a second window is saved as <png>_1.png
//
// The TOMS folder is found from the exe's folder (or the current one) upwards: the folder with assets/data/story/floors.
#include "AppShell.h"
#include "CanvasView.h"
#include "StageCanvas.h"
#include "EventEditor.h"
#include "Language.h"
#include "ObjectWindow.h"
#include "StageEditor.h"
#include "Theme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMessageBox>
#include <QPainter>
#include <QTimer>

#include <cstdio>

namespace {
QString f_FindRoot(const QStringList& l_oArgs) {
    for (const QString& l_oA : l_oArgs)
        if (l_oA.startsWith(QStringLiteral("--root="))) return QDir::cleanPath(QDir(l_oA.mid(7)).absolutePath());
    for (const QString& l_oStart : {QCoreApplication::applicationDirPath(), QDir::currentPath()}) {
        QDir l_oD(l_oStart);
        for (int l_iI = 0; l_iI < 8; l_iI++) {
            if (QFileInfo::exists(l_oD.filePath(QStringLiteral("assets/data/story/floors")))) return l_oD.absolutePath();
            if (!l_oD.cdUp()) break;
        }
    }
    return QString();
}

QString f_ArgValue(const QStringList& l_oArgs, const QString& l_oName) {
    for (const QString& l_oA : l_oArgs)
        if (l_oA.startsWith(l_oName + QLatin1Char('='))) return l_oA.mid(l_oName.size() + 1);
    return QString();
}

// Each window as the user sees it: QWidget::grab gives the widgets (GDI cannot read the game renderer's swap chain, so
// the map comes out blank there); the renderer's own screenshot of the map is drawn over the canvas's place.
void f_SaveShots(AppShell& l_oShell, StageEditor* l_pStage, const QString& l_oPng, const QString& l_oGpuPng) {
    const QImage l_oGpu(l_oGpuPng);
    int l_iI = 0;
    QList<QWidget*> l_oWindows = l_oShell.f_Windows();
    if (ObjectWindow* l_pOw = ObjectWindow::f_Current(); l_pOw && l_pOw->isVisible()) l_oWindows << l_pOw;   // a details window too
    for (QWidget* l_pW : l_oWindows) {
        QImage l_oImg = l_pW->grab().toImage();
        StageCanvas* l_pCv = l_pStage->f_Canvas();
        if (!l_oGpu.isNull() && l_pCv->window() == l_pW && l_pCv->isVisible()) {
            QPainter l_oG(&l_oImg);   // logical coordinates: the grab keeps the window's device pixel ratio
            const QPoint l_oAt = l_pCv->mapTo(l_pW, QPoint(0, 0));
            l_oG.drawImage(QRectF(l_oAt.x(), l_oAt.y(), l_pCv->width(), l_pCv->height()), l_oGpu);
        }
        QString l_oOut = l_oPng;
        if (l_iI) l_oOut.replace(QStringLiteral(".png"), QStringLiteral("_%1.png").arg(l_iI));
        l_oImg.save(l_oOut);
        std::printf("%s  %dx%d  %s\n", qPrintable(l_oOut), l_oImg.width(), l_oImg.height(), qPrintable(l_pW->windowTitle()));
        l_iI++;
    }
}
}  // namespace

int main(int l_iArgc, char** l_pArgv) {
    QApplication l_oApp(l_iArgc, l_pArgv);
    const QStringList l_oArgs = QApplication::arguments();
    const bool l_bSelftest = l_oArgs.contains(QStringLiteral("--selftest"));
    QApplication::setOrganizationName(QStringLiteral("TOMS"));
    // the self test keeps its own settings (window mode, sizes), apart from the user's
    QApplication::setApplicationName(l_bSelftest ? QStringLiteral("StageEventEditorSelftest") : QStringLiteral("StageEventEditor"));
    Theme::f_Apply(Theme::Mode::Dark);
    Language::f_Init();   // English / 中文 (tools/studio_common/Language.h), before any window
    CanvasView::f_SetCoordinatesShown(false);   // a tile map: no pixel coordinates along the edges

    const QString l_oRoot = f_FindRoot(l_oArgs);
    if (l_oRoot.isEmpty()) {
        const QString l_oMsg = QStringLiteral("Cannot find the TOMS folder (one with assets/data/story/floors). Start the editor from inside it, or pass --root=<folder>.");
        if (l_bSelftest) std::fprintf(stderr, "%s\n", qPrintable(l_oMsg));
        else QMessageBox::critical(nullptr, QStringLiteral("TOMS Stage & Event Editor"), l_oMsg);
        return 2;
    }
    auto* l_pStage = new StageEditor;
    auto* l_pEvents = new EventEditor;
    QString l_oErr;
    if (!l_pStage->f_OpenProject(l_oRoot, &l_oErr) || !l_pEvents->f_OpenProject(l_oRoot, &l_oErr)) {
        if (l_bSelftest) std::fprintf(stderr, "cannot open %s: %s\n", qPrintable(l_oRoot), qPrintable(l_oErr));
        else QMessageBox::critical(nullptr, QStringLiteral("TOMS Stage & Event Editor"), l_oErr);
        delete l_pStage;
        delete l_pEvents;
        return 2;
    }
    int l_iRc = 0;
    {
        AppShell l_oShell(l_pStage, l_pEvents);
        l_oShell.f_Start(l_oArgs.contains(QStringLiteral("--windows")));   // --windows: the event editor starts floating
        if (l_bSelftest) {
            QApplication::processEvents();
            l_pStage->f_SetSaveLayout(false);   // the test leaves the user's layout as it was
            QStringList l_oFail = l_pStage->f_SelfTest();
            l_oFail += l_pEvents->f_SelfTest();
            l_oFail += l_oShell.f_SelfTest();
            QApplication::processEvents();
            for (const QString& l_oF : l_oFail) std::printf("  FAIL: %s\n", qPrintable(l_oF));
            std::printf("stage_event_editor --selftest: %s (%d failure(s))\n", l_oFail.isEmpty() ? "passed" : "FAILED", int(l_oFail.size()));
            l_iRc = l_oFail.isEmpty() ? 0 : 1;
        } else if (const QString l_oShot = f_ArgValue(l_oArgs, QStringLiteral("--shot")); !l_oShot.isEmpty()) {
            const QString l_oFloor = f_ArgValue(l_oArgs, QStringLiteral("--floor")), l_oSel = f_ArgValue(l_oArgs, QStringLiteral("--select"));
            l_pStage->f_SetSaveLayout(false);   // a screenshot shows every panel and leaves the user's layout as it was
            l_pStage->f_ResetLayout();
            // --create=<category>:<id>: the palette's + (category as its group gives it: monster, item, npc, story)
            if (const QString l_oCr = f_ArgValue(l_oArgs, QStringLiteral("--create")); l_oCr.contains(QLatin1Char(':'))) {
                ObjectWindow::f_OpenNew(l_pStage, l_oCr.section(QLatin1Char(':'), 0, 0).toStdString());
                if (ObjectWindow* l_pW = ObjectWindow::f_Current()) {
                    l_pW->f_SetNewId(l_oCr.section(QLatin1Char(':'), 1).toStdString(), std::string());
                    l_pW->f_Apply();
                }
            }
            if (!l_oFloor.isEmpty()) l_pStage->f_ShowEventOnFloor(l_oFloor.toStdString(), std::string());
            if (const QString l_oEv = f_ArgValue(l_oArgs, QStringLiteral("--event")); !l_oEv.isEmpty()) l_pEvents->f_SelectEvent(l_oEv);
            if (const QString l_oTab = f_ArgValue(l_oArgs, QStringLiteral("--evtab")); !l_oTab.isEmpty()) l_pEvents->f_ShowTab(l_oTab);
            if (!l_oSel.isEmpty()) l_pStage->f_SelectCell(stage::Cell{l_oSel.section(QLatin1Char(','), 0, 0).toInt(), l_oSel.section(QLatin1Char(','), 1, 1).toInt()});
            const QString l_oGpu = l_oShot + QStringLiteral(".gpu.png");
            QFile::remove(l_oGpu);
            QTimer::singleShot(2500, l_pStage, [l_pStage, l_oGpu] { l_pStage->f_Canvas()->f_SaveGameRendererShot(l_oGpu); });
            QTimer::singleShot(4500, l_pStage, [&l_oShell, l_pStage, l_oShot, l_oGpu] {
                f_SaveShots(l_oShell, l_pStage, l_oShot, l_oGpu);
                QFile::remove(l_oGpu);
                QApplication::quit();
            });
            l_iRc = l_oApp.exec();
        } else {
            l_iRc = l_oApp.exec();
        }
    }
    // the shell's window owned both editors and deleted them with itself
    return l_iRc;
}

// Main.cpp -- stage_event_editor: the stage and event editors as one Windows app (docs/19_STAGE_EDITOR.md section 6).
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
QString findRoot(const QStringList& args) {
    for (const QString& a : args)
        if (a.startsWith(QStringLiteral("--root="))) return QDir::cleanPath(QDir(a.mid(7)).absolutePath());
    for (const QString& start : {QCoreApplication::applicationDirPath(), QDir::currentPath()}) {
        QDir d(start);
        for (int i = 0; i < 8; i++) {
            if (QFileInfo::exists(d.filePath(QStringLiteral("assets/data/story/floors")))) return d.absolutePath();
            if (!d.cdUp()) break;
        }
    }
    return QString();
}

QString argValue(const QStringList& args, const QString& name) {
    for (const QString& a : args)
        if (a.startsWith(name + QLatin1Char('='))) return a.mid(name.size() + 1);
    return QString();
}

// Each window as the user sees it: QWidget::grab gives the widgets (GDI cannot read the game renderer's swap chain, so
// the map comes out blank there); the renderer's own screenshot of the map is drawn over the canvas's place.
void saveShots(AppShell& shell, StageEditor* stage, const QString& png, const QString& gpuPng) {
    const QImage gpu(gpuPng);
    int i = 0;
    QList<QWidget*> windows = shell.windows();
    if (ObjectWindow* ow = ObjectWindow::current(); ow && ow->isVisible()) windows << ow;   // a details window too
    for (QWidget* w : windows) {
        QImage img = w->grab().toImage();
        StageCanvas* cv = stage->canvas();
        if (!gpu.isNull() && cv->window() == w && cv->isVisible()) {
            QPainter g(&img);   // logical coordinates: the grab keeps the window's device pixel ratio
            const QPoint at = cv->mapTo(w, QPoint(0, 0));
            g.drawImage(QRectF(at.x(), at.y(), cv->width(), cv->height()), gpu);
        }
        QString out = png;
        if (i) out.replace(QStringLiteral(".png"), QStringLiteral("_%1.png").arg(i));
        img.save(out);
        std::printf("%s  %dx%d  %s\n", qPrintable(out), img.width(), img.height(), qPrintable(w->windowTitle()));
        i++;
    }
}
}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const QStringList args = QApplication::arguments();
    const bool selftest = args.contains(QStringLiteral("--selftest"));
    QApplication::setOrganizationName(QStringLiteral("TOMS"));
    // the self test keeps its own settings (window mode, sizes), apart from the user's
    QApplication::setApplicationName(selftest ? QStringLiteral("StageEventEditorSelftest") : QStringLiteral("StageEventEditor"));
    Theme::apply(Theme::Mode::Dark);
    Language::init();   // English / 中文 (tools/studio_common/Language.h), before any window
    CanvasView::setCoordinatesShown(false);   // a tile map: no pixel coordinates along the edges

    const QString root = findRoot(args);
    if (root.isEmpty()) {
        const QString msg = QStringLiteral("Cannot find the TOMS folder (one with assets/data/story/floors). Start the editor from inside it, or pass --root=<folder>.");
        if (selftest) std::fprintf(stderr, "%s\n", qPrintable(msg));
        else QMessageBox::critical(nullptr, QStringLiteral("TOMS Stage & Event Editor"), msg);
        return 2;
    }
    auto* stage = new StageEditor;
    auto* events = new EventEditor;
    QString err;
    if (!stage->openProject(root, &err) || !events->openProject(root, &err)) {
        if (selftest) std::fprintf(stderr, "cannot open %s: %s\n", qPrintable(root), qPrintable(err));
        else QMessageBox::critical(nullptr, QStringLiteral("TOMS Stage & Event Editor"), err);
        delete stage;
        delete events;
        return 2;
    }
    int rc = 0;
    {
        AppShell shell(stage, events);
        shell.start(args.contains(QStringLiteral("--windows")));   // --windows: the event editor starts floating
        if (selftest) {
            QApplication::processEvents();
            stage->setSaveLayout(false);   // the test leaves the user's layout as it was
            QStringList fail = stage->selfTest();
            fail += events->selfTest();
            fail += shell.selfTest();
            QApplication::processEvents();
            for (const QString& f : fail) std::printf("  FAIL: %s\n", qPrintable(f));
            std::printf("stage_event_editor --selftest: %s (%d failure(s))\n", fail.isEmpty() ? "passed" : "FAILED", int(fail.size()));
            rc = fail.isEmpty() ? 0 : 1;
        } else if (const QString shot = argValue(args, QStringLiteral("--shot")); !shot.isEmpty()) {
            const QString floor = argValue(args, QStringLiteral("--floor")), sel = argValue(args, QStringLiteral("--select"));
            stage->setSaveLayout(false);   // a screenshot shows every panel and leaves the user's layout as it was
            stage->resetLayout();
            // --create=<category>:<id>: the palette's + (category as its group gives it: monster, item, npc, story)
            if (const QString cr = argValue(args, QStringLiteral("--create")); cr.contains(QLatin1Char(':'))) {
                ObjectWindow::openNew(stage, cr.section(QLatin1Char(':'), 0, 0).toStdString());
                if (ObjectWindow* w = ObjectWindow::current()) {
                    w->setNewId(cr.section(QLatin1Char(':'), 1).toStdString(), std::string());
                    w->apply();
                }
            }
            if (!floor.isEmpty()) stage->showEventOnFloor(floor.toStdString(), std::string());
            if (const QString ev = argValue(args, QStringLiteral("--event")); !ev.isEmpty()) events->selectEvent(ev);
            if (const QString tab = argValue(args, QStringLiteral("--evtab")); !tab.isEmpty()) events->showTab(tab);
            if (!sel.isEmpty()) stage->selectCell(stage::Cell{sel.section(QLatin1Char(','), 0, 0).toInt(), sel.section(QLatin1Char(','), 1, 1).toInt()});
            const QString gpu = shot + QStringLiteral(".gpu.png");
            QFile::remove(gpu);
            QTimer::singleShot(2500, stage, [stage, gpu] { stage->canvas()->saveGameRendererShot(gpu); });
            QTimer::singleShot(4500, stage, [&shell, stage, shot, gpu] {
                saveShots(shell, stage, shot, gpu);
                QFile::remove(gpu);
                QApplication::quit();
            });
            rc = app.exec();
        } else {
            rc = app.exec();
        }
    }
    // the shell's window owned both editors and deleted them with itself
    return rc;
}

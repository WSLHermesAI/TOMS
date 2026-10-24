// uied_main.cpp -- ui_editor: the game's UI files beside the running game (docs/08_RMLUI.md "The UI editor").
//   ui_editor                      the editor
//   ui_editor --selftest           -platform offscreen: the text logic and the file handling, no GPU (exit 0 = ok)
//   ui_editor --smoke=<png>        with a GPU: battle_bow previewed, the title's colour typed (not saved) shows in the
//                                  game, the file on disk is untouched, inspecting the title finds #title (exit 0 = ok)
#include "bgfx_renderer.h"
#include "bgfx_viewport.h"
#include "game_session.h"
#include "Language.h"
#include "object.h"
#include "singletons.h"
#include "uied_logic.h"
#include "uied_window.h"

#include <QApplication>
#include <algorithm>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QMouseEvent>
#include <QTimer>

#include <cstdio>

namespace {

int g_failed = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_failed++;
}

QString readFile(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

// The text logic (uied_logic.h), on small made-up files.
void logicTests() {
    const QString rml = QStringLiteral(
        "<rml>\n<head>\n  <link type=\"text/rcss\" href=\"common.rcss\"/>\n  <link type=\"text/rcss\" href=\"battle.rcss\"/>\n"
        "</head>\n<body>\n  <div id=\"title\">{{t}}</div>\n  <div class=\"hp bad\"></div>\n</body>\n</rml>\n");
    check(uied::linkedStyleSheets(rml) == QStringList({QStringLiteral("common.rcss"), QStringLiteral("battle.rcss")}),
          "the style sheets an .rml links");
    check(uied::lineOfId(rml, QStringLiteral("title")) == 7, "the line of id=\"title\"");
    check(uied::lineOfId(rml, QStringLiteral("nope")) == 0, "no such id: 0");
    check(uied::lineOfTagWithClasses(rml, QStringLiteral("div"), {QStringLiteral("hp"), QStringLiteral("bad")}) == 8,
          "the line of the first div.hp.bad");
    const QString css = QStringLiteral(
        "/* #title in a comment */\n#title { color: red; }\n.hp, .other\n{ top: 1px; }\n#titlebar { x: 1; }\n"
        "div { display: block; }\n.hp-text { y: 2; }\n@media (x) { #title { z: 3; } }\n");
    const QList<uied::RuleHit> hits = uied::rulesFor(QStringLiteral("battle.rcss"), css, QStringLiteral("title"),
                                                     {QStringLiteral("hp")}, QStringLiteral("div"));
    QList<int> lines;
    for (const uied::RuleHit& h : hits) lines << h.line;
    check(lines.contains(2) && lines.contains(3) && lines.contains(6), "rules naming #title, .hp (over two lines) and div");
    check(!lines.contains(1) && !lines.contains(5) && !lines.contains(7), "not a comment, #titlebar or .hp-text");
    const uied::Problem p = uied::parseProblem(1, QStringLiteral("Failed to parse property 'colr' in battle.rcss at line 12."));
    check(p.file == QStringLiteral("battle.rcss") && p.line == 12 && p.level == 1, "a problem's file and line");
    const uied::Problem q = uied::parseProblem(2, QStringLiteral("Data array index out of bounds."));
    check(q.file.isEmpty() && q.line == 0, "a problem that names no place");
    // RmlUi's own wording (StyleSheetParser.cpp): "... in <path>: <line>."
    const uied::Problem r = uied::parseProblem(1, QStringLiteral(
        "Syntax error parsing property declaration 'colr: red;' in D:/x/docs/examples/ui_tutorial/practice.rcss: 12."));
    check(r.file == QStringLiteral("practice.rcss") && r.line == 12, "RmlUi's \"in <file>: <line>\" message");
}

}  // namespace

int main(int argc, char** argv) {
    int rc = 0;
    {
        QApplication app(argc, argv);
        QApplication::setApplicationName(QStringLiteral("TOMS UI Editor"));
        QApplication::setOrganizationName(QStringLiteral("TOMS"));
        Language::init();
        bool selftest = false;
        QString smoke;
        for (const QString& a : QApplication::arguments()) {
            if (a == QStringLiteral("--selftest")) selftest = true;
            else if (a.startsWith(QStringLiteral("--smoke="))) smoke = a.mid(8);
        }

        UiEditorWindow w;
        if (selftest) {
            w.setQuiet(true);
            logicTests();
            const QString disk = readFile(QDir(w.uiDir()).filePath(QStringLiteral("battle.rcss")));
            check(w.fileCount() >= 10, "the UI folder's files are listed");
            check(w.openFile(QStringLiteral("battle.rcss")) && w.currentFile() == QStringLiteral("battle.rcss") &&
                      w.text(QStringLiteral("battle.rcss")) == QString(disk).replace(QStringLiteral("\r\n"), QStringLiteral("\n")),
                  "battle.rcss opens with its text");
            w.setText(QStringLiteral("battle.rcss"), w.text(QStringLiteral("battle.rcss")) + QStringLiteral("\n/* edited */\n"));
            w.previewNow();
            check(w.isModified(QStringLiteral("battle.rcss")), "typing marks it changed");
            check(readFile(QDir(w.uiDir()).filePath(QStringLiteral("battle.rcss"))) == disk, "the preview leaves the file on disk alone");
            w.revert(QStringLiteral("battle.rcss"));
            check(!w.isModified(QStringLiteral("battle.rcss")), "Revert File: back to the text on disk");
            check(!w.openFile(QStringLiteral("no_such.rml")), "a file that is not there does not open");
            check(w.openFile(QStringLiteral("practice.rcss")) && w.text(QStringLiteral("practice.rcss")).contains(QStringLiteral("#card")),
                  "the tutorial's practice files are listed and open (docs/examples/ui_tutorial)");
            std::printf(g_failed ? "ui_editor selftest: %d failed\n" : "ui_editor selftest: all passed\n", g_failed);
            rc = g_failed ? 1 : 0;
        } else if (!smoke.isEmpty()) {
            w.setQuiet(true);
            w.show();
            const QString rcssPath = QDir(w.uiDir()).filePath(QStringLiteral("battle.rcss"));
            const QString disk = readFile(rcssPath);
            QComboBox* preview = w.previewBox();
            int battle = preview ? preview->findText(QStringLiteral("battle_bow"), Qt::MatchEndsWith) : -1;
            check(battle > 0, "the Preview list has the battle_bow scenario");
            if (battle > 0) {
                w.startPreviewAt(battle);
            }
            BgfxViewport* view = w.viewport();
            // after the battle is up: type a red title (not saved), let it preview, point at the title, screenshot
            QTimer::singleShot(3000, &w, [&w, view, smoke] {
                QString t = w.text(QStringLiteral("battle.rcss"));
                if (t.isEmpty() && w.openFile(QStringLiteral("battle.rcss"))) t = w.text(QStringLiteral("battle.rcss"));
                t.replace(QStringLiteral("color: #ffcaa8;"), QStringLiteral("color: #ff2020;"));
                w.setText(QStringLiteral("battle.rcss"), t);
                w.previewNow();
                // the title's middle: design (362, 40) of 1024 x 768, letterboxed into the view
                const double s = std::min(view->width() / 1024.0, view->height() / 768.0);
                const QPointF at((view->width() - 1024 * s) / 2 + 362 * s, (view->height() - 768 * s) / 2 + 40 * s);
                QMouseEvent move(QEvent::MouseMove, at, view->mapToGlobal(at), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(view, &move);
                view->runSmokeTest(view->frameCount() + 40, smoke);
            });
            QObject::connect(view, &BgfxViewport::smokeTestDone, &w, [&w, rcssPath, disk, smoke] {
                const QImage img(smoke);
                int red = 0;
                for (int y = 0; y < img.height() / 8; y++)
                    for (int x = 0; x < img.width(); x++) {
                        const QColor c = img.pixelColor(x, y);
                        if (c.red() > 200 && c.green() < 90 && c.blue() < 90) red++;
                    }
                check(!img.isNull() && red > 200, "the typed colour shows in the game, before saving (red title)");
                check(readFile(rcssPath) == disk, "battle.rcss on disk is untouched");
                w.inspectNow();
                check(w.inspectSummary().contains(QStringLiteral("#title")) && w.inspectRuleCount() >= 1,
                      "inspecting the title finds div#title and its rule");
                w.grab().save(QString(smoke).replace(QStringLiteral(".png"), QStringLiteral("_window.png")));   // the layout, for a look
                w.revert(QStringLiteral("battle.rcss"));
                // Help > Open Practice Screen: the tutorial's card over a new game's map
                w.openPracticeScreen();
                QTimer::singleShot(5000, &w, [&w, smoke] {
                    check(w.practiceShown(), "Help > Open Practice Screen shows the tutorial's practice screen");
                    if (toms::next::BgfxRenderer* ren = w.viewport()->session().renderer())   // a picture of it, for a look
                        ren->savePNG(QString(smoke).replace(QStringLiteral(".png"), QStringLiteral("_practice.png")).toStdString());
                    QTimer::singleShot(1000, &w, [&w] {   // the picture is taken a frame or two later
                        // the tutorial's step 9: a misspelled property is listed in Problems, with its file and line
                        QString t = w.text(QStringLiteral("practice.rcss"));
                        if (t.isEmpty() && w.openFile(QStringLiteral("practice.rcss"))) t = w.text(QStringLiteral("practice.rcss"));
                        w.setText(QStringLiteral("practice.rcss"), t.replace(QStringLiteral("color: #ffe066"), QStringLiteral("colr: #ffe066")));
                        w.previewNow();
                        QTimer::singleShot(1500, &w, [&w] {
                            check(w.problemAt(QStringLiteral("practice.rcss")) > 0,
                                  "a misspelled property is in Problems, with practice.rcss and its line");
                            w.revert(QStringLiteral("practice.rcss"));
                            std::printf(g_failed ? "ui_editor smoke: %d failed\n" : "ui_editor smoke: all passed\n", g_failed);
                            QApplication::exit(g_failed ? 1 : 0);
                        });
                    });
                });
            });
            QTimer::singleShot(60000, &w, [] { check(false, "finished within 60 s"); QApplication::exit(1); });
            rc = app.exec();
        } else {
            w.show();
            rc = app.exec();
        }
        Language::shutdown();
    }
    toms::releaseSingletons();
    Object::DumpLeaks();
    return rc;
}

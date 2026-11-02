// uied_main.cpp -- ui_editor: the game's UI files beside the running game (docs/gameplay/01_RMLUI.md "The UI editor").
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

int g_iFailed = 0;

void f_Check(bool l_bOk, const char* l_pszWhat) {
    std::printf("%s %s\n", l_bOk ? "ok  " : "FAIL", l_pszWhat);
    if (!l_bOk) g_iFailed++;
}

QString f_ReadFile(const QString& l_oPath) {
    QFile l_oF(l_oPath);
    return l_oF.open(QIODevice::ReadOnly) ? QString::fromUtf8(l_oF.readAll()) : QString();
}

// The text logic (uied_logic.h), on small made-up files.
void f_LogicTests(const QString& l_oUiDir) {
    const QString l_oRml = QStringLiteral(
        "<rml>\n<head>\n  <link type=\"text/rcss\" href=\"common.rcss\"/>\n  <link type=\"text/rcss\" href=\"battle.rcss\"/>\n"
        "</head>\n<body>\n  <div id=\"title\">{{t}}</div>\n  <div class=\"hp bad\"></div>\n</body>\n</rml>\n");
    f_Check(uied::f_LinkedStyleSheets(l_oRml) == QStringList({QStringLiteral("common.rcss"), QStringLiteral("battle.rcss")}),
          "the style sheets an .rml links");
    f_Check(uied::f_LineOfId(l_oRml, QStringLiteral("title")) == 7, "the line of id=\"title\"");
    f_Check(uied::f_LineOfId(l_oRml, QStringLiteral("nope")) == 0, "no such id: 0");
    f_Check(uied::f_LineOfTagWithClasses(l_oRml, QStringLiteral("div"), {QStringLiteral("hp"), QStringLiteral("bad")}) == 8,
          "the line of the first div.hp.bad");
    const QString l_oCss = QStringLiteral(
        "/* #title in a comment */\n#title { color: red; }\n.hp, .other\n{ top: 1px; }\n#titlebar { x: 1; }\n"
        "div { display: block; }\n.hp-text { y: 2; }\n@media (x) { #title { z: 3; } }\n");
    const QList<uied::RuleHit> l_oHits = uied::f_RulesFor(QStringLiteral("battle.rcss"), l_oCss, QStringLiteral("title"),
                                                     {QStringLiteral("hp")}, QStringLiteral("div"));
    QList<int> l_oLines;
    for (const uied::RuleHit& l_oH : l_oHits) l_oLines << l_oH.m_iLine;
    f_Check(l_oLines.contains(2) && l_oLines.contains(3) && l_oLines.contains(6), "rules naming #title, .hp (over two lines) and div");
    f_Check(!l_oLines.contains(1) && !l_oLines.contains(5) && !l_oLines.contains(7), "not a comment, #titlebar or .hp-text");
    const uied::Problem l_oP = uied::f_ParseProblem(1, QStringLiteral("Failed to parse property 'colr' in battle.rcss at line 12."));
    f_Check(l_oP.m_oFile == QStringLiteral("battle.rcss") && l_oP.m_iLine == 12 && l_oP.m_iLevel == 1, "a problem's file and line");
    const uied::Problem l_oQ = uied::f_ParseProblem(2, QStringLiteral("Data array index out of bounds."));
    f_Check(l_oQ.m_oFile.isEmpty() && l_oQ.m_iLine == 0, "a problem that names no place");
    // RmlUi's own wording (StyleSheetParser.cpp): "... in <path>: <line>."
    const uied::Problem l_oR = uied::f_ParseProblem(1, QStringLiteral(
        "Syntax error parsing property declaration 'colr: red;' in D:/x/docs/examples/ui_tutorial/practice.rcss: 12."));
    f_Check(l_oR.m_oFile == QStringLiteral("practice.rcss") && l_oR.m_iLine == 12, "RmlUi's \"in <file>: <line>\" message");
    // the Preview list's names: by screen, each player menu tab its own, an included scenario's screen found
    int l_iO1 = 0, l_iO2 = 0;
    QJsonObject l_oStatus;
    l_oStatus[QStringLiteral("scene")] = QStringLiteral("menu");
    QJsonObject l_oTab;
    l_oTab[QStringLiteral("tab")] = QStringLiteral("gear");
    l_oStatus[QStringLiteral("menu")] = l_oTab;
    f_Check(uied::f_PreviewLabel(l_oStatus, QStringLiteral("menu_gear"), &l_iO1) == QStringLiteral("Player menu: Equipment  (menu_gear)"),
          "a player menu tab: its own name");
    QJsonObject l_oBattle;
    l_oBattle[QStringLiteral("scene")] = QStringLiteral("battle");
    uied::f_PreviewLabel(l_oBattle, QStringLiteral("battle_bow"), &l_iO2);
    f_Check(l_iO1 < l_iO2, "the menu tabs come before the battles");
    const QJsonObject l_oInc = uied::f_ReadScenario(QDir(l_oUiDir).filePath(QStringLiteral("../../../tests/scenarios/battle_break/scenario.json")));
    f_Check(l_oInc.value(QStringLiteral("scene")).toString() == QStringLiteral("battle"), "an included scenario's screen (battle_break: battle)");
    // the hint: what the cursor is on
    const int l_iInTitle = l_oRml.indexOf(QStringLiteral("id=\"title\""));
    f_Check(uied::f_SelectorsAtRml(l_oRml, l_iInTitle) == QStringList({QStringLiteral("#title")}), "the cursor in <div id=\"title\">: #title");
    f_Check(uied::f_SelectorsAtRml(l_oRml, l_oRml.indexOf(QStringLiteral("hp bad"))) == QStringList({QStringLiteral("div.hp.bad")}),
          "an element without an id: its tag and classes");
    f_Check(uied::f_SelectorsAtRml(l_oRml, l_oRml.indexOf(QStringLiteral("{{t}}")) + 1).isEmpty(), "the cursor in text: nothing");
    f_Check(uied::f_SelectorsAtRml(l_oRml, l_oRml.indexOf(QStringLiteral("<body>")) + 2).isEmpty(), "the document's frame: nothing");
    f_Check(uied::f_SelectorsAtRcss(l_oCss, l_oCss.indexOf(QStringLiteral("color: red"))) == QStringList({QStringLiteral("#title")}),
          "in a rule's block: its selector");
    f_Check(uied::f_SelectorsAtRcss(l_oCss, l_oCss.indexOf(QStringLiteral(".other"))) == QStringList({QStringLiteral(".hp"), QStringLiteral(".other")}),
          "on a selector over two lines: each of its comma parts");
    f_Check(uied::f_SelectorsAtRcss(l_oCss, l_oCss.indexOf(QStringLiteral("z: 3"))) == QStringList({QStringLiteral("#title")}),
          "a rule inside @media: its selector");
    f_Check(uied::f_SelectorsAtRcss(l_oCss, 3).isEmpty(), "in a comment between rules: nothing");
    f_Check(uied::f_SelectorsAtRcss(QStringLiteral(".btn:hover, .x::after { a: 1; }"), 3) ==
              QStringList({QStringLiteral(".btn"), QStringLiteral(".x")}), "pseudo-classes dropped");
}

}  // namespace

int main(int l_iArgc, char** l_pArgv) {
    int l_iRc = 0;
    {
        QApplication l_oApp(l_iArgc, l_pArgv);
        QApplication::setApplicationName(QStringLiteral("TOMS UI Editor"));
        QApplication::setOrganizationName(QStringLiteral("TOMS"));
        Language::f_Init();
        bool l_bSelftest = false;
        QString l_oSmoke;
        for (const QString& l_oA : QApplication::arguments()) {
            if (l_oA == QStringLiteral("--selftest")) l_bSelftest = true;
            else if (l_oA.startsWith(QStringLiteral("--smoke="))) l_oSmoke = l_oA.mid(8);
        }

        UiEditorWindow l_oW;
        if (l_bSelftest) {
            l_oW.f_SetQuiet(true);
            f_LogicTests(l_oW.f_UiDir());
            const QString l_oDisk = f_ReadFile(QDir(l_oW.f_UiDir()).filePath(QStringLiteral("battle.rcss")));
            f_Check(l_oW.f_FileCount() >= 10, "the UI folder's files are listed");
            {   // the Preview list: every player menu tab its own entry, by name
                QComboBox* l_pPv = l_oW.f_PreviewBox();
                bool l_bAllTabs = l_pPv != nullptr;
                for (const char* l_pszT : {"Status", "Equipment", "Items", "Events", "Cards", "System"})
                    l_bAllTabs = l_bAllTabs && l_pPv->findText(QStringLiteral("Player menu: %1").arg(QLatin1String(l_pszT)), Qt::MatchStartsWith) > 0;
                f_Check(l_bAllTabs, "the Preview list has each player menu tab (Status, Equipment, Items, Events, Cards, System)");
                if (qEnvironmentVariableIsSet("UIED_LIST_PREVIEWS"))
                    for (int l_iI = 0; l_pPv && l_iI < l_pPv->count(); l_iI++) std::printf("  preview %2d: %s\n", l_iI, l_pPv->itemText(l_iI).toUtf8().constData());
            }
            f_Check(l_oW.f_OpenFile(QStringLiteral("battle.rcss")) && l_oW.f_CurrentFile() == QStringLiteral("battle.rcss") &&
                      l_oW.f_Text(QStringLiteral("battle.rcss")) == QString(l_oDisk).replace(QStringLiteral("\r\n"), QStringLiteral("\n")),
                  "battle.rcss opens with its text");
            l_oW.f_SetText(QStringLiteral("battle.rcss"), l_oW.f_Text(QStringLiteral("battle.rcss")) + QStringLiteral("\n/* edited */\n"));
            l_oW.f_PreviewNow();
            f_Check(l_oW.f_IsModified(QStringLiteral("battle.rcss")), "typing marks it changed");
            f_Check(f_ReadFile(QDir(l_oW.f_UiDir()).filePath(QStringLiteral("battle.rcss"))) == l_oDisk, "the preview leaves the file on disk alone");
            l_oW.f_Revert(QStringLiteral("battle.rcss"));
            f_Check(!l_oW.f_IsModified(QStringLiteral("battle.rcss")), "Revert File: back to the text on disk");
            f_Check(!l_oW.f_OpenFile(QStringLiteral("no_such.rml")), "a file that is not there does not open");
            f_Check(l_oW.f_OpenFile(QStringLiteral("practice.rcss")) && l_oW.f_Text(QStringLiteral("practice.rcss")).contains(QStringLiteral("#card")),
                  "the tutorial's practice files are listed and open (docs/examples/ui_tutorial)");
            std::printf(g_iFailed ? "ui_editor selftest: %d failed\n" : "ui_editor selftest: all passed\n", g_iFailed);
            l_iRc = g_iFailed ? 1 : 0;
        } else if (!l_oSmoke.isEmpty()) {
            l_oW.f_SetQuiet(true);
            l_oW.show();
            const QString l_oRcssPath = QDir(l_oW.f_UiDir()).filePath(QStringLiteral("battle.rcss"));
            const QString l_oDisk = f_ReadFile(l_oRcssPath);
            QComboBox* l_pPreview = l_oW.f_PreviewBox();
            int l_iBattle = l_pPreview ? l_pPreview->findText(QStringLiteral("(battle_bow)"), Qt::MatchEndsWith) : -1;
            f_Check(l_iBattle > 0, "the Preview list has the battle_bow scenario");
            if (l_iBattle > 0) {
                l_oW.f_StartPreviewAt(l_iBattle);
            }
            BgfxViewport* l_pView = l_oW.f_Viewport();
            // after the battle is up: type a red title (not saved), let it preview, point at the title, screenshot
            QTimer::singleShot(3000, &l_oW, [&l_oW, l_pView, l_oSmoke] {
                QString l_oT = l_oW.f_Text(QStringLiteral("battle.rcss"));
                if (l_oT.isEmpty() && l_oW.f_OpenFile(QStringLiteral("battle.rcss"))) l_oT = l_oW.f_Text(QStringLiteral("battle.rcss"));
                l_oT.replace(QStringLiteral("color: #ffcaa8;"), QStringLiteral("color: #ff2020;"));
                l_oW.f_SetText(QStringLiteral("battle.rcss"), l_oT);
                l_oW.f_PreviewNow();
                // the title's middle: design (362, 40) of 1024 x 768, letterboxed into the view
                const double l_dS = std::min(l_pView->width() / 1024.0, l_pView->height() / 768.0);
                const QPointF l_oAt((l_pView->width() - 1024 * l_dS) / 2 + 362 * l_dS, (l_pView->height() - 768 * l_dS) / 2 + 40 * l_dS);
                QMouseEvent l_oMove(QEvent::MouseMove, l_oAt, l_pView->mapToGlobal(l_oAt), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(l_pView, &l_oMove);
                // the hint: the text cursor on the title's tag makes it glow (a gold frame over #title, in the screenshot)
                l_oW.f_OpenFile(QStringLiteral("battle.rml"));
                l_oW.f_PlaceCursor(l_oW.f_Text(QStringLiteral("battle.rml")).indexOf(QStringLiteral("id=\"title\"")));
                l_pView->f_RunSmokeTest(l_pView->f_FrameCount() + 40, l_oSmoke);
            });
            QObject::connect(l_pView, &BgfxViewport::f_SmokeTestDone, &l_oW, [&l_oW, l_oRcssPath, l_oDisk, l_oSmoke] {
                const QImage l_oImg(l_oSmoke);
                int l_iRed = 0;
                for (int l_iY = 0; l_iY < l_oImg.height() / 8; l_iY++)
                    for (int l_iX = 0; l_iX < l_oImg.width(); l_iX++) {
                        const QColor l_oC = l_oImg.pixelColor(l_iX, l_iY);
                        if (l_oC.red() > 200 && l_oC.green() < 90 && l_oC.blue() < 90) l_iRed++;
                    }
                f_Check(!l_oImg.isNull() && l_iRed > 200, "the typed colour shows in the game, before saving (red title)");
                f_Check(f_ReadFile(l_oRcssPath) == l_oDisk, "battle.rcss on disk is untouched");
                l_oW.f_UpdateHint();   // (placed in the first step: the screenshot shows it)
                f_Check(l_oW.f_HintSelectors() == QStringList({QStringLiteral("#title")}) && l_oW.f_HintCount() == 1,
                      "the cursor in the title's tag: the title glows in the game (one frame)");
                l_oW.f_InspectNow();
                f_Check(l_oW.f_InspectSummary().contains(QStringLiteral("#title")) && l_oW.f_InspectRuleCount() >= 1,
                      "inspecting the title finds div#title and its rule");
                l_oW.f_UpdateRelated();
                f_Check(l_oW.f_RelatedFiles().contains(QStringLiteral("battle.rml")) && l_oW.f_RelatedFiles().contains(QStringLiteral("battle.rcss")) &&
                          l_oW.f_RelatedFiles().contains(QStringLiteral("common.rcss")) && !l_oW.f_RelatedFiles().contains(QStringLiteral("store.rml")),
                      "the battle preview's files: battle.rml and the sheets it links, not the store's");
                l_oW.grab().save(QString(l_oSmoke).replace(QStringLiteral(".png"), QStringLiteral("_window.png")));   // the layout, for a look
                l_oW.f_Revert(QStringLiteral("battle.rcss"));
                // Help > Open Practice Screen: the tutorial's card over a new game's map
                l_oW.f_OpenPracticeScreen();
                QTimer::singleShot(5000, &l_oW, [&l_oW, l_oSmoke] {
                    f_Check(l_oW.f_PracticeShown(), "Help > Open Practice Screen shows the tutorial's practice screen");
                    if (toms::next::BgfxRenderer* l_pRen = l_oW.f_Viewport()->f_Session().f_Renderer())   // a picture of it, for a look
                        l_pRen->f_SavePNG(QString(l_oSmoke).replace(QStringLiteral(".png"), QStringLiteral("_practice.png")).toStdString());
                    QTimer::singleShot(1000, &l_oW, [&l_oW] {   // the picture is taken a frame or two later
                        // the tutorial's step 9: a misspelled property is listed in Problems, with its file and line
                        QString l_oT = l_oW.f_Text(QStringLiteral("practice.rcss"));
                        if (l_oT.isEmpty() && l_oW.f_OpenFile(QStringLiteral("practice.rcss"))) l_oT = l_oW.f_Text(QStringLiteral("practice.rcss"));
                        l_oW.f_SetText(QStringLiteral("practice.rcss"), l_oT.replace(QStringLiteral("color: #ffe066"), QStringLiteral("colr: #ffe066")));
                        l_oW.f_PreviewNow();
                        QTimer::singleShot(1500, &l_oW, [&l_oW] {
                            f_Check(l_oW.f_ProblemAt(QStringLiteral("practice.rcss")) > 0,
                                  "a misspelled property is in Problems, with practice.rcss and its line");
                            l_oW.f_Revert(QStringLiteral("practice.rcss"));
                            std::printf(g_iFailed ? "ui_editor smoke: %d failed\n" : "ui_editor smoke: all passed\n", g_iFailed);
                            QApplication::exit(g_iFailed ? 1 : 0);
                        });
                    });
                });
            });
            QTimer::singleShot(60000, &l_oW, [] { f_Check(false, "finished within 60 s"); QApplication::exit(1); });
            l_iRc = l_oApp.exec();
        } else {
            l_oW.show();
            l_iRc = l_oApp.exec();
        }
        Language::f_Shutdown();
    }
    toms::f_ReleaseSingletons();
    Object::f_DumpLeaks();
    return l_iRc;
}

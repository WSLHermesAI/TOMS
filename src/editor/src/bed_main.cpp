// bed_main.cpp -- battle_editor: tune the battle beside the running game (docs/gameplay/13_BATTLE_EDITOR.md).
//   battle_editor                  the editor
//   battle_editor --selftest       -platform offscreen: the logic and the files, no GPU (exit 0 = ok)
//   battle_editor --smoke=<png>    with a GPU: three monsters ticked, ▶ Battle, the log fills, an edit reaches the running
//                                  battle, Win now wins, and the battle starts again by itself (exit 0 = ok)
#include "bed_logic.h"
#include "bed_window.h"
#include "bgfx_viewport.h"
#include "game.h"
#include "Language.h"
#include "object.h"
#include "singletons.h"

#include <QApplication>
#include <QDir>
#include <QFile>
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

void logicTests(const QString& dataDir) {
    using bed::ojson;
    // the growth calculator: the doc's example (13 §4.3)
    const std::vector<int> p = bed::levelPoints(100, 20, 1.3);
    const std::vector<int> want = {2, 3, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7};
    check(p == want, "T 100 by Lv 20, k 1.3: the doc's 2, 3, 4, 4, 4, 5, ... 7");
    int sum = 0;
    for (int v : bed::levelPoints(37, 9, 0.7)) sum += v;
    check(sum == 37, "any curve adds up to exactly T");
    check(bed::levelPoints(10, 1, 1.0).empty(), "level 1: no levels to give points");
    const std::vector<int> flat = bed::levelPoints(30, 11, 1.0);
    check(flat.size() == 10 && flat.front() == 3 && flat.back() == 3, "k 1: the same each level");
    // its table: neighbours merged, the old table going on after L
    const ojson old = ojson::parse(R"([{ "from": 2, "points": 3 }, { "from": 20, "points": 4 }, { "from": 50, "points": 5 }])");
    const ojson rows = bed::pointsRows({2, 2, 3, 3}, 5, old);
    check(rows.size() == 4 && rows[0]["from"] == 2 && rows[0]["points"] == 2 && rows[1]["from"] == 4 && rows[1]["points"] == 3 &&
              rows[2]["from"] == 20 && rows[3]["from"] == 50, "rows merged (2: 2, 4: 3), then the old table's 20 and 50");
    check(bed::pointsAt(rows, 6) == 3 && bed::pointsAt(rows, 60) == 5, "after L the old table's values go on (Lv 6: 3, Lv 60: 5)");
    check(bed::pointsAt(bed::pointsRows({1}, 2, old), 3) == 3, "a row at L + 1 brings back the old value (Lv 3: 3)");
    // stats.json's points replaced in its text, the rest untouched
    std::string stats = "{\n  \"a\": 1,\n  \"levelUp\": {\n    \"maxLevel\": 99,\n    \"points\": [\n      { \"from\": 2, \"points\": 3 }\n    ]\n  }\n}\n";
    check(bed::replaceLevelPoints(stats, ojson::parse(R"([{ "from": 2, "points": 5 }, { "from": 9, "points": 6 }])")) &&
              stats.find("      { \"from\": 9, \"points\": 6 }\n    ]") != std::string::npos && stats.find("\"a\": 1") != std::string::npos,
          "stats.json: only the points array is rewritten");
    // moves text
    check(bed::movesToText(bed::movesFromText("strike, heavy:2 , guard")) == "strike, heavy:2, guard", "moves text both ways");
    // a setup as a scenario and back
    bed::Setup s;
    s.name = "two bats";
    s.foes = {{"bat", ojson::parse(R"({ "hp": 30 })")}, {"bat", ojson::object()}};
    s.player.lv = 5;
    s.player.weapon = "sword";
    s.player.weaponWins = 4;
    s.pace = "stamina";
    const ojson sc = bed::setupToScenario(s);
    check(sc["scene"] == "battle" && sc["battle"]["enemy"] == "bat" && sc["battle"]["hp"] == 30 &&
              sc["battle"]["party"].size() == 1 && sc["battle"]["pace"] == "stamina" && sc["player"]["equip"][0] == "sword" &&
              sc["player"]["weaponUses"]["sword"] == 4, "a setup is a battle scenario");
    bed::Setup back;
    check(bed::setupFromScenario(sc, back) && back.name == "two bats" && back.foes.size() == 2 && back.player.lv == 5 &&
              back.pace == "stamina", "and loads back");
    // the data files written back in their own layout: enemies.json unchanged by a round trip
    const QString enemies = readFile(QDir(dataDir).filePath(QStringLiteral("enemies.json")));
    const ojson ej = ojson::parse(enemies.toStdString(), nullptr, false);
    check(!ej.is_discarded() && QString::fromStdString(bed::dumpCompact(ej)) == QString(enemies).replace(QStringLiteral("\r\n"), QStringLiteral("\n")),
          "enemies.json written back is the same text");
    const QString wp = readFile(QDir(dataDir).filePath(QStringLiteral("weapon_play.json")));
    const ojson wj = ojson::parse(wp.toStdString(), nullptr, false);
    check(!wj.is_discarded() && QString::fromStdString(wj.dump(2) + "\n") == QString(wp).replace(QStringLiteral("\r\n"), QStringLiteral("\n")),
          "weapon_play.json written back is the same text");
}

}  // namespace

int main(int argc, char** argv) {
    int rc = 0;
    {
        QApplication app(argc, argv);
        QApplication::setApplicationName(QStringLiteral("TOMS Battle Editor"));
        QApplication::setOrganizationName(QStringLiteral("TOMS"));
        Language::init();
        bool selftest = false;
        QString smoke;
        for (const QString& a : QApplication::arguments()) {
            if (a == QStringLiteral("--selftest")) selftest = true;
            else if (a.startsWith(QStringLiteral("--smoke="))) smoke = a.mid(8);
        }

        BattleEditorWindow w;
        if (selftest) {
            w.setQuiet(true);
            logicTests(w.dataDir());
            check(w.monsterCount() >= 7, "the monsters of enemies.json are listed");
            check(w.inBattle() == QStringList({QStringLiteral("slime")}), "the first one starts ticked");
            check(w.setInBattle(QStringLiteral("bat"), true) && w.setInBattle(QStringLiteral("golem"), true) &&
                      !w.setInBattle(QStringLiteral("wraith"), true) && w.inBattle().size() == 3, "up to three in the battle");
            check(w.selectMonster(QStringLiteral("bat")), "a monster selected");
            w.setMonsterNumber(QStringLiteral("hp"), 77);
            const bed::Setup s = w.currentSetup();
            check(s.foes.size() == 3 && s.foes[1].id == "bat" && s.foes[1].overrides.value("hp", 0) == 77,
                  "an edit not saved goes into the battle as an override");
            check(w.isDirty(), "and marks the monsters changed");
            const std::vector<int> c = w.calculate(100, 20, 1.3);
            check(c.size() == 19, "the calculator's table");
            check(w.loadSetup(QStringLiteral("3 bats Lv5 sword")) &&
                      w.inBattle() == QStringList({QStringLiteral("bat"), QStringLiteral("bat"), QStringLiteral("bat")}) &&
                      w.currentSetup().player.weapon == "sword" && w.currentSetup().player.lv == 5,
                  "a saved setup loads (three of the same monster, the sword, Lv 5)");
            check(w.setInBattle(QStringLiteral("bat"), false) && w.inBattle().isEmpty(), "untick: every copy leaves");
            check(w.scenarioCount() >= 20, "the Scenarios tab lists tests/scenarios");
            std::printf(g_failed ? "battle_editor selftest: %d failed\n" : "battle_editor selftest: all passed\n", g_failed);
            rc = g_failed ? 1 : 0;
        } else if (!smoke.isEmpty()) {
            w.setQuiet(true);
            w.show();
            w.setInBattle(QStringLiteral("bat"), true);
            w.setInBattle(QStringLiteral("golem"), true);
            check(w.startBattle(), "▶ Battle starts the battle");
            BgfxViewport* view = w.viewport();
            QTimer::singleShot(4000, &w, [&w, view, smoke] {
                w.refreshLog();
                check(w.logText().contains(QStringLiteral("start (slime + bat + golem")), "the log shows the battle's start");
                Game* g = view->session().game();
                check(g && g->battleFoeCount() == 3, "three monsters in the battle");
                // an edit while it fights: the bat (the second) attacks every 1.5 s from now on (its move's wind-up)
                const int before = g ? g->battleFoeMoveMs(1) : 0;
                w.selectMonster(QStringLiteral("bat"));
                w.setMonsterNumber(QStringLiteral("atk_interval_ms"), 1500);
                const int after = g ? g->battleFoeMoveMs(1) : 0;
                check(before > 0 && after > 0 && after < before && after <= 1500 * 2,
                      "an attack interval typed in the form changes the running battle at once");
                view->runSmokeTest(view->frameCount() + 10, smoke);
            });
            QObject::connect(view, &BgfxViewport::smokeTestDone, &w, [&w, view, smoke] {
                w.grab().save(QString(smoke).replace(QStringLiteral(".png"), QStringLiteral("_window.png")));
                if (Game* g = view->session().game()) g->debugForceWin();
                QTimer::singleShot(500, &w, [&w] {
                    w.refreshLog();
                    check(w.logText().contains(QStringLiteral(": win")), "Win now wins (the log)");
                    // auto restart: the same three monsters again, a moment later
                    QTimer::singleShot(BattleEditorWindow::kAutoRestartMs + 2500, &w, [&w] {
                        w.refreshLog();
                        Game* g = w.viewport()->session().game();
                        check(g && g->battleFoeCount() == 3 && !w.logText().contains(QStringLiteral(": win")),
                              "the battle starts again by itself (auto restart)");
                        std::printf(g_failed ? "battle_editor smoke: %d failed\n" : "battle_editor smoke: all passed\n", g_failed);
                        QApplication::exit(g_failed ? 1 : 0);
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

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

int g_iFailed = 0;

void f_Check(bool l_bOk, const char* l_pszWhat) {
    std::printf("%s %s\n", l_bOk ? "ok  " : "FAIL", l_pszWhat);
    if (!l_bOk) g_iFailed++;
}

QString f_ReadFile(const QString& l_oPath) {
    QFile l_oF(l_oPath);
    return l_oF.open(QIODevice::ReadOnly) ? QString::fromUtf8(l_oF.readAll()) : QString();
}

void f_LogicTests(const QString& l_oDataDir) {
    using bed::ojson;
    // the growth calculator: the doc's example (13 §4.3)
    const std::vector<int> l_vP = bed::f_LevelPoints(100, 20, 1.3);
    const std::vector<int> l_vWant = {2, 3, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7};
    f_Check(l_vP == l_vWant, "T 100 by Lv 20, k 1.3: the doc's 2, 3, 4, 4, 4, 5, ... 7");
    int l_iSum = 0;
    for (int l_iV : bed::f_LevelPoints(37, 9, 0.7)) l_iSum += l_iV;
    f_Check(l_iSum == 37, "any curve adds up to exactly T");
    f_Check(bed::f_LevelPoints(10, 1, 1.0).empty(), "level 1: no levels to give points");
    const std::vector<int> l_vFlat = bed::f_LevelPoints(30, 11, 1.0);
    f_Check(l_vFlat.size() == 10 && l_vFlat.front() == 3 && l_vFlat.back() == 3, "k 1: the same each level");
    // its table: neighbours merged, the old table going on after L
    const ojson l_jOld = ojson::parse(R"([{ "from": 2, "points": 3 }, { "from": 20, "points": 4 }, { "from": 50, "points": 5 }])");
    const ojson l_jRows = bed::f_PointsRows({2, 2, 3, 3}, 5, l_jOld);
    f_Check(l_jRows.size() == 4 && l_jRows[0]["from"] == 2 && l_jRows[0]["points"] == 2 && l_jRows[1]["from"] == 4 && l_jRows[1]["points"] == 3 &&
              l_jRows[2]["from"] == 20 && l_jRows[3]["from"] == 50, "rows merged (2: 2, 4: 3), then the old table's 20 and 50");
    f_Check(bed::f_PointsAt(l_jRows, 6) == 3 && bed::f_PointsAt(l_jRows, 60) == 5, "after L the old table's values go on (Lv 6: 3, Lv 60: 5)");
    f_Check(bed::f_PointsAt(bed::f_PointsRows({1}, 2, l_jOld), 3) == 3, "a row at L + 1 brings back the old value (Lv 3: 3)");
    // stats.json's points replaced in its text, the rest untouched
    std::string l_strStats = "{\n  \"a\": 1,\n  \"levelUp\": {\n    \"maxLevel\": 99,\n    \"points\": [\n      { \"from\": 2, \"points\": 3 }\n    ]\n  }\n}\n";
    f_Check(bed::f_ReplaceLevelPoints(l_strStats, ojson::parse(R"([{ "from": 2, "points": 5 }, { "from": 9, "points": 6 }])")) &&
              l_strStats.find("      { \"from\": 9, \"points\": 6 }\n    ]") != std::string::npos && l_strStats.find("\"a\": 1") != std::string::npos,
          "stats.json: only the points array is rewritten");
    // moves text
    f_Check(bed::f_MovesToText(bed::f_MovesFromText("strike, heavy:2 , guard")) == "strike, heavy:2, guard", "moves text both ways");
    // a setup as a scenario and back
    bed::Setup l_oS;
    l_oS.m_strName = "two bats";
    l_oS.m_vFoes = {{"bat", ojson::parse(R"({ "hp": 30 })")}, {"bat", ojson::object()}};
    l_oS.m_oPlayer.m_iLv = 5;
    l_oS.m_oPlayer.m_strWeapon = "sword";
    l_oS.m_oPlayer.m_iWeaponWins = 4;
    l_oS.m_strPace = "stamina";
    const ojson l_jSc = bed::f_SetupToScenario(l_oS);
    f_Check(l_jSc["scene"] == "battle" && l_jSc["battle"]["enemy"] == "bat" && l_jSc["battle"]["hp"] == 30 &&
              l_jSc["battle"]["party"].size() == 1 && l_jSc["battle"]["pace"] == "stamina" && l_jSc["player"]["equip"][0] == "sword" &&
              l_jSc["player"]["weaponUses"]["sword"] == 4, "a setup is a battle scenario");
    bed::Setup l_oBack;
    f_Check(bed::f_SetupFromScenario(l_jSc, l_oBack) && l_oBack.m_strName == "two bats" && l_oBack.m_vFoes.size() == 2 && l_oBack.m_oPlayer.m_iLv == 5 &&
              l_oBack.m_strPace == "stamina", "and loads back");
    // the data files written back in their own layout: enemies.json unchanged by a round trip
    const QString l_oEnemies = f_ReadFile(QDir(l_oDataDir).filePath(QStringLiteral("enemies.json")));
    const ojson l_jEj = ojson::parse(l_oEnemies.toStdString(), nullptr, false);
    f_Check(!l_jEj.is_discarded() && QString::fromStdString(bed::f_DumpCompact(l_jEj)) == QString(l_oEnemies).replace(QStringLiteral("\r\n"), QStringLiteral("\n")),
          "enemies.json written back is the same text");
    const QString l_oWp = f_ReadFile(QDir(l_oDataDir).filePath(QStringLiteral("weapon_play.json")));
    const ojson l_jWj = ojson::parse(l_oWp.toStdString(), nullptr, false);
    f_Check(!l_jWj.is_discarded() && QString::fromStdString(l_jWj.dump(2) + "\n") == QString(l_oWp).replace(QStringLiteral("\r\n"), QStringLiteral("\n")),
          "weapon_play.json written back is the same text");
}

}  // namespace

int main(int l_iArgc, char** l_pArgv) {
    int l_iRc = 0;
    {
        QApplication l_oApp(l_iArgc, l_pArgv);
        QApplication::setApplicationName(QStringLiteral("TOMS Battle Editor"));
        QApplication::setOrganizationName(QStringLiteral("TOMS"));
        Language::f_Init();
        bool l_bSelftest = false;
        QString l_oSmoke;
        for (const QString& l_oA : QApplication::arguments()) {
            if (l_oA == QStringLiteral("--selftest")) l_bSelftest = true;
            else if (l_oA.startsWith(QStringLiteral("--smoke="))) l_oSmoke = l_oA.mid(8);
        }

        BattleEditorWindow l_oW;
        if (l_bSelftest) {
            l_oW.f_SetQuiet(true);
            f_LogicTests(l_oW.f_DataDir());
            f_Check(l_oW.f_MonsterCount() >= 7, "the monsters of enemies.json are listed");
            f_Check(l_oW.f_InBattle() == QStringList({QStringLiteral("slime")}), "the first one starts ticked");
            f_Check(l_oW.f_SetInBattle(QStringLiteral("bat"), true) && l_oW.f_SetInBattle(QStringLiteral("golem"), true) &&
                      !l_oW.f_SetInBattle(QStringLiteral("wraith"), true) && l_oW.f_InBattle().size() == 3, "up to three in the battle");
            f_Check(l_oW.f_SelectMonster(QStringLiteral("bat")), "a monster selected");
            l_oW.f_SetMonsterNumber(QStringLiteral("hp"), 77);
            const bed::Setup l_oS = l_oW.f_CurrentSetup();
            f_Check(l_oS.m_vFoes.size() == 3 && l_oS.m_vFoes[1].m_strId == "bat" && l_oS.m_vFoes[1].m_jOverrides.value("hp", 0) == 77,
                  "an edit not saved goes into the battle as an override");
            f_Check(l_oW.f_IsDirty(), "and marks the monsters changed");
            const std::vector<int> l_vC = l_oW.f_Calculate(100, 20, 1.3);
            f_Check(l_vC.size() == 19, "the calculator's table");
            f_Check(l_oW.f_LoadSetup(QStringLiteral("3 bats Lv5 sword")) &&
                      l_oW.f_InBattle() == QStringList({QStringLiteral("bat"), QStringLiteral("bat"), QStringLiteral("bat")}) &&
                      l_oW.f_CurrentSetup().m_oPlayer.m_strWeapon == "sword" && l_oW.f_CurrentSetup().m_oPlayer.m_iLv == 5,
                  "a saved setup loads (three of the same monster, the sword, Lv 5)");
            f_Check(l_oW.f_SetInBattle(QStringLiteral("bat"), false) && l_oW.f_InBattle().isEmpty(), "untick: every copy leaves");
            f_Check(l_oW.f_ScenarioCount() >= 20, "the Scenarios tab lists tests/scenarios");
            std::printf(g_iFailed ? "battle_editor selftest: %d failed\n" : "battle_editor selftest: all passed\n", g_iFailed);
            l_iRc = g_iFailed ? 1 : 0;
        } else if (!l_oSmoke.isEmpty()) {
            l_oW.f_SetQuiet(true);
            l_oW.show();
            l_oW.f_SetInBattle(QStringLiteral("bat"), true);
            l_oW.f_SetInBattle(QStringLiteral("golem"), true);
            f_Check(l_oW.f_StartBattle(), "▶ Battle starts the battle");
            BgfxViewport* l_pView = l_oW.f_Viewport();
            QTimer::singleShot(4000, &l_oW, [&l_oW, l_pView, l_oSmoke] {
                l_oW.f_RefreshLog();
                f_Check(l_oW.f_LogText().contains(QStringLiteral("start (slime + bat + golem")), "the log shows the battle's start");
                Game* l_pG = l_pView->f_Session().f_Game();
                f_Check(l_pG && l_pG->f_BattleFoeCount() == 3, "three monsters in the battle");
                // an edit while it fights: the bat (the second) attacks every 1.5 s from now on (its move's wind-up)
                const int l_iBefore = l_pG ? l_pG->f_BattleFoeMoveMs(1) : 0;
                l_oW.f_SelectMonster(QStringLiteral("bat"));
                l_oW.f_SetMonsterNumber(QStringLiteral("atk_interval_ms"), 1500);
                const int l_iAfter = l_pG ? l_pG->f_BattleFoeMoveMs(1) : 0;
                f_Check(l_iBefore > 0 && l_iAfter > 0 && l_iAfter < l_iBefore && l_iAfter <= 1500 * 2,
                      "an attack interval typed in the form changes the running battle at once");
                l_pView->f_RunSmokeTest(l_pView->f_FrameCount() + 10, l_oSmoke);
            });
            QObject::connect(l_pView, &BgfxViewport::f_SmokeTestDone, &l_oW, [&l_oW, l_pView, l_oSmoke] {
                l_oW.grab().save(QString(l_oSmoke).replace(QStringLiteral(".png"), QStringLiteral("_window.png")));
                if (Game* l_pG = l_pView->f_Session().f_Game()) l_pG->f_DebugForceWin();
                QTimer::singleShot(500, &l_oW, [&l_oW] {
                    l_oW.f_RefreshLog();
                    f_Check(l_oW.f_LogText().contains(QStringLiteral(": win")), "Win now wins (the log)");
                    // auto restart: the same three monsters again, a moment later
                    QTimer::singleShot(BattleEditorWindow::m_iAutoRestartMs + 2500, &l_oW, [&l_oW] {
                        l_oW.f_RefreshLog();
                        Game* l_pG = l_oW.f_Viewport()->f_Session().f_Game();
                        f_Check(l_pG && l_pG->f_BattleFoeCount() == 3 && !l_oW.f_LogText().contains(QStringLiteral(": win")),
                              "the battle starts again by itself (auto restart)");
                        std::printf(g_iFailed ? "battle_editor smoke: %d failed\n" : "battle_editor smoke: all passed\n", g_iFailed);
                        QApplication::exit(g_iFailed ? 1 : 0);
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
